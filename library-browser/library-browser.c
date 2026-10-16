/* SPDX-License-Identifier: GPL-3.0-only
 *
 * library-browser: the rekordbox playlists on the sticks in the deck, as a Mod Menu page.
 * Stick 0 is the deck's USB 1 port and stick 1 is USB 2, so the headings match the deck's
 * own SOURCE list. The knob moves through them, a click opens a playlist's tracks (title, artist, BPM, key),
 * BACK goes back to the list.
 *
 * The library services read the stick, so they are never called from tick() or a page
 * callback: the mod's own thread (thread_start) does every read and fills a snapshot, and the
 * page only draws that snapshot. The two meet in one mutex, held for a copy or a draw, never
 * across a read. Reading is READ-ONLY: the core opens the library with a connection of its
 * own that cannot write, so browsing never touches the stick's database.
 */
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "azmod-mod.h"

#define MOD_ID "library-browser"
#define NSTICKS 2
#define MAX_ROWS 512        /* playlist rows shown, both sticks */
#define MAX_TRACKS 256        /* tracks shown for one playlist */
#define NAME_MAX_B 96

static const az_core_v1 *C;

/* One row of the playlist tree: a stick heading, a folder or a playlist. */
struct row {
    int stick, id, depth, folder;    /* id 0 = the stick heading */
    char name[NAME_MAX_B];
};
struct track_row {
    char title[NAME_MAX_B], artist[NAME_MAX_B], key[8];
    double bpm;
};

/* What the page shows. Written by the reader thread under g_mu, read by the UI under g_mu. */
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static struct row g_rows[MAX_ROWS];
static int g_nrows, g_rows_ready;
static struct track_row g_tracks[MAX_TRACKS];
static int g_ntracks, g_tracks_total, g_tracks_ready;
static char g_tracks_of[NAME_MAX_B];

/* UI thread only. */
static int sel, top;                  /* selected row and the first row drawn */
static int tsel_top;                  /* first track drawn */
static int showing_tracks;

/* UI -> reader requests, and reader -> UI "something new to draw". */
static atomic_int want_list = 1;
static atomic_int want_tracks;        /* (stick << 24) | playlist id, 0 = none */
static atomic_int fresh;

/* ---- the reader thread: every stick read happens here ---------------------------------- */

struct pl {
    int id, parent, folder;
    char name[NAME_MAX_B];
};
struct pl_buf {
    struct pl *v;
    int n, cap;
};

static int on_playlist(int pl_id, int parent, const char *name, int folder, void *user)
{
    struct pl_buf *b = user;
    if (b->n == b->cap) return 1;     /* full: stop the listing */
    struct pl *p = &b->v[b->n++];
    p->id = pl_id;
    p->parent = parent;
    p->folder = folder;
    snprintf(p->name, sizeof p->name, "%s", name ? name : "");
    return 0;
}

/* An entry is at the top level when its parent is no playlist or folder in the listing. */
static int at_top(const struct pl_buf *b, int i)
{
    for (int j = 0; j < b->n; j++)
        if (b->v[j].id == b->v[i].parent) return 0;
    return 1;
}

/* The rows under `parent` (TOP = the top level) in library order, depth first. */
#define TOP (-1)
static void add_tree(struct row *out, int *n, const struct pl_buf *b, int stick, int parent, int depth)
{
    for (int i = 0; i < b->n && *n < MAX_ROWS; i++) {
        if (parent == TOP ? !at_top(b, i) : b->v[i].parent != parent) continue;
        struct row *r = &out[(*n)++];
        r->stick = stick;
        r->id = b->v[i].id;
        r->depth = depth;
        r->folder = b->v[i].folder;
        memcpy(r->name, b->v[i].name, sizeof r->name);
        if (b->v[i].folder && depth < 8) add_tree(out, n, b, stick, b->v[i].id, depth + 1);
    }
}

static void read_list(void)
{
    static struct row rows[MAX_ROWS];
    struct pl_buf b = { calloc(MAX_ROWS, sizeof(struct pl)), 0, MAX_ROWS };
    int n = 0;
    for (int stick = 0; b.v && stick < NSTICKS && n < MAX_ROWS; stick++) {
        b.n = 0;
        /* -1 = no library in that port; 1 = cut at the core's limit, still worth showing */
        if (C->lib_playlists(MOD_ID, stick, on_playlist, &b) < 0) continue;
        struct row *h = &rows[n++];
        memset(h, 0, sizeof *h);
        h->stick = stick;
        snprintf(h->name, sizeof h->name, "USB %d", stick + 1);
        add_tree(rows, &n, &b, stick, TOP, 1);
    }
    free(b.v);
    pthread_mutex_lock(&g_mu);
    memcpy(g_rows, rows, sizeof(struct row) * (size_t)n);
    g_nrows = n;
    g_rows_ready = 1;
    pthread_mutex_unlock(&g_mu);
}

