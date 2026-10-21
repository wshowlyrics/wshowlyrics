# SPDX-License-Identifier: GPL-3.0-or-later
"""Tag YouTube-downloaded Matroska files with title, artist and album.

An untagged "Title [VideoID].mkv" reaches wshowlyrics with the file name as
its title and no artist, so the LRCLIB search can only match on the title and
may pick another song with the same name (wshowlyrics#17). This tool fills
the tags from music catalogues, and only when the match can be verified:

1. The [VideoID] in the file name is looked up with `yt-dlp -j`.
2. If YouTube Music knows the video as a song (`track` and `artists`), the
   catalogue is searched for that track/artist. A song counts when its title
   matches the track and one of its credited names matches an artist.
3. Otherwise (MVs, covers, uploads with no music metadata) the catalogue is
   searched for the video title, both the file name's and yt-dlp's (yt-dlp may
   return a translated title). A song counts only when its title and one of
   its credited names both appear in the video title or channel name, so a
   same-titled song by someone else, a cover or a karaoke track is rejected.
4. In both cases the length must be within DURATION_TOLERANCE_MS, and every
   accepted result must be the same title and artist, or the file is skipped.
   When the song is on several releases, the single (album named like the
   song) wins, then the earliest.

Catalogues, in order:
- iTunes Search API (no key). Searched in the target storefront and the US
  one, because YouTube Music mixes their spellings (花譜's song shows as
  "ありふれてたい" by "KAF"); the target storefront's names are written
  (default JP: 角巻わため / リスポーン!! rather than Tsunomaki Watame / RESPAWN!!).
- Spotify Web API, opt-in: used only when SPOTIFY_CLIENT_ID and
  SPOTIFY_CLIENT_SECRET are set (an app from developer.spotify.com; the
  client-credentials flow needs no user login). It finds releases iTunes does
  not carry, e.g. some utaite covers. Only the target market is searched,
  one term at a time until a term settles the match (see Spotify.find), and
  a rate limit asking to wait more than a minute turns Spotify off for the
  rest of the run instead of hanging.

Names are compared ignoring case, width, spaces, look-alike brackets and a
"(feat. ...)" / "(with ...)" credit (see title_keys and artist_keys).

Existing tags are kept; only TITLE, ARTIST and ALBUM in the global tag are
replaced, and the segment title is removed (see write_tags).

Usage:
  nix-shell tools/youtube-tag-shell.nix --run 'python tools/tag_youtube_media.py --dry-run DIR_OR_FILES...'
  nix-shell tools/youtube-tag-shell.nix --run 'python tools/tag_youtube_media.py DIR_OR_FILES...'
"""

import argparse
import base64
import collections
import json
import os
import re
import subprocess
import sys
import time
import unicodedata
import urllib.error
import urllib.parse
import urllib.request
import xml.etree.ElementTree as ET

VIDEO_ID_RE = re.compile(r'\s*\[([A-Za-z0-9_-]{11})\]')
ITUNES_SEARCH = 'https://itunes.apple.com/search'
ITUNES_LOOKUP = 'https://itunes.apple.com/lookup'
SPOTIFY_TOKEN = 'https://accounts.spotify.com/api/token'
SPOTIFY_SEARCH = 'https://api.spotify.com/v1/search'
USER_AGENT = 'wshowlyrics-tools ( https://github.com/wshowlyrics/wshowlyrics )'
# The names decide the match; the length only keeps out other versions
# (live, remix). YouTube videos often run a few seconds longer than the audio.
DURATION_TOLERANCE_MS = 5000
# Seconds between request starts. iTunes allows roughly 20 requests per minute.
ITUNES_INTERVAL_S = 3.0
# Spotify publishes no limit, but bursts get the app blocked for hours
# (a Retry-After of 63908 s after a few hundred searches in about an hour)
SPOTIFY_INTERVAL_S = 1.0
# Longest Spotify 429 Retry-After worth waiting out
SPOTIFY_MAX_RETRY_AFTER_S = 60
# Shorter names would be found inside almost any video title
MIN_CONTAINED_LEN = 2
# Apple appends the release type to single/EP titles; the release itself has no suffix
ALBUM_SUFFIX_RE = re.compile(r' - (Single|EP)$')
CREDIT_RE = re.compile(r'(feat\.|ft\.|featuring|with)\s', re.I)
ARTIST_SPLIT_RE = re.compile(r'\s*(?:&|,|、|×|/|\sx\s|\swith\s|\sfeat\.\s|\sft\.\s)\s*', re.I)
# YouTube rejects < and > in titles, so uploads use look-alike letters.
# Applied once to everything read from YouTube (youtube_video); catalogue
# names never contain them.
LOOKALIKES = str.maketrans({'ᐸ': '<', 'ᐳ': '>'})


