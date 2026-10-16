# Testing

| Layer | Runs | Shows |
|---|---|---|
| `tests/run.sh` | the SDK's test harness in the SDK's build container (Docker only) | every mod against a stand-in core, the shipped libraries and sanitizer builds, with controls that must fail |
| `tests/sanitize.sh` | the same harness with the host's compiler, in three passes | AddressSanitizer + UBSan and ThreadSanitizer reports |
| the emulator or a test deck | the real core, font, screen, fetcher and loader | what a stand-in cannot |

## 1. The harness: `tests/run.sh`

Build the mods first (`azmod-pack build <id>` for each), then run `tests/run.sh` from the
repository root. It needs Docker and an SDK checkout (`AZ_SDK`, default `../az-mods-sdk`), and
runs in the SDK's image (`AZ_SDK_IMAGE`, default `az-mods-sdk:1`, the image `azmod-pack build`
uses).

The harness is the SDK's `tools/harness/harness.c`, with these mods' scenarios compiled in
from `tests/scenarios.c`. It loads a mod the way the core does (`dlopen`, `az_mod_v1_entry`,
`init`) against a stub `az_core_v1`, in a child process. Each mod gets six soaks of 1000
ticks: deck states changing every tick, odd values (NaN and negative BPMs, a deck count of 7,
400-byte and invalid UTF-8 titles), page areas from full screen to 120x60, row heights up to
200 px, theme switches, a clock step in the middle, random knob, click, tap and BACK events,
deck loads, ejects, plays and stops, screen changes, and a reload with the settings kept;
then the scenarios for that mod (`--functional`). The clock is simulated: 1000 ticks are 33
seconds of deck time.

| Check | Asserts | Control (in the same run) |
|---|---|---|
| `init` | `init()` returns 0 and registers one screen with `on_draw` (or, for a mod with no page, a surface or a deck-event listener); the descriptor's id and `fw_tags` are right | every soak starts from it |
| `no-crash` | the child ran to the end: no signal, no sanitizer abort | a planted null write fails it (signal 11) |
| `draw-area` | every rectangle, checkbox, line, image and text box (width from `text_width`, height `px * 3 / 2`) lies inside the page or surface area | a rectangle 1 px past the right edge fails it |
| `draw-context` | drawing services are called only from `on_draw` or `on_paint`, with the context it was handed | painting from `tick` with a kept context fails it |
| `text-size` | every size given to `draw_text` and `text_width` is 8 to 96 | text at 6 px fails it |
| `no-io` | no file, process, sleep or network call from `tick`, a page or surface callback, or a deck-event, confirm or `post_ui` callback | an `open()` from `tick` fails it |
| `no-alloc` | no heap allocation from those callbacks | a `malloc()` from `tick` fails it |
| `redraw-rate` | `request_redraw` and `surface_redraw` from `tick` stay within the mod's rate, plus a slack of 3 and one a minute | a request on every tick fails it |
| `log-quiet` | at most 30 log lines per 1000-tick soak | a line every tick fails it |
| `api` | deck numbers 0 to 3, sane buffer sizes, valid settings keys, no `claim_hook`, no `data_dir` or `assets_dir` outside `init`, surfaces and deck-event listeners only from `init`, `post_ui` only with a function inside the mod, premultiplied images | asking for deck 4 fails it |
| `functional` | the scenarios below | each scenario pairs its expectation with one that must not hold, and a broken copy of a mod must fail its scenario |

| Mod | Repaints a second (`redraw-rate`) |
|---|---|
| Set Clock | 1 |
| Deck Overview | 4 |
| Library Browser | 1 |
| Level Meter | 8 |
| Surface Demo | 2 |
| Deck Events | 1 |

`no-io` and `no-alloc` are counted by the SDK's `tools/harness/iocount.c`, preloaded with
`LD_PRELOAD` and armed only while a mod callback runs. The controls are the SDK's
`tools/harness/planted.c`, a minimal mod built once per fault: each must fail exactly the
check it was planted for and pass all the others, and the fault-free build must pass
everything. The script runs:

1. the shipped libraries (`<mod>/out/<mod>.so`) with the counters: every check passes;
2. the same sources rebuilt with AddressSanitizer and UndefinedBehaviorSanitizer: every
   check passes (the counters cannot run beside the sanitizer's allocator, so `no-io` and
   `no-alloc` say NOT-MEASURED there), and a planted stack overflow is caught;
3. the controls;
4. broken copies of Deck Events (a tempo shown when it is unknown), Surface Demo (shown on
   every screen), Library Browser (a cut list treated as no list) and Level Meter (deck 1's
   tap on deck 2), each of which must fail its scenario.

It ends with `ALL GATES PASS`, or names each mismatch.

### Functional scenarios

