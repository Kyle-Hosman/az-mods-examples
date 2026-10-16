/* SPDX-License-Identifier: GPL-3.0-only
 *
 * set-clock: the time of day and how long the set has been running, on a Mod Menu page.
 *
 * The set starts when a deck first has a track loaded. The start is kept as a settings
 * value, so the count survives a reload of the deck's program; a start more than a day
 * old, or one that cannot be checked because the deck's clock is not set, is dropped.
 *
 * While the deck's program runs, the elapsed time comes from the monotonic clock, so a
 * clock correction (the deck setting its time from the network, say) never makes the
 * count jump. The wall clock only names the start for the next run.
 *
 * The deck keeps its clock in UTC and has no time zone setting, so the page has its own:
 * a fixed offset from UTC in 15 minute steps. Daylight saving is the DJ's to adjust.
 */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "azmod-mod.h"

#define MOD_ID "set-clock"

#define NDECKS 4
#define VALID_EPOCH 1704067200LL    /* 2024-01-01: an earlier clock was never set */
#define MAX_RESUME_SEC (24 * 3600)     /* a saved start older than this is a past set */
#define STEP_SEC 2               /* a wall/monotonic shift above this is a clock step */
#define OFFSET_STEP 15              /* minutes */
#define OFFSET_MIN (-12 * 60)
#define OFFSET_MAX (14 * 60)
#define CONFIRM_MS 3000            /* 3 s to confirm a reset */

static const az_core_v1 *C;
static int have_str_cfg;

/* settings */
static int clock_24h = 1;
static int utc_offset;                  /* minutes east of UTC */

/* the set timer; start_mono is in CLOCK_MONOTONIC seconds */
static int running;
static int64_t start_mono;
static int start_saved;             /* the settings hold this run's start */
static int64_t wall_minus_mono;         /* last seen, to notice a clock step */

/* what the page shows, to repaint only when it changes */
static int64_t shown_minute = -2;       /* -1 = clock not set */
static int64_t shown_elapsed = -2;      /* -1 = not running */
static int stale;                   /* the page shows something out of date */
static int64_t redraw_sec = -1;         /* the monotonic second of the last repaint request */

/* page state; touched only on the UI thread (tick and the screen callbacks) */
enum { ROW_RESET,
       ROW_24H,
       ROW_ZONE,
       NROWS };
static int sel = -1;                /* -1 = nothing selected until the knob moves */
static int editing;                 /* the knob changes the time zone */
static int64_t confirm_until;           /* != 0: the next reset press before this (ms) resets */
static int have_layout;
static az_rect row_box[NROWS];
static az_rect zone_less, zone_more;

/* ---- clocks ------------------------------------------------------------------------- */

static int64_t mono_now(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return (int64_t)ts.tv_sec;
}

/* milliseconds on the monotonic clock, for timeouts: tick's rate varies, so never a count of ticks */
static int64_t mono_ms(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int64_t wall_now(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) return 0;
    return (int64_t)ts.tv_sec;
}

static int wall_valid(int64_t wall)
{
    return wall >= VALID_EPOCH;
}

/* Minute of the day in the chosen zone, 0..1439. */
static int minute_of_day(int64_t wall)
{
    int64_t m = (wall / 60 + utc_offset) % 1440;
    return (int)(m < 0 ? m + 1440 : m);
}

static void format_time_of_day(char *buf, size_t n, int64_t wall)
{
    if (!wall_valid(wall)) {
        snprintf(buf, n, "--:--");
        return;
    }
    int m = minute_of_day(wall);
    int h = m / 60, mm = m % 60;
    if (clock_24h)
        snprintf(buf, n, "%02d:%02d", h, mm);
    else
        snprintf(buf, n, "%d:%02d %s", h % 12 ? h % 12 : 12, mm, h < 12 ? "AM" : "PM");
}

static void format_elapsed(char *buf, size_t n, int64_t sec)
{
    if (sec < 0) {
        snprintf(buf, n, "-:--:--");
        return;
    }
    snprintf(buf, n, "%" PRId64 ":%02d:%02d", sec / 3600, (int)(sec / 60 % 60), (int)(sec % 60));
}

static void format_offset(char *buf, size_t n)
{
    if (utc_offset == 0) {
        snprintf(buf, n, "UTC");
        return;
    }
    int a = utc_offset < 0 ? -utc_offset : utc_offset;
    snprintf(buf, n, "UTC%c%02d:%02d", utc_offset < 0 ? '-' : '+', a / 60, a % 60);
}

