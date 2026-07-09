"""Offline furigana annotation for LRC/SRT/plain lyrics files.

Readings come from pyopenjtalk (OpenJTalk G2P — context-aware) laid over
pykakasi's text-preserving segmentation, so the output keeps the original text
and corpus ruby format while fixing pykakasi's context misreadings (君 -> きみ,
今日 -> きょう, 愛 -> あい). Numeral counters get a compact policy: jukujikun keep
the full reading over the whole numeral (2人{ふたり}), other counters show just
the counter kanji's reading (9時{じ}) so long numbers never produce a messy
ruby. See add_furigana_ai.py for an optional cloud-LLM overlay.
"""

import re
import sys
import jaconv
import pykakasi
import pyopenjtalk

# Initialize pykakasi converter once outside the function for efficiency
kks = pykakasi.kakasi()

# A ruby base is kanji, plus the iteration mark 々 (U+3005) which closes words
# like 日々 / 人々 / 時々 but lives outside the CJK ideograph block.
KANJI_CLASS = '一-龯々'
KANJI_RE = re.compile(f'[{KANJI_CLASS}]')
KANJI_TAIL_RE = re.compile(f'[{KANJI_CLASS}]$')
PURE_HIRAGANA_RE = re.compile(r'^[぀-ゟ]+$')
READING_RE = re.compile(r'\{[^}]*\}')

def find_common_suffix(s1, s2):
    """Finds the longest common suffix between two strings."""
    s1_rev, s2_rev = s1[::-1], s2[::-1]
    i = 0
    while i < len(s1_rev) and i < len(s2_rev) and s1_rev[i] == s2_rev[i]:
        i += 1
    return s1_rev[:i][::-1]

def add_furigana_to_text(text):
    # Only process text that contains Japanese characters.
    if not re.search('[぀-ゟ゠-ヿ一-龯]', text):
        return text

    # Pattern to identify kanji already followed by furigana, e.g., 漢字{かんじ}
    # This helps in splitting the string to process only parts without furigana.
    # NOTE: the base may end with \u3005 (\u65e5\u3005{\u3072\u3073}); KANJI_CLASS covers it, otherwise
    # such a block is not seen as annotated and a second run appends a reading.
    furigana_pattern = rf'([{KANJI_CLASS}]+(?:\{{[^\}}]*\}}))'

    parts = re.split(furigana_pattern, text)

    processed_parts = []

    for part in parts:
        if not part:
            continue
        # If the part is an already-formatted furigana block, just append it
        if re.fullmatch(furigana_pattern, part):
            processed_parts.append(part)
        else:
            # Otherwise, process the chunk to add new furigana
            result = kks.convert(part)
            processed_chunk = ''
            for item in result:
                # Add furigana if the original part contains Kanji and the reading is different.
                orig, hira = item['orig'], item['hira']
                if KANJI_RE.search(orig) and hira != orig:
                    suffix = find_common_suffix(orig, hira)
                    # If there's a common suffix (okurigana), apply furigana only to the kanji part.
                    if suffix and len(suffix) < len(orig):
                        kanji_part, hira_part = orig[:-len(suffix)], hira[:-len(suffix)]
                    else:  # No common suffix or word is all kanji.
                        kanji_part, hira_part, suffix = orig, hira, ''
                    # A ruby base must end with a kanji and carry a pure-hiragana
                    # reading. pykakasi sometimes returns a long-vowel \u30fc (\u611b\u304a\u3057\u3044
                    # -> \u3044\u3068\u30fc\u3057\u3044) which misaligns the okurigana split and leaves
                    # kana at the end of the base (\u611b\u304a{\u3044\u3068\u30fc}) -- that is not
                    # expressible as ruby, so leave the token unannotated.
                    if KANJI_TAIL_RE.search(kanji_part) and PURE_HIRAGANA_RE.match(hira_part):
                        processed_chunk += f"{kanji_part}{{{hira_part}}}{suffix}"
                    else:
                        processed_chunk += orig
                else:
                    processed_chunk += orig
            processed_parts.append(processed_chunk)

    return "".join(processed_parts)