class Throttle:
    """Keeps request starts at least interval seconds apart."""

    def __init__(self, interval):
        self.interval, self.last = interval, 0.0

    def wait(self):
        delay = self.interval - (time.monotonic() - self.last)
        if delay > 0:
            time.sleep(delay)
        self.last = time.monotonic()


_itunes_throttle = Throttle(ITUNES_INTERVAL_S)


def norm(text):
    """Compare names ignoring case, width, spaces and hyphens (cosMo@Bousou-P / bousouP)."""
    text = unicodedata.normalize('NFKC', str(text or ''))
    return re.sub(r'[\s\-‐−_]+', '', text.casefold())


def drop_groups(text, keep=lambda inner: False):
    """Remove (...) / [...] groups, nested ones included, unless keep(inner)."""
    out, i = [], 0
    while i < len(text):
        if text[i] in '([':
            depth, j = 0, i
            while j < len(text):
                depth += text[j] in '(['
                depth -= text[j] in ')]'
                if depth == 0:
                    break
                j += 1
            if depth == 0 and not keep(text[i + 1:j]):
                i = j + 1
                continue
        out.append(text[i])
        i += 1
    return ''.join(out)


def title_keys(title):
    """The title as ('full', key), plus ('bare', key) without a "(feat. ...)" credit."""
    title = str(title or '')
    full = norm(title)
    bare = norm(drop_groups(title, keep=lambda inner: not CREDIT_RE.match(inner)))
    return {('full', full)} | ({('bare', bare)} if bare != full else set())


def titles_match(a, b):
    """Equal titles, where one side may drop its credit but not both.

    "Nemurenai" matches "Nemurenai (feat. Tomori Kusunoki)", but
    "初音ミクの消失 (feat. 初音ミク)" does not match "初音ミクの消失 (feat.石川綾子)",
    another singer's version.
    """
    return any(k1 == k2 and 'full' in (t1, t2) for t1, k1 in a for t2, k2 in b)


def artist_keys(artist):
    """The names in a credit, without "(CV: ...)"-style notes.

    YouTube Music lists co-artists separately or merges them differently
    from iTunes ("Karla with Alvin & Kinoko (CV: ...)" vs "Karla (CV: ...)"),
    so artists match when any name is shared.
    """
    artist = str(artist or '')
    names = ARTIST_SPLIT_RE.split(drop_groups(artist))
    return {norm(artist)} | {norm(n) for n in names if norm(n)}


def search_term(text):
    return drop_groups(text).strip()


def contained_in(hay, keys):
    return any(len(k) >= MIN_CONTAINED_LEN and k in hay for k in keys)


# --- Song descriptions -------------------------------------------------------
#
# Both catalogues are reduced to the same dict:
#   {'title', 'artist', 'album', 'release', 'ms',
#    'titles': title_keys() pairs, 'artists': set of artist keys}
# Spotify adds 'first_artist' and 'isrc'.

# What to search for, and how a result is verified
Query = collections.namedtuple('Query', 'label terms accepts')


def music_query(track, artists, hay=''):
    """The song YouTube Music names: same title, and a shared artist name.

    With hay (a video's titles and channel, see video_hay), an artist that
    only appears there also counts.
    """
    want_titles = title_keys(track)
    want_artists = set().union(*(artist_keys(a) for a in artists))
    # Keep the brackets: "Ruby (篠田 Team A)" without them is far too broad a search
    return Query('YouTube Music', [f'{track} {artists[0]}'],
                 lambda song: titles_match(want_titles, song['titles'])
                 and bool(want_artists & song['artists'] or contained_in(hay, song['artists'])))


def video_title_query(video, artists=()):
    """A song whose title and artist both appear in the video's titles or channel.

    artists: names known from elsewhere (tags) that also count for the artist.
    """
    hay = video_hay(video)
    want_artists = set().union(*(artist_keys(a) for a in artists))
    terms = list(dict.fromkeys(search_term(t) for t in video.titles if search_term(t)))
    return Query('video title', terms,
                 lambda song: contained_in(hay, [k for _, k in song['titles']])
                 and bool(want_artists & song['artists'] or contained_in(hay, song['artists'])))


def matches(songs, duration_ms, query):
    """The candidates query accepts at about the right length."""
    return [s for s in songs
            if abs(s['ms'] - duration_ms) <= DURATION_TOLERANCE_MS and query.accepts(s)]


