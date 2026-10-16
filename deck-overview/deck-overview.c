/* SPDX-License-Identifier: GPL-3.0-only
 *
 * deck-overview: every deck in the current view on one Mod Menu page: whether a track is
 * loaded, its BPM, its file name, and how far its BPM is from a reference deck the DJ
 * picks with the knob or a tap.
 *
 * Everything shown comes straight from the core's read-only deck services. A value the
 * core does not know is shown as "--", never estimated.
 */
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "azmod-mod.h"

#define MOD_ID "deck-overview"

#define NDECKS 4
#define TITLE_MAX 128
#define MIN_REDRAW_MS 250        /* at most 4 repaints a second from deck changes */

static const az_core_v1 *C;

/* settings */
static int ref_deck;                /* the deck the others are compared with */

/* What tick() last read. Touched only on the UI thread (tick and the screen callbacks). */
struct deck {
    int loaded;
    long bpm_centi;                 /* BPM * 100, rounded; -1 = unknown */
    char title[TITLE_MAX];          /* "" = unknown */
};
static int ndecks;          /* 0 = unknown */
static struct deck decks[NDECKS];

static unsigned tick_no;            /* which deck's title this tick re-reads (one per tick) */
static int dirty;              /* something shown changed since the last request */
static long last_redraw_ms = -MIN_REDRAW_MS;

static int have_layout;
static az_rect deck_box[NDECKS];

/* ---- reading the decks -------------------------------------------------------------- */

static long round_centi(double v)
{
    return (long)(v * 100.0 + (v < 0 ? -0.5 : 0.5));
}

static long read_bpm_centi(int deck)
{
    double bpm = C->deck_bpm(deck);
    if (!(bpm > 0.0 && bpm < 1000.0)) return -1;     /* also false for NaN */
    return round_centi(bpm);
}

static void read_title(int deck, char *out)
{
    char buf[TITLE_MAX];
    int n = C->deck_title(deck, buf, (int)sizeof buf);
    if (n <= 0) {
        out[0] = '\0';
        return;
    }
    buf[sizeof buf - 1] = '\0';
    for (char *p = buf; *p; p++)
        if ((unsigned char)*p < 0x20 || *p == 0x7f) *p = ' ';
    memcpy(out, buf, sizeof buf);
}

static int read_count(void)
{
    int n = C->deck_count();
    return n == 2 || n == 4 ? n : 0;
}

/* Re-read the decks, polling one title per call; returns 1 if anything shown changed. */
static int poll(int all_titles)
{
    int changed = 0;
    int n = read_count();
    if (n != ndecks) {
        ndecks = n;
        changed = 1;
    }
    for (int deck = 0; deck < ndecks; deck++) {
        struct deck *s = &decks[deck];
        int loaded = C->deck_loaded(deck) == 1;
        long bpm = loaded ? read_bpm_centi(deck) : -1;
        int load_changed = loaded != s->loaded;
        if (load_changed || bpm != s->bpm_centi) changed = 1;
        s->loaded = loaded;
        s->bpm_centi = bpm;

        if (all_titles || load_changed || (int)(tick_no % NDECKS) == deck) {
            char title[TITLE_MAX] = "";
            if (loaded) read_title(deck, title);
            if (strcmp(title, s->title) != 0) {
                memcpy(s->title, title, sizeof title);
                changed = 1;
            }
        }
    }
    return changed;
}

/* ---- formatting --------------------------------------------------------------------- */

static void format_bpm(char *buf, size_t n, long centi)
{
    if (centi < 0) snprintf(buf, n, "--");
    else snprintf(buf, n, "%ld.%02ld", centi / 100, centi % 100);
}

/* "+1.25  +0.9%": this deck's BPM against the reference deck's. */
static void format_diff(char *buf, size_t n, int deck)
{
    const struct deck *s = &decks[deck];
    if (deck == ref_deck) {
        snprintf(buf, n, "reference");
        return;
    }
    if (ref_deck >= ndecks || s->bpm_centi < 0 || decks[ref_deck].bpm_centi <= 0) {
        snprintf(buf, n, "--");
        return;
    }
    long ref = decks[ref_deck].bpm_centi;
    long diff = s->bpm_centi - ref;
    long tenths = (diff * 1000 + (diff < 0 ? -ref / 2 : ref / 2)) / ref;   /* percent * 10 */
    long ad = diff < 0 ? -diff : diff, at = tenths < 0 ? -tenths : tenths;
    char sd = diff < 0 ? '-' : diff > 0 ? '+'
                                        : ' ';
    char st = tenths < 0 ? '-' : tenths > 0 ? '+'
                                            : ' ';
    snprintf(buf, n, "%c%ld.%02ld  %c%ld.%ld%%", sd, ad / 100, ad % 100, st, at / 10, at % 10);
}

