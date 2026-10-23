/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_navmenu.c - navigation menus: filling a level a chunk at a time,
 * descending and leaving levels, and what a menu hides.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_launch.h"
#include "tiku_trk_navmenu.h"
#include "tiku_trk_springmenu.h"
#include "tiku_volume.h"
#include <stdio.h>
#include <string.h>

void
tiku_trk_nav_open(tiku_trk_nav_t *nav, tiku_backend_t *b,
                  const char *path)
{
    if (nav == NULL) {
        return;
    }
    memset(nav, 0, sizeof *nav);
    nav->backend = b;
    nav->folders_first = 1;
    if (path != NULL) {
        snprintf(nav->level[0].path, sizeof nav->level[0].path, "%s", path);
    }
    nav->level[0].sel = -1;
    nav->depth = 1;
}

tiku_trk_nav_level_t *
tiku_trk_nav_top(tiku_trk_nav_t *nav)
{
    if (nav == NULL || nav->depth <= 0) {
        return NULL;
    }
    return &nav->level[nav->depth - 1];
}


void
tiku_trk_nav_spring_states(tiku_trk_nav_t *nav,
                           const struct tiku_trk_apps *apps,
                           const char *const *types, int ntypes)
{
    const tiku_trk_apps_t *reg = (const tiku_trk_apps_t *)apps;
    int k;

    if (nav == NULL) {
        return;
    }
    for (k = 0; k < nav->depth; k++) {
        tiku_trk_nav_level_t *lv = &nav->level[k];
        int i;

        for (i = 0; i < lv->n; i++) {
            tiku_trk_nav_item_t *it = &lv->item[i];
            tiku_model_t m;
            int broken = 0;
            int accepts = 0;

            /* An entry that does not resolve is taken as a broken link:
             * listed and greyed. */
            if (nav->backend != NULL &&
                nav->backend->ops->stat(nav->backend, it->path, &m) == 0) {
                broken = m.facts.link_broken;
            } else {
                broken = 1;
            }
            if (!it->is_container && !broken && reg != NULL) {
                int a;

                for (a = 0; a < reg->count && !accepts; a++) {
                    const tiku_trk_app_t *ap = &reg->app[a];
                    int t;

                    if (ap->is_tracker ||
                        !((ap->cmd[0] != '\0' &&
                           strcmp(ap->cmd, it->path) == 0) ||
                          (ap->path[0] != '\0' &&
                           strcmp(ap->path, it->path) == 0))) {
                        continue;
                    }
                    for (t = 0; t < ntypes && !accepts; t++) {
                        accepts = tiku_trk_launch_relation(ap, types[t]) !=
                                  TIKU_TRK_REL_NONE;
                    }
                }
            }
            it->enabled = tiku_trk_springmenu_item_enabled(
                it->is_container, accepts, broken);
        }
    }
}

int
tiku_trk_nav_wanted(const tiku_model_t *m)
{
    return (m != NULL && tiku_model_is_container(m));
}

/** @brief What one level's fill is carrying between calls. */
struct fill_ctx {
    tiku_trk_nav_t       *nav;      /* the walk                          */
    tiku_trk_nav_level_t *lv;       /* the level being filled            */
    int                   skip;     /* entries already taken             */
    int                   seen;     /* entries walked this call          */
    int                   added;    /* entries added this call           */
};

/** @brief Whether a menu shows @p m at all (CW-057). */
static int
visible(const tiku_trk_nav_t *nav, const tiku_model_t *m)
{
    /* Dot files are hidden when the window hides them; the Trash always
     * is. */
    if (nav->hide_dotfiles && m->name[0] == '.') {
        return 0;
    }
    if (m->kind == TIKU_KIND_TRASH) {
        return 0;
    }
    return 1;
}

/**
 * @brief Take one listed entry into the level being filled, skipping what
 *        an earlier chunk took; a file is listed but not choosable.
 *        @return 1 when this chunk is full.
 */