struct tr_buf {
    struct track_row *v;
    int n, total;
};

static int on_track(const az_track *t, void *user)
{
    struct tr_buf *b = user;
    if (b->n < MAX_TRACKS) {
        struct track_row *r = &b->v[b->n++];
        snprintf(r->title, sizeof r->title, "%s", t->title ? t->title : "");
        snprintf(r->artist, sizeof r->artist, "%s", t->artist ? t->artist : "");
        snprintf(r->key, sizeof r->key, "%s", t->key ? t->key : "");
        r->bpm = t->bpm;
    }
    b->total++;
    return 0;
}

static void read_tracks(int stick, int playlist, const char *name)
{
    static struct track_row rows[MAX_TRACKS];
    struct tr_buf b = { rows, 0, 0 };
    if (C->lib_tracks(MOD_ID, stick, playlist, on_track, &b) < 0) b.n = b.total = 0;   /* 1 = cut: keep what came */
    pthread_mutex_lock(&g_mu);
    memcpy(g_tracks, rows, sizeof(struct track_row) * (size_t)b.n);
    g_ntracks = b.n;
    g_tracks_total = b.total;
    snprintf(g_tracks_of, sizeof g_tracks_of, "%s", name);
    g_tracks_ready = 1;
    pthread_mutex_unlock(&g_mu);
}

static void *reader(void *arg)
{
    (void)arg;
    const struct timespec nap = { 0, 100 * 1000 * 1000 };
    for (;;) {
        if (atomic_exchange(&want_list, 0)) {
            read_list();
            atomic_store(&fresh, 1);
        }
        int t = atomic_exchange(&want_tracks, 0);
        if (t) {
            char name[NAME_MAX_B] = "";
            pthread_mutex_lock(&g_mu);
            for (int i = 0; i < g_nrows; i++)
                if (g_rows[i].stick == (t >> 24) && g_rows[i].id == (t & 0xffffff))
                    memcpy(name, g_rows[i].name, sizeof name);
            pthread_mutex_unlock(&g_mu);
            read_tracks(t >> 24, t & 0xffffff, name);
            atomic_store(&fresh, 1);
        }
        nanosleep(&nap, NULL);
    }
    return NULL;
}

/* ---- the page (UI thread) ---------------------------------------------------------------- */

static void fit(char *out, size_t n, const char *s, int px, int w)
{
    snprintf(out, n, "%s", s);
    size_t len = strlen(out);
    while (len > 0 && C->text_width(out, px) > w) {
        do len--;
        while (len > 0 && ((unsigned char)s[len] & 0xC0) == 0x80);
        snprintf(out, n, "%.*s...", (int)len, s);
    }
    if (C->text_width(out, px) > w) out[0] = '\0';   /* not even "..." fits */
}

/* One line of status text under the header, cut to the page's width. */
static void status(az_draw *d, az_rect a, int row, int px, const char *s)
{
    const int pad = row / 4;
    char f[96];
    fit(f, sizeof f, s, px, a.w - 2 * pad);
    if (f[0]) C->draw_text(d, a.x + pad, a.y + row + (row - px * 3 / 2) / 2, f, px, C->color(AZ_COL_TEXT_DIM));
}

static void draw_list(az_draw *d, az_rect a, int row, int px)
{
    const uint32_t text = C->color(AZ_COL_TEXT), dim = C->color(AZ_COL_TEXT_DIM);
    const int pad = row / 4, lines = (a.h - row) / row;
    if (!g_rows_ready || g_nrows == 0) {
        status(d, a, row, px, g_rows_ready ? "No rekordbox library on a USB." : "Reading the library...");
        return;
    }
    if (sel >= g_nrows) sel = g_nrows - 1;
    if (sel < top) top = sel;
    if (lines > 0 && sel >= top + lines) top = sel - lines + 1;
    if (top > g_nrows - lines) top = g_nrows - lines > 0 ? g_nrows - lines : 0;   /* no empty tail */
    for (int i = 0; i < lines && top + i < g_nrows; i++) {
        const struct row *r = &g_rows[top + i];
        int y = a.y + row * (i + 1);
        int on = top + i == sel;
        if (on) C->fill_rect(d, a.x, y, a.w, row, C->color(AZ_COL_HI));
        char s[NAME_MAX_B + 8], f[NAME_MAX_B + 8];
        snprintf(s, sizeof s, "%s%s", r->folder ? "+ " : "", r->name);
        int x = a.x + pad + r->depth * pad * 2;
        if (x > a.x + a.w - pad) continue;
        fit(f, sizeof f, s, px, a.x + a.w - pad - x);
        if (!f[0]) continue;
        uint32_t c = on ? C->color(AZ_COL_HI_TEXT) : r->id == 0 || r->folder ? dim
                                                                             : text;
        C->draw_text(d, x, y + (row - px * 3 / 2) / 2, f, px, c);
    }
}