/* The longest prefix of `s` (cut on a UTF-8 character boundary, "..." added) that fits in
 * `w` pixels at `px`. Writes "" if not even "..." fits. */
static void fit_text(char *out, size_t n, const char *s, int px, int w)
{
    snprintf(out, n, "%s", s);
    if (C->text_width(out, px) <= w) return;
    size_t lo = 0, hi = strlen(out);
    while (lo < hi) {                       /* the longest prefix that still fits */
        size_t mid = (lo + hi + 1) / 2, cut = mid;
        while (cut > 0 && ((unsigned char)s[cut] & 0xC0) == 0x80) cut--;
        snprintf(out, n, "%.*s...", (int)cut, s);
        if (C->text_width(out, px) <= w) lo = mid;
        else hi = mid - 1;
    }
    size_t cut = lo;
    while (cut > 0 && ((unsigned char)s[cut] & 0xC0) == 0x80) cut--;
    snprintf(out, n, "%.*s...", (int)cut, s);
    if (C->text_width(out, px) > w) out[0] = '\0';
}

/* ---- the page ----------------------------------------------------------------------- */

static void on_draw(az_draw *d, az_rect area, void *user)
{
    (void)user;
    const uint32_t text = C->color(AZ_COL_TEXT);
    const uint32_t dim = C->color(AZ_COL_TEXT_DIM);
    const int row = C->row_height(d);
    const int pad = row / 4;

    C->draw_header(d, "Deck Overview");
    have_layout = 0;
    const int mpx = row / 2 > 96 ? 96 : row / 2;     /* the menu's own text size */
    if (mpx < 8 || area.h < 2 * row) return;

    int top = area.y + row;
    if (ndecks == 0) {
        const char *msg = "Deck information is not available.";
        if (C->text_width(msg, mpx) <= area.w - 2 * pad)
            C->draw_text(d, area.x + pad, top + pad, msg, mpx, dim);
        return;
    }

    const char *hint = "Turn the knob or tap a deck to compare the others with it.";
    int hint_px = mpx * 2 / 3 < 8 ? 8 : mpx * 2 / 3;
    int show_hint = area.h >= row * (ndecks + 2) && C->text_width(hint, hint_px) <= area.w - 2 * pad;
    int avail = area.y + area.h - top - (show_hint ? row : 0);
    int rh = avail / ndecks;
    if (rh > 2 * row) rh = 2 * row;
    int px = rh / 2 > 96 ? 96 : rh / 2;
    int px2 = px * 2 / 3 < 8 ? 8 : px * 2 / 3;
    if (px < 8 || px2 * 3 / 2 > rh) return;

    for (int deck = 0; deck < ndecks; deck++) {
        const struct deck *s = &decks[deck];
        az_rect r = { area.x, top + deck * rh, area.w, rh };
        deck_box[deck] = r;
        int is_ref = deck == ref_deck;
        uint32_t fg = is_ref ? C->color(AZ_COL_HI_TEXT) : text;
        uint32_t fg_dim = is_ref ? C->color(AZ_COL_HI_TEXT) : dim;
        if (is_ref) C->fill_rect(d, r.x, r.y, r.w, r.h, C->color(AZ_COL_HI));
        else if (deck > 0) C->fill_rect(d, r.x + pad, r.y, r.w - 2 * pad, 1, dim);

        int ty = r.y + (rh - px * 3 / 2) / 2;      /* big text, centered in the row */
        int sy = r.y + (rh - px2 * 3 / 2) / 2;     /* small text */
        int x = r.x + pad, right = r.x + r.w - pad;
        char buf[TITLE_MAX + 8];

        /* deck number */
        snprintf(buf, sizeof buf, "%d", deck + 1);
        int col = C->text_width("4", px) + 2 * pad;
        if (x + col > right) continue;
        C->draw_text(d, x, ty, buf, px, fg);
        x += col;

        /* BPM, right-aligned in its column */
        format_bpm(buf, sizeof buf, s->bpm_centi);
        col = C->text_width("888.88", px);
        if (x + col > right) continue;
        C->draw_text(d, x + col - C->text_width(buf, px), ty, buf, px, s->bpm_centi < 0 ? fg_dim : fg);
        x += col + 2 * pad;

        /* difference from the reference deck */
        format_diff(buf, sizeof buf, deck);
        col = C->text_width("+88.88  +88.8%", px2);
        if (x + col > right) continue;
        C->draw_text(d, x, sy, buf, px2, is_ref || (buf[0] == '-' && buf[1] == '-') ? fg_dim : fg);
        x += col + 2 * pad;

        /* file name, cut to what is left */
        const char *name = !s->loaded ? "No track" : s->title[0] ? s->title
                                                                 : "--";
        fit_text(buf, sizeof buf, name, px2, right - x);
        if (buf[0]) C->draw_text(d, x, sy, buf, px2, s->loaded && s->title[0] ? fg : fg_dim);
    }

    if (show_hint)
        C->draw_text(d, area.x + pad, top + ndecks * rh + (row - hint_px * 3 / 2) / 2, hint,
                     hint_px, dim);
    have_layout = 1;
}

