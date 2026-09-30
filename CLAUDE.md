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

On NixOS, `nix-shell` (root `shell.nix`) provides all build/fuzz deps — enter it first, then run the meson commands above. It sets `hardeningDisable = [ "fortify" ]` because nixpkgs injects `-D_FORTIFY_SOURCE=2`, which warns under the default debug (`-O0`) build and `werror=true` promotes that to an error.

### Fuzz Targets

Parsers (LRC/LRCX/SRT) plus the MPRIS-URI percent-decoder (`url_decode`) have libFuzzer + ASan targets. Build separately with clang:

```bash
# ASan build (default — runtime memory error detection)
CC=clang meson setup build-fuzz -Dfuzzing=true
meson compile -C build-fuzz
./build-fuzz/fuzz_lrc fuzz/corpus/lrc/ -max_total_time=60

# Valgrind-compatible build (ASan disabled — they conflict)
CC=clang meson setup build-valgrind -Dfuzzing=true -Dfuzz_sanitizer=none
valgrind --leak-check=full ./build-valgrind/fuzz_lrc fuzz/corpus/lrc/
```

Fuzz sources are in `fuzz/`, seed corpora in `fuzz/corpus/{lrc,lrcx,srt,url_decode}/`. The parser targets share `parser_sources` with the main binary; `fuzz_url_decode` links only the self-contained `src/utils/url/url_utils.c` (no parser deps). See `meson.build`.

## CI/CD: GitLab Primary, GitHub Mirror

**Source of truth is the self-hosted GitLab** (`origin` → `gitlab.gggames.synology.me/unstable-code/wshowlyrics`); `downstream` (`github.com/wshowlyrics/wshowlyrics`) is an auto push mirror — never push it manually. Use `glab` instead of `gh` for MRs.

**Public artifact URLs point at the GitHub mirror**, not at `origin`. Packaging metadata (`URL`, `Homepage`, `Vcs-Git`, `Source`) and every source-tarball fetch in `.gitlab/actions/publish.yml` use `github.com/wshowlyrics/wshowlyrics`, so end users (AUR/COPR/PPA/NUR) never depend on the self-hosted instance being reachable. Keep it that way when adding publish steps.

- **GitLab CI** (`.gitlab-ci.yml` + `.gitlab/actions/*.yml`) handles build, security (gitleaks, semgrep), package (AUR/PPA/COPR), publish (NUR, releases).
- **GitHub Actions** (`.github/workflows/coverity-scan.yml`) runs Coverity Scan only — Coverity is GitHub-only.
- Pipeline rules trigger on: `RUN_ALL=true`, version tags `v*.*.*`, MRs, or master pushes.

### Runners: the Synology Host Runs an Old Kernel

Jobs land on one of two runners, and the split is not cosmetic.

| Runner | Executor | Jobs |
|---|---|---|
| Synology instance runner | docker | everything untagged |
| `nix-build` (HM-iMac, NixOS) | **shell** | `publish:nur-unstable`, `publish:nur-stable` |

The Synology host runs **kernel 4.4.302+**. Containers share the host kernel, so
anything that installs its **own seccomp filter inside the container fails there**
with `EINVAL`, no matter what image or `security_opt` is used — `seccomp=unconfined`
does not help, because the failure is nix/pacman's own `seccomp()` call being
rejected, not Docker's profile. Two consequences are baked into the CI files:

- **pacman jobs must disable its sandbox before the first `pacman` call.** Every
  such job does `sed -i '/^\[options\]/a DisableSandbox' /etc/pacman.conf` first
  (`build:makepkg`, `publish:aur-git`, `publish:aur-stable`). Omit it and the job
  dies on `pacman -Syu` with `error restricting syscalls via seccomp: 22!`.
- **nix jobs cannot run there at all.** They are tagged `nix-build` and run on the
  NixOS host instead, where nix is native. `filter-syscalls = false` would also
  work around it, but routing to a modern kernel is preferred.
- **Syscalls newer than 4.4 return `ENOSYS`, and not every tool falls back.**
  Fedora 44's tar extracts with `openat2(RESOLVE_BENEATH)` (kernel 5.6), so
  `rpmbuild` `%prep` fails with `Cannot mkdir: Function not implemented`;
  `package:rpm` is therefore pinned to `fedora:43`. Before bumping an image, run
  the job locally under a seccomp profile that returns errno 38 for post-4.4
  syscalls (`openat2`, `clone3`, `fchmodat2`, `faccessat2`, ...) — `statx` and
  the `fs*` mount calls cannot be blocked because runc itself needs them.