static void draw_tracks(az_draw *d, az_rect a, int row, int px)
{
    const uint32_t text = C->color(AZ_COL_TEXT), dim = C->color(AZ_COL_TEXT_DIM);
    const int pad = row / 4, lines = (a.h - 2 * row) / row;
    char s[2 * NAME_MAX_B + 32], f[2 * NAME_MAX_B + 32];
    if (!g_tracks_ready) {
        status(d, a, row, px, "Reading the playlist...");
        return;
    }
    snprintf(s, sizeof s, "%s: %d track%s%s", g_tracks_of, g_tracks_total, g_tracks_total == 1 ? "" : "s",
             g_tracks_total > g_ntracks ? " (the first ones shown)" : "");
    fit(f, sizeof f, s, px, a.w - 2 * pad);
    if (f[0]) C->draw_text(d, a.x + pad, a.y + row + (row - px * 3 / 2) / 2, f, px, dim);
    if (tsel_top > g_ntracks - lines) tsel_top = g_ntracks - lines;
    if (tsel_top < 0) tsel_top = 0;
    for (int i = 0; i < lines && tsel_top + i < g_ntracks; i++) {
        const struct track_row *t = &g_tracks[tsel_top + i];
        int y = a.y + row * (i + 2) + (row - px * 3 / 2) / 2;
        char right[32];
        if (t->bpm > 0) snprintf(right, sizeof right, "%.1f  %s", t->bpm, t->key);
        else snprintf(right, sizeof right, "%s", t->key);
        int rw = C->text_width(right, px);
        snprintf(s, sizeof s, "%s%s%s", t->title, t->artist[0] ? " - " : "", t->artist);
        if (rw > a.w / 3) {           /* a narrow page: the title only */
            right[0] = '\0';
            rw = 0;
        }
        fit(f, sizeof f, s, px, a.w - 3 * pad - rw);
        if (f[0]) C->draw_text(d, a.x + pad, y, f, px, text);
        if (rw) C->draw_text(d, a.x + a.w - pad - rw, y, right, px, dim);
    }
}

static void on_draw(az_draw *d, az_rect area, void *user)
{
    (void)user;
    const int row = C->row_height(d);
    int px = row / 2;
    if (px > 96) px = 96;
    C->draw_header(d, "Library Browser");
    if (px < 8 || area.h < 2 * row) return;
    pthread_mutex_lock(&g_mu);
    if (showing_tracks) draw_tracks(d, area, row, px);
    else draw_list(d, area, row, px);
    pthread_mutex_unlock(&g_mu);
}

static int on_input(const az_event *ev, void *user)
{
    (void)user;
    switch (ev->kind) {
    case AZ_EV_KNOB:
        if (showing_tracks) tsel_top += ev->dir < 0 ? -1 : 1;
        else {
            sel += ev->dir < 0 ? -1 : 1;
            if (sel < 0) sel = 0;
        }
        C->request_redraw();
        return 1;
    case AZ_EV_CLICK:
        if (!showing_tracks) {
            pthread_mutex_lock(&g_mu);
            int ok = sel < g_nrows && g_rows[sel].id > 0 && !g_rows[sel].folder;
            int req = ok ? (g_rows[sel].stick << 24) | (g_rows[sel].id & 0xffffff) : 0;
            if (ok) g_tracks_ready = 0;
            pthread_mutex_unlock(&g_mu);
            if (ok) {
                showing_tracks = 1;
                tsel_top = 0;
                atomic_store(&want_tracks, req);
                C->request_redraw();
            }
        }
        return 1;
    case AZ_EV_BACK:
        if (showing_tracks) {
            showing_tracks = 0;
            C->request_redraw();
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
    showing_tracks = 0;
    sel = top = 0;
    atomic_store(&want_list, 1);      /* a stick may have come or gone */
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
    if (atomic_exchange(&fresh, 0)) C->request_redraw();
}

static int init(const az_core_v1 *core)
{
    if (!core || core->abi != AZ_MOD_ABI) return 1;
    C = core;
    if (!AZ_HAS(C, register_screen) || !AZ_HAS(C, request_redraw) || !AZ_HAS(C, fill_rect) ||
        !AZ_HAS(C, lib_playlists) || !AZ_HAS(C, lib_tracks) || !AZ_HAS(C, thread_start)) {
        C->log(MOD_ID, "this AZ-MODS has no library services (1.2.3 or newer needed)");
        return 2;
    }
    if (C->register_screen(MOD_ID, &SCREEN) != 0) {
        C->log(MOD_ID, "register_screen refused");
        return 3;
    }
    /* last, after everything that can fail: a thread outlives a refused init */
    if (C->thread_start(MOD_ID, "library reader", reader, NULL) != 0) {
        C->log(MOD_ID, "thread_start refused");
        return 4;
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
