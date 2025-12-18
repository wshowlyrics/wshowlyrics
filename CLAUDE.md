# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build System

This project uses **Meson** as its build system:

```bash
# Initial setup
meson setup build

# Compile
meson compile -C build

# Run the binary
./build/lyrics

# Clean rebuild
rm -rf build && meson setup build && meson compile -C build
```

The main binary is `build/lyrics` and is renamed to `wshowlyrics` during installation.

## Code Architecture

### Multi-Provider Lyrics System

The application uses a **provider chain architecture** for lyrics retrieval:

1. **Local Provider** (`src/provider/lyrics/lyrics_provider.c`)
   - Searches local filesystem in priority order:
     - Same directory as music file (highest priority)
     - Current directory (only for local builds)
     - `$XDG_MUSIC_DIR`
     - `~/.lyrics/`
     - `$HOME`
   - Supports URL decoding for Unicode paths (Korean, Japanese, etc.)
   - Handles file-based formats: `.lrcx`, `.lrc`, `.srt`, `.vtt`

2. **Online Provider** (`src/provider/lrclib/lrclib_provider.c`)
   - Fallback when local files not found
   - Fetches synchronized lyrics from lrclib.net API
   - Only uses synced lyrics (plain text is ignored)

### Parser Architecture

The project has **three format-specific parsers** sharing common utilities (`src/parser/utils/lrc_common.c`):

1. **LRCX Parser** (`src/parser/lrcx/lrcx_parser.c`)
   - Karaoke-style word-level timing: `[00:12.00][00:12.20]First [00:12.50]word`
   - Uses `word_segment` structure with timestamps
   - Supports unfill effect: `[<MM:SS.xx]` for blinking/oscillating characters
   - Progressive fill rendering

2. **LRC Parser** (`src/parser/lrc/lrc_parser.c`)
   - Standard line-level synchronized lyrics: `[00:12.00]Lyrics line`
   - Uses `ruby_segment` structure (no timing, only text)
   - Simpler rendering (no karaoke effects)

3. **SRT Parser** (`src/parser/srt/srt_parser.c`)
   - Subtitle format with time ranges
   - Uses `ruby_segment` structure
   - Handles both start and end timestamps

**Ruby Text Support**: All formats support furigana/pinyin syntax: `心{こころ}` renders "こころ" above "心"

**Shared Utilities**: `lrc_common.c` provides timestamp parsing, ruby text extraction, and validation used by all parsers

### Core Data Structures

Defined in `src/lyrics_types.h`:

- **`struct word_segment`**: LRCX karaoke segments with timing + ruby text
- **`struct ruby_segment`**: LRC/SRT segments with only text + ruby text (no timing)
- **`struct lyrics_line`**: Contains either `word_segment*` OR `ruby_segment*` (never both)
- **`struct lyrics_data`**: Container for all lyrics with metadata and MD5 checksum

### MPRIS Integration

`src/utils/mpris/mpris.c` handles music player communication:
- Uses `playerctl` to detect currently playing songs
- Extracts metadata: title, artist, album, file location
- Monitors playback position for sync
- Tracks file changes to reload lyrics

### Translation System

Multi-provider translation with **async background processing** (`src/translator/`):

**Supported Providers** (configured via `settings.ini`):
- **OpenAI**: gpt-4o, gpt-4o-mini, gpt-3.5-turbo (`openai_translator.c`)
- **DeepL**: deepl-pro, deepl-free (`deepl_translator.c`)
- **Google Gemini**: gemini-2.5-flash, gemini-2.5-pro (`gemini_translator.c`)
- **Anthropic Claude**: claude-sonnet-4-5, claude-haiku-4, claude-opus-4-5 (`claude_translator.c`)

**Architecture**:
- **LRC-only**: Only LRC format supports translation (LRCX has word-level timing, SRT/VTT are subtitle formats)
- **Async threads**: Translation happens in background without blocking rendering
- **Smart caching**: `/tmp/wshowlyrics/translated/{md5}_{lang}.json` for resume capability
- **Language detection**: Skips API calls for text already in target language (`src/utils/lang_detect/`)
- **Rate limiting**: Configurable delays (200ms default) between API calls
- **Retry logic**: Exponential backoff with configurable max retries

