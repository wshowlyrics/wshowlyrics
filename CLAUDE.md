# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build System

Meson with strict flags (`c_std=c11`, `warning_level=2`, `werror=true`).

```bash
meson setup build
meson compile -C build
./build/lyrics              # binary is renamed to wshowlyrics on install
```

The installed binary is `wshowlyrics`; locally it is `build/lyrics`. Always rebuild with `werror=true` in mind — any new warning breaks the build.

### Fuzz Targets

Parsers (LRC/LRCX/SRT) have libFuzzer + ASan targets. Build separately with clang:

```bash
# ASan build (default — runtime memory error detection)
CC=clang meson setup build-fuzz -Dfuzzing=true
meson compile -C build-fuzz
./build-fuzz/fuzz_lrc fuzz/corpus/lrc/ -max_total_time=60

# Valgrind-compatible build (ASan disabled — they conflict)
CC=clang meson setup build-valgrind -Dfuzzing=true -Dfuzz_sanitizer=none
valgrind --leak-check=full ./build-valgrind/fuzz_lrc fuzz/corpus/lrc/
```

Fuzz sources are in `fuzz/`, seed corpora in `fuzz/corpus/{lrc,lrcx,srt}/`. Fuzz targets share `parser_sources` with the main binary (see `meson.build`).

## CI/CD: GitLab Primary, GitHub Mirror

**Source of truth is GitLab** (`gitlab.com/wshowlyrics/wshowlyrics`); GitHub is a push mirror. Use `glab` instead of `gh` for MRs.

- **GitLab CI** (`.gitlab-ci.yml` + `.gitlab/actions/*.yml`) handles build, security (gitleaks, semgrep), package (AUR/PPA/COPR), publish (NUR, releases).
- **GitHub Actions** (`.github/workflows/coverity-scan.yml`) runs Coverity Scan only — Coverity is GitHub-only.
- Pipeline rules trigger on: `RUN_ALL=true`, version tags `v*.*.*`, MRs, or master pushes.

## Release Process

Version strings live in two places and **must be synced before tagging**:

1. `meson.build` line 4: `version: 'X.Y.Z'`
2. `src/constants.h`: `#define USER_AGENT_STRING "wshowlyrics/X.Y.Z"`

```bash
sed -i "s/version: '[^']*'/version: 'X.Y.Z'/" meson.build
sed -i 's|"wshowlyrics/[^"]*"|"wshowlyrics/X.Y.Z"|' src/constants.h
git commit -am "chore: Bump version to X.Y.Z"
git tag -s vX.Y.Z -m "Release vX.Y.Z"
git push origin master --tags
```

Tagging triggers downstream package workflows (AUR `wshowlyrics`, PPA, COPR stable, NUR `default.nix`). Non-tag master pushes trigger nightly publishes (COPR nightly, NUR `unstable.nix`).

## Code Architecture

### Two-Tier Lyrics Provider Chain

`src/provider/lyrics/lyrics_provider.c` searches local files first (same dir as music file → `$XDG_MUSIC_DIR` → `~/.lyrics/` → `$HOME`), then `src/provider/lrclib/lrclib_provider.c` falls back to lrclib.net. **URL decoding is mandatory** for MPRIS file URIs (Korean/Japanese/Unicode paths). Online provider only accepts synced lyrics — plain text is dropped to keep sync intact.

If `[lyrics] extensions` in settings.ini is empty, local search is skipped entirely.

### Parsers — All Live Under `src/parser/lrc/`

Despite the directory name, **`src/parser/lrc/` holds LRC, LRCX, and the shared `lrc_common.c`**. Only SRT lives elsewhere (`src/parser/srt/`). General parser helpers (ruby/timestamp parsing) are in `src/parser/utils/parser_utils.c`.

| Format | File | Segment type | Timing |
|--------|------|--------------|--------|
| LRC    | `parser/lrc/lrc_parser.c`   | `ruby_segment` | line-level |
| LRCX   | `parser/lrc/lrcx_parser.c`  | `word_segment` | word-level + unfill |
| SRT    | `parser/srt/srt_parser.c`   | `ruby_segment` | line + end time |

**Format detection is by extension, not content** (`is_lyrics_format(state, ".lrcx")` in `main.c`). A `.lrc` containing word timestamps will not get karaoke rendering.

Ruby/furigana syntax `主{ふり}` is parsed by `parse_ruby_segments` / `parse_word_segments_with_ruby` in `parser_utils.c` and works in all formats. SRT/VTT also support inline `{translation}` lines (no API needed).

### Core Data Structures (`src/lyrics_types.h`)

- `struct lyrics_line` holds **either** `segments` (LRCX `word_segment*`) **or** `ruby_segments` (LRC/SRT `ruby_segment*`) — never both. Free both branches when cleaning up.
- `struct lyrics_data` owns translation thread state via atomics (`translation_in_progress`, `translation_should_cancel`, `translation_thread_active`) plus `pthread_t translation_thread`.
- `ruby_segment` and `word_segment` both carry a `translation` slot (per-segment, used by inline SRT/VTT translation).