# --- pyopenjtalk reading enhancement ------------------------------------
#
# add_furigana_to_text (above) provides pykakasi's SEGMENTATION, which preserves
# the original text exactly and matches the existing corpus format. Its readings
# are often context-wrong, so pyopenjtalk's contextual readings are overlaid on
# top. pyopenjtalk normalises punctuation/latin/digits in its surface, so it can
# only be a READING source: its kanji runs (never normalised) are mapped back
# onto the original by string search.

# Numeral + counter: digits followed by kanji, plus any ruby already attached.
NUM_COUNTER_RE = re.compile(r'([0-9０-９]+)([一-龯]+)(\{[^}]*\})?')

# Standard compositional counter readings (voiced/rendaku variants first so the
# longest matches before the plain one). The 'り' reading of 人 is intentionally
# omitted so ひとり / ふたり fall through to the jukujikun (full-reading) branch.
COUNTER_READINGS = {
    '時': ['じ'], '分': ['ぷん', 'ぶん', 'ふん'], '秒': ['びょう'], '円': ['えん'],
    '番': ['ばん'], '個': ['こ'], '回': ['かい'], '歳': ['さい'], '才': ['さい'],
    '年': ['ねん'], '月': ['げつ', 'がつ'], '日': ['にち'], '点': ['てん'],
    '度': ['ど'], '号': ['ごう'], '枚': ['まい'], '台': ['だい'],
    '本': ['ぼん', 'ぽん', 'ほん'], '匹': ['びき', 'ぴき', 'ひき'],
    '杯': ['ばい', 'ぱい', 'はい'], '階': ['がい', 'かい'], '倍': ['ばい'],
    '週': ['しゅう'], '人': ['にん', 'じん'],
}


def strip_readings(text):
    """Remove all {...} reading groups, leaving only the base text."""
    return READING_RE.sub('', text)


def annotated_segments(text):
    """Split an annotated line into (base, reading|None) segments.

    A ruby unit's base is the maximal run of kanji (or 々) immediately before
    '{' -- exactly how the C renderer's find_kanji_boundary reads it, so this
    segmentation matches what will actually be drawn.
    Concatenating the base parts reproduces the ruby-free original text.
    """
    base_tail_re = re.compile(f'[{KANJI_CLASS}]+$')
    segments = []
    idx = 0
    for match in re.finditer(r'\{([^}]*)\}', text):
        chunk = text[idx:match.start()]
        kanji = base_tail_re.search(chunk)
        if kanji is None:
            # No kanji owns this reading — keep it verbatim as plain text.
            segments.append((chunk + match.group(0), None))
        else:
            if kanji.start() > 0:
                segments.append((chunk[:kanji.start()], None))
            segments.append((chunk[kanji.start():], match.group(1)))
        idx = match.end()
    if idx < len(text):
        segments.append((text[idx:], None))
    return segments


def reading_runs_from_annotated(annotated_text):
    """(start, end, reading) runs over the ruby-stripped text of an annotated string."""
    runs = []
    offset = 0
    for base, reading in annotated_segments(annotated_text):
        if reading is not None:
            runs.append((offset, offset + len(base), reading))
        offset += len(base)
    return runs


