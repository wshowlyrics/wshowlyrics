# SPDX-License-Identifier: GPL-3.0-or-later
"""Copy cue timings from a reference SRT (e.g. a YouTube subtitle) onto a draft SRT.

Only the timestamps are taken; the draft's text, readings and translations stay
as they are. One text line per draft block (--line, default the last: the
translation) is aligned character by character against the reference cues read
as one stream, so the two need not split lines the same way:

  * a draft line that owns whole cues takes the cue edges as they are;
  * a cue the draft splits across two lines is cut where their characters meet,
    with times interpolated across that cue;
  * credits and other cues the draft has no line for are skipped.

A romaji reference (lyric videos often carry only that) is matched with
--romaji: the draft line is romanised with pykakasi and both sides are reduced
to a-z, so spelling differences (wa/ha, ou/o) only lower the match a little.

Wording may differ (another translator's Korean, ruby vs. none); a block whose
characters mostly failed to match is flagged LOW, so check those by ear.

When the reference subtitle belongs to another upload of the same backing track
(an original MV for a cover), --audio REF_AUDIO DRAFT_AUDIO measures the offset
between the two by cross-correlating their onsets, and prints the offset per
30 s window so a re-arranged cover (a drifting offset) shows up. It needs ffmpeg
and numpy; the offset is applied with --offset.

Usage:
  python tools/retime_srt.py [--dry-run] [--line N] [--offset MS] [--romaji] draft.srt ref.srt
  python tools/retime_srt.py --audio orig.wav cover.wav
"""

import argparse
import difflib
import re
import subprocess
import sys

PUNCT_RE = re.compile(r'[\s　,.、。!?！？「」『』"\'“”‘’()（）~\-]')
READING_RE = re.compile(r'\{[^}]*\}')
LOW_COVERAGE = 0.6


def strip_braces(text):
    """Drop ruby readings, but unwrap a whole-line {translation} instead of
    dropping it: SRT inline translations are written that way, and --line -1
    (the default) matches exactly those lines."""
    stripped = text.strip()
    if stripped.startswith('{') and stripped.endswith('}') and stripped.count('{') == 1:
        return stripped[1:-1]
    return READING_RE.sub('', text)


def norm_chars(text):
    return [c for c in strip_braces(text) if not PUNCT_RE.match(c)]


_KAKASI = None


def romaji_chars(text, convert):
    """Lowercase a-z only; convert=True romanises Japanese first (pykakasi)."""
    global _KAKASI
    text = strip_braces(text)
    if convert:
        if _KAKASI is None:
            import pykakasi
            _KAKASI = pykakasi.kakasi()
        text = ' '.join(item['hepburn'] for item in _KAKASI.convert(text))
    return [c for c in text.lower() if 'a' <= c <= 'z']


def to_ms(stamp):
    h, m, rest = stamp.strip().split(':')
    sec, milli = rest.replace('.', ',').split(',')
    return ((int(h) * 60 + int(m)) * 60 + int(sec)) * 1000 + int(milli)


def fmt(ms):
    ms = max(int(round(ms)), 0)
    return (f'{ms // 3600000:02}:{ms // 60000 % 60:02}:'
            f'{ms // 1000 % 60:02},{ms % 1000:03}')


def read_blocks(path):
    with open(path, encoding='utf-8') as f:
        text = f.read().strip()
    return [b.split('\n') for b in re.split(r'\n\s*\n', text)]


