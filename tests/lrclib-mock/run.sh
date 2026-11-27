#!/usr/bin/env bash
# Does the lrclib provider retry a failure that may pass, tell it apart from
# "not found", refuse an oversized answer, and stop waiting when the track
# changes? (#22)
#
# lrclib.net cannot be made to fail on demand, so this plays silent tracks
# against a mock server (mock.py) whose answer depends on the track's title.
#
# Needs a test build that sends the lrclib requests to the mock:
#     meson setup build-mock -Dlrclib_mock_port=8071
#     meson compile -C build-mock
# then, from the repository root and with no wshowlyrics running:
#     tests/lrclib-mock/run.sh
#
# Also needs mpv, playerctl, ffmpeg, curl, busctl, stdbuf and python3 (on NixOS
# python3 is taken from nix-shell if it is not on PATH). A Wayland session is
# required, as for the program itself.
#
# Nothing is sent to lrclib.net. The program runs with the shipped example
# settings and a cache of its own, in a temporary directory that is removed at
# the end (KEEP=1 keeps it); the user's settings and cache are not touched.
# Players that are playing are paused meanwhile and resumed afterwards.
# About 100 s. The output is for reading: each case states what to expect.
set -uo pipefail
here=$(dirname "$(readlink -f "$0")")
lyrics_bin=${LYRICS_BIN:-./build-mock/lyrics}
port=${LRCLIB_MOCK_PORT:-8071}

[ -x "$lyrics_bin" ] || { echo "$lyrics_bin not found - build it first (see the top of this script)"; exit 1; }
[ -f settings.ini.example ] || { echo "run this from the repository root (settings.ini.example not found)"; exit 1; }
if busctl --user status org.wshowlyrics.Control >/dev/null 2>&1; then
    echo "wshowlyrics is already running - stop it first"; exit 1
fi

# (no "lrclib" in the name: the log filter below matches on that word)
work=$(mktemp -d "${XDG_RUNTIME_DIR:-/tmp}/wshowlyrics-mocktest.XXXXXX")
env_dir=$work/env
requests=$work/requests.log
mkdir -p "$env_dir/config/wshowlyrics" "$env_dir/cache" "$work/tracks"
install -m 600 settings.ini.example "$env_dir/config/wshowlyrics/settings.ini"
: > "$requests"

# Silent tracks; the title selects the mock's behaviour, artist and album are
# there so that the exact-match API is asked first
for name in retry-ok ratelimit toolong down rejected huge hugelen notfound cancel; do
    ffmpeg -loglevel error -y -f lavfi -i anullsrc=r=8000:cl=mono -t 30 -c:a libopus -b:a 6k \
        -metadata title="$name" -metadata artist="Mock" -metadata album="Mock" \
        "$work/tracks/$name.mka" || { echo "ffmpeg could not make the test tracks"; exit 1; }
done

were_playing=()
mpv_pids=()
lyrics_pid=
cleanup() {
    [ -n "$lyrics_pid" ] && kill -INT "$lyrics_pid" 2>/dev/null
    for pid in "${mpv_pids[@]}"; do kill "$pid" 2>/dev/null; done
    [ -f "$work/mock.pid" ] && kill "$(cat "$work/mock.pid")" 2>/dev/null
    for p in "${were_playing[@]}"; do playerctl -p "$p" play 2>/dev/null; done
    if [ -n "${KEEP:-}" ]; then
        echo "(kept: $work)"
    else
        rm -rf "$work"
    fi
}
trap cleanup EXIT

if command -v python3 >/dev/null 2>&1; then
    python3 "$here/mock.py" "$port" "$requests" "$work/mock.pid" </dev/null >"$work/mock.out" 2>&1 &
else
    nix-shell -p python3 --run "exec python3 '$here/mock.py' $port '$requests' '$work/mock.pid'" \
        </dev/null >"$work/mock.out" 2>&1 &
fi
for _ in $(seq 150); do
    curl -s -m 1 "http://127.0.0.1:$port/ping" >/dev/null 2>&1 && break
    sleep 0.2
done
curl -s -m 1 "http://127.0.0.1:$port/ping" >/dev/null 2>&1 ||
    { echo "the mock server did not start:"; cat "$work/mock.out"; exit 1; }
for p in $(playerctl -l 2>/dev/null); do
    if [ "$(playerctl -p "$p" status 2>/dev/null)" = Playing ]; then
        were_playing+=("$p")
        playerctl -p "$p" pause
    fi
