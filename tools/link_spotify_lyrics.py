# SPDX-License-Identifier: GPL-3.0-or-later
"""Make local lyrics of YouTube downloads findable while playing on Spotify.

wshowlyrics finds the lyrics of a local "Title [VideoID].mkv" by its file
name, but Spotify's MPRIS URL is not a local file, so only the title patterns
apply: "<title>.<ext>" and "<artist> - <title>.<ext>" in the configured
search_dirs. Spotify's names are those of the release your account's market
plays, which may be an international one ("角巻わため - RESPAWN!!" where the
Japanese release is "リスポーン!!"). This tool links each lyrics file under
those exact names:

1. The .mkv must already carry TITLE/ARTIST tags (tag_youtube_media.py).
2. The Japanese release is searched on Spotify's JP market. Spotify often
   credits the artist in Latin letters (NANAOAKARI, Jin, Hanatan), so besides
   the tags the artist may match YouTube Music's credit or appear in the
   video title or channel name, as in tag_youtube_media.py; the length must
   match too. A title equal to the tag is preferred over one that is only
   contained in the video title.
3. Its ISRC is searched on your market (default KR). The same recording
   released elsewhere shares the ISRC, and its names are what Spotify reports
   over MPRIS while you play it; a song not released in Japan is looked up
   on your market directly.
4. Each lyrics file named like the .mkv (.lrc/.lrcx/.srt/.vtt, next to it or
   in LYRICS_DIR) is linked as LYRICS_DIR/"<first artist> - <title>.<ext>".
   Drafts are not: files in DRAFTS_DIR (default LYRICS_DIR/drafts, which
   wshowlyrics never searches) and .lrc/.lrcx without real timestamps.
   wshowlyrics reads only the first MPRIS artist, and replaces
   / \\ : * ? " < > | with "_" when building the name, so the link does too.

With --rename, a file whose name has no Japanese is first renamed to the
Japanese release's title, "<title> [VideoID].<ext>", along with its lyrics
files and drafts (characters a Samba share rejects become their full-width
forms, as yt-dlp does). Names that already contain Japanese are left alone.

LYRICS_DIR (default ~/.lyrics) must be in [lyrics] search_dirs, e.g.
search_dirs = {music_dir}:~/.lyrics

Needs SPOTIFY_CLIENT_ID and SPOTIFY_CLIENT_SECRET (see tag_youtube_media.py).

Usage:
  nix-shell tools/youtube-tag-shell.nix --run 'python tools/link_spotify_lyrics.py --dry-run [--rename] DIR_OR_FILES...'
"""

import argparse
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tag_youtube_media as tym  # noqa: E402

LYRICS_EXTS = ('.lrc', '.lrcx', '.srt', '.vtt')
LRC_TIME_RE = re.compile(r'^\s*\[(\d+:\d+(?:[.:]\d+)?)\]')
LRC_TAG_RE = re.compile(r'^\s*\[[a-zA-Z#]+:.*\]\s*$')
JAPANESE_RE = re.compile(r'[\u3040-\u30ff\u3400-\u4dbf\u4e00-\u9fff]')
# Matches sanitize_filename() in src/provider/lyrics/lyrics_provider.c
APP_UNSAFE = str.maketrans({c: '_' for c in '/\\:*?"<>|'})
# What yt-dlp substitutes; Samba (CIFS) rejects the ASCII forms
FS_UNSAFE = str.maketrans({'/': '⧸', '\\': '⧹', ':': '：', '*': '＊', '?': '？',
                           '"': '＂', '<': '＜', '>': '＞', '|': '｜'})


def lyrics_files(mkv, dirs):
    """Lyrics named like the mkv in any of dirs (not links)."""
    stem = os.path.splitext(os.path.basename(mkv))[0]
    return [p for d in dict.fromkeys(dirs) for ext in LYRICS_EXTS
            for p in [os.path.join(d, stem + ext)]
            if os.path.isfile(p) and not os.path.islink(p)]