**Common Utilities** (`src/translator/common/translator_common.c`):
- Cache loading/saving
- Language detection
- Ruby notation stripping
- Last-line extraction (handles AI over-explanation)

**Format**: Cache stored as JSON with line-by-line translations for partial resume

### Rendering Pipeline

**Modern Architecture** (`src/core/rendering/`, `src/utils/render/`):

1. **Rendering Manager** (`rendering_manager.c`)
   - Central orchestrator for all rendering operations
   - Format detection and renderer selection
   - Frame composition and Wayland buffer management

2. **Format-Specific Renderers**:
   - **Word Render** (`word_render.c`): LRCX karaoke with progressive fill
   - **Ruby Render** (`ruby_render.c`): LRC/SRT with furigana positioning
   - **Render Common** (`render_common.c`): Shared utilities (plain text, background)

3. **Wayland Manager** (`src/utils/wayland/wayland_manager.c`)
   - Handles wlr-layer-shell protocol
   - Creates transparent overlay surfaces
   - Manages compositor interactions

4. **Main Event Loop** (`src/main.c`)
   - Poll-based Wayland event dispatch
   - Determines format from file extension (`.lrcx` → karaoke mode)
   - Handles translation rendering with configurable opacity

### System Tray

`src/user_experience/system_tray/system_tray.c`:
- Displays album art via SNI (StatusNotifierItem)
- Uses libappindicator-gtk3
- Shows default music icon when album art unavailable
- Tooltip shows: "Artist - Title"

## Configuration

Settings are loaded from (in priority order):
1. `~/.config/wshowlyrics/settings.ini` (user config)
2. `/etc/wshowlyrics/settings.ini` (system-wide)
3. Command-line arguments (highest priority)

**Hot Reload**: Configuration files are monitored via MD5 checksums and automatically reloaded when changed.

**Key Sections** (`settings.ini.example`):
- `[display]`: Colors, fonts, positioning, opacity
- `[lyrics]`: Format preferences, line count, alignment
- `[translation]`: Provider, API key, target language, rate limits
- `[monitor]`: File monitoring intervals, cache settings

See `settings.ini.example` for all available options.

## Testing

Quick test workflow:

```bash
# 1. Place lyrics file with same name as music file
mkdir -p ~/test-lyrics
cp song.mp3 ~/test-lyrics/
cp song.lrc ~/test-lyrics/

# 2. Play music with MPRIS-compatible player
mpv --force-window=yes ~/test-lyrics/song.mp3

# 3. Run lyrics overlay
./build/lyrics

# 4. Verify MPRIS metadata
playerctl metadata

# 5. Check playback position (microseconds)
playerctl metadata -f '{{position}}'
```

For detailed testing guide including Unicode path testing, see `docs/TESTING.md`.

## Critical Code Patterns

### Memory Management for Lyrics

When freeing lyrics data, handle both segment types:

```c
// Free word segments (LRCX)
if (line->segments) {
    struct word_segment *seg = line->segments;
    while (seg) {
        struct word_segment *next = seg->next;
        free(seg->text);
        free(seg->ruby); // May be NULL
        free(seg);
        seg = next;
    }
}

// Free ruby segments (LRC/SRT)
if (line->ruby_segments) {
    struct ruby_segment *seg = line->ruby_segments;
    while (seg) {
        struct ruby_segment *next = seg->next;
        free(seg->text);
        free(seg->ruby); // May be NULL
        free(seg);
        seg = next;
    }
}
```

### Format Detection

Format is determined by file extension, not content:

```c
// In src/main.c
bool is_karaoke = is_lyrics_format(state, ".lrcx");
```

Only `.lrcx` files use karaoke rendering, even if they contain word-level timing.

### Unicode Path Handling

Always use URL decoding for file paths from MPRIS:

```c
// Extract directory from file:///home/user/%EC%9D%8C%EC%95%85/song.mp3
// Result: /home/user/음악
char *dir = extract_directory_from_url(url); // Handles URL decoding
```

### Translation Thread Safety

Translation runs asynchronously - always check state before accessing:

