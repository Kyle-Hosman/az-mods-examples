#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
#
# Run the az-mods-sdk test harness (tools/harness/sanitize.sh) on every example mod, with
# their functional scenarios (tests/scenarios.c) and the harness's planted controls, in
# three passes: plain, ASan+UBSan and TSan.
#
#     tests/sanitize.sh [--sdk DIR] [plain|asan|tsan ...]   (default: all three)
#
# DIR is an az-mods-sdk checkout: --sdk, else AZ_SDK, else ../az-mods-sdk next to this
# repository. Needs clang (CC overrides it). The exit status is non-zero if a mod fails a
# check, a binary is not instrumented, or a planted control is not caught.
set -eu

cd "$(dirname "$0")/.."
SDK=${AZ_SDK:-../az-mods-sdk}
if [ "${1:-}" = --sdk ]; then
    [ $# -ge 2 ] || { echo "--sdk needs a directory" >&2; exit 2; }
    SDK=$2; shift 2
fi
RUNNER=$SDK/tools/harness/sanitize.sh
[ -f "$RUNNER" ] || { echo "no $RUNNER: give --sdk or AZ_SDK an az-mods-sdk checkout" >&2; exit 2; }
PASSES=${*:-plain asan tsan}

# ID:max repaints a second from tick:sources
exec sh "$RUNNER" --controls --scenarios tests/scenarios.c --passes "$PASSES" \
    set-clock:1:set-clock/set-clock.c \
    deck-overview:4:deck-overview/deck-overview.c \
    library-browser:1:library-browser/library-browser.c \
    level-meter:8:level-meter/level-meter.c \
    surface-demo:2:surface-demo/surface-demo.c \
    deck-events:1:deck-events/deck-events.c
