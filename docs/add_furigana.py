import re
import sys
import pykakasi

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

def process_file(filepath):
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
            else: # Line without timestamp, treat as plain text (e.g., metadata or empty line)
                prefix = ""
                text_to_process = cleaned_line
        elif is_srt and '-->' not in cleaned_line and not cleaned_line.strip().isdigit() and cleaned_line.strip():
            text_to_process = cleaned_line
        else: # For non-lrc/srt files or lines not matching specific patterns, process the whole line
            text_to_process = cleaned_line

        # Process lines with Japanese text. The function now handles partial furigana.
        if text_to_process and re.search('[\u3040-\u309F\u30A0-\u30FF\u4E00-\u9FAF]', text_to_process):
            new_lines.append(prefix + add_furigana_to_text(text_to_process))
        else:
            new_lines.append(cleaned_line)

    try:
        with open(filepath, 'w', encoding='utf-8') as f:
            f.write('\n'.join(new_lines))
    except Exception as e:
        print(f"Error writing to file {filepath}: {e}", file=sys.stderr)

if __name__ == "__main__":
    if len(sys.argv) > 1:
        for filepath in sys.argv[1:]:
            process_file(filepath)
    else:
        print("Please provide one or more file paths as arguments.", file=sys.stderr)
