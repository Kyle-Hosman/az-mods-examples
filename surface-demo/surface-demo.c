/* SPDX-License-Identifier: GPL-3.0-only
 *
 * surface-demo: the drawing services of AZ-MODS 1.2.3, in one small mod.
 *
 * A panel on the deck screen (a surface on AZ_LAYER_TOP, shown only on AZ_VIEW_DECK), painted
 * by the mod with the core's helpers: text, a line, a filled box and an RGBA image. A tap
 * changes its accent color; a long press gives it the browse knob (surface_focus), so turns
 * move a counter until BACK hands the knob back. view() names the screen the deck is on.
 *
 * Everything runs on the core's UI thread: init(), tick() and the surface's callbacks. The mod
 * checks every service with AZ_HAS first, so on an older core it does nothing.
 */
#include <stdio.h>
#include <string.h>

#include "azmod-mod.h"

#define MOD_ID "surface-demo"
#define PANEL_W 420
#define PANEL_H 132
#define ICON 24

static const az_core_v1 *C;
static az_surface *panel;
static int accent;          /* index into ACCENTS */
static int focused;         /* we hold the browse knob */
static int counter;
static char last[48] = "tap or hold the panel";
static unsigned char icon[ICON * ICON * 4];   /* premultiplied RGBA */

static const unsigned ACCENTS[] = { 0xff8a00, 0x2ec4ff, 0x7cff6b, 0xff4fd8 };

static const char *view_name(int v)
{
    switch (v) {
    case AZ_VIEW_SOURCE: return "SOURCE";
    case AZ_VIEW_BROWSE: return "BROWSE"; /* also Tag List and Playlist */
    case AZ_VIEW_SEARCH: return "SEARCH";
    case AZ_VIEW_DECK: return "DECK";
    case AZ_VIEW_MENU: return "MOD MENU";
    default: return "OTHER";
    }
}

/* A round icon in the accent color, fading out at its edge (premultiplied alpha). */
static void make_icon(unsigned rgb)
{
    for (int y = 0; y < ICON; y++)
        for (int x = 0; x < ICON; x++) {
            int dx = 2 * x - (ICON - 1), dy = 2 * y - (ICON - 1);
            int d2 = dx * dx + dy * dy, r2 = (ICON - 1) * (ICON - 1);
            unsigned a = d2 >= r2 ? 0 : (unsigned)(255 - 255 * d2 / r2);
            unsigned char *p = icon + (y * ICON + x) * 4;
            p[0] = (unsigned char)(((rgb >> 16) & 0xff) * a / 255);
            p[1] = (unsigned char)(((rgb >> 8) & 0xff) * a / 255);
            p[2] = (unsigned char)((rgb & 0xff) * a / 255);
            p[3] = (unsigned char)a;
        }
}

static void paint(az_draw *d, az_rect a, void *user)
{
    (void)user;
    unsigned acc = ACCENTS[accent];
    int px = 18, line = px * 3 / 2;
    C->fill_rect(d, a.x, a.y, a.w, 4, acc);
    C->draw_text(d, a.x + 12, a.y + 10, "SURFACE DEMO", px, C->color(AZ_COL_TEXT));
    char s[64];
    snprintf(s, sizeof s, "screen: %s", view_name(C->view()));
    C->draw_text(d, a.x + 12, a.y + 10 + line, s, px - 2, C->color(AZ_COL_TEXT_DIM));
    if (focused)
        snprintf(s, sizeof s, "knob held: %d  (BACK releases)", counter);
    else
        snprintf(s, sizeof s, "%s", last);
    C->draw_text(d, a.x + 12, a.y + 10 + 2 * line, s, px - 2, C->color(AZ_COL_TEXT));
    C->draw_line(d, a.x + 12, a.y + a.h - 14, a.x + a.w - 60, a.y + a.h - 30, 3, acc);
    C->draw_image(d, a.x + a.w - ICON - 16, a.y + 14, icon, ICON, ICON);
    if (focused)
        C->draw_rect(d, a.x, a.y, a.w, a.h, 2, acc);
}

static int input(const az_event *ev, void *user)
{
    (void)user;
    switch (ev->kind) {
    case AZ_EV_TAP:
        accent = (accent + 1) % (int)(sizeof ACCENTS / sizeof ACCENTS[0]);
        make_icon(ACCENTS[accent]);
        snprintf(last, sizeof last, "tap at %d,%d", ev->x, ev->y);
        break;
    case AZ_EV_LONG:
        focused = C->surface_focus(panel, 1);
        snprintf(last, sizeof last, focused ? "long press: knob taken" : "long press: knob refused");
        break;
    case AZ_EV_KNOB:
        counter += ev->dir;
        break;
    case AZ_EV_CLICK:
        counter = 0;
        break;
    case AZ_EV_BACK:
        C->surface_focus(panel, 0);
        focused = 0;
        snprintf(last, sizeof last, "knob given back");
        break;
    default:
        return 0;
    }
    C->surface_redraw(panel);
    return 1;
}

static int init(const az_core_v1 *core)
{
    C = core;
    if (!AZ_HAS(C, surface_create) || !AZ_HAS(C, draw_line) || !AZ_HAS(C, draw_image) ||
        !AZ_HAS(C, view)) {
        C->log(MOD_ID, "this core has no drawing services; nothing to show");
        return 0;
    }
    static const az_surface_v1 desc = {
        .size = sizeof(az_surface_v1),
        .layer = AZ_LAYER_TOP,
        .views = AZ_VIEW_BIT(AZ_VIEW_DECK),
        .on_paint = paint,
        .on_input = input,
    };
    make_icon(ACCENTS[accent]);
    panel = C->surface_create(MOD_ID, &desc);
    if (!panel)
        return 1; /* the reason is in the log */
    C->surface_place(panel, (az_rect){ (1280 - PANEL_W) / 2, 96, PANEL_W, PANEL_H });
    C->surface_show(panel, 1);
    return 0;
}

/* The screen name in the panel follows view(); repaint when it changes. */
static void tick(void)
{
    static int seen = -1;
    if (!panel)
        return;
    int v = C->view();
    if (v != seen) {
        seen = v;
        C->surface_redraw(panel);
    }
}

static const char *const TAGS[] = { "any", NULL };
static const az_mod_v1 MOD = { AZ_MOD_ABI, sizeof(az_mod_v1), MOD_ID, TAGS, init, tick, NULL };

AZ_MOD_EXPORT const az_mod_v1 *az_mod_v1_entry(void)
{
    return &MOD;
}
