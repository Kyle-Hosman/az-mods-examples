# Level Meter

An example of the audio taps added in AZ-MODS 1.2.3: a level meter for each deck, on a Mod Menu
page. RMS is the bar, the peak is a marker that holds for 1.5 seconds and then falls at 15 dB a
second, and the number is the peak in dBFS (red at -1 dBFS and above). The hold, the fall and
the repaints (at most 8 a second while the page is open) are timed with the monotonic clock.

## What it hears

The XDJ-AZ mixes its channels in hardware: faders, EQ, the crossfader and the master all happen
after the deck's software. So a tap carries one deck's own output before the mixer, and there
is no master to tap. The meter shows a playing deck at full level with its channel fader down.

## What it shows

| Service | Where |
|---|---|
| `audio_tap` | `init()`: one tap per deck, `AZ_TAP_DECK(0)` to `AZ_TAP_DECK(3)`, 1024 frames a block |
| the tap callback | `on_block()`: peak and RMS of one block, stored in two atomics |
| `deck_count` | 2 or 4 meters, as the deck shows |
| `register_screen`, `draw_rect`, `fill_rect`, `draw_text` | the page |

## Rules it follows

- The tap callback runs on the core's tap thread, never the audio thread, and does no
  allocation, locking, I/O or logging: a loop over the block and two atomic stores.
- A slow callback never delays the audio, it only loses blocks (the core says so in the mod's
  log). The meter carries on with the next block.
- A mod may have 4 taps, and the core runs 16 for all mods together. A deck whose tap is
  refused shows "no audio tap"; the others still work.
- The taps are asked for last in `init()`, because a tap outlives a refused `init()`.

## Build

From the repository root, with the SDK beside it:

```sh
../az-mods-sdk/tools/azmod-pack build level-meter
```

It needs the 1.2.3 SDK or newer.