def unsynced(path):
    """True for an .lrc/.lrcx draft: lines without a time, or every line at one time.

    Linking a draft would make Spotify playback show it instead of falling
    back to LRCLIB. SRT/VTT cannot exist without cue times.
    """
    if os.path.splitext(path)[1] not in ('.lrc', '.lrcx'):
        return False
    with open(path, encoding='utf-8', errors='replace') as f:
        text = [line for line in f if line.strip() and not LRC_TAG_RE.match(line)]
    times = [m.group(1) for m in map(LRC_TIME_RE.match, text) if m]
    return len(set(times)) < 2 or len(times) < len(text) * 0.9


def find_release(spotify, tags, video, markets):
    """(song, market, None) for the first market with a verified match, else (None, None, why).

    A title equal to the tag wins over one only contained in the video title:
    "DARK ARIA" (the soundtrack piece) is contained in "DARK ARIA <LV2>".
    """
    title, artist = tags
    artists = [artist] + video.artists
    terms = [f'{title} {artist}', f'{tym.search_term(title)} {tym.search_term(artist)}']
    terms += [f'{tym.search_term(title)} {a}' for a in video.artists]
    terms += [tym.search_term(video.titles[1])]
    terms = list(dict.fromkeys(t for t in terms if t.strip()))
    queries = [tym.music_query(title, artists, tym.video_hay(video))._replace(label='title', terms=terms),
               tym.video_title_query(video, artists)._replace(terms=terms)]

    reasons = []
    for market in markets:
        for query in queries:
            song, why = spotify.find(query, video.duration_ms, market=market)
            if song:
                return song, market, None
            reasons.append(f'{market}/{query.label}: {why}')
    return None, None, '; '.join(reasons)


def market_names(spotify, isrc, market):
    """(first artist, title) of each release of the recording playable in market."""
    return sorted({(s['first_artist'], s['title'])
                   for s in map(spotify.song, spotify.search(f'isrc:{isrc}', market))
                   if s['isrc'] == isrc})


def plan_rename(mkv, files, jp_title):
    """[(src, dst)] renaming a non-Japanese name to the Japanese title, or []."""
    name = os.path.basename(mkv)
    # The bare title: "(with ...)" credits would make long file names
    title = tym.drop_groups(jp_title, keep=lambda inner: not tym.CREDIT_RE.match(inner))
    title = title.translate(FS_UNSAFE).strip(' .')
    if JAPANESE_RE.search(tym.file_title(name)) or not JAPANESE_RE.search(title):
        return []
    stem = f'{title} [{tym.video_id(name)}]'
    moves = [(p, os.path.join(os.path.dirname(p), stem + os.path.splitext(p)[1]))
             for p in [mkv] + files]
    clash = [dst for _, dst in moves if os.path.lexists(dst)]
    if clash:
        raise RuntimeError('rename target exists: ' + ', '.join(map(os.path.basename, clash)))
    return moves


def link(target, link_path, dry_run):
    """Create link_path -> target; return a status word."""
    if os.path.islink(link_path):
        if os.readlink(link_path) == target:
            return 'exists'
        return 'skip (links elsewhere: ' + os.readlink(link_path) + ')'
    if os.path.lexists(link_path):
        return 'skip (a file is in the way)'
    if not dry_run:
        os.symlink(target, link_path)
    return 'would link' if dry_run else 'linked'