def pick(songs, duration_ms, query):
    """Return (song, None) or (None, reason) from candidates gathered for query."""
    matched = matches(songs, duration_ms, query)
    if not matched:
        return None, 'no match'
    names = {(s['title'], s['artist']) for s in matched}
    if len(names) > 1:
        return None, 'matches disagree: ' + '; '.join(f'{a} - {t}' for t, a in sorted(names))
    # Same song on several releases: the single, then the earliest release
    matched.sort(key=lambda s: (norm(s['album']) != norm(s['title']), s['release']))
    return matched[0], None


# --- iTunes ------------------------------------------------------------------


def itunes(url, params):
    _itunes_throttle.wait()
    req = urllib.request.Request(url + '?' + urllib.parse.urlencode(params),
                                 headers={'User-Agent': USER_AGENT})
    with urllib.request.urlopen(req, timeout=15) as resp:
        return json.load(resp).get('results', [])


def itunes_find(query, duration_ms, country):
    """Search the target and US storefronts; describe each song by its target names.

    A song is verified against the names it has in either storefront, then
    reported with the target storefront's names.
    """
    storefronts = [country] if country == 'US' else [country, 'US']
    seen_titles, seen_artists = {}, {}  # trackId -> name keys seen in any storefront
    target = {}                         # trackId -> result in the target storefront
    for term in query.terms:
        for store in storefronts:
            for r in itunes(ITUNES_SEARCH, {'term': term, 'entity': 'song',
                                            'country': store, 'limit': 25}):
                if abs(r.get('trackTimeMillis', 0) - duration_ms) > DURATION_TOLERANCE_MS:
                    continue
                tid = r['trackId']
                seen_titles.setdefault(tid, set()).update(title_keys(r.get('trackName')))
                seen_artists.setdefault(tid, set()).update(artist_keys(r.get('artistName')))
                if store == country:
                    target[tid] = r
                else:
                    target.setdefault(tid, None)

    missing = [tid for tid, r in target.items() if r is None]
    if missing:
        for r in itunes(ITUNES_LOOKUP, {'id': ','.join(map(str, missing)), 'country': country}):
            target[r['trackId']] = r

    songs = [{'title': r['trackName'], 'artist': r['artistName'],
              'album': ALBUM_SUFFIX_RE.sub('', r.get('collectionName', '')),
              'release': r.get('releaseDate', ''), 'ms': r.get('trackTimeMillis', 0),
              'titles': seen_titles[tid], 'artists': seen_artists[tid]}
             for tid, r in target.items() if r is not None]
    return pick(songs, duration_ms, query)


# --- Spotify (opt-in) --------------------------------------------------------


class SpotifyBlocked(RuntimeError):
    """Spotify asked to wait longer than SPOTIFY_MAX_RETRY_AFTER_S."""


class Spotify:
    def __init__(self, client_id, client_secret, market):
        self.auth = base64.b64encode(f'{client_id}:{client_secret}'.encode()).decode()
        self.market = market
        self.token, self.expires = None, 0.0
        self._cache = {}  # (term, market) -> items; queries repeat terms
        self._throttle = Throttle(SPOTIFY_INTERVAL_S)
        # Set once Spotify blocks the app; every later search fails at once,
        # without a request that would only prolong the block
        self.blocked = None

    def _token(self):
        if self.token and time.monotonic() < self.expires - 60:
            return self.token
        req = urllib.request.Request(
            SPOTIFY_TOKEN, data=b'grant_type=client_credentials', method='POST',
            headers={'Authorization': 'Basic ' + self.auth,
                     'Content-Type': 'application/x-www-form-urlencoded'})
        with urllib.request.urlopen(req, timeout=15) as resp:
            body = json.load(resp)
        self.token = body['access_token']
        self.expires = time.monotonic() + body.get('expires_in', 3600)
        return self.token

    def search(self, term, market=None):
        if self.blocked:
            raise SpotifyBlocked(self.blocked)
        key = (term, market or self.market)
        if key not in self._cache:
            self._cache[key] = self._search(*key)
        return self._cache[key]

    def _search(self, term, market):
        params = urllib.parse.urlencode({'q': term, 'type': 'track', 'market': market, 'limit': 10})
        for _ in range(2):
            self._throttle.wait()
            req = urllib.request.Request(SPOTIFY_SEARCH + '?' + params,
                                         headers={'Authorization': 'Bearer ' + self._token()})
            try:
                with urllib.request.urlopen(req, timeout=15) as resp:
                    return json.load(resp)['tracks']['items']
            except urllib.error.HTTPError as e:
                if e.code != 429:
                    raise RuntimeError(f'Spotify search failed: HTTP {e.code}') from e
                # After a burst Spotify can ask for hours; give up instead of hanging
                wait = int(e.headers.get('Retry-After', '5'))
                if wait > SPOTIFY_MAX_RETRY_AFTER_S:
                    self.blocked = f'Spotify rate limit: retry after {wait} s'
                    raise SpotifyBlocked(self.blocked) from e
                time.sleep(wait)
        raise RuntimeError('Spotify search kept answering 429 (rate limited)')

    @staticmethod
    def song(t):
        names = [a['name'] for a in t.get('artists', [])]
        return {'title': t['name'], 'artist': ' & '.join(names),
                'first_artist': names[0] if names else '',
                'album': t.get('album', {}).get('name', ''),
                'release': t.get('album', {}).get('release_date', ''),
                'ms': t.get('duration_ms', 0), 'isrc': t.get('external_ids', {}).get('isrc'),
                'titles': title_keys(t['name']),
                'artists': set().union(*(artist_keys(n) for n in names)) if names else set()}

    def find(self, query, duration_ms, market=None):
        """pick() over the results of the terms, searched one at a time.

        Stops at the first term that brings a candidate, which keeps searches
        (and the rate limit) down to one or two per song. The trade-off: a
        disagreeing candidate that only a later term would bring in is not
        seen. More candidates never resolve a disagreement, so a disagreement
        stops the search too.
        """
        songs = {}
        try:
            for term in query.terms:
                for t in self.search(term, market):
                    songs[t['id']] = self.song(t)
                if matches(list(songs.values()), duration_ms, query):
                    break
        except SpotifyBlocked as e:
            return None, str(e)
        return pick(list(songs.values()), duration_ms, query)


