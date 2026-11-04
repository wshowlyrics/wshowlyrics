# SPDX-License-Identifier: GPL-3.0-or-later
"""Restore flattened furigana (僕ぼく達たち) to ruby syntax (僕{ぼく}達{たち}).

Lyrics copied from a page that renders ruby often lose the markup: each reading
is left inline after its kanji. This puts the braces back, keeping the readings
the source already had instead of guessing new ones (as add_furigana.py would).

Two ways to tell a reading from okurigana (言いうんだ -> 言{い}うんだ):

  --ref PLAIN  The plain lyrics without readings. A flattened line is that text
               with hiragana inserted after kanji, so aligning the two is exact.
               Needs no extra dependency.
  (default)    pyopenjtalk: a split is accepted when the kanji plus the remaining
               kana read back as the kana that followed them. Run inside
               tools/furigana-shell.nix.

Lines that already contain '{' (annotated lyrics, inline translations), SRT
indices/timestamps and LRC tags are left alone, so a partly converted file is
safe to run again. A line that cannot be resolved is kept as is and reported.

Usage: python tools/restore_ruby.py [--ref PLAIN] [--dry-run] <file...>
"""

import argparse
import re
import sys

KANJI_RE = re.compile(r'[一-龯々]')
HIRA_RE = re.compile(r'[ぁ-ゖー]')
SPACES = ' 　'
BOUNDARY = '|'          # stands for a line break / space in the reference
LRC_TIME_RE = re.compile(r'^((?:\[\d{2}:\d{2}[.:]\d{2,3}\])+)(.*)$')
SKIP_RE = re.compile(r'\d+|[\d:,.]+ --> [\d:,.]+.*|\[[a-zA-Z]+:.*\]|')


# --- reference mode ---------------------------------------------------------

def load_reference(path):
    with open(path, encoding='utf-8') as f:
        text = f.read()
    text = re.sub(r'（[^）]*）|\([^)]*\)', '', text)   # 驕傲（きょうごう）
    return re.sub(r'\s+', BOUNDARY, text)


def _align(f, o, i, j):
    """Ruby string for f[i:] aligned to o[j:], or None.

    f may hold extra hiragana right after a kanji (the reading) and spaces;
    a reading is required where a kanji run ends. Shortest reading first.
    """
    if i == len(f):
        return '' if j >= len(o) or o[j] == BOUNDARY else None
    after_kanji = (i > 0 and j > 0 and KANJI_RE.match(o[j - 1])
                   and f[i - 1] == o[j - 1])
    if j < len(o) and o[j] == BOUNDARY and (f[i] in SPACES or not after_kanji):
        r = _align(f, o, i, j + 1)
        if r is not None:
            return r
    c = f[i]
    if c in SPACES:
        r = _align(f, o, i + 1, j)
        return None if r is None else c + r
    if after_kanji:
        k = i
        while k < len(f) and HIRA_RE.match(f[k]):
            k += 1
            r = _align(f, o, k, j)
            if r is not None:
                return '{' + f[i:k] + '}' + r
        if j >= len(o) or not KANJI_RE.match(o[j]):
            return None
    if j < len(o) and c == o[j]:
        r = _align(f, o, i + 1, j + 1)
        if r is not None:
            return c + r
    return None


def restore_with_reference(line, ref):
    first = line.lstrip(SPACES)[:1]
    if not first:
        return line
    for m in re.finditer(re.escape(first), ref):
        r = _align(line, ref, 0, m.start())
        if r is not None:
            return r
    return None


# --- pyopenjtalk mode -------------------------------------------------------

_KAKASI = None


def _readings(text):
    """(pyopenjtalk, pykakasi) hiragana readings of text."""
    global _KAKASI
    import jaconv
    import pykakasi
    import pyopenjtalk
    if _KAKASI is None:
        _KAKASI = pykakasi.kakasi()
    oj = ''.join(jaconv.kata2hira(e.get('read', '') or '')
                 for e in pyopenjtalk.run_frontend(text))
    kk = ''.join(item['hira'] for item in _KAKASI.convert(text))
    return oj, kk


def _split_reading(kanji, kana):
    """Length of the reading at the head of kana, or None.

    A split r + rest passes when kanji + rest reads back as kana. pyopenjtalk
    alone is not enough: it fills in missing okurigana (求て -> もとめて,
    叫けんで -> さけんで), so either a too-short or a too-long r can pass.
    Splits both engines accept are preferred, then pyopenjtalk's; among
    several, the shortest wins (浮{う}かぶ over 浮{うか}ぶ, 重{かさ}なる).
    """
    both, oj_only = [], []
    for n in range(1, len(kana) + 1):
        oj, kk = _readings(kanji + kana[n:])
        if oj == kana:
            oj_only.append(n)
            if kk == kana:
                both.append(n)
    candidates = both or oj_only
    return candidates[0] if candidates else None


def restore_with_openjtalk(line):
    """Each kanji run K followed by hiragana H becomes K{r} + rest."""
    out, i = [], 0
    for m in re.finditer(r'([一-龯々]+)([ぁ-ゖー]+)', line):
        kanji, kana = m.groups()
        n = _split_reading(kanji, kana)
        if n is None:
            return None
        out.append(line[i:m.start()] + f'{kanji}{{{kana[:n]}}}{kana[n:]}')
        i = m.end()
    out.append(line[i:])
    result = ''.join(out)
    # Every kanji must have ended up with a reading, or the line was not flat.
    if re.search(r'[一-龯々](?![一-龯々{])', result):
        return None
    return result


# --- driver -----------------------------------------------------------------

def restore_line(text, ref):
    if '{' in text or not KANJI_RE.search(text):
        return text
    if ref is not None:
        return restore_with_reference(text, ref)
    return restore_with_openjtalk(text)


def process_file(path, ref, dry_run):
    with open(path, encoding='utf-8') as f:
        lines = f.read().split('\n')
    changed, unresolved = 0, []
    out = []
    for n, line in enumerate(lines, 1):
        prefix, text = '', line
        m = LRC_TIME_RE.match(line)
        if m:
            prefix, text = m.groups()
        elif SKIP_RE.fullmatch(line.strip()):
            out.append(line)
            continue
        result = restore_line(text, ref)
        if result is None:
            unresolved.append((n, line))
            result = text
        if result != text:
            changed += 1
            if dry_run:
                print(f'{path}:{n}: {prefix}{result}')
        out.append(prefix + result)
    for n, line in unresolved:
        print(f'{path}:{n}: unresolved, left as is: {line}', file=sys.stderr)
    if not dry_run and changed:
        with open(path, 'w', encoding='utf-8') as f:
            f.write('\n'.join(out))
    print(f'{path}: {changed} line(s) restored, {len(unresolved)} unresolved',
          file=sys.stderr)
    return not unresolved


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('files', nargs='+')
    parser.add_argument('--ref', help='plain lyrics without readings')
    parser.add_argument('--dry-run', action='store_true',
                        help='print the restored lines, do not write')
    args = parser.parse_args()
    sys.setrecursionlimit(10000)
    ref = load_reference(args.ref) if args.ref else None
    ok = all([process_file(p, ref, args.dry_run) for p in args.files])
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