```c
// Check if translation is in progress
if (data->translation_in_progress) {
    // Access translation_current/translation_total for progress
    // Don't modify lyrics_line->translation (being written by thread)
}

// Cancel translation when loading new lyrics
data->translation_should_cancel = true;
pthread_join(data->translation_thread, NULL);
```

### Language Detection Pattern

Use `is_already_in_language()` before expensive API calls:

```c
// In src/utils/lang_detect/lang_detect.c (requires libexttextcat)
if (is_already_in_language(text, target_lang)) {
    return; // Skip translation
}
```

Falls back to simple heuristics if libexttextcat unavailable.

## Dependencies

**Build-time**:
- cairo, pango, pangocairo (rendering)
- wayland-client, wayland-protocols (Wayland)
- libcurl, openssl, json-c (online lyrics + translation)
- libappindicator-gtk3, gdk-pixbuf-2.0 (system tray)
- fontconfig (font discovery)
- libexttextcat (optional, language detection)
- meson, ninja (build system)

**Runtime**:
- playerctl (MPRIS integration)
- Compositor with wlr-layer-shell support (Sway, Hyprland, etc.)

## Project Structure

```
src/
├── main.c                                    # Main event loop & rendering orchestration
├── lyrics_types.h                            # Core data structures
├── constants.h                               # Color macros, constants
├── core/
│   ├── rendering/
│   │   ├── rendering_manager.c              # Central rendering coordinator
│   │   ├── word_render.c                    # LRCX karaoke renderer
│   │   ├── ruby_render.c                    # LRC/SRT ruby text renderer
│   │   └── render_common.c                  # Shared rendering utilities
│   └── state/
│       └── lyrics_state.c                   # Rendering state management
├── lyrics/
│   └── lyrics_manager.c                     # Lyrics lifecycle management
├── parser/
│   ├── lrc/lrc_parser.c                     # LRC format (line-level timing)
│   ├── lrcx/lrcx_parser.c                   # LRCX format (word-level timing)
│   ├── srt/srt_parser.c                     # SRT format (subtitle)
│   └── utils/lrc_common.c                   # Shared parsing utilities
├── provider/
│   ├── lyrics/lyrics_provider.c             # Local file search (11 paths)
│   ├── lrclib/lrclib_provider.c             # Online API fallback
│   └── itunes/itunes_artwork.c              # Album art fallback
├── translator/
│   ├── openai/openai_translator.c           # OpenAI GPT models
│   ├── deepl/deepl_translator.c             # DeepL API
│   ├── gemini/gemini_translator.c           # Google Gemini
│   ├── claude/claude_translator.c           # Anthropic Claude
│   └── common/translator_common.c           # Shared translation utilities
├── utils/
│   ├── mpris/mpris.c                        # Music player integration
│   ├── wayland/wayland_manager.c            # Wayland surface management
│   ├── lang_detect/lang_detect.c            # Language detection
│   ├── curl/curl_utils.c                    # HTTP requests
│   ├── file/file_utils.c                    # File I/O & MD5 checksums
│   ├── pango/pango_utils.c                  # Text layout
│   └── shm/shm.c                            # Shared memory buffers
├── user_experience/
│   ├── config/config.c                      # INI file parsing & hot reload
│   └── system_tray/system_tray.c            # Album art tray icon (SNI)
└── events/
    └── wayland_events.c                     # Wayland event handlers
```

## Code Quality & Analysis

### SonarCloud Integration

This project uses SonarCloud for continuous code quality analysis. Issues can be accessed via REST API without requiring browser MCP.

**Project:** `unstable-code_lyrics`
**Dashboard:** https://sonarcloud.io/project/overview?id=unstable-code_lyrics

### Accessing Issues via API

**Base Endpoint:**
```
https://sonarcloud.io/api/issues/search
```

**Common Parameters:**
- `componentKeys`: Project identifier (unstable-code_lyrics)
- `resolved`: false (unresolved issues only)
- `ps`: Page size (number of issues per page)
- `s`: Sort field (SEVERITY, FILE, LINE, etc.)
- `asc`: Sort order (false = descending)
- `types`: Issue type filter (BUG, VULNERABILITY, CODE_SMELL)

**Example Queries:**