/* ---- the set timer ------------------------------------------------------------------ */

static void save_start(int64_t wall_start)
{
    if (!have_str_cfg) return;
    char v[24];
    snprintf(v, sizeof v, "%" PRId64, wall_start);
    C->cfg_set_str(MOD_ID, "start", v);
}

static void clear_start(void)
{
    if (have_str_cfg) C->cfg_set_str(MOD_ID, "start", "");
}

static int any_deck_loaded(void)
{
    for (int deck = 0; deck < NDECKS; deck++)
        if (C->deck_loaded(deck) == 1) return 1;
    return 0;
}

static int64_t elapsed_now(int64_t mono)
{
    if (!running) return -1;
    int64_t e = mono - start_mono;
    return e < 0 ? 0 : e;
}

static void reset_timer(void)
{
    running = 0;
    start_saved = 0;
    clear_start();
    C->log(MOD_ID, "set timer reset");
}

/* Resume a start saved by an earlier run, when it can be trusted. */
static void resume_saved_start(void)
{
    if (!have_str_cfg) return;
    char v[32];
    if (C->cfg_get_str(MOD_ID, "start", v, (int)sizeof v, "") <= 0) return;

    char *end = NULL;
    long long saved = strtoll(v, &end, 10);
    int64_t mono = mono_now(), wall = wall_now();
    int64_t age = wall - (int64_t)saved;
    if (end == v || *end != '\0' || !wall_valid(wall) || !wall_valid(saved) ||
        age < 0 || age > MAX_RESUME_SEC) {
        clear_start();
        C->log(MOD_ID, "saved start %s dropped", v);
        return;
    }
    running = 1;
    start_mono = mono - age;
    start_saved = 1;
    wall_minus_mono = wall - mono;
    C->log(MOD_ID, "resumed a set started %" PRId64 " s ago", age);
}

/* ---- the page ----------------------------------------------------------------------- */

static int inside(const az_rect *r, int x, int y)
{
    return r->w > 0 && x >= r->x && x < r->x + r->w && y >= r->y && y < r->y + r->h;
}

/* The largest size, at most `cap`, at which `s` fits in `w` pixels; 0 if none from 8 up. */
static int fit_px(const char *s, int w, int cap)
{
    if (cap > 96) cap = 96;
    for (int px = cap; px >= 8; px -= px > 24 ? 4 : 1)
        if (C->text_width(s, px) <= w) return px;
    return 0;
}

static void draw_centered(az_draw *d, az_rect area, int y, const char *s, int px, uint32_t rgb)
{
    int w = C->text_width(s, px);
    C->draw_text(d, area.x + (area.w - w) / 2, y, s, px, rgb);
}

/* A label over a big value, centered in [y, y + h). `widest` sizes the value, so the
 * digits keep their size as they change (a longer value, such as a set of 100 hours or
 * more, sizes itself). */
static void draw_block(az_draw *d, az_rect area, int y, int h, const char *label,
                       const char *value, const char *widest, uint32_t value_rgb, int label_px)
{
    int label_h = label_px * 3 / 2;
    int lpx = fit_px(label, area.w - label_px, label_px);
    if (strlen(value) > strlen(widest)) widest = value;
    int big = fit_px(widest, area.w - label_px, (h - label_h) * 2 / 3);
    if (lpx < 8 || big < 8) return;
    int top = y + (h - label_h - big * 3 / 2) / 2;
    draw_centered(d, area, top, label, lpx, C->color(AZ_COL_TEXT_DIM));
    draw_centered(d, area, top + label_h, value, big, value_rgb);
}

/* Row text at `px`, smaller if it would not fit in `w`; left out if it cannot fit at all. */
static int draw_fitted(az_draw *d, int x, int y, int w, const char *s, int px, uint32_t rgb)
{
    int fpx = fit_px(s, w, px);
    if (fpx >= 8) C->draw_text(d, x, y, s, fpx, rgb);
    return fpx >= 8 ? C->text_width(s, fpx) : 0;
}

static void draw_row_back(az_draw *d, const az_rect *r, int selected)
{
    if (selected) C->fill_rect(d, r->x, r->y, r->w, r->h, C->color(AZ_COL_HI));
}

