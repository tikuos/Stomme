/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_switcher.c - ordering, the hair trigger, and the keys.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_switcher.h"
#include <stdio.h>
#include <string.h>

void
tiku_trk_switcher_init(tiku_trk_switcher_t *s)
{
    if (s != NULL) {
        memset(s, 0, sizeof *s);
    }
}

int
tiku_trk_switcher_app(tiku_trk_switcher_t *s, const char *name, int is_self)
{
    int i;

    if (s == NULL || name == NULL) {
        return -1;
    }
    for (i = 0; i < s->n; i++) {
        if (strcmp(s->app[i].name, name) == 0) {
            return i;
        }
    }
    if (s->n >= TIKU_TRK_SW_APPS) {
        return -1;
    }
    memset(&s->app[s->n], 0, sizeof s->app[0]);
    snprintf(s->app[s->n].name, sizeof s->app[0].name, "%s", name);
    s->app[s->n].is_self = is_self;
    return s->n++;
}

void
tiku_trk_switcher_window(tiku_trk_switcher_t *s, int i, const char *title)
{
    if (s == NULL || i < 0 || i >= s->n ||
        s->app[i].nwin >= TIKU_TRK_SW_WINS) {
        return;
    }
    snprintf(s->app[i].win[s->app[i].nwin], TIKU_TRK_SW_NAME, "%s",
             (title != NULL) ? title : "");
    s->app[i].nwin++;
}

/** @brief Most recently used first, stable among equals. */
static void
sort_mru(tiku_trk_switcher_t *s)
{
    int i, j;

    for (i = 1; i < s->n; i++) {
        tiku_trk_sw_app_t t = s->app[i];

        for (j = i; j > 0 && s->app[j - 1].used < t.used; j--) {
            s->app[j] = s->app[j - 1];
        }
        s->app[j] = t;
    }
}

/**
 * @brief Step the selection one application on, or back, wrapping, and
 *        scroll the strip to it; the slide starts at @p now.
 */
static void
cycle_app(tiku_trk_switcher_t *s, int64_t now, int back)
{
    if (s->n == 0) {
        return;
    }
    s->prev = s->sel;
    s->moved = now;
    s->sel += back ? -1 : 1;
    if (s->sel >= s->n) { s->sel = 0; }
    if (s->sel < 0)     { s->sel = s->n - 1; }
    s->selwin = 0;
    /* The strip follows the selection, and slides there, so where it left
     * from is remembered (DB-058). */
    s->scroll_from = s->scroll;
    if (s->sel < s->scroll) {
        s->scroll = s->sel;
    } else if (s->sel >= s->scroll + TIKU_TRK_SW_SLOTS) {
        s->scroll = s->sel - TIKU_TRK_SW_SLOTS + 1;
    }
}

void
tiku_trk_switcher_open(tiku_trk_switcher_t *s, int64_t now, int back)
{
    if (s == NULL || s->n == 0) {
        return;
    }
    if (!s->shown) {
        /* Re-sorted only after an idle spell (DB-055). */
        if (now - s->last_switch > TIKU_TRK_SW_RESORT_US) {
            sort_mru(s);
            s->sel = 0;
        }
        s->shown = 1;
        s->opened = now;
        s->selwin = 0;
        s->scroll = 0;
    }
    cycle_app(s, now, back);
}

int
tiku_trk_switcher_scroll_px(const tiku_trk_switcher_t *s, int slot_w,
                            int64_t now)
{
    int64_t gone;
    int done;

    if (s == NULL) {
        return 0;
    }
    if (!tiku_trk_switcher_animating(s, now) ||
        s->scroll_from == s->scroll) {
        return s->scroll * slot_w;
    }
    gone = now - s->moved;
    done = (int)(gone / TIKU_TRK_SW_STEP_US) + 1;
    if (done > TIKU_TRK_SW_STEPS) { done = TIKU_TRK_SW_STEPS; }
    /* Quantised like the icon sizes: each step covers a share of the
     * distance, and the last step lands exactly. */
    return s->scroll_from * slot_w +
           (s->scroll - s->scroll_from) * slot_w * done / TIKU_TRK_SW_STEPS;
}

int
tiku_trk_switcher_release(tiku_trk_switcher_t *s, int64_t now)
{
    int quick;

    if (s == NULL || !s->shown) {
        return 0;
    }
    /* Released inside the hair trigger: a quick switch to the previous
     * application.  Only the timing tells it from a look at the list
     * (DB-054). */
    quick = (now - s->opened <= TIKU_TRK_SW_HAIR_US);
    s->shown = 0;
    s->last_switch = now;
    if (s->sel >= 0 && s->sel < s->n) {
        s->app[s->sel].used = now;
    }
    return quick;
}

