/* SPDX-License-Identifier: GPL-3.0-only
 *
 * deck-events: the deck services of AZ-MODS 1.2.3, in one small mod.
 *
 *   on_deck_event   a banner when a deck loads a track ("Deck 2: Title - Artist, 124.0 BPM"),
 *                   and, if the DJ turned them on, when a deck starts or stops
 *   deck_info       the title, artist and tempo for that banner
 *   thread_start    a worker thread that follows every deck's beat with deck_grid and counts
 *                   the beats each deck plays
 *   post_ui         the worker hands its counts to the UI thread, where the stop banner reads
 *                   them ("Deck 1 stopped after 64 beats")
 *   confirm         ejecting a deck asks, in a dialog over whatever is on screen, whether to
 *                   show play and stop banners too
 *
 * A value the core does not know (-1, "") is left out of a banner, never guessed. Every
 * service is checked with AZ_HAS in init(), so on an older core the mod loads and does nothing.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "azmod-mod.h"

#define MOD_ID "deck-events"
#define NDECKS 4
#define BANNER_MS 2500
#define POLL_MS 20          /* the worker's step: well under a beat at any tempo */

static const az_core_v1 *C;

/* UI thread only: init, the event callback, the confirm answer and the post_ui callback. */
static int play_banners;               /* show PLAY and STOP too */
static unsigned beats_ui[NDECKS];      /* beats played since the deck last started */

/* ---- the worker: counts beats from the grid phase ------------------------------------ */

/* Packs a deck and a count into post_ui's user pointer, so nothing is allocated per call. */
static void *pack(int deck, unsigned beats)
{
    return (void *)(uintptr_t)(((uintptr_t)beats << 2) | (unsigned)deck);
}

static void on_beats(void *user)        /* UI thread, via post_ui */
{
    uintptr_t v = (uintptr_t)user;
    beats_ui[v & 3] = (unsigned)(v >> 2);
}

static void *worker(void *arg)
{
    (void)arg;
    double last[NDECKS];
    unsigned beats[NDECKS] = { 0 };
    int was_playing[NDECKS] = { 0 };
    for (int d = 0; d < NDECKS; d++) last[d] = -1.0;

    for (;;) {
        for (int d = 0; d < NDECKS; d++) {
            az_deck_info di;
            memset(&di, 0, sizeof di);
            di.size = (int)sizeof di;
            int playing = C->deck_info(d, &di) == 0 && di.playing == 1;
            if (playing && !was_playing[d]) {
                beats[d] = 0;                       /* a new run: count from zero */
                C->post_ui(MOD_ID, on_beats, pack(d, 0));
            }
            was_playing[d] = playing;

            double beat = -1.0, bar = -1.0;
            if (!playing || C->deck_grid(d, &beat, &bar) != 0) {
                last[d] = -1.0;                     /* no grid or not playing: no beats */
                continue;
            }
            /* the phase wraps from near 1 back to near 0 on each beat */
            if (last[d] >= 0.75 && beat < 0.25) {
                beats[d]++;
                C->post_ui(MOD_ID, on_beats, pack(d, beats[d]));
            }
            last[d] = beat;
        }
        struct timespec ts = { 0, POLL_MS * 1000000L };
        nanosleep(&ts, NULL);
    }
    return NULL;
}

/* ---- deck events (UI thread) ----------------------------------------------------------- */

static void on_answer(int yes, void *user)
{
    (void)user;
    if (!yes) return;                           /* Cancel or BACK: leave it as it is */
    play_banners = !play_banners;
    C->cfg_set(MOD_ID, "play_banners", play_banners);
    C->banner(MOD_ID, play_banners ? "Deck Events: play and stop banners on" : "Deck Events: play and stop banners off", BANNER_MS);
}

static void on_deck(int deck, int what, void *user)
{
    (void)user;
    char msg[320];             /* the longest title and artist fit; the core shortens what the box cannot show */
    az_deck_info di;
    memset(&di, 0, sizeof di);
    di.size = (int)sizeof di;
    if (C->deck_info(deck, &di) != 0) return;

    switch (what) {
    case AZ_DECK_LOADED: {
        char tempo[24] = "";
        if (di.bpm > 0.0 && di.bpm < 1000.0) snprintf(tempo, sizeof tempo, ", %.1f BPM", di.bpm);
        if (di.title[0] && di.artist[0])
            snprintf(msg, sizeof msg, "Deck %d: %s - %s%s", deck + 1, di.title, di.artist, tempo);
        else if (di.title[0])
            snprintf(msg, sizeof msg, "Deck %d: %s%s", deck + 1, di.title, tempo);
        else
            snprintf(msg, sizeof msg, "Deck %d loaded%s", deck + 1, tempo);
        C->banner(MOD_ID, msg, BANNER_MS);
        break;
    }
    case AZ_DECK_PLAY:
        if (!play_banners) break;
        snprintf(msg, sizeof msg, "Deck %d playing", deck + 1);
        C->banner(MOD_ID, msg, BANNER_MS);
        break;
    case AZ_DECK_STOP: {
        if (!play_banners) break;
        unsigned n = beats_ui[deck];
        if (n)
            snprintf(msg, sizeof msg, "Deck %d stopped after %u beats", deck + 1, n);
        else
            snprintf(msg, sizeof msg, "Deck %d stopped", deck + 1);
        C->banner(MOD_ID, msg, BANNER_MS);
        break;
    }
    case AZ_DECK_EJECTED:
        /* -1 = another question is already on screen: then do not ask */
        (void)C->confirm(MOD_ID, "Deck Events",
                         play_banners ? "Stop showing a banner each time a deck starts or stops?"
                                      : "Show a banner each time a deck starts or stops, too?",
                         play_banners ? "Turn off" : "Turn on", on_answer, NULL);
        break;
    }
}

/* ---- lifecycle ------------------------------------------------------------------------- */

static int init(const az_core_v1 *core)
{
    if (!core || core->abi != AZ_MOD_ABI) return 1;
    C = core;
    if (!AZ_HAS(C, deck_info) || !AZ_HAS(C, deck_grid) || !AZ_HAS(C, on_deck_event) ||
        !AZ_HAS(C, banner) || !AZ_HAS(C, confirm) || !AZ_HAS(C, post_ui) ||
        !AZ_HAS(C, thread_start)) {
        C->log(MOD_ID, "this AZ-MODS lacks the deck services (1.2.3 or newer)");
        return 2;
    }
    play_banners = C->cfg_get(MOD_ID, "play_banners", 0) ? 1 : 0;
    if (C->on_deck_event(MOD_ID, on_deck, NULL) != 0) {
        C->log(MOD_ID, "on_deck_event refused");
        return 3;
    }
    if (C->thread_start(MOD_ID, "deck-events", worker, NULL) != 0)
        C->log(MOD_ID, "no worker thread: stop banners show no beat count");
    return 0;
}

static const char *const FW_TAGS[] = { "any", NULL };

static const az_mod_v1 MOD = {
    .abi = AZ_MOD_ABI,
    .size = sizeof(az_mod_v1),
    .id = MOD_ID,
    .fw_tags = FW_TAGS,
    .init = init,
    .tick = NULL,
    .on_frame = NULL,
};

const az_mod_v1 *az_mod_v1_entry(void)
{
    return &MOD;
}