static void on_draw(az_draw *d, az_rect area, void *user)
{
    (void)user;
    const uint32_t text = C->color(AZ_COL_TEXT);
    const uint32_t dim = C->color(AZ_COL_TEXT_DIM);
    const int row = C->row_height(d);
    const int px = row / 2 > 96 ? 96 : row / 2;
    const int pad = row / 4;

    C->draw_header(d, "Set Clock");
    have_layout = 0;
    if (px < 8 || area.h < row * (NROWS + 3)) return;

    int64_t mono = mono_now(), wall = wall_now();
    int64_t elapsed = elapsed_now(mono);
    shown_minute = wall_valid(wall) ? wall / 60 : -1;
    shown_elapsed = elapsed;

    /* the two readouts share the space between the header and the rows */
    int top = area.y + row;
    int rows_top = area.y + area.h - NROWS * row;
    int block_h = (rows_top - top) / 2;
    char value[32], label[64], zone[24];

    format_offset(zone, sizeof zone);
    format_time_of_day(value, sizeof value, wall);
    if (wall_valid(wall))
        snprintf(label, sizeof label, "Time of day (%s)", zone);
    else
        snprintf(label, sizeof label, "Time of day (the deck's clock is not set)");
    draw_block(d, area, top, block_h, label, value, clock_24h ? "88:88" : "88:88 PM", text, px);

    format_elapsed(value, sizeof value, elapsed);
    draw_block(d, area, top + block_h, block_h,
               running ? "Set running" : "Set starts when a track is loaded",
               value, "88:88:88", running ? text : dim, px);

    /* the rows */
    for (int i = 0; i < NROWS; i++)
        row_box[i] = (az_rect){ area.x, rows_top + i * row, area.w, row };
    C->fill_rect(d, area.x + pad, rows_top - 1, area.w - 2 * pad, 1, dim);

    const az_rect *r = &row_box[ROW_RESET];
    draw_row_back(d, r, sel == ROW_RESET);
    uint32_t fg = sel == ROW_RESET ? C->color(AZ_COL_HI_TEXT) : text;
    const char *reset = confirm_until ? "Press again to reset" : "Reset set timer";
    draw_fitted(d, r->x + pad, r->y + pad, r->w - 2 * pad, reset, px, fg);

    r = &row_box[ROW_24H];
    draw_row_back(d, r, sel == ROW_24H);
    fg = sel == ROW_24H ? C->color(AZ_COL_HI_TEXT) : text;
    int box = row - 2 * pad;
    C->draw_checkbox(d, r->x + pad, r->y + pad, box, clock_24h, sel == ROW_24H);
    draw_fitted(d, r->x + 2 * pad + box, r->y + pad, r->w - 3 * pad - box, "24-hour clock", px, fg);

    r = &row_box[ROW_ZONE];
    draw_row_back(d, r, sel == ROW_ZONE);
    fg = sel == ROW_ZONE ? C->color(AZ_COL_HI_TEXT) : text;
    int label_end = r->x + pad + draw_fitted(d, r->x + pad, r->y + pad, r->w / 2 - pad, "Time zone", px, fg);

    /* "<  UTC+05:45  >", right-aligned; the arrows are tap targets */
    int arrow_w = row;
    int value_w = C->text_width("UTC+00:00", px) + 2 * pad;
    int more_x = r->x + r->w - pad - arrow_w;
    int value_x = more_x - value_w;
    int less_x = value_x - arrow_w;
    int zw = C->text_width(zone, px);
    zone_less.w = zone_more.w = 0;
    if (less_x >= label_end + pad) {
        int aw = C->text_width("<", px);
        zone_less = (az_rect){ less_x, r->y, arrow_w, r->h };
        zone_more = (az_rect){ more_x, r->y, arrow_w, r->h };
        C->draw_text(d, less_x + (arrow_w - aw) / 2, r->y + pad, "<", px, fg);
        C->draw_text(d, more_x + (arrow_w - aw) / 2, r->y + pad, ">", px, fg);
        C->draw_text(d, value_x + (value_w - zw) / 2, r->y + pad, zone, px, fg);
        if (editing) C->draw_rect(d, value_x, r->y + pad / 2, value_w, row - pad, 2, fg);
    } else if (r->x + r->w - pad - zw >= label_end + pad) {
        /* too narrow for the arrows: the value alone, set with the knob */
        C->draw_text(d, r->x + r->w - pad - zw, r->y + pad, zone, px, fg);
    }
    have_layout = 1;
}

static void redraw(void)
{
    C->request_redraw();
}

static void set_offset(int minutes)
{
    if (minutes < OFFSET_MIN) minutes = OFFSET_MIN;
    if (minutes > OFFSET_MAX) minutes = OFFSET_MAX;
    if (minutes == utc_offset) return;
    utc_offset = minutes;
    C->cfg_set(MOD_ID, "utc_offset", utc_offset);
}

