# AZ-MODS examples

First-party mods for the XDJ-AZ, built with the
[AZ-MODS SDK](https://github.com/Kyle-Hosman/az-mods-sdk) the same way any mod is. They use
only the services the AZ-MODS core provides and claim no hooks (`"firmware": ["any"]`), so
they load on every firmware AZ-MODS supports. All of them need AZ-MODS 1.2.3 or newer.

| Mod | What it does | Services it shows | Runs from |
|---|---|---|---|
| [Set Clock](set-clock/) | the time of day and how long the set has been running, in big digits on its own Mod Menu page | a Mod Menu page, settings (numbers and text), drawing, `deck_loaded` | `tick()` |
| [Deck Overview](deck-overview/) | every deck in the current view on one page: loaded or not, BPM, file name, and the BPM difference to a reference deck | a Mod Menu page, settings, drawing, deck state | `tick()` |
| [Library Browser](library-browser/) | the rekordbox playlists on the USBs, browsed with the knob; a playlist opens to its tracks | `lib_playlists`, `lib_tracks`, `thread_start` | its own thread, `tick()` |
| [Level Meter](level-meter/) | a level meter for each deck: RMS, a held peak, the peak in dBFS | `audio_tap` | tap callbacks, `tick()` |
| [Surface Demo](surface-demo/) | a panel over the deck screen that answers taps, a long press and the browse knob | surfaces, `view`, `draw_line`, `draw_image`, `surface_focus` | surface callbacks, `tick()` |
| [Deck Events](deck-events/) | banners when a deck loads, starts and stops, with the beats it played | `on_deck_event`, `deck_info`, `deck_grid`, `banner`, `confirm`, `post_ui`, `thread_start` | deck events and its own thread (`.tick = NULL`) |

Each mod's README lists the services it uses and where in its source.

## Install

Add the repository `Kyle-Hosman/az-mods-examples`, then pick a mod under **Browse**:

| Where | How |
|---|---|
| on the deck | **Mod Menu > Community Mods > Repositories > Add** |
| in the AZ-MODS desktop app | **Add repository**, with the USB in the computer |
| by hand | copy a package to the USB, as the SDK's [testing guide](https://github.com/Kyle-Hosman/az-mods-sdk/blob/main/docs/04-testing.md#by-copying-it-to-the-usb) describes |

## Layout

A repository with several mods, as the SDK's
[publishing guide](https://github.com/Kyle-Hosman/az-mods-sdk/blob/main/docs/06-publishing.md)
describes: each mod in its own folder, one `azmods-repo.json` at the root, one release
workflow per mod.

```
azmods-repo.json                       the repository file decks read
set-clock/  deck-overview/  ...        one folder per mod: source, azmod.json, icon, Makefile
.github/workflows/release-<id>.yml     a tag <id>-<version> builds and releases that mod
tests/                                 scenarios and runners for the SDK's test harness (TESTING.md)
tools/draw-icons.py                    draws the icons
tools/serve-local.py                   serves the mods as a local repository for testing
```

## Build and test

With the SDK checked out next to this repository and Docker running:

```sh
for m in set-clock deck-overview library-browser level-meter surface-demo deck-events; do
    ../az-mods-sdk/tools/azmod-pack build $m
done
tests/run.sh
```

In PowerShell (run `tests/run.sh` in Git Bash):

```powershell
foreach ($m in 'set-clock','deck-overview','library-browser','level-meter','surface-demo','deck-events') {
    ..\az-mods-sdk\tools\azmod-pack.cmd build $m
}
```

Each build ends with `validate ...: OK`. [TESTING.md](TESTING.md) describes what the tests
check and how to try the mods on a deck or the emulator.

## Release

Bump `version` in the mod's `azmod.json`, commit, then push a tag:

```sh
git tag set-clock-1.0.1 && git push origin set-clock-1.0.1
```

The mod's workflow builds it in the SDK container, validates and packages it, attaches the
package and icon to a GitHub release, and commits the new entry to `azmods-repo.json`, so
`git pull` before the next push.

## Related repositories

| Repository | |
|---|---|
| [xdj-az-mods](https://github.com/Kyle-Hosman/xdj-az-mods) | AZ-MODS itself: install, features, releases |
| [xdj-az-emu](https://github.com/Kyle-Hosman/xdj-az-emu) | the developer emulator: the deck's program on a computer |
| [az-mods-sdk](https://github.com/Kyle-Hosman/az-mods-sdk) | the header, the build container, the developer guide |
| [az-mods-index](https://github.com/Kyle-Hosman/az-mods-index) | the Mod Index: repositories decks show by default, an optional route for publishing |

## License

GPL-3.0-only. See [LICENSE](LICENSE).

<p align="center"><sub>Not affiliated with AlphaTheta, Pioneer DJ or rekordbox; trademarks belong to their owners.</sub></p>
