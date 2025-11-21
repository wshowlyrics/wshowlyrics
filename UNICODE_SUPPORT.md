# Unicode Support Documentation

## Overview

The lyrics overlay program fully supports Unicode characters in file paths and names, including:
- 🇯🇵 Japanese (日本語)
- 🇰🇷 Korean (한국어)
- 🇨🇳 Chinese (中文)
- And all other Unicode characters

## Problem

MPRIS returns file URLs with percent-encoding:
```
file:///home/user/Music/%E5%88%9D%E9%9F%B3%E3%83%9F%E3%82%AF.mp3
```

This needs to be decoded to:
```
/home/user/Music/初音ミク.mp3
```

## Solution

### 1. URL Decoding

Implemented `url_decode()` function that:
- Decodes percent-encoded sequences (`%XX`)
- Converts `+` to spaces
- Handles multi-byte UTF-8 sequences correctly

```c
// Example: %E5%88%9D → 初 (Hatsu)
char *decoded = url_decode(encoded_path);
```

### 2. Title Cleaning

Enhanced `remove_extension()` to handle:
- YouTube video IDs: `[VWVtIg5cdDU]` → removed
- File extensions: `.mkv`, `.mp3`, etc. → removed
- Trailing spaces and dashes → trimmed

Before: `初音ミクの消失 - cosMo＠暴走P [VWVtIg5cdDU].mkv`
After:  `初音ミクの消失 - cosMo＠暴走P`

### 3. Directory Extraction

The `get_directory_from_url()` function:
1. Strips `file://` prefix
2. Decodes percent-encoding
3. Extracts directory path

```
Input:  file:///home/user/Music/%E5%88%9D%E9%9F%B3%E3%83%9F%E3%82%AF.mp3
Output: /home/user/Music
```

## Test Cases

### Test 1: Japanese Characters
```
File: /home/user/Music/初音ミクの消失.mp3
MPRIS URL: file:///home/user/Music/%E5%88%9D%E9%9F%B3%E3%83%9F%E3%82%AF%E3%81%AE%E6%B6%88%E5%A4%B1.mp3
Decoded: /home/user/Music/初音ミクの消失.mp3
Result: ✅ Correctly finds 初音ミクの消失.lrc
```

### Test 2: Korean Characters
```
File: /home/user/Music/아이유 - 좋은날.mp3
MPRIS URL: file:///home/user/Music/%EC%95%84%EC%9D%B4%EC%9C%A0%20-%20%EC%A2%8B%EC%9D%80%EB%82%A0.mp3
Decoded: /home/user/Music/아이유 - 좋은날.mp3
Result: ✅ Correctly finds 아이유 - 좋은날.lrc
```

### Test 3: Chinese Characters
```
File: /home/user/Music/周杰倫 - 晴天.mp3
MPRIS URL: file:///home/user/Music/%E5%91%A8%E6%9D%B0%E5%80%AB%20-%20%E6%99%B4%E5%A4%A9.mp3
Decoded: /home/user/Music/周杰倫 - 晴天.mp3
Result: ✅ Correctly finds 周杰倫 - 晴天.lrc
```

### Test 4: Mixed with Video IDs
```
File: /home/user/Music/初音ミクの消失 [VWVtIg5cdDU].mkv
Title from MPRIS: 初音ミクの消失(THE END OF HATSUNE MIKU) - cosMo＠暴走P [VWVtIg5cdDU].mkv
Cleaned: 初音ミクの消失(THE END OF HATSUNE MIKU) - cosMo＠暴走P
Searches for:
  - 初音ミクの消失(THE END OF HATSUNE MIKU) - cosMo＠暴走P.lrc
  - 初音ミクの消失(THE END OF HATSUNE MIKU) - cosMo＠暴走P.srt
  - And other variations
```

## Implementation Details

### Character Encoding

All internal processing uses UTF-8:
- C strings are UTF-8 encoded
- Pango handles rendering of all Unicode characters
- No special handling needed for multi-byte characters in display

### URL Encoding Reference

Common encodings you might see:
- `%20` → space
- `%2D` → `-` (dash)
- `%E5%88%9D` → `初` (Japanese)
- `%ED%95%9C` → `한` (Korean)
- `%E4%B8%AD` → `中` (Chinese)

### Hex to Character Conversion

```c
// Converts hex digit to value
'0'-'9' → 0-9
'A'-'F' → 10-15
'a'-'f' → 10-15

// Combines two hex digits into one byte
high = '9' → 9
low  = 'F' → 15
result = (9 << 4) | 15 = 0x9F = 159
```

## Debugging

The program now prints:
```
File location: file:///home/user/Music/%E5%88%9D%E9%9F%B3...
Decoded directory from URL: /home/user/Music
```

This helps verify that URL decoding is working correctly.

## Future Improvements

Potential enhancements:
1. Cache decoded URLs to avoid repeated decoding
2. Support for other URL schemes (http://, https://)
3. Handle network paths (smb://, nfs://)
4. Normalize Unicode (NFD vs NFC)

## References

- RFC 3986: Uniform Resource Identifier (URI)
- UTF-8 encoding standard
- MPRIS D-Bus specification