def reconcile_readings(structure_text, reading_runs):
    """Adopt structure_text's segmentation, substituting a reading from
    reading_runs wherever those runs cleanly tile one of its bases.

    reading_runs are (start, end, reading) over the ruby-stripped base text, so
    they record exactly which characters each reading belongs to. Passing them
    as data — rather than re-parsing an annotated string — is what keeps a
    reading for 中 from being widened onto a preceding bare kanji (日中).
    """
    out = []
    offset = 0
    for base, reading in annotated_segments(structure_text):
        start, end = offset, offset + len(base)
        offset = end
        if reading is None:
            # The structure left this span bare (pykakasi could not express a
            # valid base, e.g. 愛おしい). Insert any reading run that fits inside
            # it so pyopenjtalk still supplies 愛{いと}おしい.
            inside = sorted(r for r in reading_runs if r[0] >= start and r[1] <= end)
            pos = start
            for run_start, run_end, run_reading in inside:
                if run_start < pos:
                    continue
                out.append(base[pos - start:run_start - start])
                out.append(f"{base[run_start - start:run_end - start]}{{{run_reading}}}")
                pos = run_end
            out.append(base[pos - start:])
            continue
        chosen = reading  # structure_text's reading is the fallback
        inside = sorted(r for r in reading_runs if r[0] >= start and r[1] <= end)
        if (inside and inside[0][0] == start and inside[-1][1] == end and
                all(inside[k][1] == inside[k + 1][0] for k in range(len(inside) - 1))):
            merged = ''.join(r[2] for r in inside)
            if PURE_HIRAGANA_RE.match(merged):
                chosen = merged
        out.append(f"{base}{{{chosen}}}")
    return ''.join(out)


def pyopenjtalk_runs(original):
    """pyopenjtalk readings as (start, end, reading) runs over `original`.

    Only kanji runs (which pyopenjtalk does NOT normalise) are located in the
    original by string search; a run that cannot be matched (e.g. 2 -> 二 after
    numeral normalisation) is skipped. Returning offsets instead of an annotated
    string keeps each reading bound to exactly the characters it belongs to.
    """
    units = []
    for entry in pyopenjtalk.run_frontend(original):
        surf = entry.get("string", "")
        read = jaconv.kata2hira(entry.get("read", "") or "")
        if not surf or not KANJI_RE.search(surf) or not PURE_HIRAGANA_RE.match(read) \
                or read == surf:
            continue
        suffix = find_common_suffix(surf, read)
        if suffix and len(suffix) < len(surf) and len(suffix) < len(read):
            kanji, hira = surf[:-len(suffix)], read[:-len(suffix)]
        else:
            kanji, hira = surf, read
        units.append((kanji, hira))

    runs, cursor = [], 0
    for kanji, hira in units:
        pos = original.find(kanji, cursor)
        if pos == -1:            # normalised mismatch (e.g. 2 -> 二) -> skip safely
            continue
        runs.append((pos, pos + len(kanji), hira))
        cursor = pos + len(kanji)
    return runs


def _token_reading(token):
    """Hiragana reading of a short token via pyopenjtalk (for numerals)."""
    return jaconv.kata2hira(pyopenjtalk.g2p(token, kana=True) or '')


def fix_numerals(text):
    """Apply the numeral-counter policy, overriding any prior ruby on the span.

    jukujikun (no standard counter reading, e.g. 2人 -> ふたり) keep the full
    reading over the whole numeral; compositional counters (9時, 258円) get only
    the counter kanji's short reading so long numbers never make a messy ruby.
    The C parser groups the leading digits into the ruby base either way.
    """
    def repl(match):
        digits, kanji = match.group(1), match.group(2)
        token = digits + kanji
        reading = _token_reading(token)
        if not reading:
            return match.group(0)

        # Counter readings to try as a suffix of the full reading. A multi-kanji
        # counter (時間 -> じかん) is its own compound reading, so look that up
        # via pyopenjtalk rather than keeping every compound in the table.
        candidates = list(COUNTER_READINGS.get(kanji[-1], []))
        if len(kanji) > 1:
            own = _token_reading(kanji)
            if own and PURE_HIRAGANA_RE.match(own):
                candidates.append(own)
        for candidate in sorted(candidates, key=len, reverse=True):
            if reading.endswith(candidate) and len(reading) > len(candidate):
                # compositional: the counter suffix is clean hiragana even when
                # the number part carries a long-vowel ー (じゅーしちじ -> じ).
                return f"{token}{{{candidate}}}"

        # jukujikun: the full reading covers the numeral, and the parser only
        # absorbs a numeral into a SINGLE-kanji base. Emitting a number-inclusive
        # reading over a multi-kanji base would misalign it, so leave those be.
        if len(kanji) == 1 and PURE_HIRAGANA_RE.match(reading):
            return f"{token}{{{reading}}}"
        return match.group(0)
    return NUM_COUNTER_RE.sub(repl, text)