| Mod | Scenarios |
|---|---|
| Set Clock | not started with no track; 0:01:05 sixty-five seconds after a track loads on deck 2; a one-hour clock step moves the time of day and not the set time; the set carries on after a reload; a click with nothing selected does nothing; reset needs two presses and times out after one; the time zone steps by 15 minutes, stops at UTC-12:00, and is saved; BACK leaves the editor before the page; the 12-hour clock; a start from yesterday is not resumed; with the deck's clock unset the set still counts and the start is saved once the clock is set; the tap targets |
| Deck Overview | both BPMs, the reference deck and `+2.00  +1.6%`; the knob moves the reference (`-2.00  -1.5%`) and it is saved; a reference deck with no BPM gives no differences; a reference outside the 2-deck view gives none until the knob picks a deck; a long UTF-8 name is cut on a character boundary; a BPM moving every tick repaints about four times a second |
| Library Browser | the playlists are read on the mod's own thread and shown, with the folder and the playlist inside it, a top-level playlist whose parent id is not in the listing, and no heading for a USB with no library; a click on a folder opens nothing; a click on a playlist lists its two tracks with title, artist, BPM and key, in playlist order, and none from another playlist, although the core reports that list as cut (return 1); BACK returns to the list, and a second BACK leaves the page to the core. The stub library has three playlists on USB 0 and none on USB 1 |
| Level Meter | a tap on each of the four decks; a -6 dBFS sine on deck 1 shows `-6.0`, full scale on deck 3 shows `0.0`, silent decks show `--`, and no level that was not played appears; the peak holds after the deck goes quiet and falls away after the hold; two meters in a 2-deck view. The scenario plays blocks into the mod's tap callbacks itself, armed like a page callback, so `no-io` and `no-alloc` cover the callback too |
| Surface Demo | one surface, placed, shown, and only on the deck screen; nothing painted on SOURCE; on DECK the panel names the screen; a tap is reported where it landed; the knob is not held before a long press, and is after; three knob steps count to 3, a click resets the count, BACK gives the knob back |
| Deck Events | a listener and a worker thread; a load shows the deck, title, artist and tempo; an unknown tempo and artist are left out; play banners stay off until the DJ turns them on; an eject asks, and Yes turns them on and saves it; with them on, a play shows a banner and a stop counts the beats played (four wraps of the beat phase on the real clock, the worker polling every 20 ms against 200 ms holds); no count for a deck that played no beats |

### Threads across a reload

Mods that start a thread (`thread_start`) or an audio tap are never unloaded by the harness,
as the core never unloads a mod; the reload in the middle of the soaks calls `init()` again on
the same library. A reload stands for a restart of the program, which ends the mod's threads:
the harness parks each thread from the previous lifetime at its next core call and waits for
that before calling `init()` again, so the new `init()` never shares the mod's globals with an
old thread (a thread that makes no core call within 2 seconds is reported in a `note:` line
and keeps running). `no-io` and `no-alloc` are armed per thread: a mod's own thread may do
I/O, while what a callback does on the core's thread is counted.

### Sanitizers on the host: `tests/sanitize.sh`