def spotify_from_env(market):
    """A Spotify client when SPOTIFY_CLIENT_ID and SPOTIFY_CLIENT_SECRET are set, else None."""
    client_id = os.environ.get('SPOTIFY_CLIENT_ID')
    client_secret = os.environ.get('SPOTIFY_CLIENT_SECRET')
    return Spotify(client_id, client_secret, market) if client_id and client_secret else None


# --- YouTube -------------------------------------------------------------------


# track/artists: YouTube Music's credit, empty for other videos.
# titles: the file name's title and yt-dlp's (which may be translated).
Video = collections.namedtuple('Video', 'track artists titles channel duration_ms')


def video_id(name):
    return VIDEO_ID_RE.search(name).group(1)


def file_title(name):
    """"Title [VideoID].mkv" -> "Title"."""
    return VIDEO_ID_RE.sub('', os.path.splitext(name)[0]).strip().translate(LOOKALIKES)


def youtube_video(path):
    """What YouTube says about the video in this file's name."""
    name = os.path.basename(path)
    proc = subprocess.run(
        ['yt-dlp', '--skip-download', '--no-warnings', '-j',
         'https://www.youtube.com/watch?v=' + video_id(name)],
        capture_output=True, text=True)
    if proc.returncode != 0:
        raise RuntimeError('yt-dlp failed: ' + (proc.stderr.strip().splitlines() or ['?'])[-1])
    info = json.loads(proc.stdout)

    def text(value):
        # yt-dlp parses numeric names (e.g. the artist 310) as numbers
        return str(value or '').translate(LOOKALIKES)

    artists = info.get('artists') or ([info['artist']] if info.get('artist') else [])
    return Video(track=text(info.get('track')), artists=[text(a) for a in artists],
                 titles=[file_title(name), text(info.get('title'))],
                 channel=text(info.get('channel') or info.get('uploader')),
                 duration_ms=int(info.get('duration') or 0) * 1000)


def video_hay(video):
    """Everything the video is called, for "contained in" checks."""
    return norm(' '.join(video.titles + [video.channel]))


def queries_for(video):
    """The verified searches to try for this video, most trusted first."""
    queries = []
    if video.track and video.artists:
        queries.append(music_query(video.track, video.artists))
    queries.append(video_title_query(video))
    return queries


# --- Matroska tags -------------------------------------------------------------


def read_tags(path):
    proc = subprocess.run(['mkvextract', path, 'tags', '-'], capture_output=True)
    if proc.returncode != 0:
        raise RuntimeError('mkvextract failed: ' + proc.stderr.decode(errors='replace').strip())
    text = proc.stdout.decode('utf-8-sig').strip()
    return ET.fromstring(text) if text else ET.Element('Tags')


