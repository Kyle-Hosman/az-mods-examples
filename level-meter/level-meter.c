/* SPDX-License-Identifier: GPL-3.0-only
 *
 * level-meter: a level meter for each deck, as a Mod Menu page: RMS as a bar, the peak as a
 * marker that holds for a moment, and the peak in dBFS.
 *
 * It listens with audio_tap, one tap per deck. On the XDJ-AZ the faders, EQ, crossfader and
 * master are mixed in hardware, so what a mod can hear is each deck's own output before the
 * mixer: the meter shows a playing deck at full level even with its channel fader down.
 *
 * The tap callback runs on a core thread (never the audio thread), once per block of 1024
 * frames. It only measures the block and stores two numbers in atomics; tick() and the page
 * read them. A callback that is slow does not hurt the audio, it only loses blocks, but it
 * should still be quick, and this one does no allocation, locking or I/O.
 */
#include <math.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#include "azmod-mod.h"

#define MOD_ID "level-meter"
#define NDECKS 4
#define BLOCK 1024             /* frames per measurement, ~23 ms at 44.1 kHz */
#define FLOOR_CDB (-6000)          /* -60.00 dBFS: the bottom of the scale */
#define HOLD_MS 1500             /* the peak marker holds 1.5 s, then falls */
#define FALL_CDB_S 1500             /* at 15 dB a second */
#define REDRAW_MS 125              /* at most 8 repaints a second while the page is open */

static const az_core_v1 *C;

/* Written by the tap thread, read on the UI thread: dBFS * 100. */
static atomic_int rms_cdb[NDECKS], peak_cdb[NDECKS];
static atomic_int tapped[NDECKS];     /* 1 = this deck's tap was accepted */

/* UI thread only. */
static int hold_cdb[NDECKS];
static long hold_until[NDECKS];       /* ms: the held peak starts to fall after this */
static int page_open;                /* on_enter seen since the last BACK */
static long last_ms, last_redraw_ms;  /* 0 = not yet */

/* milliseconds on the monotonic clock: tick's rate varies, so the hold, the fall and the
 * repaints are timed by the clock, never by counting ticks */
static long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int to_cdb(float v)
{
    if (!(v > 1e-6f)) return FLOOR_CDB;        /* also NaN */
    int c = (int)lrintf(2000.0f * log10f(v));  /* 20 log10, in hundredths */
    return c < FLOOR_CDB ? FLOOR_CDB : c > 0 ? 0
                                             : c;
}

/* The core's tap thread: one block of interleaved stereo. */
static void on_block(const float *lr, int frames, void *user)
{
    int deck = (int)(intptr_t)user;
    float peak = 0.0f;
    double sum = 0.0;
    for (int i = 0; i < 2 * frames; i++) {
        float a = fabsf(lr[i]);
        if (a > peak) peak = a;
        sum += (double)lr[i] * lr[i];
    }
    atomic_store_explicit(&peak_cdb[deck], to_cdb(peak), memory_order_relaxed);
    atomic_store_explicit(&rms_cdb[deck], to_cdb((float)sqrt(sum / (2.0 * frames))), memory_order_relaxed);
}

/* ---- the page (UI thread) ---------------------------------------------------------------- */

static int x_of(int cdb, int x0, int w)       /* -60..0 dBFS across w pixels */
{
    return x0 + (int)((long)(cdb - FLOOR_CDB) * w / -FLOOR_CDB);
}

