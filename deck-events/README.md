# Deck Events

An example of the deck services added in AZ-MODS 1.2.3: banners when a deck loads, starts and
stops, a worker thread that counts beats from the beat grid, and a yes/no question.

## What it shows

| Service | Where |
|---|---|
| `on_deck_event` | `init()`: one callback, `on_deck()`, for every deck |
| `deck_info` | `on_deck()` (title, artist, tempo for the load banner) and `worker()` (is the deck playing) |
| `banner` | `on_deck()` and `on_answer()` |
| `confirm` | `on_deck()` on `AZ_DECK_EJECTED`: turn the play and stop banners on or off |
| `thread_start` | `init()`: starts `worker()` |
| `deck_grid` | `worker()`: the beat phase wraps from near 1 to near 0 on each beat |
| `post_ui` | `worker()` hands each deck's beat count to `on_beats()` |

Every service is checked with `AZ_HAS` in `init()`, so on a core without them the mod loads and
does nothing.

## Rules it follows

- `on_deck_event` is called from `init()` only. Its callback, the `confirm` answer and the
  `post_ui` callback all run on the core's UI thread, so the mod's state needs no locks.
- `deck_info` and `deck_grid` are safe from any thread and never block, so the worker calls
  them directly. A value the core does not know is -1 or an empty string, and is left out.
- `confirm` shows a dialog over whatever is on screen, with Cancel focused first; BACK answers
  no, and the mod treats no as "leave it as it is". It returns -1 when another question is
  already open, and then no answer comes.
- The worker does no I/O and allocates nothing. It sleeps between steps and never takes a lock
  the deck holds.

## Build

From the repository root, with the SDK beside it:

```sh
../az-mods-sdk/tools/azmod-pack build deck-events
```

It needs the 1.2.3 SDK or newer.