def process(mkv, args):
    name = os.path.basename(mkv)
    # Drafts are renamed along with the mkv but never linked
    files = lyrics_files(mkv, [os.path.dirname(mkv), args.lyrics_dir, args.drafts_dir])
    held = {}
    for p in files:
        if os.path.dirname(p) == args.drafts_dir:
            held[p] = 'draft'
        elif unsynced(p):
            held[p] = 'unsynced'
    linkable = [p for p in files if p not in held]
    if not linkable and not args.rename:
        # Nothing to link and nothing to rename: spare the network
        return [f'[skip] {p}: {why}' for p, why in held.items()] or \
            [f'[skip] {name}: no lyrics file named like it']

    tag = tym.global_tag(tym.read_tags(mkv))
    title, artist = (tym.get_simple(tag, k) for k in ('TITLE', 'ARTIST'))
    if title is None or artist is None:
        return [f'[skip] {name}: no TITLE/ARTIST tags (run tag_youtube_media.py first)']
    tags = (title.findtext('String'), artist.findtext('String'))

    markets = list(dict.fromkeys(['JP', args.market]))
    found, market, why = find_release(args.spotify, tags, tym.youtube_video(mkv), markets)
    if not found:
        return [f'[skip] {name}: not on Spotify ({why}) for {tags[1]} - {tags[0]}']

    out, renamed = [], {}
    if args.rename:
        # plan_rename only renames to a Japanese title, whichever market gave it
        renamed = dict(plan_rename(mkv, files, found['title']))
        verb = 'would rename' if args.dry_run else 'renamed'
        for src, dst in renamed.items():
            if not args.dry_run:
                os.rename(src, dst)
            out.append(f'[{verb}] {os.path.basename(src)} -> {os.path.basename(dst)}')
    out += [f'[skip] {renamed.get(p, p)}: {why}' for p, why in held.items()]
    if not files:
        out.append(f'[skip] {os.path.basename(renamed.get(mkv, mkv))}: no lyrics file named like it')
    if not linkable:
        return out

    # The ISRC also finds the market's other releases of the recording
    names = market_names(args.spotify, found['isrc'], args.market) if found['isrc'] else []
    if not names and market == args.market:
        names = [(found['first_artist'], found['title'])]
    if not names:
        return out + [f'[skip] {name}: {found["isrc"]} is not playable on the {args.market} market']
    for first_artist, track in names:
        stem = f'{first_artist.translate(APP_UNSAFE)} - {track.translate(APP_UNSAFE)}'
        for path in map(lambda p: renamed.get(p, p), linkable):
            link_path = os.path.join(args.lyrics_dir, stem + os.path.splitext(path)[1])
            status = link(path, link_path, args.dry_run)
            out.append(f'[{status}] {os.path.basename(link_path)} -> {path}')
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('paths', nargs='+', help='.mkv files or directories containing them')
    parser.add_argument('--market', default='KR',
                        help="your Spotify account's country (default: KR)")
    parser.add_argument('--lyrics-dir', default=os.path.expanduser('~/.lyrics'),
                        help='where the links go; must be in search_dirs (default: ~/.lyrics)')
    parser.add_argument('--drafts-dir', default=None,
                        help='unfinished lyrics: renamed with the mkv, never linked '
                             '(default: LYRICS_DIR/drafts)')
    parser.add_argument('--rename', action='store_true',
                        help="rename files without Japanese in the name to the Japanese release's title")
    parser.add_argument('--dry-run', action='store_true', help='show what would be done')
    args = parser.parse_args()
    args.market = args.market.upper()
    args.lyrics_dir = os.path.abspath(args.lyrics_dir)
    args.drafts_dir = os.path.abspath(args.drafts_dir or os.path.join(args.lyrics_dir, 'drafts'))

    args.spotify = tym.spotify_from_env(args.market)
    if not args.spotify:
        sys.exit('Set SPOTIFY_CLIENT_ID and SPOTIFY_CLIENT_SECRET')
    if not args.dry_run:
        os.makedirs(args.lyrics_dir, exist_ok=True)

    failed = False
    for mkv in tym.expand(args.paths):
        # Keep the path as given (e.g. through ~/Music/Youtube), not resolved,
        # so links survive the share being mounted elsewhere
        mkv = os.path.abspath(mkv)
        try:
            lines = process(mkv, args)
        except (RuntimeError, OSError, ValueError, KeyError, tym.ET.ParseError) as e:
            lines, failed = [f'[error] {os.path.basename(mkv)}: {e}'], True
        print('\n'.join(lines))
    sys.exit(1 if failed else 0)


if __name__ == '__main__':
    main()