static void on_draw(az_draw *d, az_rect a, void *user)
{
    (void)user;
    const uint32_t text = C->color(AZ_COL_TEXT), dim = C->color(AZ_COL_TEXT_DIM);
    const uint32_t bar = C->color(AZ_COL_HI), clip = 0xff3b30;
    const int row = C->row_height(d), pad = row / 4;
    int px = row / 2;
    if (px > 96) px = 96;
    C->draw_header(d, "Level Meter");
    if (px < 8 || a.h < 2 * row) return;

    int n = C->deck_count() == 2 ? 2 : NDECKS;
    int label_w = C->text_width("DECK 4", px) + 2 * pad;
    int num_w = C->text_width("-60.0", px) + 2 * pad;
    int bx = a.x + label_w, bw = a.w - label_w - num_w;
    if (bw < 40) return;
    for (int deck = 0; deck < n && (deck + 2) * row <= a.h; deck++) {
        int y = a.y + row * (deck + 1), ty = y + (row - px * 3 / 2) / 2;
        char s[16];
        snprintf(s, sizeof s, "DECK %d", deck + 1);
        C->draw_text(d, a.x + pad, ty, s, px, text);
        int by = y + row / 4, bh = row / 2;
        C->draw_rect(d, bx, by, bw, bh, 1, dim);
        if (!atomic_load(&tapped[deck])) {
            C->draw_text(d, bx + pad, ty, "no audio tap", px, dim);
            continue;
        }
        int rms = atomic_load_explicit(&rms_cdb[deck], memory_order_relaxed);
        int pk = hold_cdb[deck];
        int xr = x_of(rms, bx, bw);
        if (xr > bx) C->fill_rect(d, bx, by, xr - bx, bh, bar);
        if (pk > FLOOR_CDB) {
            int xp = x_of(pk, bx, bw);
            if (xp > bx + bw - 2) xp = bx + bw - 2;
            C->fill_rect(d, xp, by - 2, 2, bh + 4, pk >= -100 ? clip : text);
        }
        if (pk > FLOOR_CDB) snprintf(s, sizeof s, "%.1f", pk / 100.0);
        else snprintf(s, sizeof s, "--");
        C->draw_text(d, bx + bw + pad, ty, s, px, pk >= -100 ? clip : dim);
    }
}

static int on_input(const az_event *ev, void *user)
{
    (void)user;
    if (ev->kind == AZ_EV_BACK) page_open = 0;
    return 0;                         /* nothing to choose here: the core decides */
}

static void on_enter(void *user)
{
    (void)user;
    page_open = 1;
    C->request_redraw();
}

static const az_screen_v1 SCREEN = {
    .size = sizeof(az_screen_v1),
    .on_draw = on_draw,
    .on_input = on_input,
    .on_enter = on_enter,
    .user = NULL,
};

/* ---- lifecycle --------------------------------------------------------------------------- */

static void tick(void)
{
    long now = now_ms();
    long dt = last_ms ? now - last_ms : 0;
    last_ms = now;
    int fall = (int)(dt * FALL_CDB_S / 1000);
    for (int deck = 0; deck < NDECKS; deck++) {    /* peak hold, then a steady fall */
        int pk = atomic_load_explicit(&peak_cdb[deck], memory_order_relaxed);
        if (pk >= hold_cdb[deck]) {
            hold_cdb[deck] = pk;
            hold_until[deck] = now + HOLD_MS;
        } else if (now >= hold_until[deck]) {
            hold_cdb[deck] = hold_cdb[deck] - fall > pk ? hold_cdb[deck] - fall : pk;
        }
    }
    if (page_open && now - last_redraw_ms >= REDRAW_MS) {
        last_redraw_ms = now;
        C->request_redraw();
    }
}

static int init(const az_core_v1 *core)
{
    if (!core || core->abi != AZ_MOD_ABI) return 1;
    C = core;
    if (!AZ_HAS(C, register_screen) || !AZ_HAS(C, request_redraw) || !AZ_HAS(C, fill_rect) ||
        !AZ_HAS(C, draw_rect) || !AZ_HAS(C, deck_count) || !AZ_HAS(C, audio_tap)) {
        C->log(MOD_ID, "this AZ-MODS has no audio taps (1.2.3 or newer needed)");
        return 2;
    }
    for (int deck = 0; deck < NDECKS; deck++) {
        atomic_store(&rms_cdb[deck], FLOOR_CDB);
        atomic_store(&peak_cdb[deck], FLOOR_CDB);
        hold_cdb[deck] = FLOOR_CDB;
    }
    if (C->register_screen(MOD_ID, &SCREEN) != 0) {
        C->log(MOD_ID, "register_screen refused");
        return 3;
    }
    /* Last: a tap outlives a refused init. A mod may have 4 taps and the core runs 16 in all;
     * a deck whose tap is refused shows "no audio tap", the others still work. */
    int ok = 0;
    for (int deck = 0; deck < NDECKS; deck++)
        if (C->audio_tap(MOD_ID, AZ_TAP_DECK(deck), BLOCK, on_block, (void *)(intptr_t)deck) == 0) {
            atomic_store(&tapped[deck], 1);
            ok++;
        }
    if (!ok) C->log(MOD_ID, "no audio tap was accepted");
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
