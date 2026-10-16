#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
#
# Run the host-side test harness on the example mods, in the SDK's build container.
#
#     tests/run.sh                  from the repository root, after building the mods:
#                                   azmod-pack build set-clock (and deck-overview,
#                                   library-browser, level-meter, surface-demo, deck-events)
#
# AZ_SDK points at an az-mods-sdk checkout (default: ../az-mods-sdk); the harness, its
# controls and iocount.c come from its tools/harness, the scenarios from tests/scenarios.c.
# AZ_SDK_IMAGE names the container image (default: az-mods-sdk:1, as azmod-pack builds it).
# Four runs:
#   1. the SHIPPED libraries (<mod>/out/<mod>.so) with iocount.so preloaded:
#      every check must pass;
#   2. the same sources rebuilt with AddressSanitizer and UndefinedBehaviorSanitizer:
#      every check must pass (no-io and no-alloc are not measured there);
#   3. the controls: the SDK's planted.c once per fault, each of which must fail exactly the
#      check it was planted for and pass all the others, plus the fault-free build;
#   4. broken copies of some examples (one line changed), each of which must fail its own
#      functional scenario.
set -eu

if [ ! -d /sdk/include ]; then
    SDK=${AZ_SDK:-$(cd "$(dirname "$0")/../.." && pwd)/az-mods-sdk}
    [ -f "$SDK/include/azmod-mod.h" ] && [ -f "$SDK/tools/harness/harness.c" ] ||
        { echo "set AZ_SDK to an az-mods-sdk checkout with tools/harness" >&2; exit 2; }
    SDK=$(cd "$SDK" && pwd)
    ROOT=$(cd "$(dirname "$0")/.." && pwd)
    # Git Bash on Windows would rewrite /repo and the other container paths into Windows
    # paths; MSYS_NO_PATHCONV stops that and is ignored everywhere else
    MSYS_NO_PATHCONV=1 exec docker run --rm --platform=linux/arm64 -v "$ROOT":/repo:ro -v "$SDK/include":/sdk/include:ro \
        -v "$SDK/tools/harness":/sdk/harness:ro -w /repo "${AZ_SDK_IMAGE:-az-mods-sdk:1}" sh tests/run.sh
fi
H=/sdk/harness
SCEN='-DAZ_HARNESS_SCENARIOS="/repo/tests/scenarios.c"'

B=$(mktemp -d)
FLAGS="-O2 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -fno-omit-frame-pointer -fexceptions \
-fasynchronous-unwind-tables -I/sdk/include"
SAN="-g -O1 -fsanitize=address,undefined -fno-sanitize-recover=all"

gcc $FLAGS -shared -o "$B/iocount.so" $H/iocount.c -ldl
gcc -O1 -g -Wall -Wextra -I/sdk/include "$SCEN" -rdynamic -o "$B/harness" $H/harness.c -ldl -lm -lpthread
gcc $SAN -Wall -Wextra -I/sdk/include "$SCEN" -rdynamic -o "$B/harness-san" $H/harness.c -ldl -lm -lpthread

# id:max repaints a second from tick
MODS="set-clock:1 deck-overview:4 library-browser:1 level-meter:8 surface-demo:2 deck-events:1"
FAILED=0
note() { printf '%s\n' "$*"; }
bad() { note "MISMATCH: $*"; FAILED=1; }

# expect_pass LABEL OUTFILE: every CHECK line must be PASS or NOT-MEASURED
expect_pass() {
    if grep -q '^CHECK .* FAIL' "$2"; then
        bad "$1: $(grep '^CHECK .* FAIL' "$2" | tr '\n' ';')"
    fi
    passed=$(grep -c '^CHECK .* PASS' "$2" || true)
    unmeasured=$(grep '^CHECK .* NOT-MEASURED' "$2" | cut -d' ' -f2 | tr '\n' ' ' || true)
    note "$1: $passed checks passed${unmeasured:+, not measured: $unmeasured}"
}