static void set_ref(int deck)
{
    if (deck < 0 || deck >= ndecks || deck == ref_deck) return;
    ref_deck = deck;
    C->cfg_set(MOD_ID, "ref_deck", ref_deck);
}

/* milliseconds on the monotonic clock: tick's rate varies, so repaints are paced by time */
static long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void redraw_now(void)
{
    dirty = 0;
    last_redraw_ms = now_ms();
    C->request_redraw();
}

static int on_input(const az_event *ev, void *user)
{
    (void)user;
    switch (ev->kind) {
    case AZ_EV_KNOB:
        if (ndecks > 0) {
            int next = ref_deck >= ndecks ? 0 : ref_deck + (ev->dir < 0 ? -1 : 1);
            if (next < 0) next = 0;
            if (next >= ndecks) next = ndecks - 1;
            set_ref(next);
            redraw_now();
        }
        return 1;
    case AZ_EV_TAP:
        if (!have_layout) return 1;
        for (int deck = 0; deck < ndecks; deck++) {
            const az_rect *r = &deck_box[deck];
            if (ev->x >= r->x && ev->x < r->x + r->w && ev->y >= r->y && ev->y < r->y + r->h) {
                set_ref(deck);
                redraw_now();
                break;
            }
        }
        return 1;
    case AZ_EV_CLICK:
        return 1;
    case AZ_EV_BACK:
    default:
        return 0;
    }
}

static void on_enter(void *user)
{
    (void)user;
    poll(1);
    redraw_now();
}

static const az_screen_v1 SCREEN = {
    .size = sizeof(az_screen_v1),
    .on_draw = on_draw,
    .on_input = on_input,
    .on_enter = on_enter,
    .user = NULL,
};

/* ---- lifecycle ---------------------------------------------------------------------- */

static void tick(void)
{
    tick_no++;
    if (poll(0)) dirty = 1;
    if (dirty && now_ms() - last_redraw_ms >= MIN_REDRAW_MS) redraw_now();
}

static int init(const az_core_v1 *core)
{
    if (!core || core->abi != AZ_MOD_ABI) return 1;
    C = core;
    if (!AZ_HAS(C, register_screen) || !AZ_HAS(C, request_redraw) || !AZ_HAS(C, text_width) ||
        !AZ_HAS(C, deck_count) || !AZ_HAS(C, deck_loaded) || !AZ_HAS(C, deck_bpm) ||
        !AZ_HAS(C, deck_title)) {
        C->log(MOD_ID, "this AZ-MODS lacks a service the page needs");
        return 2;
    }

    ref_deck = C->cfg_get(MOD_ID, "ref_deck", 0);
    if (ref_deck < 0 || ref_deck >= NDECKS) ref_deck = 0;
    for (int deck = 0; deck < NDECKS; deck++) decks[deck].bpm_centi = -1;

    if (C->register_screen(MOD_ID, &SCREEN) != 0) {
        C->log(MOD_ID, "register_screen refused");
        return 3;
    }
    return 0;
}

static const char *const FW_TAGS[] = { "any", NULL };

static const az_mod_v1 MOD = {
    .abi = AZ_MOD_ABI,
    .size = sizeof(az_mod_v1),
    .id = MOD_ID,
    .fw_tags = FW_TAGS,
    .init = init,
    .tick = tick,
    .on_frame = NULL,
};

const az_mod_v1 *az_mod_v1_entry(void)
{
    return &MOD;
}
