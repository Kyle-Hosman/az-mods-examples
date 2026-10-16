/* SPDX-License-Identifier: GPL-3.0-only
 *
 * scenarios: the functional scenarios of the example mods, for the az-mods-sdk test harness
 * (tools/harness/harness.c). Not built on its own: the harness includes it when it is built
 * with -DAZ_HARNESS_SCENARIOS='"tests/scenarios.c"', so these functions use its stub core
 * and helpers directly. harness_scenarios() is the entry the harness calls.
 */

static void functional_set_clock(void)
{
    printf("  functional set-clock\n");
    reset_world();
    if (!load_mod()) return;
    call_enter();
    ticks(30);
    call_draw(FULL, 60);
    expect(frame_has("-:--:--") && !frame_has("0:00:"), "no track loaded: the set has not started");
    expect(frame_has("20:00"), "time of day 20:00 in UTC");
    expect(!frame_has("21:00"), "  control: 21:00 is not shown");

    D.loaded[1] = 1;                            /* deck 2 */
    ticks(65 * 30);
    call_draw(FULL, 60);
    expect(frame_has("0:01:05"), "a track on deck 2, 65 s later: 0:01:05");
    expect(!frame_has("-:--:--"), "  control: the not-started text is gone");
    const char *st = cfg_str("start");
    expect(st && strtoll(st, NULL, 10) == T0_WALL + 1, "the start is saved as the wall-clock second it began");

    wall_ns += 3600 * NS;                       /* the deck sets its clock from the network */
    ticks(10 * 30);
    call_draw(FULL, 60);
    expect(frame_has("0:01:15"), "a one-hour clock step does not move the set time (0:01:15)");
    expect(frame_has("21:01"), "the time of day follows the step (21:01)");
    st = cfg_str("start");
    expect(st && strtoll(st, NULL, 10) == T0_WALL + 1 + 3600, "the saved start moves with the step");

    advance(20 * NS);                           /* reload: the program restarts, ~20 s */
    if (!load_mod()) return;
    call_enter();
    ticks(1);
    call_draw(FULL, 60);
    expect(frame_has("0:01:35"), "after a reload the set carries on (0:01:35)");

    /* reset needs two presses; one press times out */
    D.loaded[1] = 0;
    call_input(AZ_EV_CLICK, 0, 0, 0);
    ticks(1);
    call_draw(FULL, 60);
    expect(frame_has("0:01:3"), "a click with nothing selected does nothing");
    call_input(AZ_EV_KNOB, 1, 0, 0);            /* selects the reset row */
    call_input(AZ_EV_CLICK, 0, 0, 0);
    call_draw(FULL, 60);
    expect(frame_has("Press again to reset"), "the first press asks for confirmation");
    ticks(100);
    call_draw(FULL, 60);
    expect(frame_has("Reset set timer") && frame_has("0:01:"), "unconfirmed, it times out and keeps the set");
    call_input(AZ_EV_CLICK, 0, 0, 0);
    call_input(AZ_EV_CLICK, 0, 0, 0);
    ticks(1);
    call_draw(FULL, 60);
    expect(frame_has("-:--:--"), "two presses reset; with no track loaded it waits");
    st = cfg_str("start");
    expect(st && !st[0], "the saved start is cleared");

    /* the time zone: down two rows, click, turn the knob, BACK */
    call_input(AZ_EV_KNOB, 1, 0, 0);
    call_input(AZ_EV_KNOB, 1, 0, 0);
    call_input(AZ_EV_CLICK, 0, 0, 0);
    for (int i = 0; i < 4; i++) call_input(AZ_EV_KNOB, 1, 0, 0);
    call_draw(FULL, 60);
    expect(frame_has("UTC+01:00") && frame_has("22:0"), "four knob steps: UTC+01:00, an hour ahead");
    expect(cfg_int("utc_offset", 0) == 60, "the offset is saved");
    expect(call_input(AZ_EV_BACK, 0, 0, 0) == 1, "BACK leaves the time zone editor first");
    expect(call_input(AZ_EV_BACK, 0, 0, 0) == 0, "then BACK is the core's");
    for (int i = 0; i < 200; i++) call_input(AZ_EV_KNOB, 1, 0, 0);     /* stops at the last row */
    call_input(AZ_EV_CLICK, 0, 0, 0);
    for (int i = 0; i < 200; i++) call_input(AZ_EV_KNOB, -1, 0, 0);
    call_draw(FULL, 60);
    expect(frame_has("UTC-12:00"), "the zone stops at UTC-12:00");
    expect(call_input(AZ_EV_BACK, 0, 0, 0) == 1, "BACK leaves the editor");

    /* 12-hour clock */
    call_enter();
    call_input(AZ_EV_KNOB, 1, 0, 0);
    call_input(AZ_EV_KNOB, 1, 0, 0);
    call_input(AZ_EV_CLICK, 0, 0, 0);
    call_draw(FULL, 60);
    expect(frame_has("9:0") && frame_has(" AM") && cfg_int("clock_24h", 1) == 0,
           "the 24-hour box off: a 12-hour clock (21:0x UTC at UTC-12:00 is 9:0x AM)");

    /* a saved start from yesterday is dropped */
    reset_world();
    s_cfg_set_str("set-clock", "start", "1791570000");      /* ~25.7 h before T0 */
    if (!load_mod()) return;
    ticks(1);
    call_draw(FULL, 60);
    expect(frame_has("-:--:--"), "a start more than a day old is not resumed");

    /* the deck's clock not set: the set still counts, the start is saved once it is */
    reset_world();
    wall_ns = 1000 * NS;
    if (!load_mod()) return;
    D.loaded[3] = 1;
    ticks(5 * 30);
    call_draw(FULL, 60);
    expect(frame_has("--:--") && frame_has("not set") && frame_has("0:00:05"),
           "clock not set: no time of day, the set still counts");
    st = cfg_str("start");
    expect(!st || !st[0], "nothing is saved while the clock is not set");
    wall_ns = (T0_WALL + 100) * NS;
    ticks(1);
    st = cfg_str("start");
    expect(st && strtoll(st, NULL, 10) == T0_WALL + 100 - 5, "the start is saved once the clock is set");

    /* taps: the arrows step the zone, the reset row needs two taps */
    reset_world();
    if (!load_mod()) return;
    call_enter();
    call_draw(FULL, 60);
    int zone_y = 800 - 60 + 30;
    call_input(AZ_EV_TAP, 0, 1280 - 15 - 30, zone_y);       /* the ">" arrow */
    call_draw(FULL, 60);
    expect(frame_has("UTC+00:15"), "tapping > steps the zone by 15 minutes");
    if (!frame_has("UTC+00:15")) dump_frame();
}