### Rendering Pipeline — Files Are NOT Under `core/rendering/`

Only the orchestrator lives there:
- `src/core/rendering/rendering_manager.c` — coordinator, format detection, frame composition
- `src/utils/render/word_render.c` — LRCX karaoke progressive fill
- `src/utils/render/ruby_render.c` — LRC/SRT with furigana positioning
- `src/utils/render/render_common.c` — shared utilities (background, plain text)
- `src/utils/render/render_params.h` — shared render parameter struct

Wayland surface lifecycle is split:
- `src/utils/wayland/wayland_manager.c` — connection lifecycle, reconnection (`wayland_manager_reconnect_full`), event dispatch
- `src/utils/wayland/wayland_init.c` — surface init wrapper called from `main.c`

The compositor quirk flag `state->no_buffer_detach` exists because some compositors reset surface position when `wl_surface_attach(NULL)` is used; in that case a transparent buffer is used instead.

### Translation System (Async)

`src/translator/{openai,deepl,gemini,claude}/` — all share `src/translator/common/translator_common.c` for caching, language detection, ruby stripping, and last-line extraction (handles AI over-explanation where the model wraps the translation in commentary).

- **LRC only** for API-based translation. LRCX (word-level timing) and SRT/VTT (own subtitle format) are excluded.
- Translation runs in a pthread; check `translation_in_progress` before reading per-line `translation` fields, and set `translation_should_cancel = true` then `pthread_join` when loading new lyrics.
- **Cache lives at `~/.cache/wshowlyrics/` (or `$XDG_CACHE_HOME/wshowlyrics/`)** — JSON keyed by `{md5}_{lang}` for partial resume across runs. Cleared via `--purge=translations`.
- `is_already_in_language()` (`src/utils/lang_detect/lang_detect.c`) skips API calls when text is already in target language. Uses libexttextcat if available (optional dep, gracefully degrades).
- Rate limit format is intuitive: `200` (ms), `5s`, `10m` (10 req/min).

### MPRIS, Monitoring, D-Bus Control

- `src/utils/mpris/mpris.c` — uses `playerctl` to extract metadata and playback position, tracks file changes to trigger re-search.
- `src/monitor/file_monitor.c` — generic MD5-based hot-reload for both lyrics and config files (`file_monitor_check_and_reload`).
- `src/utils/dbus_control/dbus_control.c` — exposes `org.wshowlyrics.Control` D-Bus service used by the `wshowlyrics-offset` shell helper. Methods: `AdjustTimingOffset(int16)`, `SetTimingOffset(int16)`, `ResetTimingOffset()`, `ToggleOverlay()`, `SetOverlay(bool)`. Range −10000..+10000 ms; session offset auto-resets on track change, global offset comes from `[lyrics] global_offset_ms`.
- `src/utils/lock/lock_file.c` — single-instance lock at `$XDG_RUNTIME_DIR/wshowlyrics/wshowlyrics.lock` (path provided by `src/utils/runtime/runtime_dir.c`).

### System Tray

`src/user_experience/system_tray/system_tray.c` uses libappindicator-gtk3 to publish an SNI tray icon. Album art fallback chain: MPRIS metadata → iTunes Search API (`src/provider/itunes/itunes_artwork.c`) → default theme icon. Tray menu has track info, overlay toggle, timing offset submenu, and "Edit Settings" (needs `$EDITOR` and `$TERMINAL`).

### Configuration

Loaded from `~/.config/wshowlyrics/settings.ini`. If absent, copies from `/etc/wshowlyrics/settings.ini` (installed by meson); if that also fails, built-in defaults apply. CLI args override file. Config is hot-reloaded via MD5 checksum (`state->config_md5_checksum`).

Sections: `[display]`, `[lyrics]`, `[translation]`, `[monitor]`. See `settings.ini.example`.

### Help Text Is Fetched at Runtime

`--help` fetches `docs/help.txt` from GitHub raw at runtime (`display_detailed_help` in `main.c`) with a 5s timeout, falling back to a stub if offline. **Update `docs/help.txt` on master to change help output for installed users without rebuild.**

## Critical Code Patterns

### Free Both Segment Types

```c
if (line->segments) { /* word_segment list — LRCX */ }
if (line->ruby_segments) { /* ruby_segment list — LRC/SRT */ }
```

Both can have non-NULL `ruby` and `translation` strings.

### Cancel Translation Before Reload

```c
data->translation_should_cancel = true;
if (data->translation_thread_active) {
    pthread_join(data->translation_thread, NULL);
    data->translation_thread_active = false;
}
```

### Unicode Paths from MPRIS

MPRIS gives `file:///` URIs with percent-encoding. Always URL-decode before opening (`extract_directory_from_url` and friends).

## Code Quality (SAST)

| Tool | Where | Trigger | Focus |
|------|-------|---------|-------|
| Gitleaks | GitLab CI | After build | Secrets in history |
| Semgrep | GitLab CI | After build | OWASP, C patterns (`p/security-audit`, `p/c`, `p/ci`) |
| SonarCloud | external | Every commit/MR | General quality, PR decoration |
| Coverity | GitHub Actions | Sundays 00:00 UTC | Race/deadlock, deep dataflow |