tiku_trk_sw_act_t
tiku_trk_switcher_key(tiku_trk_switcher_t *s, const char *key, int shift,
                      int64_t now)
{
    if (s == NULL || !s->shown || key == NULL) {
        return TIKU_TRK_SW_NOTHING;
    }
    if (strcmp(key, "Escape") == 0) {
        s->shown = 0;
        return TIKU_TRK_SW_CANCEL;
    }
    if (strcmp(key, "space") == 0 || strcmp(key, "Return") == 0) {
        s->shown = 0;
        return TIKU_TRK_SW_COMMIT;
    }
    if (strcmp(key, "Tab") == 0 || strcmp(key, "Right") == 0 ||
        strcmp(key, "Left") == 0) {
        cycle_app(s, now, (strcmp(key, "Left") == 0) || shift);
        return TIKU_TRK_SW_MOVED;
    }
    if (strcmp(key, "Down") == 0 || strcmp(key, "Up") == 0) {
        /* Windows do not wrap: Up on the first window stays there. */
        int to = s->selwin + ((strcmp(key, "Down") == 0) ? 1 : -1);
        int have = (s->sel >= 0 && s->sel < s->n) ? s->app[s->sel].nwin : 0;

        if (to < 0 || to >= have) {
            return TIKU_TRK_SW_NOTHING;
        }
        s->selwin = to;
        return TIKU_TRK_SW_MOVED;
    }
    if (strcmp(key, "q") == 0) {
        /* Tracker is not quit from its own switcher: it is the thing
         * drawing the switcher. */
        if (s->sel >= 0 && s->sel < s->n && s->app[s->sel].is_self) {
            return TIKU_TRK_SW_NOTHING;
        }
        return TIKU_TRK_SW_QUIT_APP;
    }
    if (strcmp(key, "h") == 0) {
        return TIKU_TRK_SW_HIDE_APP;
    }
    return TIKU_TRK_SW_NOTHING;
}

tiku_trk_sw_act_t
tiku_trk_switcher_click(tiku_trk_switcher_t *s, int slot)
{
    if (s == NULL || !s->shown) {
        return TIKU_TRK_SW_NOTHING;
    }
    if (slot < 0) {
        /* A click outside the panel dismisses it without switching. */
        s->shown = 0;
        return TIKU_TRK_SW_CANCEL;
    }
    if (s->scroll + slot >= s->n) {
        return TIKU_TRK_SW_NOTHING;
    }
    s->sel = s->scroll + slot;
    s->selwin = 0;
    return TIKU_TRK_SW_MOVED;
}

void
tiku_trk_switcher_place(int sw, int sh, int w, int h, int *x, int *y)
{
    if (x != NULL) { *x = (sw - w) / 2; }
    /* Slightly above centre: lifted by a sixteenth of the screen height. */
    if (y != NULL) { *y = (sh - h) / 2 - sh / 16; }
}

void
tiku_trk_switcher_title(const tiku_trk_switcher_t *s, int i, int w,
                        char *out, size_t max)
{
    if (out == NULL || max == 0) {
        return;
    }
    out[0] = '\0';
    if (s == NULL || i < 0 || i >= s->n) {
        return;
    }
    /* "App: Window", or just the application when the window has no
     * title of its own. */
    if (w >= 0 && w < s->app[i].nwin && s->app[i].win[w][0] != '\0') {
        snprintf(out, max, "%s: %s", s->app[i].name, s->app[i].win[w]);
    } else {
        snprintf(out, max, "%s", s->app[i].name);
    }
}

int
tiku_trk_switcher_animating(const tiku_trk_switcher_t *s, int64_t now)
{
    if (s == NULL || !s->shown || s->prev == s->sel) {
        return 0;
    }
    return (now - s->moved < (int64_t)TIKU_TRK_SW_STEP_US *
                             TIKU_TRK_SW_STEPS);
}

int
tiku_trk_switcher_icon_size(const tiku_trk_switcher_t *s, int i, int64_t now)
{
    int64_t gone;
    int span = TIKU_TRK_SW_LARGE - TIKU_TRK_SW_SMALL;
    int done;

    if (s == NULL || i < 0 || i >= s->n) {
        return TIKU_TRK_SW_SMALL;
    }
    if (!tiku_trk_switcher_animating(s, now)) {
        /* Settled: only the selection is large. */
        return (i == s->sel) ? TIKU_TRK_SW_LARGE : TIKU_TRK_SW_SMALL;
    }
    gone = now - s->moved;
    /* Quantised to whole steps of TIKU_TRK_SW_STEP_US. */
    done = (int)(gone / TIKU_TRK_SW_STEP_US) + 1;
    if (done > TIKU_TRK_SW_STEPS) { done = TIKU_TRK_SW_STEPS; }
    if (i == s->sel) {
        return TIKU_TRK_SW_SMALL + span * done / TIKU_TRK_SW_STEPS;
    }
    if (i == s->prev) {
        return TIKU_TRK_SW_LARGE - span * done / TIKU_TRK_SW_STEPS;
    }
    return TIKU_TRK_SW_SMALL;
}

int
tiku_trk_switcher_scroller(const tiku_trk_switcher_t *s, int right)
{
    if (s == NULL || s->n <= TIKU_TRK_SW_SLOTS) {
        return 0;                   /* everything fits: nothing to reach */
    }
    return right ? (s->scroll + TIKU_TRK_SW_SLOTS < s->n) : (s->scroll > 0);
}
