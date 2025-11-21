# Lyrics Overlay for Wayland

A Wayland-based lyrics overlay program. Built on the [wshowkeys project](https://github.com/unstable-code/wshowkeys) and inspired by [LyricsX](https://github.com/ddddxxx/LyricsX).

[한국어 README](README.ko.md)

## Features

- **MPRIS Integration**: Automatically detects currently playing songs via playerctl (supports all MPRIS-compatible players: mpv, Spotify, VLC, etc.)
- **Smart Local Lyrics Search**:
  - Prioritizes searching in the same directory as the currently playing file
  - URL decoding support for Unicode paths (Korean, Japanese, etc.)
  - Automatic filename-based matching
- **Synchronized Lyrics**: Supports LRC and SRT formats
- **Real-time Sync**: Automatically displays lyrics based on music playback position
- Uses Wayland protocol (wlr-layer-shell)
- Transparent background support
- Full Unicode character support (Korean, Chinese, Japanese, etc. via Pango)
- Adjustable screen position (top/bottom/left/right)
- Customizable colors and fonts

## Build

```bash
meson setup build
meson compile -C build
```

## Usage

### MPRIS Mode (Default)

Automatically displays lyrics for the currently playing music:

```bash
# Basic execution - automatically finds and displays lyrics for the current song
./build/lyrics

# Play music with mpv
mpv --force-window=yes song.mp3

# Also works with other MPRIS-compatible players like Spotify, VLC, etc.
```

### Manual Mode

Display a specific LRC/SRT file:

```bash
./build/lyrics -l sample.lrc
```

### Options

| Option | Description | Default |
|--------|-------------|---------|
| `-h` | Show help | - |
| `-b COLOR` | Background color (#RRGGBB[AA] format) | `#00000080` (black, 50% transparent) |
| `-f COLOR` | Foreground/text color (#RRGGBB[AA] format) | `#FFFFFFFF` (white, opaque) |
| `-F FONT` | Font setting | `"Sans 20"` |
| `-a POSITION` | Screen position (top/bottom/left/right) | `bottom` |
| `-m PIXELS` | Screen edge margin (pixels) | `32` |
| `-l FILE` | Load specific lyrics file (.lrc/.srt/.vtt) | MPRIS auto-detect |

**Color Format:**
- `#RRGGBB`: RGB value (e.g., `#FF0000` = red)
- `#RRGGBBAA`: RGB + transparency (e.g., `#FF000080` = red 50% transparent)
- `AA` value: `00` = fully transparent, `FF` = opaque, `80` = 50% transparent

**Font Format:**
- Basic: `"Sans 20"` (Sans font, 20pt)
- Bold: `"Sans Bold 24"`
- Korean: `"Noto Sans CJK KR 18"`
- Japanese: `"Noto Sans CJK JP Bold 22"`

### Examples

```bash
# Show help
./build/lyrics -h

# Run in MPRIS mode (default)
./build/lyrics

# Run with Korean font
./build/lyrics -F "Noto Sans CJK KR 20"

# Display at top of screen
./build/lyrics -a top -m 50

# Transparent background with yellow text
./build/lyrics -b 00000066 -f FFFF00FF

# Display specific LRC file
./build/lyrics -l "Artist - Song.lrc"

# Large bold font at bottom of screen
./build/lyrics -F "Sans Bold 28" -a bottom -m 40
```

## Lyrics File Formats

### LRC Format (Recommended)

Synchronized lyrics file format:

```lrc
[ti:Song Title]
[ar:Artist]
[al:Album]

[00:12.00]First line of lyrics
[00:17.50]Second line of lyrics
[00:23.00]Third line of lyrics
```

### SRT Format

Subtitle format is also supported:

```srt
1
00:00:12,000 --> 00:00:17,500
First line of lyrics

2
00:00:17,500 --> 00:00:23,000
Second line of lyrics
```

### Local Lyrics File Search Paths

The program automatically searches for lyrics files in the following **priority order**:

1. **Same directory as the currently playing music file** (highest priority!)
   - First searches for a lyrics file with the same name as the music file
   - Example: `song.mp3` → `song.lrc` or `song.srt`
2. Title-based search (starting from current directory)
3. `$XDG_MUSIC_DIR`
4. `~/.lyrics/`
5. `$HOME`

Filename formats:
- `filename.lrc` / `filename.srt` (recommended! same name as music file)
- `title.lrc` / `title.srt`
- `artist - title.lrc` / `artist - title.srt`
- `artist/title.lrc`

**Tips:**
- Place lyrics files with the **same name in the same directory** as your music files!
- Unicode filenames (Korean, Japanese, etc.) are fully supported
- Example: `/music/song.mp3` + `/music/song.lrc` ✅

## Dependencies

### Build Requirements
- cairo
- fontconfig
- pango
- pangocairo
- wayland-client
- wayland-protocols
- wlr-layer-shell protocol
- libcurl
- meson & ninja

### Runtime Requirements
- playerctl (for MPRIS mode)
- Wayland compositor with wlr-layer-shell support (Sway, Hyprland, etc.)

### Installation (Arch Linux)

```bash
sudo pacman -S cairo fontconfig pango wayland wayland-protocols curl meson ninja playerctl
```

### Installation (Ubuntu/Debian)

```bash
sudo apt install libcairo2-dev libfontconfig1-dev libpango1.0-dev \
                 libwayland-dev wayland-protocols libcurl4-openssl-dev \
                 meson ninja-build playerctl
```

## License

GNU General Public License v3.0 (GPL-3.0)

This project is based on wshowkeys and follows the same GPL-3.0 license.