def retime(draft_blocks, ref_blocks, line, offset, romaji=False):
    """Returns [(start, end, coverage)] per draft block."""
    rchars, rtimes, rcue, cues = [], [], [], []
    for b in ref_blocks:
        s, e = (to_ms(x) for x in b[1].split(' --> '))
        cs = (romaji_chars(' '.join(b[2:]), False) if romaji
              else norm_chars(' '.join(b[2:])))
        for k in range(len(cs)):
            rtimes.append((s + (e - s) * k / len(cs), s + (e - s) * (k + 1) / len(cs)))
            rcue.append(len(cues))
        rchars += cs
        cues.append((s, e))

    dchars, owner = [], []
    for n, b in enumerate(draft_blocks):
        cs = romaji_chars(b[2:][line], True) if romaji else norm_chars(b[2:][line])
        dchars += cs
        owner += [n] * len(cs)

    mapped = {}
    sm = difflib.SequenceMatcher(None, dchars, rchars, autojunk=False)
    for i, j, size in sm.get_matching_blocks():
        for k in range(size):
            mapped[i + k] = j + k

    spans = []
    for n in range(len(draft_blocks)):
        idx = [i for i, o in enumerate(owner) if o == n]
        hits = [mapped[i] for i in idx if i in mapped]
        spans.append((hits, len(hits) / max(len(idx), 1)))

    def neighbour(n, step):
        m = n + step
        while 0 <= m < len(spans):
            if spans[m][0]:
                return spans[m][0]
            m += step
        return None

    result, prev_end = [], 0
    for n, (hits, coverage) in enumerate(spans):
        if not hits:                 # nothing matched: zero-length after the previous
            result.append((prev_end, prev_end, coverage))
            continue
        first, last = rcue[hits[0]], rcue[hits[-1]]
        prev, nxt = neighbour(n, -1), neighbour(n, 1)
        if prev and rcue[prev[-1]] == first:
            start = (rtimes[prev[-1]][1] + rtimes[hits[0]][0]) / 2
        else:
            start = cues[first][0]
        if nxt and rcue[nxt[0]] == last:
            end = (rtimes[hits[-1]][1] + rtimes[nxt[0]][0]) / 2
        else:
            end = cues[last][1]
        result.append((start + offset, end + offset, coverage))
        prev_end = end + offset
    # Reference cues may overlap (a duplicated or late-ending cue); never let a
    # line run past the start of the next one.
    for n in range(len(result) - 1):
        start, end, coverage = result[n]
        if end > result[n + 1][0]:
            result[n] = (start, max(start, result[n + 1][0]), coverage)
    return result


def measure_offset(ref_audio, draft_audio):
    """Print the ms to add to ref timings so they fit draft_audio."""
    import numpy as np

    rate, hop = 2000, 20             # 10 ms frames

    def onsets(path):
        raw = subprocess.run(['ffmpeg', '-v', 'quiet', '-i', path, '-ac', '1',
                              '-ar', str(rate), '-f', 'f32le', '-'],
                             capture_output=True, check=True).stdout
        x = np.frombuffer(raw, np.float32)
        x = x[:len(x) // hop * hop].reshape(-1, hop)
        energy = np.sqrt((x ** 2).sum(axis=1))
        d = np.maximum(np.diff(energy, prepend=energy[0]), 0)
        return (d - d.mean()) / (d.std() + 1e-9)

    def lag(a, b, max_lag):
        n = 1 << (len(a) + len(b)).bit_length()
        c = np.fft.irfft(np.fft.rfft(a, n) * np.conj(np.fft.rfft(b, n)), n)
        c = np.concatenate([c[-max_lag:], c[:max_lag + 1]])
        return int(np.argmax(c)) - max_lag

    a, b = onsets(ref_audio), onsets(draft_audio)
    print(f'overall: --offset {-lag(a, b, 1000) * 10}')
    window = 3000
    for t in range(0, min(len(a), len(b)) - window, 2000):
        print(f'  {t // 100:4d}s  {-lag(a[t:t + window], b[t:t + window], 700) * 10:+d} ms')


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('files', nargs='*', metavar='draft.srt ref.srt')
    ap.add_argument('--line', type=int, default=-1,
                    help='text line of a draft block to match (0 first, -1 last)')
    ap.add_argument('--offset', type=int, default=0, help='ms added to ref timings')
    ap.add_argument('--romaji', action='store_true',
                    help='the reference is romaji: romanise the draft line to match '
                         '(pykakasi, tools/furigana-shell.nix)')
    ap.add_argument('--dry-run', action='store_true', help='print, do not write')
    ap.add_argument('--audio', nargs=2, metavar=('REF_AUDIO', 'DRAFT_AUDIO'),
                    help='measure the offset between two audio files and exit')
    args = ap.parse_args()

    if args.audio:
        measure_offset(*args.audio)
        return 0
    if len(args.files) != 2:
        ap.error('need draft.srt and ref.srt')
    draft_path, ref_path = args.files

    draft = read_blocks(draft_path)
    times = retime(draft, read_blocks(ref_path), args.line, args.offset, args.romaji)
    low = 0
    for b, (start, end, coverage) in zip(draft, times):
        flag = '' if coverage >= LOW_COVERAGE else '   <-- LOW'
        low += bool(flag)
        print(f'{b[0]:>4} {coverage:.2f} {fmt(start)} --> {fmt(end)}  {b[2:][args.line]}{flag}')
        b[1] = f'{fmt(start)} --> {fmt(end)}'
    if not args.dry_run:
        with open(draft_path, 'w', encoding='utf-8') as f:
            f.write('\n\n'.join('\n'.join(b) for b in draft) + '\n')
    print(f'{draft_path}: {len(draft)} block(s) retimed, {low} LOW', file=sys.stderr)
    return 0


if __name__ == '__main__':
    sys.exit(main())
