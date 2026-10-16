# Library Browser

An example of the library services added in AZ-MODS 1.2.3: the rekordbox playlists on the
USBs in the deck, on a Mod Menu page. The knob moves, a click opens a playlist's tracks
(title, artist, BPM, key), BACK goes back to the list.

## What it shows

| Service | Where |
|---|---|
| `lib_playlists` | `read_list()`: every playlist and folder on the USB in the USB 1 port (USB 0) and the USB 2 port (USB 1), built into a tree |
| `lib_tracks` | `read_tracks()`: one playlist's tracks, in playlist order |
| `thread_start` | `init()`: the reader thread, where every library call is made |
| `register_screen`, `draw_header`, `draw_text`, `fill_rect`, `text_width` | the page |

## Rules it follows

- The library services read the USB, so they are never called from `tick()` or a page
  callback (the core refuses them on the UI thread). The mod's own thread does the reading;
  the page only draws what that thread last read.
- The thread and the page share one mutex, held for a copy or a draw, never across a read.
- The callbacks copy what they need: the strings in `az_track` are valid during the callback
  only.
- The thread is started last in `init()`, after everything that can fail, because a thread
  outlives a refused `init()`.
- A port the core cannot tell (a USB behind a hub) has no library here: the call returns -1
  rather than guessing, and the page leaves that heading out.
- A return of 1 means the list was cut at the core's limit of 20,000 entries: the mod shows
  what it got rather than nothing.
- An entry whose parent is not in the listing is at the top level; the mod does not assume the
  top level's parent id.
- Reading is read-only: the core opens the library with a connection of its own that cannot
  write, so browsing never changes the USB.

## Build

From the repository root, with the SDK beside it:

```sh
../az-mods-sdk/tools/azmod-pack build library-browser
```

It needs the 1.2.3 SDK or newer.