static void activate(int which)
{
    switch (which) {
    case ROW_RESET:
        if (confirm_until) {
            confirm_until = 0;
            reset_timer();
        } else {
            confirm_until = mono_ms() + CONFIRM_MS;
        }
        break;
    case ROW_24H:
        clock_24h = !clock_24h;
        C->cfg_set(MOD_ID, "clock_24h", clock_24h);
        break;
    case ROW_ZONE:
        editing = !editing;
        break;
    default:
        break;
    }
}

static void select_row(int which)
{
    if (which != ROW_RESET) confirm_until = 0;
    if (which != ROW_ZONE) editing = 0;
    sel = which;
}

static int on_input(const az_event *ev, void *user)
{
    (void)user;
    switch (ev->kind) {
    case AZ_EV_KNOB:
        if (editing) {
            set_offset(utc_offset + (ev->dir < 0 ? -OFFSET_STEP : OFFSET_STEP));
        } else {
            int next = sel < 0 ? 0 : sel + (ev->dir < 0 ? -1 : 1);
            if (next < 0) next = 0;
            if (next >= NROWS) next = NROWS - 1;
            select_row(next);
        }
        redraw();
        return 1;
    case AZ_EV_CLICK:
        if (sel >= 0) {
            activate(sel);
            redraw();
        }
        return 1;
    case AZ_EV_TAP:
        if (!have_layout) return 1;
        if (inside(&zone_less, ev->x, ev->y) || inside(&zone_more, ev->x, ev->y)) {
            select_row(ROW_ZONE);
            set_offset(utc_offset + (inside(&zone_less, ev->x, ev->y) ? -OFFSET_STEP : OFFSET_STEP));
            redraw();
            return 1;
        }
        for (int i = 0; i < NROWS; i++) {
            if (inside(&row_box[i], ev->x, ev->y)) {
                select_row(i);
                activate(i);
                redraw();
                break;
            }
        }
        return 1;
    case AZ_EV_BACK:
        if (editing) {
            editing = 0;
            redraw();
            return 1;
        }
        return 0;
    default:
        return 0;
    }
}

static void on_enter(void *user)
{
    (void)user;
    sel = -1;
    editing = 0;
    confirm_until = 0;
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
    int64_t mono = mono_now(), wall = wall_now();

    if (!running && any_deck_loaded()) {
        running = 1;
        start_mono = mono;
        start_saved = 0;
        C->log(MOD_ID, "set started");
    }

    /* Name the start in wall time for the next run: once the clock is valid, and again
     * whenever the clock is stepped. */
    if (running && wall_valid(wall)) {
        int64_t wm = wall - mono;
        int64_t step = wm - wall_minus_mono;
        if (!start_saved || step > STEP_SEC || step < -STEP_SEC) {
            save_start(wall - (mono - start_mono));
            start_saved = 1;
        }
        wall_minus_mono = wm;
    }

    if (confirm_until && mono_ms() >= confirm_until) {
        confirm_until = 0;
        stale = 1;
    }

    /* At most one repaint a second: the elapsed time is the fastest thing on the page. */
    int64_t minute = wall_valid(wall) ? wall / 60 : -1;
    int64_t elapsed = elapsed_now(mono);
    if (minute != shown_minute || elapsed != shown_elapsed) stale = 1;
    if (stale && mono != redraw_sec) {
        stale = 0;
        redraw_sec = mono;
        shown_minute = minute;
        shown_elapsed = elapsed;
        redraw();
    }
}

static int init(const az_core_v1 *core)
{
    if (!core || core->abi != AZ_MOD_ABI) return 1;
    C = core;
    if (!AZ_HAS(C, register_screen) || !AZ_HAS(C, request_redraw) ||
        !AZ_HAS(C, text_width) || !AZ_HAS(C, deck_loaded)) {
        C->log(MOD_ID, "this AZ-MODS lacks a service the page needs");
        return 2;
    }
    have_str_cfg = AZ_HAS(C, cfg_get_str) && AZ_HAS(C, cfg_set_str);

    clock_24h = C->cfg_get(MOD_ID, "clock_24h", 1) ? 1 : 0;
    utc_offset = C->cfg_get(MOD_ID, "utc_offset", 0);
    if (utc_offset < OFFSET_MIN || utc_offset > OFFSET_MAX || utc_offset % OFFSET_STEP)
        utc_offset = 0;
    resume_saved_start();

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
