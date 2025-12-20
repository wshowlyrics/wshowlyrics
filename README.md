# Lyrics Overlay for Wayland
[![Build Status](https://github.com/unstable-code/lyrics/actions/workflows/ci.yml/badge.svg)](https://github.com/unstable-code/lyrics/actions/workflows/ci.yml)
[![GitHub release](https://img.shields.io/github/v/release/unstable-code/lyrics)](https://github.com/unstable-code/lyrics/releases)
[![AUR version](https://img.shields.io/aur/version/wshowlyrics-git)](https://aur.archlinux.org/packages/wshowlyrics-git)
[![Platform: Linux](https://img.shields.io/badge/Platform-Linux-green.svg)](https://www.linux.org/)
[![Wayland](https://img.shields.io/badge/Wayland-Only-orange.svg)](https://wayland.freedesktop.org/)
<a href="https://scan.coverity.com/projects/unstable-code-lyrics">
  <img alt="Coverity Scan Build Status"
       src="https://scan.coverity.com/projects/32766/badge.svg"/>
</a>
<a href="https://sonarcloud.io/summary/new_code?id=unstable-code_lyrics">
  <img alt="Quality Gate Status"
       src="https://sonarcloud.io/api/project_badges/measure?project=unstable-code_lyrics&metric=alert_status"/>
</a>

<img width="696" height="77" alt="a65e765" src="https://github.com/user-attachments/assets/1909f0c1-445b-4526-b30f-6a5df93e624d" />

A Wayland-based lyrics overlay program. Built on the [wshowkeys project](https://github.com/unstable-code/wshowkeys) and inspired by [LyricsX](https://github.com/ddddxxx/LyricsX).

[한국어 README](docs/README.ko.md)

## Features

- **MPRIS Integration**: Automatically detects currently playing songs via playerctl (supports all MPRIS-compatible players: mpv, Spotify, VLC, etc.)
- **System Tray Integration**: Displays album art in system tray (Swaybar/Waybar)
  - Automatically loads album art from MPRIS metadata
  - **iTunes API Fallback**: Automatically fetches album artwork from iTunes Search API when MPRIS doesn't provide it
  - Shows default music icon when album art is unavailable from all sources
  - Tooltip displays current track info (Artist - Title)
- **Smart Lyrics Search**:
  - **Local file search**: Prioritizes searching in the same directory as the currently playing file
  - **Online fallback**: Automatically fetches lyrics from [lrclib.net](https://lrclib.net) when local files are not found
  - URL decoding support for Unicode paths (Korean, Japanese, etc.)
  - Automatic filename-based matching
- **Translation Support**: Multi-provider API integration for automatic lyrics translation
  - Multiple translation providers: OpenAI, DeepL, Google Gemini, Anthropic Claude
  - Smart caching system - translates once, uses forever
  - Language detection optimization - skips API calls for text already in target language
  - Configurable display modes (both original + translation, or translation only)
  - Adjustable translation text opacity
  - Supports LRC, SRT, and VTT formats (LRCX excluded)
- **Karaoke Mode**: LRCX format with word-level timing and progressive fill effect
  - Past words: normal color (already sung)
  - Current word: progressively fills from left to right (currently singing)
  - Future words: dimmed (not yet sung)
- **Synchronized Lyrics**: Supports LRC, LRCX, SRT, and VTT formats
- **Real-time Sync**: Automatically displays lyrics based on music playback position
- Uses Wayland protocol (wlr-layer-shell)
- Transparent background support
- Full Unicode character support (Korean, Chinese, Japanese, etc. via Pango)
- Adjustable screen position (top/bottom/left/right)
- Customizable colors and fonts

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

### Ubuntu/Debian (PPA)

Add the PPA and install:

```bash
sudo add-apt-repository ppa:unstable-code/wshowlyrics
sudo apt update
sudo apt install wshowlyrics
```

Or download `.deb` from [Releases](https://github.com/unstable-code/lyrics/releases):

```bash
sudo dpkg -i wshowlyrics_*_amd64.deb
sudo apt-get install -f  # Install dependencies
```

### Fedora/RHEL (COPR)

Enable the COPR repository and install:

```bash
sudo dnf copr enable unstable-code/wshowlyrics
sudo dnf install wshowlyrics
```

Or download `.rpm` from [Releases](https://github.com/unstable-code/lyrics/releases):

```bash
sudo dnf install wshowlyrics-*.rpm
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

## Usage

### Running as a systemd User Service (Recommended)

After installation, you can manage wshowlyrics as a systemd user service:

```bash
# Enable auto-start on login
systemctl --user enable wshowlyrics.service

# Start the service
systemctl --user start wshowlyrics.service

# Check service status
systemctl --user status wshowlyrics.service

# Stop the service
systemctl --user stop wshowlyrics.service

# Disable auto-start
systemctl --user disable wshowlyrics.service
```

**View logs with journalctl:**

```bash
# View all logs
journalctl --user -u wshowlyrics

# Follow logs in real-time
journalctl --user -u wshowlyrics -f

# View last 50 lines
journalctl --user -u wshowlyrics -n 50

# View logs since today
journalctl --user -u wshowlyrics --since today

# View logs with priority level (err, warning, info)
journalctl --user -u wshowlyrics -p err
```

The systemd service automatically captures all `log_info()`, `log_warn()`, and `log_error()` output to the systemd journal with proper timestamps and priority levels.

### Running Manually

You can also run wshowlyrics directly from the command line:

```bash
# Basic execution - automatically finds and displays lyrics for the current song
wshowlyrics

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
wshowlyrics -h
wshowlyrics --help

# Run in MPRIS mode (default)
wshowlyrics

# Run with Korean font
wshowlyrics -F "Noto Sans CJK KR 20"
wshowlyrics --font="Noto Sans CJK KR 20"

# Display at top of screen
wshowlyrics -a top -m 50
wshowlyrics --anchor=top --margin=50

# Transparent background with yellow text
wshowlyrics -b 00000066 -f FFFF00FF
wshowlyrics --background=00000066 --foreground=FFFF00FF

# Large bold font at bottom of screen
wshowlyrics -F "Sans Bold 28" -a bottom -m 40
wshowlyrics --font="Sans Bold 28" --anchor=bottom --margin=40
```

## Timing Offset Control

Adjust lyrics synchronization timing in real-time without restarting wshowlyrics.

### Overview

When lyrics are slightly out of sync with your music (due to audio latency, different player buffer times, or imperfect lyrics files), you can adjust the timing offset on the fly:

```bash
# Make lyrics appear 100ms later (slower)
echo "+100" > /tmp/wshowlyrics.fifo

# Make lyrics appear 100ms earlier (faster)
echo "-100" > /tmp/wshowlyrics.fifo

# Reset offset to 0
echo "0" > /tmp/wshowlyrics.fifo
```

### Helper Script

Install the convenience script for easier usage:

```bash
chmod +x wshowlyrics-offset
sudo cp wshowlyrics-offset /usr/local/bin/

# Usage
wshowlyrics-offset +100   # 100ms slower
wshowlyrics-offset -100   # 100ms faster
wshowlyrics-offset 0      # reset
```

### Sway Integration

Add these bindings to `~/.config/sway/config`:

```sway
# Timing offset control
bindsym $mod+Plus exec echo "+100" > /tmp/wshowlyrics.fifo
bindsym $mod+Minus exec echo "-100" > /tmp/wshowlyrics.fifo
bindsym $mod+0 exec echo "0" > /tmp/wshowlyrics.fifo
```

### Hyprland Integration

Add these bindings to `~/.config/hypr/hyprland.conf`:

```conf
# Timing offset control (using numpad keys)
bind = $mainMod, KP_Add, exec, echo "+100" > /tmp/wshowlyrics.fifo
bind = $mainMod, KP_Subtract, exec, echo "-100" > /tmp/wshowlyrics.fifo
bind = $mainMod, KP_0, exec, echo "0" > /tmp/wshowlyrics.fifo

# Alternative: using regular keys
bind = $mainMod SHIFT, equal, exec, echo "+100" > /tmp/wshowlyrics.fifo
bind = $mainMod, minus, exec, echo "-100" > /tmp/wshowlyrics.fifo
bind = $mainMod, 0, exec, echo "0" > /tmp/wshowlyrics.fifo
```

### Behavior

- **Cumulative mode**: Commands with `+` or `-` prefix add to the current offset (e.g., `+100` followed by `+100` = `+200ms` total)
- **Absolute mode**: Commands without prefix set the exact offset value (e.g., `500` sets offset to exactly 500ms)
- **Auto-reset**: Offset automatically resets to 0ms when a new track starts playing
- **Range**: Valid range is -10000ms to +10000ms (-10s to +10s)

### Multiple Instance Prevention

wshowlyrics uses a lock file (`/tmp/wshowlyrics.lock`) to prevent multiple instances from running simultaneously. If you see an error about another instance running but believe it's incorrect:

```bash
rm /tmp/wshowlyrics.lock
```

## Lyrics File Formats

### LRCX Format (Karaoke)
<img width="332" height="90" alt="1f8b963" src="https://github.com/user-attachments/assets/43dae72e-e38d-4b26-bb62-f76b96d8b65a" />


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

All lyrics file formats (LRCX, LRC, SRT, VTT) support ruby text annotations:

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
<img width="696" height="77" alt="a65e765" src="https://github.com/user-attachments/assets/1909f0c1-445b-4526-b30f-6a5df93e624d" />

Synchronized lyrics file format:

```lrc
[ti:Song Title]
[ar:Artist]
[al:Album]

[00:12.00]First line of lyrics
[00:17.50]Second line of lyrics
[00:23.00]Third line of lyrics
```

### SRT/VTT Format
<img width="361" height="131" alt="4cfd477" src="https://github.com/user-attachments/assets/664a2f52-c186-42dc-8f1b-c5e98c186b38" />


Subtitle formats are also supported:

**SRT Format:**
```srt
1
00:00:12,000 --> 00:00:17,500
First line of lyrics

2
00:00:17,500 --> 00:00:23,000
Second line of lyrics
```

**VTT (WebVTT) Format:**
```vtt
WEBVTT

00:00:12.000 --> 00:00:17.500
First line of lyrics

00:00:17.500 --> 00:00:23.000
Second line of lyrics
```

**Inline Translation Support**

Both SRT and VTT files support inline translation using the `{translation}` syntax at the beginning of a line:

```srt
1
00:00:12,000 --> 00:00:17,500
心の中で　響く
{Echoing in my heart}

2
00:00:17,500 --> 00:00:23,000
君の声が　聞こえる
{I can hear your voice}
```

- Syntax: `{translation text}` at the **beginning of a line** within the subtitle block
- Translation appears below the original text in smaller, dimmed font
- Works independently of DeepL API translation (no API key required)
- Ruby text (`main{ruby}`) can be combined with inline translation
- Supported in both SRT and VTT formats

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
- `filename.lrcx` / `filename.lrc` / `filename.srt` / `filename.vtt` (recommended! same name as music file)
- `title.lrcx` / `title.lrc` / `title.srt` / `title.vtt`
- `artist - title.lrcx` / `artist - title.lrc` / `artist - title.srt` / `artist - title.vtt`
- `artist/title.lrcx` / `artist/title.lrc` / `artist/title.vtt`

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

### Album Artwork

The program automatically displays album artwork in the system tray using a fallback chain:

1. **MPRIS Metadata** (Priority): Loads album art from MPRIS metadata if the music player provides it
2. **iTunes Search API Fallback**: If MPRIS doesn't provide album art, automatically fetches artwork from Apple's iTunes Search API
3. **Default Icon**: Shows a default music icon from your system theme if no artwork is available

The iTunes API feature can be disabled in `settings.ini` by setting `enable_itunes = false`.

## Translation Support (Multi-Provider)

wshowlyrics supports automatic lyrics translation using multiple AI provider APIs for LRC format files.

### Configuration

Add the following settings to `~/.config/wshowlyrics/settings.ini`:

```ini
[translation]
# Translation provider and model
# Available providers:
#   - gpt-4o-mini: OpenAI GPT-4o Mini (recommended, fast and cost-effective)
#   - gpt-4o: OpenAI GPT-4o (higher accuracy)
#   - gpt-3.5-turbo: OpenAI GPT-3.5 Turbo (older model, not recommended)
#   - deepl: DeepL API (https://www.deepl.com/docs-api)
#   - gemini-2.5-flash: Google Gemini 2.5 Flash model
#   - gemini-2.5-pro: Google Gemini 2.5 Pro model
#   - claude-sonnet-4-5: Anthropic Claude Sonnet 4.5
#   - claude-opus-4-5: Anthropic Claude Opus 4.5
#   - claude-haiku-4-5: Anthropic Claude Haiku 4.5
#   - false: Disable translation
provider = gpt-4o-mini

# API key for the selected provider
# Format varies by provider (get at provider's website):
#   - OpenAI: https://platform.openai.com/api-keys
#   - DeepL: https://www.deepl.com/pro-api
#   - Google Gemini: https://aistudio.google.com/apikey
#   - Anthropic Claude: https://console.anthropic.com/
api_key = your-api-key-here

# Target language for translation
# For supported language codes, see:
#   - DeepL: https://developers.deepl.com/docs/getting-started/supported-languages
#   - Google Gemini: Use language codes (e.g., EN, KO, JA, ZH, ES, FR, DE)
#   - Anthropic Claude: Use language codes (e.g., EN, KO, JA, ZH, ES, FR, DE)
# Common examples: EN, KO, JA, ZH-HANS (Simplified Chinese), ZH-HANT (Traditional Chinese), ES, FR, DE
target_language = EN

# Translation display mode
# Options:
#   both - Show both original and translation (translation displayed below original)
#   translation_only - Show only translated lyrics (hide original)
translation_display = both

# Translation text opacity (0.0 - 1.0)
# Controls how transparent/visible the translation text appears
# 0.7 = 70% opacity (default, slightly transparent)
# 0.9 = 90% opacity (more visible)
# 1.0 = 100% opacity (fully opaque, same as original text)
translation_opacity = 0.7

# Language detection optimization
# When enabled, automatically skips translation for text already in target language
# This saves API costs for mixed-language lyrics (e.g., English + Korean)
# Uses libexttextcat for detection (optional dependency - gracefully degrades if unavailable)
# Options: true (enabled, default) or false (disabled)
detect_language = true

# Rate limit delay (intuitive format)
# Controls the delay between translation requests to avoid hitting API rate limits
# Examples:
#   200: 200 milliseconds between requests
#   5s: 5 seconds between requests (1000ms = 1s)
#   10m: 10 requests per minute (60000ms / 10)
# Recommended by provider:
#   OpenAI: 1s (500 requests per minute for free tier)
#   DeepL: 200 or 5s (supports 300 requests/minute)
#   Gemini free tier: 10m (10 requests/minute limit)
#   Claude: 50m or higher (depending on account tier)
rate_limit = 10m

# Maximum retry attempts for rate limit errors
# Default: 3
# Use higher values for rate-limited accounts
max_retries = 3
```

### Supported Providers

**OpenAI API**
- Visit: https://platform.openai.com/api-keys
- Supported models:
  - `gpt-4o-mini`: Recommended - fast and cost-effective for translation
  - `gpt-4o`: Higher accuracy, more capable
  - `gpt-3.5-turbo`: Older model, not recommended
- Pricing: Pay-as-you-go based on tokens used
- Documentation: https://platform.openai.com/docs/guides/gpt

**DeepL API**
- Visit: https://www.deepl.com/pro-api
- Supported languages: https://developers.deepl.com/docs/getting-started/supported-languages
- Free tier: 500,000 characters/month
- Pro tier: Higher character limits with more competitive pricing

**Google Gemini**
- Visit: https://aistudio.google.com/apikey
- Available models:
  - `gemini-2.5-flash`: Fastest, lower cost, ideal for real-time translation
  - `gemini-2.5-pro`: Most capable, higher accuracy
- Free tier: 15 requests per minute
- Documentation: https://ai.google.dev/gemini-api/docs/models

**Anthropic Claude**
- Visit: https://console.anthropic.com/
- Available models:
  - `claude-haiku-4-5`: Fastest, most cost-effective
  - `claude-sonnet-4-5`: Balanced performance and cost
  - `claude-opus-4-5`: Most capable, highest accuracy
- Free trial and paid options available
- Documentation: https://claude.com/pricing#api

### Features

- **Multiple provider support**: Choose from OpenAI, DeepL, Google Gemini, or Anthropic Claude
- **Smart caching**: Translations are cached in `/tmp/wshowlyrics/translated/` and reused on subsequent playbacks
- **Language detection optimization**: Automatically skips translation for text already in target language, saving API costs for mixed-language lyrics
- **Format support**: Provider-based translation applies to LRC format files. SRT and VTT files support inline translation using `{translation}` syntax at line start (no API required). LRCX format is excluded.
- **Rate limiting**: Configurable rate limiting to respect API quotas and avoid errors
- **Auto-retry**: Automatic retry on rate limit errors (configurable attempts)
- **Cost-effective**: Translations are only performed once per song and cached until system restart
- **Configurable display**: Choose between showing both original + translation, or translation only
- **Adjustable opacity**: Control how visible the translation text appears

### Example Configuration

**OpenAI Setup:**
```ini
[translation]
provider = gpt-4o-mini
api_key = sk-proj-your-openai-api-key
target_language = EN
rate_limit = 1s
detect_language = true
```

**DeepL Setup:**
```ini
[translation]
provider = deepl
api_key = your-deepl-api-key
target_language = EN
rate_limit = 200
```

**Google Gemini Setup (Free Tier):**
```ini
[translation]
provider = gemini-2.5-flash
api_key = your-gemini-api-key
target_language = EN
rate_limit = 10m
max_retries = 3
```

**Claude Setup:**
```ini
[translation]
provider = claude-sonnet-4-5
api_key = your-anthropic-api-key
target_language = EN
rate_limit = 50m
```

### Example Output

With Japanese lyrics and `target_language = EN`:

```
Original:  心の中で　響く
Translation: Echoing in my heart
```

The translation appears below the original lyrics in a smaller, slightly dimmed font.

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
- **libexttextcat**: For language detection optimization in translation (improves translation efficiency for mixed-language lyrics)
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

## Build (for Development)

If you want to contribute to the project or test the latest changes:

```bash
meson setup build
meson compile -C build
```

### Development Usage

Run the compiled binary directly without installation:

```bash
# Basic execution - automatically finds and displays lyrics for the current song
./build/lyrics

# Play music with mpv
mpv --force-window=yes song.mp3

# Run with Korean font
./build/lyrics -F "Noto Sans CJK KR 20"

# Display at top of screen
./build/lyrics -a top -m 50

# All options from the Usage section above work with ./build/lyrics
```

## Legal Notice

> [!NOTE]
> wshowlyrics is a lyrics display tool only. The developers do not provide, host, or distribute lyrics content.
>
> - **Local files**: Users are responsible for obtaining lyrics files from legitimate sources.
> - **Online lyrics**: When local files are not found, this tool retrieves publicly available lyrics from [lrclib.net](https://lrclib.net) API (MIT License). The developers do not host, modify, or redistribute this content—lyrics are fetched directly from lrclib.net and displayed to the user in real-time.

> [!NOTE]
> **Translation Services**: This tool integrates with third-party AI translation APIs (OpenAI, DeepL, Gemini, Claude). Users are responsible for:
> - Obtaining and securing their own API keys
> - API usage costs according to their provider's pricing
> - Translation quality and accuracy (AI translations may contain errors)
> - Lyrics text being sent to external API services for private processing only (not public redistribution)

> [!CAUTION]
> **Explicit Content Warning**: Lyrics and their translations may contain explicit content (profanity, sexual references, violence, drug use, etc.). This tool does not provide content filtering. Parental supervision is recommended for minors.

> [!WARNING]
> Publicly sharing or redistributing lyrics content (e.g., uploading to websites, sharing in public repositories) may violate copyright laws. Users are solely responsible for ensuring compliance with applicable copyright laws in their jurisdiction.

## License

GNU General Public License v3.0 (GPL-3.0)

This project is based on wshowkeys and follows the same GPL-3.0 license.