note "== 1. shipped libraries, iocount.so preloaded"
for m in $MODS; do
    id=${m%%:*}; rate=${m#*:}
    so=$id/out/$id.so
    [ -f "$so" ] || { bad "$so missing: build it with azmod-pack first"; continue; }
    LD_PRELOAD=$B/iocount.so "$B/harness" "$so" "$id" "$rate" --functional > "$B/$id.out" 2>&1 || true
    cat "$B/$id.out"
    expect_pass "$id (shipped)" "$B/$id.out"
done

note "== 2. sanitizer builds"
for m in $MODS; do
    id=${m%%:*}; rate=${m#*:}
    gcc $FLAGS $SAN -shared -o "$B/$id-san.so" "$id/$id.c" -lpthread -lm
    ASAN_OPTIONS=exitcode=86:detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1:exitcode=87 \
        "$B/harness-san" "$B/$id-san.so" "$id" "$rate" --functional > "$B/$id-san.out" 2>&1 || true
    grep -v '^    ok' "$B/$id-san.out" | grep -v '^  ' || true
    expect_pass "$id (sanitizers)" "$B/$id-san.out"
done

gcc $FLAGS $SAN -DFAULT=10 -shared -o "$B/planted-overflow.so" $H/planted.c
ASAN_OPTIONS=exitcode=86:detect_leaks=0 "$B/harness-san" "$B/planted-overflow.so" planted 1 \
    > "$B/overflow.out" 2>&1 || true
if grep -q '^CHECK no-crash FAIL' "$B/overflow.out" && grep -q 'stack-buffer-overflow' "$B/overflow.out"; then
    note "control sanitizers: a planted stack overflow fails no-crash ($(grep '^CHECK no-crash' "$B/overflow.out" | cut -d' ' -f4-))"
else
    bad "control sanitizers: the planted overflow was not caught"
fi

note "== 3. controls: each planted fault must fail exactly its own check"
for f in 0:none 1:no-crash 2:draw-area 3:draw-context 4:text-size 5:no-io 6:no-alloc 7:redraw-rate 8:log-quiet 9:api; do
    n=${f%%:*}; want=${f#*:}
    gcc $FLAGS -DFAULT="$n" -shared -o "$B/planted-$n.so" $H/planted.c
    LD_PRELOAD=$B/iocount.so "$B/harness" "$B/planted-$n.so" planted 1 > "$B/p$n.out" 2>&1 || true
    fails=$(grep '^CHECK .* FAIL' "$B/p$n.out" | cut -d' ' -f2 | tr '\n' ' ' | sed 's/ $//')
    if [ "$want" = none ]; then
        [ -z "$fails" ] && note "control none: all checks pass" || bad "control none failed: $fails"
    elif [ "$fails" = "$want" ]; then
        note "control $want: fails as planted ($(grep "^CHECK $want FAIL" "$B/p$n.out" | cut -d' ' -f4-))"
    else
        bad "control $want: expected only '$want' to fail, got '${fails:-nothing}'"
    fi
done

note "== 4. controls: a broken copy of a mod must fail its own scenario"
# control_mod ID RATE WHAT SED: build ID with one line changed by SED; functional must FAIL
control_mod() {
    sed "$4" "$1/$1.c" > "$B/$1-ctl.c"
    if cmp -s "$1/$1.c" "$B/$1-ctl.c"; then bad "control $1 ($3): the change did not apply"; return; fi
    gcc $FLAGS -shared -o "$B/$1-ctl.so" "$B/$1-ctl.c" -lpthread -lm
    "$B/harness" "$B/$1-ctl.so" "$1" "$2" --functional > "$B/$1-ctl.out" 2>&1 || true
    if grep -q '^CHECK functional FAIL' "$B/$1-ctl.out"; then
        note "control $1 ($3): fails ($(grep '^CHECK functional FAIL' "$B/$1-ctl.out" | cut -d' ' -f4-))"
    else
        bad "control $1 ($3): its scenario still passes"
    fi
}
control_mod deck-events 1 "a tempo shown when it is unknown" 's/if (di.bpm > 0.0 \&\& di.bpm < 1000.0)/if (1)/'
control_mod surface-demo 2 "shown on every screen" 's/\.views = AZ_VIEW_BIT(AZ_VIEW_DECK),/.views = ~0u,/'
control_mod library-browser 1 "a cut list treated as no list" 's/on_track, \&b) < 0)/on_track, \&b) != 0)/'
control_mod level-meter 8 "deck 1's tap on deck 2" 's/AZ_TAP_DECK(deck), BLOCK/AZ_TAP_DECK(deck == 0 ? 1 : deck), BLOCK/'

rm -rf "$B"
if [ "$FAILED" = 0 ]; then note "ALL GATES PASS"; else note "SOME GATES FAILED"; fi
exit "$FAILED"
