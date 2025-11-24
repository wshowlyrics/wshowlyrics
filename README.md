# Lyrics Overlay for Wayland

A Wayland-based lyrics overlay program. Built on the [wshowkeys project](https://github.com/unstable-code/wshowkeys) and inspired by [LyricsX](https://github.com/ddddxxx/LyricsX).

[한국어 README](README.ko.md)

## Features

- **MPRIS Integration**: Automatically detects currently playing songs via playerctl (supports all MPRIS-compatible players: mpv, Spotify, VLC, etc.)
- **System Tray Integration**: Displays album art in system tray (Swaybar/Waybar)
  - Automatically loads album art from MPRIS metadata
  - Shows default music icon when album art is unavailable
  - Tooltip displays current track info (Artist - Title)
- **Smart Lyrics Search**:
  - **Local file search**: Prioritizes searching in the same directory as the currently playing file
  - **Online fallback**: Automatically fetches lyrics from [lrclib.net](https://lrclib.net) when local files are not found
  - URL decoding support for Unicode paths (Korean, Japanese, etc.)
  - Automatic filename-based matching
- **Karaoke Mode**: LRCX format with word-level timing and progressive fill effect
  - Past words: normal color (already sung)
  - Current word: progressively fills from left to right (currently singing)
  - Future words: dimmed (not yet sung)
- **Synchronized Lyrics**: Supports LRC, LRCX, and SRT formats
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

Automatically displays lyrics for the currently playing music via MPRIS:

```bash
# Basic execution - automatically finds and displays lyrics for the current song
./build/lyrics

# Play music with mpv
mpv --force-window=yes song.mp3

# Also works with other MPRIS-compatible players like Spotify, VLC, etc.
```

### Options

| Short | Long | Description | Default |
|-------|------|-------------|---------|
| `-h` | `--help` | Show help | - |
| `-b COLOR` | `--background=COLOR` | Background color (#RRGGBB[AA] format) | `#00000080` (black, 50% transparent) |
| `-f COLOR` | `--foreground=COLOR` | Foreground/text color (#RRGGBB[AA] format) | `#FFFFFFFF` (white, opaque) |
| `-F FONT` | `--font=FONT` | Font setting | `"Sans 20"` |
| `-a POSITION` | `--anchor=POSITION` | Screen position (top/bottom/left/right) | `bottom` |
| `-m PIXELS` | `--margin=PIXELS` | Screen edge margin (pixels) | `32` |

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
./build/lyrics --help

# Run in MPRIS mode (default)
./build/lyrics

# Run with Korean font
./build/lyrics -F "Noto Sans CJK KR 20"
./build/lyrics --font="Noto Sans CJK KR 20"

# Display at top of screen
./build/lyrics -a top -m 50
./build/lyrics --anchor=top --margin=50

# Transparent background with yellow text
./build/lyrics -b 00000066 -f FFFF00FF
./build/lyrics --background=00000066 --foreground=FFFF00FF

# Large bold font at bottom of screen
./build/lyrics -F "Sans Bold 28" -a bottom -m 40
./build/lyrics --font="Sans Bold 28" --anchor=bottom --margin=40
```

## Lyrics File Formats

### LRCX Format (Karaoke)

Karaoke-style lyrics with word-level timing:

```lrcx
[ar:Artist]
[ti:Song Title]

[00:12.00][00:12.20]First [00:12.50]word [00:12.80]by [00:13.00]word
[00:17.00][00:17.15]Karaoke [00:17.40]style [00:17.70]lyrics
```

- First timestamp: Line start time
- Subsequent timestamps: Individual word timing
- Words progressively fill from left to right as they're sung
- Use `.lrcx` file extension

**Unfill Effect (Blinking)**

For special vocal patterns where a character needs to blink/oscillate, use the `[<MM:SS.xx]` unfill syntax:

```lrcx
[00:10.00]僕{ぼく}[00:10.50]の[00:11.00]S[<00:11.50][00:12.00][<00:12.50][00:13.00]OS[00:14.00]を
```

- `[<MM:SS.xx]`: Unfill timestamp (note the `<` prefix)
- The character before the unfill timestamp will oscillate between 0% and 50% fill
- Use case: When the same character is held/repeated in vocals
- Creates a blinking effect during the unfill duration

**Ruby Text (Furigana) Support**

All lyrics file formats (LRCX, LRC, SRT) support ruby text annotations:

```lrcx
[00:12.00][00:12.34]心{こころ}[00:13.50]の[00:13.80]中{なか}
```

```lrc
[00:12.00]心{こころ}の中{なか}で　響{ひび}く
```

- Syntax: `main_text{ruby_text}` - Ruby text appears above the main text in smaller font
- Example: `心{こころ}` displays "こころ" (hiragana) above "心" (kanji)
- Use cases: Japanese furigana, Chinese pinyin, Korean pronunciation guides
- Format-agnostic: The `{}` syntax is automatically detected regardless of file extension
- Ruby text is rendered at half size, centered above the base text

### LRC Format (Standard)

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

### Lyrics Search Process

The program uses a two-stage approach to find lyrics:

#### 1. Local File Search (Priority)

Automatically searches for lyrics files in the following **priority order**:

1. **Same directory as the currently playing music file** (highest priority!)
   - First searches for a lyrics file with the same name as the music file
   - Example: `song.mp3` → `song.lrcx`, `song.lrc`, or `song.srt`
2. Title-based search (starting from current directory)
3. `$XDG_MUSIC_DIR`
4. `~/.lyrics/`
5. `$HOME`

Filename formats (searched in order):
- `filename.lrcx` / `filename.lrc` / `filename.srt` (recommended! same name as music file)
- `title.lrcx` / `title.lrc` / `title.srt`
- `artist - title.lrcx` / `artist - title.lrc` / `artist - title.srt`
- `artist/title.lrcx` / `artist/title.lrc`

**Tips:**
- Place lyrics files with the **same name in the same directory** as your music files!
- Unicode filenames (Korean, Japanese, etc.) are fully supported
- Example: `/music/song.mp3` + `/music/song.lrc` ✅

#### 2. Online Lyrics Fallback

If no local lyrics file is found, the program automatically fetches synchronized lyrics from [lrclib.net](https://lrclib.net):

- **Requirements**: Track must have both **title** and **artist** metadata
- **Format**: Only uses **synchronized lyrics** (LRC format with timestamps)
  - **Plain text lyrics are ignored** - only synced lyrics with timestamps are displayed
  - This ensures lyrics stay perfectly synchronized with the music
- **No internet connection?** The program will simply skip online search and continue
- **Privacy**: Only sends song metadata (title, artist, album) to lrclib.net API

## Dependencies

### Build Requirements
- cairo
- curl (libcurl)
- fontconfig
- pango
- pangocairo
- wayland-client
- wayland-protocols
- wlr-layer-shell protocol
- libappindicator-gtk3 (appindicator3-0.1)
- gdk-pixbuf-2.0
- meson & ninja

### Runtime Requirements
- curl (for online lyrics fetching)
- playerctl (for MPRIS mode)
- Wayland compositor with wlr-layer-shell support (Sway, Hyprland, etc.)

### Optional Dependencies
- **Swaybar users**:
  - `snixembed` - SNI (StatusNotifierItem) bridge for Swaybar (optional, but recommended for tray icons)
  - Enable system tray in Swaybar config:
    ```
    bar {
        tray {
            icon_theme Adwaita
            tray_padding 2
        }
    }
    ```
- **Waybar users**: System tray is supported by default, no additional configuration needed

## Installation

### Arch Linux (AUR)

Install using an AUR helper like `yay`:

```bash
yay -S wshowlyrics-git
```

Or manually:

```bash
git clone https://aur.archlinux.org/wshowlyrics-git.git
cd wshowlyrics-git
makepkg -si
```

### Manual Installation (Arch Linux)

Install dependencies:

```bash
sudo pacman -S cairo curl fontconfig pango wayland wayland-protocols meson ninja playerctl \
               libappindicator-gtk3 gdk-pixbuf2
```

Build and install:

```bash
meson setup build
meson compile -C build
sudo install -Dm755 build/lyrics /usr/bin/wshowlyrics
```

### Manual Installation (Ubuntu/Debian)

Install dependencies:

```bash
sudo apt install libcairo2-dev libcurl4-openssl-dev libfontconfig1-dev libpango1.0-dev \
                 libwayland-dev wayland-protocols \
                 libappindicator3-dev libgdk-pixbuf2.0-dev \
                 meson ninja-build playerctl
```

Build and install:

```bash
meson setup build
meson compile -C build
sudo install -Dm755 build/lyrics /usr/bin/wshowlyrics
```

## License

GNU General Public License v3.0 (GPL-3.0)

This project is based on wshowkeys and follows the same GPL-3.0 license.
