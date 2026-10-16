# Surface Demo

An example of the drawing services added in AZ-MODS 1.2.3: a panel the mod paints on the
deck screen, which answers taps, a long press and the browse knob.

## What it shows

| Service | Where |
|---|---|
| `surface_create`, `surface_place`, `surface_show` | `init()`: one surface on `AZ_LAYER_TOP`, shown only on `AZ_VIEW_DECK` |
| `on_paint` with `draw_text`, `fill_rect`, `draw_rect`, `draw_line`, `draw_image` | `paint()` |
| `view()` | the "screen:" line, repainted from `tick()` when the screen changes |
| `AZ_EV_TAP` | changes the accent color (and the icon, a premultiplied RGBA image) |
| `AZ_EV_LONG` + `surface_focus` | a long press takes the browse knob |
| `AZ_EV_KNOB`, `AZ_EV_CLICK`, `AZ_EV_BACK` | turns move a counter, a click resets it, BACK gives the knob back |
| `surface_redraw` | after every change |

Every service is checked with `AZ_HAS` in `init()`, so on a core without them the mod loads and
does nothing.

## Rules it follows

- Everything runs on the core's UI thread: `init()`, `tick()`, `paint()` and `input()`.
- `paint()` draws only inside the area it is given, only with the core's helpers.
- `surface_create` is called from `init()` only; placing, showing and repainting from the UI
  thread.
- While the surface has the knob, the deck's own list does not scroll and BACK does not leave
  the screen; returning the knob is the mod's job (BACK here).

## Build

From the repository root, with the SDK beside it:

```sh
../az-mods-sdk/tools/azmod-pack build surface-demo
```

It needs the 1.2.3 SDK or newer.