static void functional_deck_overview(void)
{
    printf("  functional deck-overview\n");
    reset_world();
    if (!load_mod()) return;
    D.count = 4;
    D.loaded[0] = 1;
    D.bpm[0] = 128.0;
    snprintf(D.title[0], sizeof D.title[0], "Artist - First");
    D.loaded[1] = 1;
    D.bpm[1] = 130.0;
    snprintf(D.title[1], sizeof D.title[1], "Artist - Second");
    D.loaded[2] = 0;
    D.loaded[3] = 1;
    D.bpm[3] = 0.0;
    snprintf(D.title[3], sizeof D.title[3], "Artist - Fourth");
    call_enter();
    ticks(8);
    call_draw(FULL, 60);
    expect(frame_has("128.00") && frame_has("130.00"), "both known BPMs are shown");
    expect(frame_has("reference") && frame_has("+2.00  +1.6%"), "deck 1 is the reference; deck 2 is +2.00 / +1.6%");
    expect(!frame_has("-2.00"), "  control: no -2.00 while deck 1 is the reference");
    expect(frame_has("No track") && frame_has("Artist - Fourth"), "the empty deck and the titles");

    call_input(AZ_EV_KNOB, 1, 0, 0);
    ticks(1);
    call_draw(FULL, 60);
    expect(frame_has("-2.00  -1.5%"), "knob: deck 2 is the reference; deck 1 is -2.00 / -1.5%");
    expect(cfg_int("ref_deck", -1) == 1, "the reference deck is saved");

    /* a tap on deck 4's row (BPM unknown): no differences can be given */
    int rh = (800 - 60 - 60) / 4;
    if (rh > 120) rh = 120;
    call_input(AZ_EV_TAP, 0, 640, 60 + 3 * rh + rh / 2);
    call_draw(FULL, 60);
    expect(cfg_int("ref_deck", -1) == 3 && !frame_has("+2.00") && !frame_has("-2.00"),
           "tap: deck 4 is the reference; its BPM is unknown, so no differences");

    /* back to two decks: the reference (deck 4) is out of view, nothing is guessed */
    D.count = 2;
    ticks(10);
    call_draw(FULL, 60);
    expect(!frame_has("reference") && !frame_has("+2.00"), "2-deck view with deck 4 as reference: no differences");
    call_input(AZ_EV_KNOB, 1, 0, 0);
    call_draw(FULL, 60);
    expect(frame_has("reference") && frame_has("+2.00"), "the knob picks deck 1 again");

    /* a long title is cut to the space left, on a character boundary */
    D.count = 4;
    snprintf(D.title[1], sizeof D.title[1], "%s", "Ünïcødé Tïtlé – Ünïcødé Tïtlé – Ünïcødé Tïtlé – Ünïcødé Tïtlé – Ünïcødé Tïtlé – Ünïcødé Tïtlé");
    ticks(8);
    call_draw((az_rect){ 0, 0, 800, 480 }, 48);
    int cut = 0, valid = 1;
    for (int i = 0; i < frame_n; i++) {
        size_t n = strlen(frame[i]);
        if (n > 3 && !strcmp(frame[i] + n - 3, "...") && frame[i][0] == '\xc3') {
            cut = 1;
            for (size_t j = 0; j + 3 < n; j++) {
                unsigned char c = (unsigned char)frame[i][j];
                int len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2
                                     : (c >> 4) == 14  ? 3
                                     : (c >> 3) == 30  ? 4
                                                       : 0;
                if (!len || j + (size_t)len > n - 3) {
                    valid = 0;
                    break;
                }
                j += (size_t)len - 1;
            }
        }
    }
    expect(cut && valid, "a long UTF-8 title ends in \"...\" and is cut on a character boundary");

    /* a moving pitch fader: deck changes every tick, repaints coalesced to ~4 a second */
    unsigned long r0 = redraw_tick;
    for (int i = 0; i < 300; i++) {
        D.bpm[0] += 0.05;
        advance(TICK_NS);
        call_tick();
    }
    unsigned long n = redraw_tick - r0;
    char what[120];
    snprintf(what, sizeof what, "BPM moving every tick for 10 s: %lu repaints (30..40 expected)", n);
    expect(n >= 30 && n <= 40, what);
}

