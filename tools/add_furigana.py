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
    furigana_pattern = r'([\u4e00-\u9faf]+(?:\{[^\}]*\}))'

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
                if re.search('[\u4e00-\u9faf]', item['orig']) and item['hira'] != item['orig']:
                    suffix = find_common_suffix(item['orig'], item['hira'])
                    # If there's a common suffix (okurigana), apply furigana only to the kanji part.
                    if suffix and len(suffix) < len(item['orig']):
                        kanji_part = item['orig'][:-len(suffix)]
                        hira_part = item['hira'][:-len(suffix)]
                        processed_chunk += f"{kanji_part}{{{hira_part}}}{suffix}"
                    else: # No common suffix or word is all kanji.
                        processed_chunk += f"{item['orig']}{{{item['hira']}}}"
                else:
                    processed_chunk += item['orig']
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

PURE_HIRAGANA_RE = re.compile(r'^[぀-ゟ]+$')
KANJI_RE = re.compile(r'[一-龯]')
READING_RE = re.compile(r'\{[^}]*\}')

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

    A ruby unit's base is the run ending at '{', trimmed to start at its first
    kanji: pykakasi groups okurigana verbs like 思い出{おもいだ}, whose base
    contains internal kana, so the base is NOT simply "trailing kanji".
    Concatenating the base parts reproduces the ruby-free original text.
    """
    segments = []
    idx = 0
    for match in re.finditer(r'\{([^}]*)\}', text):
        chunk = text[idx:match.start()]
        kanji = KANJI_RE.search(chunk)
        if kanji is None:
            segments.append((chunk + match.group(0), None))
        else:
            if kanji.start() > 0:
                segments.append((chunk[:kanji.start()], None))
            segments.append((chunk[kanji.start():], match.group(1)))
        idx = match.end()
    if idx < len(text):
        segments.append((text[idx:], None))
    return segments


def reconcile_readings(structure_text, reading_text):
    """Adopt structure_text's segmentation while substituting reading_text's
    reading wherever its runs cleanly tile a base of structure_text.

    Both inputs annotate the SAME base text, so character offsets line up.
    """
    reading_runs = []
    offset = 0
    for base, reading in annotated_segments(reading_text):
        if reading is not None:
            reading_runs.append((offset, offset + len(base), reading))
        offset += len(base)

    out = []
    offset = 0
    for base, reading in annotated_segments(structure_text):
        start, end = offset, offset + len(base)
        offset = end
        if reading is None:
            out.append(base)
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


def pyopenjtalk_annotate(original):
    """Annotate `original` (text preserved) with pyopenjtalk readings.

    Only kanji runs (which pyopenjtalk does NOT normalise) are matched back onto
    the original by string search; everything else is copied verbatim.
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

    out, cursor = [], 0
    for kanji, hira in units:
        pos = original.find(kanji, cursor)
        if pos == -1:            # normalised mismatch (e.g. 2 -> 二) -> skip safely
            continue
        out.append(original[cursor:pos])
        out.append(f"{kanji}{{{hira}}}")
        cursor = pos + len(kanji)
    out.append(original[cursor:])
    return ''.join(out)


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
        counter = kanji[-1]
        for candidate in sorted(COUNTER_READINGS.get(counter, []),
                                key=len, reverse=True):
            if reading.endswith(candidate) and len(reading) > len(candidate):
                # compositional: the counter suffix is clean hiragana even when
                # the number part carries a long-vowel ー (じゅーしちじ -> じ).
                return f"{token}{{{candidate}}}"
        # jukujikun: use the full reading only if it renders as clean hiragana.
        if PURE_HIRAGANA_RE.match(reading):
            return f"{token}{{{reading}}}"
        return match.group(0)
    return NUM_COUNTER_RE.sub(repl, text)


def annotate_line(text):
    """Default engine: pykakasi structure + pyopenjtalk readings + numeral policy.

    Idempotent: a line that already contains ruby is left to the pykakasi-only
    path (which preserves existing {…} blocks and only fills bare kanji), so
    re-running never rewrites readings. To regenerate readings, strip the
    existing ruby first.
    """
    if not KANJI_RE.search(text):
        return text
    base = strip_readings(text)
    if base != text:
        # Already annotated: preserve existing ruby, only fill bare kanji.
        result = add_furigana_to_text(text)
    else:
        structure = add_furigana_to_text(text)
        readings = pyopenjtalk_annotate(text)
        result = fix_numerals(reconcile_readings(structure, readings))
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

    new_lines = []
    is_srt = filepath.lower().endswith('.srt')
    is_lrc = filepath.lower().endswith('.lrc')

    for line in lines:
        cleaned_line = line.rstrip()

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
            f.write('\n'.join(new_lines))
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