def annotate_line(text):
    """Default engine: pykakasi structure + pyopenjtalk readings + numeral policy.

    Idempotent: a line that already contains ruby is returned untouched, so a
    re-run can never rewrite or duplicate a reading. (A base ending in 々 or in
    kana would otherwise be re-annotated, appending a second reading each run.)
    Pass --regenerate to rebuild readings from scratch.
    """
    if not KANJI_RE.search(text):
        return text
    base = strip_readings(text)
    if base != text:
        return text
    structure = add_furigana_to_text(text)
    result = fix_numerals(reconcile_readings(structure, pyopenjtalk_runs(text)))
    # Safety net: never alter the underlying (ruby-stripped) text. If any stage
    # changed it (mojibake input, a normalization edge), return it unchanged.
    return result if strip_readings(result) == base else text


def process_file(filepath, annotate_fn=annotate_line, regenerate=False):
    try:
        with open(filepath, 'r', encoding='utf-8') as f:
            lines = f.readlines()
    except Exception as e:
        print(f"Error reading file {filepath}: {e}", file=sys.stderr)
        return

    # Preserve the file's trailing newline (and therefore any trailing blank
    # line): '\n'.join() alone would silently drop it.
    ends_with_newline = bool(lines) and lines[-1].endswith('\n')

    new_lines = []
    is_srt = filepath.lower().endswith('.srt')
    # .lrcx is LRC with inline word timestamps: it carries the same [ti:]/[al:]
    # metadata tags, so it must take the LRC path (a plain endswith('.lrc') is
    # False for '.lrcx' and would let the tags be annotated).
    is_lrc = filepath.lower().endswith(('.lrc', '.lrcx'))

    for line in lines:
        # Universal newlines already normalised the EOL, so strip just '\n' and
        # leave any trailing spaces in the lyrics untouched.
        cleaned_line = line.rstrip('\n')

        text_to_process = ""
        prefix = ""

        if is_lrc:
            match = re.match(r'(\[\d{2}:\d{2}\.\d{2,3}\])(.*)', cleaned_line)
            if match: # Line with timestamp
                prefix, text_to_process = match.groups()
            elif re.match(r'\[[a-zA-Z]+:', cleaned_line):
                # LRC metadata tag ([ti:], [ar:], [al:], [offset:] …) — the tag
                # value is a title/artist/album, not lyrics, so never annotate it.
                text_to_process = ""
            else: # Line without timestamp, treat as plain text
                prefix = ""
                text_to_process = cleaned_line
        elif is_srt and '-->' not in cleaned_line and not cleaned_line.strip().isdigit() and cleaned_line.strip():
            text_to_process = cleaned_line
        else: # For non-lrc/srt files or lines not matching specific patterns, process the whole line
            text_to_process = cleaned_line

        # Process lines with Japanese text. The function now handles partial furigana.
        if text_to_process and re.search('[\u3040-\u309F\u30A0-\u30FF\u4E00-\u9FAF]', text_to_process):
            # --regenerate: drop existing ruby so readings are rebuilt from
            # scratch (upgrades old pykakasi readings like 2\u4EBA{\u306B\u3093} -> \u3075\u305F\u308A).
            if regenerate:
                text_to_process = strip_readings(text_to_process)
            new_lines.append(prefix + annotate_fn(text_to_process))
        else:
            new_lines.append(cleaned_line)

    try:
        with open(filepath, 'w', encoding='utf-8') as f:
            f.write('\n'.join(new_lines) + ('\n' if ends_with_newline else ''))
    except Exception as e:
        print(f"Error writing to file {filepath}: {e}", file=sys.stderr)

if __name__ == "__main__":
    args = sys.argv[1:]
    regenerate = "--regenerate" in args
    files = [a for a in args if not a.startswith("-")]
    if files:
        for filepath in files:
            process_file(filepath, regenerate=regenerate)
    else:
        print("Usage: python add_furigana.py [--regenerate] <file...>", file=sys.stderr)
