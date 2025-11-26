# Changelog

## v0.2.0 - Local-First Update

### Major Changes

1. **lrclib API Disabled (Temporarily)**
   - Focused on perfecting local file support first
   - Eliminates unnecessary network calls
   - Faster startup and lyrics loading

2. **URL Decoding for Unicode Support** ✅
   - Added proper URL decoding (`%XX` → actual characters)
   - Fixed issue where encoded paths prevented file loading
   - Now handles Korean, Japanese, Chinese filenames perfectly
   - Example: `%ED%95%9C%EA%B8%80.mp3` → `한글.mp3` ✅

3. **Filename-Based Search** ✅
   - NEW: Searches for `filename.lrc` matching `filename.mp3`
   - Most reliable method for finding lyrics
   - No dependency on metadata accuracy

### Search Priority (Updated)

When a track is playing at `/music/artist/song.mp3`:

1. **`/music/artist/song.lrc`** (filename match) ⭐ NEW!
2. `/music/artist/song.srt` (filename match)
3. `/music/artist/<title>.lrc` (metadata title)
4. Other standard locations...

### Why This Matters

**Before**:
- Encoded URLs like `file:///music/%ED%85%8C%EC%8A%A4%ED%8A%B8.mp3`
- Program searched for file: `/music/%ED%85%8C%EC%8A%A4%ED%8A%B8.lrc`
- File actually at: `/music/테스트.lrc`
- ❌ Not found!

**After**:
- URL decoded to: `/music/테스트.mp3`
- Program searches: `/music/테스트.lrc`
- ✅ Found immediately!

## Previous Updates (v0.1.0)

### Fixed Issues

1. **MPRIS Position Detection** ✅
   - Changed from `playerctl position` to `playerctl metadata -f '{{position}}'`
   - Now correctly returns position in microseconds instead of seconds
   - Fixes synchronization accuracy

2. **Manual Mode Timing** ✅
   - `-l` option now uses MPRIS for timing when available
   - Previously showed only the first line
   - Now properly syncs lyrics with playback even when loading a specific file

3. **API Search Optimization** ✅
   - Automatically removes file extensions (.mp3, .flac, etc.) before searching
   - Improves search success rate on lrclib.net
   - Example: "song.mp3" → searches for "song"

4. **Smart Local File Search** ✅
   - **Priority 1**: Directory of currently playing file (NEW!)
   - Priority 2: Current directory
   - Priority 3: `$XDG_MUSIC_DIR`
   - Priority 4: `~/.lyrics`
   - Priority 5: `$HOME`

### New Features

1. **File URL Tracking**
   - Added `url` field to track metadata
   - Uses `playerctl metadata xesam:url` to get file path
   - Enables directory-based lyrics search

2. **Provider Priority System**
   - Local files always checked first
   - Online APIs (lrclib) only used as fallback
   - Significantly faster when lyrics exist locally

### Implementation Details

#### Changed Files

**mpris.c/h**:
- Added `url` field to `track_metadata` struct
- Fixed `mpris_get_position()` to use metadata format
- Added URL retrieval from MPRIS

**lyrics_provider.c/h**:
- Updated provider interface to accept `url` parameter
- Added `get_directory_from_url()` helper function
- Added `remove_extension()` for better API searches
- Reordered search directories to prioritize file location
- Added support for `file://` URL scheme

**main.c**:
- Manual mode (-l) now attempts MPRIS timing sync
- Better fallback handling when MPRIS unavailable

### Usage Example

```bash
# With music file at: /home/user/Music/Artist/song.mp3
# And lyrics at:     /home/user/Music/Artist/song.lrc

./build/lyrics

# The program will:
# 1. Detect song.mp3 via MPRIS
# 2. Extract directory: /home/user/Music/Artist/
# 3. Search for song.lrc in that directory FIRST
# 4. Find and load it immediately (no API call needed!)
```

### Performance Improvements

- **Faster lyrics loading**: Checks file directory first
- **Reduced API calls**: Only queries lrclib when no local file found
- **Better accuracy**: Proper microsecond timing from MPRIS

### Migration Notes

No breaking changes. All existing functionality preserved.

The `-l` option now has improved behavior:
- **Before**: Static display, no timing sync
- **After**: Syncs with playback if MPRIS available

Provider search order is now:
1. Local files (smart search)
2. lrclib.net (fallback)

This ensures the fastest possible lyrics loading while maintaining
automatic download capability for songs without local lyrics.