static int
fill_one(const tiku_model_t *m, void *ctx)
{
    struct fill_ctx *c = ctx;
    tiku_trk_nav_item_t *it;

    if (!visible(c->nav, m)) {
        return 0;
    }
    /* The backend's walk has no cursor, so a chunk resumes by passing over
     * the entries it already took. */
    if (c->seen++ < c->skip) {
        return 0;
    }
    if (c->lv->n >= TIKU_TRK_NAV_MAX || c->added >= TIKU_TRK_NAV_CHUNK) {
        return 1;                   /* enough for this slice */
    }
    it = &c->lv->item[c->lv->n];
    memset(it, 0, sizeof *it);
    snprintf(it->name, sizeof it->name, "%s", m->name);
    snprintf(it->path, sizeof it->path, "%s", m->path);
    it->is_container = tiku_model_is_container(m);
    /* A file is listed but cannot be chosen in a navigation menu
     * (CW-056). */
    it->enabled = it->is_container;
    c->lv->n++;
    c->added++;
    return 0;
}

/** @brief Folders before files when folders_first is set, then by name in
 *         byte order. */
static void
sort_level(tiku_trk_nav_t *nav, tiku_trk_nav_level_t *lv)
{
    int i, j;

    for (i = 1; i < lv->n; i++) {
        tiku_trk_nav_item_t t = lv->item[i];

        for (j = i; j > 0; j--) {
            const tiku_trk_nav_item_t *p = &lv->item[j - 1];
            int before;

            if (nav->folders_first && p->is_container != t.is_container) {
                before = p->is_container;
            } else {
                before = (strcmp(p->name, t.name) <= 0);
            }
            if (before) {
                break;
            }
            lv->item[j] = lv->item[j - 1];
        }
        lv->item[j] = t;
    }
}

int
tiku_trk_nav_fill(tiku_trk_nav_t *nav, int level)
{
    tiku_trk_nav_level_t *lv;
    struct fill_ctx c;
    int before;

    if (nav == NULL || level < 0 || level >= nav->depth ||
        nav->backend == NULL) {
        return 0;
    }
    lv = &nav->level[level];
    if (lv->done) {
        return 0;
    }
    if (strcmp(lv->path, "/") == 0 && nav->backend->devid[0] == '\0') {
        /* On a local backend the root level lists the volumes, as the
         * root window does (CW-057). */
        tiku_volumes_t vs;
        int k;

        if (tiku_volumes_scan(&vs) > 0) {
            for (k = 0; k < vs.n && lv->n < TIKU_TRK_NAV_MAX; k++) {
                tiku_trk_nav_item_t *it;

                if (!tiku_volume_shows_icon(&vs, &vs.v[k])) {
                    continue;
                }
                it = &lv->item[lv->n];
                memset(it, 0, sizeof *it);
                snprintf(it->name, sizeof it->name, "%s", vs.v[k].name);
                snprintf(it->path, sizeof it->path, "%s", vs.v[k].mount);
                it->is_container = 1;
                it->enabled = 1;
                lv->n++;
            }
        }
        lv->done = 1;
        sort_level(nav, lv);
        return 0;
    }
    before = lv->n;
    c.nav = nav;
    c.lv = lv;
    c.skip = lv->n;
    c.seen = 0;
    c.added = 0;
    (void)nav->backend->ops->list(nav->backend, lv->path, fill_one, &c);
    if (lv->n == before || lv->n >= TIKU_TRK_NAV_MAX) {
        /* Nothing new arrived, or the level is full: it is complete. */
        lv->done = 1;
        sort_level(nav, lv);
        return 0;
    }
    sort_level(nav, lv);
    return 1;                       /* ask again */
}

void
tiku_trk_nav_fill_all(tiku_trk_nav_t *nav, int level)
{
    int guard = 0;

    while (tiku_trk_nav_fill(nav, level) && guard++ < TIKU_TRK_NAV_MAX) {
        /* nothing */
    }
}

