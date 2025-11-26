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

The project has **three format-specific parsers** with different data structures:

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

### Rendering Pipeline

1. **Wayland Manager** (`src/utils/wayland/wayland_manager.c`)
   - Handles wlr-layer-shell protocol
   - Creates transparent overlay surfaces
   - Manages compositor interactions

2. **Render Helpers** (`src/utils/render/render_helpers.c`)
   - `render_karaoke_segments()`: Progressive word fill for LRCX
   - `render_ruby_segments()`: Standard text with furigana for LRC/SRT
   - Both handle ruby text positioning above base text

3. **Main Render Loop** (`src/main.c`)
   - Determines format from file extension (`.lrcx` → karaoke mode)
   - Switches between karaoke and normal rendering
   - Handles empty lines (instrumental breaks) with transparent background

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

## Dependencies

**Build-time**:
- cairo, pango, pangocairo (rendering)
- wayland-client, wayland-protocols (Wayland)
- libcurl, openssl (online lyrics)
- libappindicator-gtk3, gdk-pixbuf-2.0 (system tray)
- fontconfig (font discovery)
- meson, ninja (build system)

**Runtime**:
- playerctl (MPRIS integration)
- Compositor with wlr-layer-shell support (Sway, Hyprland, etc.)

## Project Structure

```
src/
├── main.c                          # Main render loop
├── lyrics_types.h                  # Core data structures
├── constants.h                     # Color macros, constants
├── parser/
│   ├── lrc/lrc_parser.c           # LRC format (line-level timing)
│   ├── lrcx/lrcx_parser.c         # LRCX format (word-level timing)
│   ├── srt/srt_parser.c           # SRT format (subtitle)
│   └── utils/parser_utils.c       # Shared parsing utilities
├── provider/
│   ├── lyrics/lyrics_provider.c   # Local file search
│   └── lrclib/lrclib_provider.c   # Online API fallback
├── utils/
│   ├── mpris/mpris.c              # Music player integration
│   ├── render/render_helpers.c    # Karaoke & ruby text rendering
│   ├── wayland/wayland_manager.c  # Wayland surface management
│   ├── curl/curl_utils.c          # HTTP requests
│   ├── file/file_utils.c          # File I/O utilities
│   ├── pango/pango_utils.c        # Text layout
│   └── shm/shm.c                  # Shared memory buffers
└── user_experience/
    ├── config/config.c            # Configuration loading
    └── system_tray/system_tray.c  # Album art tray icon
```