Jobs that run `git` against the checkout also need
`git config --global --add safe.directory "$CI_PROJECT_DIR"` — the build directory
is owned by a different uid than the job user (`fatal: detected dubious ownership`,
exit 128).

**The `nix-build` runner is a shell executor**, so for those two jobs:
`image:` is ignored (don't add one), the script runs on the real host with the
runner service's PATH (git, nix, coreutils, gnused, gnugrep, tar, gzip, xz, openssh
— but **no `findutils`**), and the build directory is reused between runs, so a
clone target is `rm -rf`'d before `git clone`. Never install packages there
(`nix-env -i` would mutate the actual machine's profile on every nightly).

**GitHub release binaries are x86_64 only.** The arm64 deb/rpm/AppImage jobs were
removed (wshowlyrics#11): across 65 releases the arm64 and x86_64 download counts
matched exactly for 65 of 76 asset pairs — whole-release scrapers, not people — and
the human-attributable aarch64 residue was 2 downloads. Rebuilding them would also
need a runner nobody has: the Synology host cannot register binfmt emulation (the
`binfmt_misc` `F` flag needs kernel 4.8).

aarch64 users are still served, because every other channel builds on its own
side from source: AUR (`arch=('x86_64' 'aarch64')`), COPR (`fedora-*-aarch64`
chroots), the Launchpad PPA (arm64 enabled) and NUR (`platforms.linux`). Those
architecture lists live outside this repo — in the AUR PKGBUILDs, the COPR project
settings, the PPA settings and `nur-packages` — so nothing here needs to change
to keep them.

## Release Process

Version strings live in two places and **must be synced before tagging**:

1. `meson.build` line 4: `version: 'X.Y.Z'`
2. `src/constants.h`: `#define USER_AGENT_STRING "wshowlyrics/X.Y.Z"`

```bash
sed -i "0,/^\tversion: '[^']*'/s//\tversion: 'X.Y.Z'/" meson.build   # anchored: must not touch meson_version
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

To hide the overlay (e.g. during an instrumental break), `rendering_manager_render_transparent` attaches a **fully-transparent cleared buffer** rather than `wl_surface_attach(NULL)`. Attaching NULL unmaps the layer surface, and compositors (KWin, and wlroots' layer-shell on Hyprland/Sway) do not reliably re-map or restore position when a buffer is re-attached afterwards — the overlay would stay gone. Keeping the surface mapped with a cleared buffer works uniformly across compositors (no per-compositor detection).

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

`src/user_experience/system_tray/system_tray.c` uses libappindicator-gtk3 to publish an SNI tray icon. Album art fallback chain: per-track cache (`~/.cache/wshowlyrics/album_art/{md5}.png`) → MPRIS `mpris:artUrl` → local file embedded cover (`try_local_embedded_artwork`) → local video thumbnail (`try_local_video_thumbnail`) → iTunes Search API (`src/provider/itunes/itunes_artwork.c`) → default theme icon. The two `ffmpeg`-based local steps extract from `xesam:url` and run before iTunes so a local track whose player omits `mpris:artUrl` (e.g. mpv) still resolves offline: the embedded step maps only the `attached_pic` stream (`-map 0:v -map -0:V`, so it never grabs a real video frame), and the thumbnail step centre-crops a representative frame (`-map 0:V:0` + `thumbnail,crop,scale`) for video files with no embedded cover. `ffmpeg` is an optional runtime dep — both steps degrade to iTunes when it is absent. Shared helpers: `resolve_local_media_path` (decode + `realpath` + regular-file check), `run_ffmpeg_extract` (spawn + exit/output verify), `commit_local_art` (validated decode + cache). Tray menu has track info, overlay toggle, timing offset submenu, and "Edit Settings" (needs `$EDITOR` and `$TERMINAL`).

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

**Runtime**: playerctl, a `wlr-layer-shell` compositor (Sway, Hyprland, KDE Plasma 5.27+). Optional: `ffmpeg` (extracts embedded cover art from local files for the tray icon; without it that fallback step is skipped).

## Helper Scripts

- `wshowlyrics-offset` — D-Bus client for timing offset and overlay toggle (installed to `bindir`).
- `tools/add_furigana.py` — offline furigana (pyopenjtalk readings over pykakasi structure; `--regenerate` rebuilds existing files). `tools/add_furigana_ai.py` — optional cloud-LLM overlay. `tools/convert_vtt_to_srt.py` — VTT→SRT. All content-prep helpers (not built/installed); NixOS deps via `tools/furigana-shell.nix`.