`tests/sanitize.sh [--sdk DIR] [plain|asan|tsan ...]` runs the SDK's
`tools/harness/sanitize.sh` on every example, with `tests/scenarios.c` and the planted
controls. `DIR` is an az-mods-sdk checkout (`--sdk`, else `AZ_SDK`, else `../az-mods-sdk`).
It builds the harness and the mods with the host's own compiler (clang by default, `CC`
overrides it; Linux or macOS; no Docker) and runs three passes: without instrumentation, with
AddressSanitizer and UndefinedBehaviorSanitizer, and with ThreadSanitizer (the harness runs a
mod's `thread_start` workers as real threads). Every check must pass in every pass; on Linux
the plain pass also measures `no-io` and `no-alloc`, which macOS reports as not measured. The
`sanitizers` workflow checks out the SDK at `SDK_REF` and runs it on every push and pull
request. Each pass runs controls beside the mods:

| Pass | Control (built from the SDK's `tools/harness/planted.c`) | Must report |
|---|---|---|
| all | the fault-free build | every check passes |
| plain | each fault of the check table above | exactly its own check fails (`no-io` and `no-alloc` on Linux only) |
| all | every binary carries the pass's instrumentation (`nm` finds `__asan_` or `__tsan_`) | a build without the flags is a failure |
| asan | one byte written past an 8-byte heap block | `heap-buffer-overflow` |
| asan | an 8-byte stack buffer handed to `deck_title` as 64 bytes | `stack-buffer-overflow` |
| asan | `INT_MAX + 1` | `runtime error: signed integer overflow` |
| tsan | a worker thread writes a plain `int` that `tick` reads | `ThreadSanitizer: data race` |

Each asan and tsan control must fail `no-crash` with that report; a control that is not
caught fails the run. It ends with `ALL SANITIZER GATES PASS`, or names each mismatch.

The harness cannot show the real font's widths, the core's own clipping and paint timing, or
anything about EP147's threads: that is the next section.

## 2. On the emulator or a test deck

### By hand

```sh
../az-mods-sdk/tools/azmod-pack package set-clock
../az-mods-sdk/tools/azmod-pack package deck-overview
```

1. Unzip `set-clock/dist/set-clock-1.0.0.azmod` into `<stick>/MODS/community-mods/set-clock/`
   and `deck-overview/dist/deck-overview-1.0.0.azmod` into
   `<stick>/MODS/community-mods/deck-overview/`.
2. Add `community-mods.set-clock = on` and `community-mods.deck-overview = on` to
   `<stick>/MODS/azmods.cfg`.
3. Power the deck on with the USB inserted.
4. Under **Mod Menu > Community Mods > Installed**, allow each mod (**New mod found: allow?**),
   then press **Reload now**.

For a new build of a mod the deck already loads, the SDK's
[Over SSH](https://github.com/Kyle-Hosman/az-mods-sdk/blob/main/docs/04-testing.md#over-ssh)
loop is faster: swap the `.so` in `/home/root/settings/az-usbload/community/<id>/`, approve
it and restart EP147. The USB wins at the next power-on, so copy the build there too. On the
emulator, the emulator repository's `tools/az-push-mod.py` does this in one command.

### Through a repository (fetcher, hash check, install, reload)

The deck fetches only `https://` addresses and checks certificates. For a repository not
served by raw.githubusercontent.com (local changes, a private fork), serve it from the
computer:

```sh
python3 tools/serve-local.py --host 10.0.2.2       # the emulator: QEMU's address for this machine
python3 tools/serve-local.py --host YOUR-COMPUTER-IP   # a deck: the computer's address on its network
```

It packages Set Clock and Deck Overview with download addresses on the computer, writes
`.scratch/local/azmods-repo.json`, makes a throwaway certificate authority and a certificate
for `--host`, and serves `https://HOST:8443/` (`/ping` answers too; `--port` changes it).
The same check a deck makes, from the computer, trusting that CA:

```sh
../az-mods-sdk/tools/azmod-pack repo check https://HOST:8443/azmods-repo.json --download --cafile .scratch/local/ca.pem
```

A `source` link that is not publicly reachable is reported as refused; the repository file and the
packages are checked all the same.

Then, on the deck or the emulator guest, as root:

1. Trust the test CA (the deck's curl reads `/etc/ssl/certs/<hash>.0`). On the computer,
   `openssl x509 -subject_hash -noout -in .scratch/local/ca.pem` prints the hash. Copy
   `ca.pem` to the guest's `/etc/ssl/certs/az-test-ca.pem` and link it:
   `ln -s az-test-ca.pem /etc/ssl/certs/<hash>.0`. Remove both afterwards: the CA's key sits
   unprotected in `.scratch/local/`.
2. Add and install, from the Mod Menu (**Community Mods > Repositories > Add**, type
   `https://HOST:8443/azmods-repo.json`, then **Browse**), or over SSH by running the fetcher
   the way the loader's watcher does (this also skips its github.com reachability probe, which
   fails on an emulator without DNS):

   ```sh
   S=$(sed -n 's|^/dev/[^ ]* \(/media/usb/[^ ]*\) .*|\1|p' /proc/mounts | head -1)   # the USB with MODS/
   E="AZ_MNT=$S AZ_MODS=$S/MODS AZ_STAGE=/home/root/settings/az-usbload AZ_COMMUNITY_PING=https://HOST:8443/ping"
   env $E sh /tmp/az-deck/lib/az-community addrepo https://HOST:8443/azmods-repo.json
   env $E sh /tmp/az-deck/lib/az-community install https://HOST:8443/azmods-repo.json set-clock
   env $E sh /tmp/az-deck/lib/az-community install https://HOST:8443/azmods-repo.json deck-overview
   cat /tmp/az-community/status
   ```

   With two USBs in, `$S` must be the one with `MODS/`.
3. Apply it: **Reload now** on the Community Mods page, or over SSH the same request to the
   loader's watcher, which copies `MODS/community-mods/` to the deck and restarts EP147:

   ```sh
   echo apply > /run/az-community.req.new && mv /run/az-community.req.new /run/az-community.req
   ```

   (Running `az-community apply` by hand only marks the request; the copy and restart come
   from the watcher.)

Run this way against the deck's 2.01 userland in a container (with `CURL_CA_BUNDLE` naming
the CA instead of step 1), the fetcher listed both mods, installed both, and refused a package
altered by one byte (`sha256 mismatch`).

### What to look at

| Where | Check |
|---|---|
| both pages, under **Mod Menu > Community Mods > Installed** | digits large and centered, nothing cut off, in the Day theme and a dark one, switched while the page is open |
| Set Clock | the set starts when a track loads on any deck (deck 3 in the 4-deck view too); the count carries on across an EP147 restart; reset takes two presses; the time zone arrows respond to taps; BACK leaves the time zone editor, then the page |
| Deck Overview | the 2-deck and 4-deck views and switching between them; empty decks; loading and ejecting while the page is open; the pitch fader moving (the BPM and difference follow without stutter); a tap on a row makes it the reference |
| the logs, with `touch /home/root/settings/azmod-logs.on` | each mod's log in `/tmp/azmod-community/` has only its load result, and for Set Clock a line per set start or reset |
| `systemctl show EP147 -p NRestarts` after the session | unchanged |
