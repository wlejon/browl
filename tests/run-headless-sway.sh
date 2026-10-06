#!/usr/bin/env bash
# Runs one test against a private headless sway (wlroots' headless backend,
# the pixman renderer, no input devices, no XWayland) in its own
# XDG_RUNTIME_DIR, and names that compositor to the test in
# BROWL_TEST_WAYLAND_DISPLAY. The desktop's own compositor is never touched:
# WAYLAND_DISPLAY is cleared. Exits with the test's status; 77 (skip) when
# sway is not installed.
#
#   run-headless-sway.sh <test executable> [args...]
set -uo pipefail

name="${1##*/}"
if ! command -v sway >/dev/null 2>&1; then
    echo "[$name] SKIP: sway is not installed (the real compositor these tests run against)"
    exit 77
fi

here="$(cd "$(dirname "$0")" && pwd)"
rt="$(mktemp -d "${TMPDIR:-/tmp}/browl-sway.XXXXXX")"
chmod 700 "$rt"
cleanup() {
    [ -n "${sway_pid:-}" ] && kill "$sway_pid" 2>/dev/null && wait "$sway_pid" 2>/dev/null
    rm -rf "$rt"
}
trap cleanup EXIT

unset DISPLAY WAYLAND_DISPLAY SWAYSOCK
export XDG_RUNTIME_DIR="$rt"
WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 \
    sway -c "$here/sway.conf" >"$rt/sway.log" 2>&1 &
sway_pid=$!

socket=""
for _ in $(seq 200); do
    for s in "$rt"/wayland-*; do
        case "$s" in *.lock | *"*") continue ;; esac
        socket="$(basename "$s")"
    done
    [ -n "$socket" ] && break
    kill -0 "$sway_pid" 2>/dev/null || break
    sleep 0.05
done
if [ -z "$socket" ]; then
    echo "headless sway did not come up:" >&2
    cat "$rt/sway.log" >&2
    exit 1
fi

echo "headless $(sway --version) on $socket"
BROWL_TEST_WAYLAND_DISPLAY="$socket" "$@"
rc=$?
if [ "$rc" -ne 0 ] && [ "$rc" -ne 77 ]; then
    echo "--- sway log ---"
    tail -n 40 "$rt/sway.log"
fi
exit "$rc"