/* Ticks until the page shows `s` or `secs` of real time pass (the mod's own thread reads). */
static int wait_for(const char *s, double secs)
{
    for (int i = 0; i < (int)(secs * 50); i++) {
        ticks(1);
        call_draw(FULL, 60);
        if (frame_has(s)) return 1;
        usleep(20000);
    }
    return 0;
}

static void functional_library_browser(void)
{
    printf("  functional library-browser\n");
    reset_world();
    if (!load_mod()) return;
    call_enter();
    expect(wait_for("Warmup", 3.0),
           "the playlists are read on the mod's own thread and shown, "
           "including one whose parent is not in the listing (the top level)");
    expect(frame_has("USB 1") && frame_has("+ Sets") && frame_has("Peak"), "USB 1, the folder and the playlist in it");
    expect(!frame_has("USB 2"), "  control: no heading for the USB with no library");

    /* rows: USB 1, + Sets, Peak, Warmup. A click on the folder opens nothing. */
    call_input(AZ_EV_KNOB, 1, 0, 0);
    call_input(AZ_EV_CLICK, 0, 0, 0);
    call_draw(FULL, 60);
    expect(frame_has("Warmup") && !frame_has("track"), "a click on a folder opens nothing");
    call_input(AZ_EV_KNOB, 1, 0, 0);
    call_input(AZ_EV_CLICK, 0, 0, 0);
    expect(wait_for("Peak: 2 tracks", 3.0), "a click on Peak lists its 2 tracks (a cut list, return 1, still shows)");
    expect(frame_has("Opening Track - Artist A") && frame_has("128.0  8A") && frame_has("126.5  9A"),
           "title, artist, BPM and key, in playlist order");
    expect(!frame_has("Slow Start"), "  control: no track from another playlist");
    expect(call_input(AZ_EV_BACK, 0, 0, 0) == 1, "BACK from the tracks is handled by the page");
    call_draw(FULL, 60);
    expect(frame_has("Warmup") && !frame_has("Opening Track"), "and shows the playlists again");
    expect(call_input(AZ_EV_BACK, 0, 0, 0) == 0, "BACK from the playlists leaves the page to the core");
}