def global_tag(root):
    """The Tag whose Targets name no track/edition/chapter/attachment."""
    for tag in root.findall('Tag'):
        targets = tag.find('Targets')
        if targets is None or all(child.tag in ('TargetTypeValue', 'TargetType') for child in targets):
            return tag
    tag = ET.SubElement(root, 'Tag')
    ET.SubElement(tag, 'Targets')
    return tag


def get_simple(tag, name):
    for simple in tag.findall('Simple'):
        if simple.findtext('Name') == name:
            return simple
    return None


def set_simple(tag, name, value):
    simple = get_simple(tag, name)
    if simple is None:
        simple = ET.SubElement(tag, 'Simple')
        ET.SubElement(simple, 'Name').text = name
    string = simple.find('String')
    if string is None:
        string = ET.SubElement(simple, 'String')
    string.text = value


def write_tags(path, root, xml_path):
    """Write the global tags and drop the segment title.

    mpv reports a segment title and a TITLE tag together as "A / B" in
    xesam:title, so the tag is kept as the only title.
    """
    ET.ElementTree(root).write(xml_path, encoding='utf-8', xml_declaration=True)
    proc = subprocess.run(['mkvpropedit', path, '--edit', 'info', '--delete', 'title',
                           '--tags', 'global:' + xml_path], capture_output=True, text=True)
    if proc.returncode != 0:
        raise RuntimeError('mkvpropedit failed: ' + proc.stdout.strip() + proc.stderr.strip())


# --- Driver ----------------------------------------------------------------------


def find_song(video, args):
    """Try each query on each catalogue; return (song, source) or (None, reasons)."""
    catalogues = [('iTunes', lambda q: itunes_find(q, video.duration_ms, args.country))]
    if args.spotify:
        catalogues.append(('Spotify', lambda q: args.spotify.find(q, video.duration_ms)))
    reasons = []
    for query in queries_for(video):
        for name, find in catalogues:
            song, why = find(query)
            if song:
                return song, f'{name} via {query.label}'
            reasons.append(f'{name}/{query.label}: {why}')
    return None, '; '.join(reasons)


def tag_file(path, args):
    name = os.path.basename(path)
    if not VIDEO_ID_RE.search(name):
        return 'skip', 'no [VideoID] in file name'
    if not name.lower().endswith('.mkv'):
        return 'skip', 'not a Matroska (.mkv) file'

    root = read_tags(path)
    tag = global_tag(root)
    current = get_simple(tag, 'ARTIST')
    if current is not None and not args.force:
        return 'skip', f'already has ARTIST={current.findtext("String")!r} (use --force)'

    song, source = find_song(youtube_video(path), args)
    if not song:
        return 'skip', source

    new = {'TITLE': song['title'], 'ARTIST': song['artist'], 'ALBUM': song['album']}
    summary = ', '.join(f'{k}={v!r}' for k, v in new.items() if v) + f' [{source}]'
    if args.dry_run:
        return 'would tag', summary

    for key, value in new.items():
        if value:
            set_simple(tag, key, value)
    write_tags(path, root, args.tags_xml)
    return 'tagged', summary


def expand(paths):
    for p in paths:
        if os.path.isdir(p):
            for entry in sorted(os.listdir(p)):
                full = os.path.join(p, entry)
                if (os.path.isfile(full) and entry.lower().endswith('.mkv')
                        and VIDEO_ID_RE.search(entry)):
                    yield full
        else:
            yield p


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('paths', nargs='+', help='.mkv files or directories containing them')
    parser.add_argument('--country', default='JP',
                        help='storefront / market whose names are written (default: JP)')
    parser.add_argument('--dry-run', action='store_true', help='show what would be written')
    parser.add_argument('--force', action='store_true', help='retag files that already have ARTIST')
    args = parser.parse_args()
    args.country = args.country.upper()
    args.tags_xml = os.path.join(os.environ.get('XDG_RUNTIME_DIR') or '.', 'wshowlyrics-tags.xml')

    args.spotify = spotify_from_env(args.country)
    print('Spotify: ' + ('enabled' if args.spotify else
                         'disabled (set SPOTIFY_CLIENT_ID and SPOTIFY_CLIENT_SECRET to enable)'))

    failed = False
    try:
        for path in expand(args.paths):
            try:
                status, detail = tag_file(path, args)
            except (RuntimeError, OSError, ValueError, KeyError, ET.ParseError) as e:
                status, detail, failed = 'error', str(e), True
            print(f'[{status}] {os.path.basename(path)}: {detail}')
    finally:
        if os.path.exists(args.tags_xml):
            os.remove(args.tags_xml)
    sys.exit(1 if failed else 0)


if __name__ == '__main__':
    main()