done
[ ${#were_playing[@]} -gt 0 ] && echo "paused for the test: ${were_playing[*]}"

start_lyrics() {  # $1 = log file
    # stdbuf: line-buffered stdout, so that INF lines reach the log as they happen
    env "XDG_CONFIG_HOME=$env_dir/config" "XDG_CACHE_HOME=$env_dir/cache" stdbuf -oL \
        "$lyrics_bin" >"$1" 2>&1 &
    lyrics_pid=$!
}

stop_all() {
    kill -INT "$lyrics_pid" 2>/dev/null
    wait "$lyrics_pid" 2>/dev/null
    lyrics_pid=
    for pid in "${mpv_pids[@]}"; do kill "$pid" 2>/dev/null; wait "$pid" 2>/dev/null; done
    mpv_pids=()
}

show() {  # $1 = log file, $2 = track name
    echo "requests the mock server saw:"
    grep -E " $2 " "$requests" | sed 's/^/    /'
    echo "lyrics log:"
    sed 's/\x1b\[[0-9;]*m//g' "$1" |
        grep -E "lrclib|Response body|synced lyrics|Cached lyrics|Loaded .* lines|No lyrics found|dropped has ended|Track changed|Title:" |
        grep -v "Skipped provider" | sed 's/^/    /'
}

run_case() {  # $1 = track name, $2 = seconds to let it run, $3 = what is expected
    echo
    echo "=== $1 - expected: $3"
    mpv --no-video --ao=null --really-quiet --loop-file=inf "$work/tracks/$1.mka" </dev/null >/dev/null 2>&1 &
    mpv_pids+=($!)
    sleep 2
    start_lyrics "$work/$1.log"
    sleep "$2"
    stop_all
    show "$work/$1.log" "$1"
}

run_case retry-ok  9 "503, 503, 200 about 1 s and 3 s apart; lyrics loaded (3 lines)"
run_case ratelimit 7 "429, then 200 about 2 s later (Retry-After); lyrics loaded"
run_case toolong   5 "one 429 only, 'asks to wait more than 5 s', no search request"
run_case down      9 "three 503s, 'gave up after 3 failed attempts', no search request"
run_case rejected  5 "one 400 (WRN, no retry), then the search API is still asked"
run_case huge      5 "one request, refused as too large (by libcurl 8.4+ itself: 'Maximum file size exceeded'; by the write callback on older libcurl: 'Response body exceeds'); no retry, no search"
run_case hugelen   5 "one request, refused on its announced length; no retry, no search"
run_case notfound  5 "one 404 and one search, no retry, logged as INF"

# Track change while waiting to retry: the search must stop, not finish its
# attempts. The second player takes over once the mock has seen two requests,
# which is inside the 3 s wait before the third.
echo
echo "=== cancel - expected: two 503s only; 'dropped has ended' within a second of the track change"
mpv --no-video --ao=null --really-quiet --loop-file=inf "$work/tracks/cancel.mka" </dev/null >/dev/null 2>&1 &
mpv_pids+=($!)
sleep 2
first=$(playerctl -l 2>/dev/null | grep '^mpv' | while read -r p; do
    playerctl -p "$p" metadata xesam:url 2>/dev/null | grep -q "/tracks/cancel.mka" && echo "$p"; done | head -n 1)
mpv --no-video --ao=null --really-quiet --loop-file=inf --pause "$work/tracks/notfound.mka" </dev/null >/dev/null 2>&1 &
mpv_pids+=($!)
sleep 2
second=$(playerctl -l 2>/dev/null | grep '^mpv' | while read -r p; do
    playerctl -p "$p" metadata xesam:url 2>/dev/null | grep -q "/tracks/notfound.mka" && echo "$p"; done | head -n 1)
if [ -z "$first" ] || [ -z "$second" ]; then
    echo "could not find the two test players on MPRIS"; exit 1
fi
: > "$work/cancel.log"
start_lyrics "$work/cancel.log"
for _ in $(seq 100); do
    [ "$(grep -c " cancel " "$requests")" -ge 2 ] && break
    sleep 0.1
done
changed_at=$(date +%s%N)
playerctl -p "$second" play
playerctl -p "$first" pause
ended_ms=
for _ in $(seq 60); do
    if grep -q "dropped has ended" "$work/cancel.log"; then
        ended_ms=$(( ($(date +%s%N) - changed_at) / 1000000 ))
        break
    fi
    sleep 0.1
done
sleep 5   # long enough for a third attempt to show up if the wait went on
stop_all
if [ -n "$ended_ms" ]; then
    echo "the dropped search ended within ${ended_ms} ms of the track change"
else
    echo "the dropped search had NOT ended 6 s after the track change"
fi
show "$work/cancel.log" cancel