/* One block of a sine at `amp` (both channels) into every tap on `deck`, armed like a callback. */
static void play_block(int deck, float amp)
{
    for (int i = 0; i < ntaps; i++) {
        if (taps[i].source != deck) continue;
        static float lr[2 * 8192];
        for (int f = 0; f < taps[i].frames; f++)
            lr[2 * f] = lr[2 * f + 1] = amp * sinf(2.0f * 3.14159265f * 441.0f * (float)f / 44100.0f);
        arm(1);
        taps[i].fn(lr, taps[i].frames, taps[i].user);
        arm(0);
    }
}

static void functional_level_meter(void)
{
    printf("  functional level-meter\n");
    reset_world();
    if (!load_mod()) return;
    int decks = 0;
    for (int i = 0; i < ntaps; i++) decks |= 1 << taps[i].source;
    expect(decks == 0xf, "a tap on each of the four decks");
    D.count = 4;
    call_enter();
    play_block(0, 0.5f);       /* -6.0 dBFS peak */
    play_block(1, 0.0f);       /* silence */
    play_block(2, 1.0f);       /* full scale */
    ticks(2);
    call_draw(FULL, 60);
    expect(frame_has("DECK 1") && frame_has("DECK 4"), "four decks in a 4-deck view");
    expect(frame_has("-6.0"), "deck 1: a -6 dBFS sine shows -6.0");
    expect(frame_has("0.0") && frame_has("--"), "deck 3 at full scale shows 0.0; silent decks show --");
    expect(!frame_has("-12.0"), "  control: no level that was not played");
    /* the peak holds ~1.5 s, then falls */
    play_block(0, 0.0f);
    ticks(20);
    call_draw(FULL, 60);
    expect(frame_has("-6.0"), "the peak holds after the deck goes quiet");
    ticks(200);
    call_draw(FULL, 60);
    expect(!frame_has("-6.0"), "and falls away after the hold");
    D.count = 2;
    call_draw(FULL, 60);
    expect(frame_has("DECK 2") && !frame_has("DECK 3"), "two decks in a 2-deck view");
}

static int text_has(const char *s, const char *sub) { return s && strstr(s, sub) != NULL; }

static void set_deck(int d, int loaded, int playing, double bpm, const char *title, const char *artist)
{
    pthread_mutex_lock(&dmu);
    D.loaded[d] = loaded;
    D.playing[d] = playing;
    D.bpm[d] = bpm;
    snprintf(D.title[d], sizeof D.title[d], "%s", title);
    snprintf(D.artist[d], sizeof D.artist[d], "%s", artist);
    pthread_mutex_unlock(&dmu);
}

static void set_phase(int d, double ph)
{
    pthread_mutex_lock(&dmu);
    D.phase[d] = ph;
    pthread_mutex_unlock(&dmu);
}

static void real_sleep_ms(int ms)       /* the mod's own thread runs on the real clock */
{
    struct timespec ts = { 0, ms * 1000000L };
    while (nanosleep(&ts, &ts) != 0 && errno == EINTR) {}
}