int
tiku_trk_nav_enter(tiku_trk_nav_t *nav, int i)
{
    tiku_trk_nav_level_t *lv = tiku_trk_nav_top(nav);
    tiku_trk_nav_level_t *to;

    if (lv == NULL || i < 0 || i >= lv->n || !lv->item[i].is_container) {
        return 0;
    }
    if (nav->depth >= TIKU_TRK_NAV_DEPTH) {
        return 0;
    }
    to = &nav->level[nav->depth];
    /* The new level is cleared, so the fill lists the folder afresh
     * (CW-055). */
    {
        char into[TIKU_PATH_MAX];

        snprintf(into, sizeof into, "%s", lv->item[i].path);
        memset(to, 0, sizeof *to);
        snprintf(to->path, sizeof to->path, "%s", into);
    }
    to->sel = -1;
    nav->depth++;
    return 1;
}

int
tiku_trk_nav_leave(tiku_trk_nav_t *nav)
{
    if (nav == NULL || nav->depth <= 1) {
        return 0;                   /* the first level is the menu itself */
    }
    nav->depth--;
    memset(&nav->level[nav->depth], 0, sizeof nav->level[0]);
    return 1;
}

int
tiku_trk_nav_move(tiku_trk_nav_t *nav, tiku_trk_nav_key_t k)
{
    tiku_trk_nav_level_t *lv = tiku_trk_nav_top(nav);

    if (lv == NULL) {
        return 0;
    }
    switch (k) {
    case TIKU_TRK_NAV_DOWN:
        if (lv->n == 0) {
            return 0;
        }
        /* No wrap at the last row. */
        if (lv->sel + 1 >= lv->n) {
            return 0;
        }
        lv->sel++;
        return 1;
    case TIKU_TRK_NAV_UP:
        if (lv->sel <= 0) {
            return 0;
        }
        lv->sel--;
        return 1;
    case TIKU_TRK_NAV_IN:
        return (lv->sel >= 0) ? tiku_trk_nav_enter(nav, lv->sel) : 0;
    case TIKU_TRK_NAV_OUT:
    default:
        return tiku_trk_nav_leave(nav);
    }
}

const char *
tiku_trk_nav_chosen(tiku_trk_nav_t *nav)
{
    tiku_trk_nav_level_t *lv = tiku_trk_nav_top(nav);

    if (lv == NULL || lv->sel < 0 || lv->sel >= lv->n) {
        return NULL;
    }
    return lv->item[lv->sel].path;
}

void
tiku_trk_nav_place(const tiku_trk_nav_t *nav, int level, int px, int py,
                   int screen_w, int screen_h, int *x, int *y, int *w,
                   int *h)
{
    const tiku_trk_nav_level_t *lv;
    int rows, ww, hh, xx, yy;

    if (nav == NULL || level < 0 || level >= nav->depth) {
        return;
    }
    lv = &nav->level[level];
    rows = (lv->n > 0) ? lv->n : 1;      /* an empty level still shows a row */
    ww = 190;
    hh = rows * TIKU_TRK_NAV_ROW + 4;
    xx = px;
    yy = py;
    /* A level that would run off the right edge opens to the left of px
     * instead. */
    if (xx + ww > screen_w) {
        xx = px - ww;
    }
    if (xx < 0) {
        xx = 0;
    }
    if (yy + hh > screen_h) {
        yy = screen_h - hh;
    }
    if (yy < 0) {
        yy = 0;
    }
    if (x != NULL) { *x = xx; }
    if (y != NULL) { *y = yy; }
    if (w != NULL) { *w = ww; }
    if (h != NULL) { *h = hh; }
}

int
tiku_trk_nav_item_at(const tiku_trk_nav_t *nav, int level, int fx, int fy,
                     int x, int y)
{
    const tiku_trk_nav_level_t *lv;
    int i, ww = 190;

    if (nav == NULL || level < 0 || level >= nav->depth) {
        return -1;
    }
    lv = &nav->level[level];
    if (x < fx || x >= fx + ww || y < fy + 2) {
        return -1;
    }
    i = (y - fy - 2) / TIKU_TRK_NAV_ROW;
    return (i >= 0 && i < lv->n) ? i : -1;
}
