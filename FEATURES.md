# Features Implementation Summary

## Overview

This lyrics overlay application is inspired by LyricsX and built on top of the wshowkeys project. It displays synchronized lyrics as an overlay on Wayland compositors.

## Core Features Implemented

### 1. MPRIS Integration (`mpris.h`, `mpris.c`)

- Uses `playerctl` command to interact with MPRIS D-Bus interface
- Automatically detects currently playing music from any MPRIS-compatible player:
  - mpv (with `--force-window=yes`)
  - Spotify
  - VLC
  - Any other MPRIS-compatible media player
- Retrieves track metadata:
  - Title
  - Artist
  - Album
  - Track length
  - Current playback position
- Detects track changes automatically
- Checks playback status (playing/paused)

### 2. LRC Parser (`lrc_parser.h`, `lrc_parser.c`)

- Full LRC (LyRiCs) format support
- Parses synchronized lyrics with timestamps
- Supports metadata tags:
  - `[ti:]` - Title
  - `[ar:]` - Artist
  - `[al:]` - Album
  - `[offset:]` - Time offset in milliseconds
- Timestamp format: `[mm:ss.xx]` where xx can be centiseconds or milliseconds
- Efficiently finds the current line based on playback position
- Handles UTF-8 encoded files (Korean, Chinese, Japanese, emoji, etc.)

### 3. SRT Parser (`srt_parser.h`, `srt_parser.c`)

- SubRip (SRT) subtitle format support
- Parses timed subtitles
- Timestamp format: `hh:mm:ss,ms --> hh:mm:ss,ms`
- Automatically merges multi-line subtitles into single display lines
- Reuses the same data structures as LRC parser for consistency

### 4. Lyrics Provider System (`lyrics_provider.h`, `lyrics_provider.c`)

Abstract provider interface allowing multiple lyrics sources:

#### Local Provider
- Searches for lyrics files in multiple locations:
  1. Current directory
  2. `~/.lyrics/`
  3. `$XDG_MUSIC_DIR`
  4. `$HOME`
- Supports multiple filename patterns:
  - `Title.lrc` / `Title.srt`
  - `Artist - Title.lrc` / `Artist - Title.srt`
  - `Artist/Title.lrc`
- Sanitizes filenames to handle special characters

#### lrclib.net Provider
- Fetches synchronized lyrics from lrclib.net API
- Uses libcurl for HTTP requests
- Simple JSON parsing (no external library needed)
- Falls back to plain lyrics if synced lyrics unavailable
- Respects rate limits and uses appropriate User-Agent

#### Provider Priority
1. Local files (fastest, most reliable)
2. Online APIs (automatic fallback)

### 5. Real-time Synchronization

- Polls MPRIS position every 100ms
- Updates displayed lyrics in real-time
- Smooth transitions between lines
- Handles seeking and playback position changes
- Automatically loads new lyrics when track changes

### 6. Wayland Integration

Based on wshowkeys implementation:
- Uses `wlr-layer-shell-v1` protocol
- Transparent overlay support
- Configurable screen position (top/bottom/left/right)
- HiDPI/scaling support
- Multi-monitor aware

### 7. Text Rendering

- Cairo + Pango for high-quality text rendering
- Font customization support
- Full Unicode support (all languages)
- Subpixel antialiasing
- Centered text alignment
- Dynamic text sizing

## Architecture

```
Main Application (main.c)
    ├── MPRIS Interface (mpris.c)
    │   └── playerctl commands
    │
    ├── Lyrics Providers (lyrics_provider.c)
    │   ├── Local File Provider
    │   └── lrclib.net Provider
    │
    ├── Format Parsers
    │   ├── LRC Parser (lrc_parser.c)
    │   └── SRT Parser (srt_parser.c)
    │
    └── Rendering
        ├── Wayland Layer Shell
        ├── Cairo Surface
        ├── Pango Text Layout
        └── Shared Memory Buffers (shm.c)
```

## Usage Modes

### 1. MPRIS Mode (Default)
```bash
./lyrics
```
- Automatically detects current track
- Searches for lyrics locally
- Falls back to online search if needed
- Updates in real-time with playback

### 2. Manual Mode
```bash
./lyrics -l song.lrc
```
- Displays specific lyrics file
- Still uses timestamps for synchronization
- Useful for testing or when metadata detection fails

## Configuration

All configuration via command-line arguments:
- `-F`: Font (e.g., "Noto Sans CJK KR Bold 48")
- `-b`: Background color (RRGGBBAA hex)
- `-f`: Foreground/text color (RRGGBBAA hex)
- `-a`: Anchor position (top/bottom/left/right)
- `-m`: Margin from screen edge (pixels)
- `-l`: Manual lyrics file path

## Future Enhancement Possibilities

The codebase is designed to be extensible:

1. **Additional Lyrics Providers**
   - Add new providers by implementing the `lyrics_provider` interface
   - Examples: Genius API, Musixmatch, local database

2. **Enhanced Display**
   - Show multiple lines (current + next/previous)
   - Fade in/out effects
   - Color transitions
   - Karaoke-style word highlighting

3. **Caching**
   - Save downloaded lyrics locally
   - Build lyrics database

4. **Advanced Synchronization**
   - Manual offset adjustment
   - Learning from user corrections
   - Beat detection

## Technical Highlights

- **No external JSON library**: Minimal JSON parsing implementation
- **Memory efficient**: Streaming parsers, reusable buffers
- **Error resilient**: Graceful fallbacks at every level
- **Clean separation**: Modular design, clear interfaces
- **POSIX compliant**: Standard C11 code

## Building

```bash
meson setup build
meson compile -C build
```

## Dependencies

- cairo, fontconfig, pango, pangocairo: Rendering
- wayland-client, wayland-protocols: Wayland integration
- libcurl: HTTP requests for online lyrics
- playerctl: MPRIS interface (runtime only)