static void functional_surface_demo(void)
{
    printf("  functional surface-demo\n");
    reset_world();
    if (!load_mod()) return;
    expect(nsurf == 1 && surfs[0].shown && surfs[0].placed, "one surface, placed and shown");
    expect(surfs[0].d.views == AZ_VIEW_BIT(AZ_VIEW_DECK), "shown on the deck screen only");
    __atomic_store_n(&D.view, AZ_VIEW_SOURCE, __ATOMIC_RELAXED);
    ticks(1);
    paint_surfaces(1);
    expect(frame_n == 0, "  control: nothing painted on SOURCE");
    __atomic_store_n(&D.view, AZ_VIEW_DECK, __ATOMIC_RELAXED);
    ticks(1);
    paint_surfaces(0);
    expect(frame_has("SURFACE DEMO") && frame_has("screen: DECK"), "on DECK the panel names the screen");
    az_rect r = surfs[0].r;
    call_input(AZ_EV_TAP, 0, r.x + 10, r.y + 10);
    paint_surfaces(0);
    char want[48];
    snprintf(want, sizeof want, "tap at %d,%d", r.x + 10, r.y + 10);
    expect(frame_has(want), "a tap is reported where it landed");
    expect(!frame_has("knob held"), "  control: the knob is not held before a long press");
    surface_input(0, AZ_EV_LONG, 0, r.x + 10, r.y + 10);
    paint_surfaces(0);
    expect(focus_surf == 0 && frame_has("knob held: 0"), "a long press takes the knob");
    call_input(AZ_EV_KNOB, 1, 0, 0);
    call_input(AZ_EV_KNOB, 1, 0, 0);
    call_input(AZ_EV_KNOB, 1, 0, 0);
    paint_surfaces(0);
    expect(frame_has("knob held: 3"), "three knob steps count to 3");
    call_input(AZ_EV_CLICK, 0, 0, 0);
    paint_surfaces(0);
    expect(frame_has("knob held: 0"), "the click resets the count");
    call_input(AZ_EV_BACK, 0, 0, 0);
    paint_surfaces(0);
    expect(focus_surf < 0 && frame_has("knob given back"), "BACK gives the knob back");
}

static void functional_deck_events(void)
{
    printf("  functional deck-events\n");
    reset_world();
    if (!load_mod()) return;
    expect(nlisten == 1 && nthreads >= 1, "a deck-event listener and a worker thread");
    set_deck(1, 1, 0, 124.0, "Opening Track", "Artist A");
    fire(1, AZ_DECK_LOADED);
    expect(!strcmp(last_banner(), "Deck 2: Opening Track - Artist A, 124.0 BPM"),
           "a load shows deck, title, artist and tempo");
    set_deck(2, 1, 0, 0.0, "Slow Start", "");
    fire(2, AZ_DECK_LOADED);
    expect(!strcmp(last_banner(), "Deck 3: Slow Start"), "an unknown tempo and artist are left out");
    expect(!text_has(last_banner(), "BPM"), "  control: no tempo guessed for deck 3");
    int before = nbanner;
    set_deck(0, 1, 1, 120.0, "Beats", "Artist B");
    fire(0, AZ_DECK_PLAY);
    expect(nbanner == before, "play banners are off until the DJ turns them on");
    fire(2, AZ_DECK_EJECTED);
    expect(Q.open && !strcmp(Q.yes, "Turn on"), "an eject asks whether to show play and stop banners");
    answer(1);
    expect(text_has(last_banner(), "banners on") && cfg_int("play_banners", 0) == 1,
           "Yes turns them on and saves it");

    /* four beats on deck 1: the phase wraps four times while it plays */
    set_deck(0, 1, 0, 120.0, "Beats", "Artist B");
    set_phase(0, 0.5);
    real_sleep_ms(150);
    set_deck(0, 1, 1, 120.0, "Beats", "Artist B");
    fire(0, AZ_DECK_PLAY);
    expect(!strcmp(last_banner(), "Deck 1 playing"), "with them on, a play shows a banner");
    for (int b = 0; b < 4; b++) {
        set_phase(0, 0.9);
        real_sleep_ms(200);
        set_phase(0, 0.1);
        real_sleep_ms(200);
    }
    ticks(2);
    fire(0, AZ_DECK_STOP);
    expect(!strcmp(last_banner(), "Deck 1 stopped after 4 beats"), "a stop counts the beats played");
    set_deck(3, 1, 0, 0.0, "Idle", "");
    fire(3, AZ_DECK_STOP);
    expect(!strcmp(last_banner(), "Deck 4 stopped"), "  control: no count for a deck that played no beats");
}

static int harness_scenarios(const char *id)
{
    if (!strcmp(id, "set-clock")) functional_set_clock();
    else if (!strcmp(id, "deck-overview")) functional_deck_overview();
    else if (!strcmp(id, "library-browser")) functional_library_browser();
    else if (!strcmp(id, "level-meter")) functional_level_meter();
    else if (!strcmp(id, "surface-demo")) functional_surface_demo();
    else if (!strcmp(id, "deck-events")) functional_deck_events();
    else return 0;
    return 1;
}
