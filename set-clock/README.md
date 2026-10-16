# Set Clock

The time of day and how long your set has been running, large and readable, on its own
page: **Mod Menu > Community Mods > Installed > Set Clock**.

![icon](icon.png)

## Using it

- **The set starts by itself** when a deck first has a track loaded, and keeps counting
  through a reload of the deck's program. A start more than a day old is forgotten.
- **Reset** when the next set begins: select *Reset set timer* and press twice (the first
  press asks for confirmation, which times out after three seconds). With a track loaded, the
  count starts again from zero; with none, it waits for the next track.
- **Time zone.** The deck keeps its clock in UTC and has no time zone of its own, so set
  yours here, in 15 minute steps from UTC-12:00 to UTC+14:00. Tap the arrows, or select the
  row, press the knob and turn it. Daylight saving is not applied: change the offset when it starts or ends.
- **24-hour clock** or 12-hour.

Turn the browse knob to move between the rows and press it to use one; BACK leaves the
page (or the time zone, while you are changing it).

If the deck's clock has never been set, the time of day shows `--:--`; the set time still
counts.

## What it shows

| Service | Where |
|---|---|
| `register_screen` and the drawing services (`draw_header`, `draw_text`, `draw_checkbox`, `fill_rect`, `draw_rect`, `text_width`, `row_height`, `color`) | the page |
| `cfg_get`, `cfg_set`, `cfg_get_str`, `cfg_set_str` | the time zone, the 12- or 24-hour clock and the start of the set, kept across reloads |
| `deck_loaded` | the set starts when a deck first has a track |
| `request_redraw` | from `tick()`, once a second when the shown time changes |

## What it changes

Nothing on the deck. It reads the clock and whether a track is loaded, and keeps three
settings (time zone, 24-hour clock, and when the current set started) in `MODS/azmods.cfg`
on the USB, so they follow the USB.
No hooks, no files of its own, no network, no LEDs, controls or audio.

## Build

From the repository root, with the SDK beside it:

```sh
../az-mods-sdk/tools/azmod-pack build set-clock
```

It needs the 1.2.3 SDK or newer.

## License

GPL-3.0-only. See [LICENSE](LICENSE).