Dashboards:
- SonarCloud: https://sonarcloud.io/project/overview?id=wshowlyrics_wshowlyrics
- Coverity: https://scan.coverity.com/projects/wshowlyrics

### Querying SonarCloud Issues via API (no auth for public project)

```bash
# Top 10 by severity
curl "https://sonarcloud.io/api/issues/search?componentKeys=wshowlyrics_wshowlyrics&resolved=false&ps=10&s=SEVERITY&asc=false"

# Filter by type
curl "https://sonarcloud.io/api/issues/search?componentKeys=wshowlyrics_wshowlyrics&types=BUG&resolved=false"
curl "https://sonarcloud.io/api/issues/search?componentKeys=wshowlyrics_wshowlyrics&types=VULNERABILITY&resolved=false"
```

Quality Gate uses **"Previous version"** as new-code definition — release tagging is the cadence, not days.

### Marking Issues / Hotspots (Write API)

Token at `~/.config/sonarcloud/token` (chmod 600). Pass with `-u "${TOKEN}:"`. Required when bulk-marking false positives or Safe hotspots.

```bash
TOKEN="$(cat ~/.config/sonarcloud/token)"

# Issue: transition = wontfix | falsepositive | resolve | reopen
curl -u "${TOKEN}:" -X POST "https://sonarcloud.io/api/issues/do_transition" \
  --data-urlencode "issue=<key>" --data-urlencode "transition=falsepositive"
curl -u "${TOKEN}:" -X POST "https://sonarcloud.io/api/issues/add_comment" \
  --data-urlencode "issue=<key>" --data-urlencode "text=<reason>"

# Hotspot: resolution = FIXED | SAFE only (ACKNOWLEDGED is web-UI only — API rejects it)
curl -u "${TOKEN}:" -X POST "https://sonarcloud.io/api/hotspots/change_status" \
  --data-urlencode "hotspot=<key>" --data-urlencode "status=REVIEWED" \
  --data-urlencode "resolution=SAFE" --data-urlencode "comment=<reason>"
```

**Standing marking policy** (apply when issues recur, e.g. project-key reanalysis):

| Rule | Disposition | Reason |
|---|---|---|
| `c:S107`, `c:S995` in events/wayland_events, system_tray, dbus_control, shm, wayland_manager | wontfix | External callback signatures (Wayland/GTK/GDBus) — parameter list fixed by framework |
| `c:S4423` (weak TLS) on any `curl_easy_setopt` site | falsepositive | Code explicitly enforces `CURL_SSLVERSION_TLSv1_2` |
| `c:S3519` parser_utils.c UTF-8 backwards iter | falsepositive | `NOSONAR` + defensive bounds check already in place |
| `c:S5813` `strlen` hotspot | SAFE | Inputs are NULL-terminated by call sites (Phase H verified) |
| `c:S5849` permission/capability hotspot | SAFE | Verified in Phase S (uses `g_spawn_async`, no shell injection) |
| `c:S4790` MD5 hotspot in file_utils | SAFE | Cache key / change detector only, no cryptographic use |
| `c:S5332` HTTP hotspot in system_tray.c | SAFE + acknowledgement comment | URL is external (MPRIS / iTunes CDN); cannot enforce HTTPS without breaking compatibility. API does not accept ACKNOWLEDGED resolution, so use SAFE with explicit note |
| `c:S1820` `lyrics_state` 42-field struct | **do not mark — fix in code** | Real refactor (split into sub-structs) |

### Commit Format for SAST Fixes

```
fix: <brief description>

<explanation>

Changes:
- <change 1>
- <change 2>

Benefits:
- <benefit 1>
- <benefit 2>

Fixes: SonarCloud issues <key1>, <key2>      (or: Coverity defect CID-12345)
```

Always include the issue keys for traceability.

## Testing

```bash
# Same-name lyrics file beats everything else
cp song.mp3 ~/test-lyrics/
cp song.lrc ~/test-lyrics/
mpv --force-window=yes ~/test-lyrics/song.mp3
./build/lyrics
playerctl metadata                    # verify MPRIS data
playerctl metadata -f '{{position}}'  # microseconds
```

See `docs/TESTING.md` for the Unicode-path scenarios.

## Dependencies

**Build**: cairo, pango, pangocairo, fontconfig, wayland-client, wayland-protocols, libcurl, openssl, json-c, libappindicator-gtk3 (`appindicator3-0.1`), gdk-pixbuf-2.0, gio-2.0, librt, meson, ninja. Optional: libexttextcat (language detection).

**Runtime**: playerctl, a `wlr-layer-shell` compositor (Sway, Hyprland, KDE Plasma 5.27+).

## Helper Scripts

- `wshowlyrics-offset` — D-Bus client for timing offset and overlay toggle (installed to `bindir`).
- `docs/add_furigana.py`, `docs/convert_vtt_to_srt.py` — content-prep helpers (not built/installed).