```bash
# Get top 10 most severe issues
curl "https://sonarcloud.io/api/issues/search?componentKeys=unstable-code_lyrics&resolved=false&ps=10&s=SEVERITY&asc=false"

# Get all security vulnerabilities
curl "https://sonarcloud.io/api/issues/search?componentKeys=unstable-code_lyrics&types=VULNERABILITY&resolved=false"

# Get all bugs
curl "https://sonarcloud.io/api/issues/search?componentKeys=unstable-code_lyrics&types=BUG&resolved=false"

# Get code smells in specific file
curl "https://sonarcloud.io/api/issues/search?componentKeys=unstable-code_lyrics&types=CODE_SMELL&componentKeys=unstable-code_lyrics:src/parser/utils/parser_utils.c"
```

**Response Format:**
```json
{
  "issues": [
    {
      "key": "issue-id",
      "type": "BUG|VULNERABILITY|CODE_SMELL",
      "severity": "BLOCKER|CRITICAL|MAJOR|MINOR|INFO",
      "component": "unstable-code_lyrics:src/path/to/file.c",
      "line": 123,
      "message": "Issue description"
    }
  ]
}
```

### Browser MCP vs API Comparison

**SonarCloud API (Recommended):**
- ✅ No installation required
- ✅ Fetch all issues programmatically
- ✅ Automation-friendly (CI/CD integration)
- ✅ Works with WebFetch tool in Claude Code
- ✅ No authentication required for public projects
- ✅ Direct access to structured data (JSON)

**Browser MCP:**
- ⚠️ Requires installation and configuration
- ⚠️ Useful for complex interactions (clicking, scrolling, form filling)
- ⚠️ Currently unnecessary for SonarCloud issue review
- ⚠️ Overhead for simple data retrieval

**Recommendation:** Use SonarCloud API for code quality analysis. Browser MCP is only needed for complex web interactions that cannot be achieved via API.

### Quality Gate Settings

**New Code Definition:** "Previous version"
- Recommended for projects with regular release tagging
- This project uses semantic versioning (v0.6.x)
- Release frequency: Variable (hours to days)
- See release history: https://github.com/unstable-code/lyrics/tags

**Why not "Number of days":**
- Release intervals are inconsistent (1 hour to 7 days)
- Fixed day threshold would either include too much or miss rapid releases
- Version-based tracking aligns with existing workflow

### Commit Message Format for SonarCloud Fixes

When fixing issues identified by SonarCloud, use the following commit message format:

```
fix: [Brief description of the fix]

[Detailed explanation of the issue and solution]

Changes:
- [Specific change 1]
- [Specific change 2]
- [Specific change 3]

Benefits:
- [Benefit 1]
- [Benefit 2]
- [Benefit 3]

Fixes: SonarCloud issues [issue-key-1], [issue-key-2]

🤖 Generated with [Claude Code](https://claude.com/claude-code)

Co-Authored-By: Claude Sonnet 4.5 <noreply@anthropic.com>
```

**Example:**
```
fix: Add explicit bounds checking to prevent buffer overflow

Address SonarCloud BLOCKER issues in find_word_start() function.
Add defensive boundary checks to ensure returned pointer is always
within [limit, end] range, even in theoretically impossible cases.

Changes:
- Add explicit bounds check: (p >= limit && p <= end) ? p : limit
- Applied to both return paths (space-based and kanji-based)
- Works in both debug and release builds
- No assertion dependency (cleaner code)

Benefits:
- Satisfies static analyzer with explicit validation
- Prevents potential buffer overflow in edge cases
- Defensive coding against memory corruption
- Minimal performance impact (~0.1%, branch prediction)

Fixes: SonarCloud issues AZsw3M7sHpp0TbwUWQYl, AZsw3M7sHpp0TbwUWQYm

🤖 Generated with [Claude Code](https://claude.com/claude-code)

Co-Authored-By: Claude Sonnet 4.5 <noreply@anthropic.com>
```

**Key Elements:**
- **Title prefix**: `fix:` for bug/security fixes
- **Changes section**: Bullet points listing specific code changes
- **Benefits section**: Explain why the fix is valuable
- **Fixes reference**: Include SonarCloud issue keys for traceability
- **Standard footer**: Claude Code signature and co-authorship
