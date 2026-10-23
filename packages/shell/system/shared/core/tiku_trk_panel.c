/*
 * Desktop shell and file manager.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_panel.c - the panel's rules: where it starts, what it will
 * accept, and what it refuses to write over.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_shell_product.h"
#include "tiku_state.h"
#include "tiku_trk_input.h"
#include "tiku_trk_panel.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** @brief Where the shared panel state lives; one slot for every panel. */
#define PANEL_SLOT      "\x01panel"
#define ATTR_FRAME      "_trk/panel_frame"
#define ATTR_FAVORITES  "_trk/favorites"

/** @brief Whether @p path names a directory the backend can see. */
static int
is_dir(tiku_backend_t *b, const char *path, tiku_model_t *out)
{
    tiku_model_t m;

    if (b == NULL || b->ops == NULL || b->ops->stat == NULL ||
        path == NULL || path[0] == '\0') {
        return 0;
    }
    if (b->ops->stat(b, path, &m) != 0) {
        return 0;
    }
    if (out != NULL) {
        *out = m;
    }
    return tiku_model_is_container(&m);
}

/** @brief The user's home, or "" when the environment does not say. */
static const char *
home_dir(void)
{
    const char *h = getenv("HOME");

    return (h != NULL && h[0] == '/') ? h : "";
}

void
tiku_trk_panel_open(tiku_trk_panel_t *p, tiku_trk_panel_mode_t mode,
                    unsigned flavors, const char *want,
                    tiku_backend_t *b, const char *client)
{
    if (p == NULL) {
        return;
    }
    memset(p, 0, sizeof *p);
    p->mode = mode;
    p->flavors = (flavors != 0u) ? flavors : TIKU_TRK_PANEL_FILE;
    p->multiple = (mode == TIKU_TRK_PANEL_OPEN);
    p->focus = (mode == TIKU_TRK_PANEL_SAVE) ? TIKU_TRK_PANEL_FOCUS_NAME
                                             : TIKU_TRK_PANEL_FOCUS_LIST;
    snprintf(p->client, sizeof p->client, "%s",
             (client != NULL) ? client : TIKU_SHELL_TRACKER_NAME);
    snprintf(p->home, sizeof p->home, "%s", home_dir());
    (void)tiku_desktop_dir(p->desktop, sizeof p->desktop);
    /* Three rungs; the last, the root, cannot fail (AW-050). */
    if (is_dir(b, want, NULL)) {
        snprintf(p->dir, sizeof p->dir, "%s", want);
    } else if (is_dir(b, p->home, NULL)) {
        snprintf(p->dir, sizeof p->dir, "%s", p->home);
    } else {
        snprintf(p->dir, sizeof p->dir, "/");
    }
}

void
tiku_trk_panel_title(const tiku_trk_panel_t *p, char *out, size_t max)
{
    if (out == NULL || max == 0u) {
        return;
    }
    if (p == NULL) {
        out[0] = '\0';
        return;
    }
    /* The title is only what is being asked, Save or Open (AW-064). */
    snprintf(out, max, "%s",
             (p->mode == TIKU_TRK_PANEL_SAVE) ? "Save" : "Open");
}

/** @brief Whether one model is a legal answer to what the panel asked. */
static int
acceptable(const tiku_trk_panel_t *p, const tiku_model_t *m)
{
    if (m == NULL) {
        return 0;
    }
    if (tiku_model_is_container(m)) {
        return (p->flavors & TIKU_TRK_PANEL_DIR) != 0u;
    }
    return (p->flavors & TIKU_TRK_PANEL_FILE) != 0u;
}

int
tiku_trk_panel_choosable(const tiku_trk_panel_t *p, const tiku_model_t *m)
{
    return (p != NULL) ? acceptable(p, m) : 0;
}

const char *
tiku_trk_panel_button_label(const tiku_trk_panel_t *p,
                            const tiku_model_t *sel, int n)
{
    if (p == NULL) {
        return "Open";
    }
    /* A lone folder relabels the button Open whatever the panel is for:
     * the button enters it (AW-053). */
    if (n == 1 && sel != NULL && tiku_model_is_container(&sel[0])) {
        return "Open";
    }
    return (p->mode == TIKU_TRK_PANEL_SAVE) ? "Save" : "Open";
}

int
tiku_trk_panel_button_enabled(const tiku_trk_panel_t *p,
                              const tiku_model_t *sel, int n)
{
    int i;

    if (p == NULL) {
        return 0;
    }
    if (n == 1 && sel != NULL && tiku_model_is_container(&sel[0])) {
        return 1;               /* entering is always available */
    }
    if (p->mode == TIKU_TRK_PANEL_SAVE) {
        /* A save panel follows the name field, not the selection
         * (AW-053). */
        return (p->name_len > 0);
    }
    if (n <= 0) {
        /* Nothing picked.  A panel asking for a folder still has an
         * answer, the one it is showing, which is how "choose this folder"
         * works without a row for it. */
        return (p->flavors & TIKU_TRK_PANEL_DIR) != 0u;
    }
    if (n > 1 && !p->multiple) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        if (!acceptable(p, &sel[i])) {
            return 0;
        }
    }
    return 1;
}

int
tiku_trk_panel_pick_name(tiku_trk_panel_t *p, const tiku_model_t *sel,
                         int n)
{
    if (p == NULL || p->mode != TIKU_TRK_PANEL_SAVE || n != 1 ||
        sel == NULL || tiku_model_is_container(&sel[0])) {
        return 0;
    }
    if (strcmp(p->name, sel[0].name) == 0) {
        return 0;
    }
    snprintf(p->name, sizeof p->name, "%s", sel[0].name);
    p->name_len = (int)strlen(p->name);
    /* The keyboard focus stays where it is: the user is picking with the
     * mouse (AW-054). */
    return 1;
}

tiku_trk_panel_act_t
tiku_trk_panel_activate(const tiku_trk_panel_t *p, const tiku_model_t *m)
{
    if (p == NULL || m == NULL) {
        return TIKU_TRK_PANEL_ACT_NOTHING;
    }
    /* A folder is entered even by a panel that would accept one as its
     * answer; the default button is what chooses it (AW-055). */
    if (tiku_model_is_container(m)) {
        return TIKU_TRK_PANEL_ACT_ENTER;
    }
    return acceptable(p, m) ? TIKU_TRK_PANEL_ACT_CHOOSE
                            : TIKU_TRK_PANEL_ACT_NOTHING;
}

tiku_trk_save_t
tiku_trk_panel_save_check(const tiku_trk_panel_t *p, tiku_backend_t *b,
                          char *out, size_t max)
{
    char full[TIKU_PATH_MAX];
    tiku_model_t m;

    if (out != NULL && max > 0u) {
        out[0] = '\0';
    }
    if (p == NULL) {
        return TIKU_TRK_SAVE_EMPTY;
    }
    if (p->name_len <= 0) {
        return TIKU_TRK_SAVE_EMPTY;
    }
    if (strcmp(p->name, ".") == 0 || strcmp(p->name, "..") == 0) {
        /* Neither is a name; both would resolve to a directory that
         * already exists and is not the one being written (AW-052). */
        return TIKU_TRK_SAVE_DOTS;
    }
    if (strcmp(p->dir, "/") == 0) {
        return TIKU_TRK_SAVE_ROOT;
    }
    if (!is_dir(b, p->dir, &m) || !tiku_model_is_mutable_container(&m)) {
        return TIKU_TRK_SAVE_UNWRITABLE;
    }
    if (strlen(p->dir) + strlen(p->name) + 2u > sizeof full) {
        /* Longer than a path can be is not a name this folder can hold. */
        return TIKU_TRK_SAVE_UNWRITABLE;
    }
    snprintf(full, sizeof full, "%.*s/%.*s",
             (int)(sizeof full - sizeof p->name - 2u), p->dir,
             (int)(sizeof p->name - 1u), p->name);
    if (out != NULL) {
        snprintf(out, max, "%s", full);
    }
    if (b != NULL && b->ops != NULL && b->ops->stat != NULL &&
        b->ops->stat(b, full, &m) == 0) {
        /* A folder of that name refuses the save; a file of that name
         * asks whether to replace it. */
        return tiku_model_is_container(&m) ? TIKU_TRK_SAVE_IS_FOLDER
                                               : TIKU_TRK_SAVE_REPLACE;
    }
    return TIKU_TRK_SAVE_OK;
}

const char *
tiku_trk_panel_save_text(tiku_trk_save_t why)
{
    switch (why) {
    case TIKU_TRK_SAVE_EMPTY:
        return "The file must have a name.";
    case TIKU_TRK_SAVE_ROOT:
        return "You cannot save into the system root.";
    case TIKU_TRK_SAVE_DOTS:
        return "That name cannot be used.";
    case TIKU_TRK_SAVE_UNWRITABLE:
        return "That folder cannot be written to.";
    case TIKU_TRK_SAVE_IS_FOLDER:
        return "A folder of that name is already there.";
    case TIKU_TRK_SAVE_REPLACE:
        return "A file of that name is already there.  Replace it?";
    default:
        return NULL;
    }
}

int
tiku_trk_panel_drop(tiku_trk_panel_t *p, tiku_backend_t *b,
                    const char *path, char *select, size_t max)
{
    tiku_model_t m;
    const char *slash;

    if (select != NULL && max > 0u) {
        select[0] = '\0';
    }
    if (p == NULL || path == NULL || path[0] == '\0') {
        return 0;
    }
    if (is_dir(b, path, &m)) {
        snprintf(p->dir, sizeof p->dir, "%s", path);
        return 1;
    }
    if (b == NULL || b->ops == NULL || b->ops->stat == NULL ||
        b->ops->stat(b, path, &m) != 0) {
        return 0;
    }
    /* A file opens the folder it is in and is selected there: a drop is
     * the user saying "this one" (AW-059). */
    slash = strrchr(path, '/');
    if (slash == NULL || slash == path) {
        snprintf(p->dir, sizeof p->dir, "/");
    } else {
        int n = (int)(slash - path);

        if (n >= (int)sizeof p->dir) {
            n = (int)sizeof p->dir - 1;
        }
        memcpy(p->dir, path, (size_t)n);
        p->dir[n] = '\0';
    }
    if (select != NULL) {
        snprintf(select, max, "%s", m.name);
    }
    if (p->mode == TIKU_TRK_PANEL_SAVE) {
        snprintf(p->name, sizeof p->name, "%s", m.name);
        p->name_len = (int)strlen(p->name);
    }
    return 1;
}

int
tiku_trk_panel_dir_moved(tiku_trk_panel_t *p, const char *from,
                         const char *to)
{
    size_t n;

    if (p == NULL || from == NULL || to == NULL || from[0] == '\0') {
        return 0;
    }
    if (strcmp(p->dir, from) == 0) {
        snprintf(p->dir, sizeof p->dir, "%s", to);
        return 1;
    }
    /* An ancestor moving takes the panel with it, at a component boundary
     * so "/a/b" does not claim "/a/bc". */
    n = strlen(from);
    if (n > 0u && strncmp(from, p->dir, n) == 0 && p->dir[n] == '/') {
        char rebuilt[TIKU_PATH_MAX];

        if ((int)snprintf(rebuilt, sizeof rebuilt, "%s%s", to,
                          p->dir + n) < (int)sizeof rebuilt) {
            snprintf(p->dir, sizeof p->dir, "%s", rebuilt);
            return 1;
        }
    }
    return 0;
}

void
tiku_trk_panel_dir_gone(tiku_trk_panel_t *p, tiku_backend_t *b)
{
    if (p == NULL) {
        return;
    }
    /* The panel stays open, since closing it would answer its question as
     * cancel, and moves to home, else the root (PVN-029, PVN-043). */
    if (is_dir(b, p->home, NULL)) {
        snprintf(p->dir, sizeof p->dir, "%s", p->home);
    } else {
        snprintf(p->dir, sizeof p->dir, "/");
    }
}

int
tiku_trk_panel_parent(const tiku_trk_panel_t *p, char *out, size_t max)
{
    const char *slash;

    if (p == NULL || out == NULL || max == 0u) {
        return 0;
    }
    out[0] = '\0';
    if (p->desktop_is_root) {
        if (p->desktop[0] != '\0' && strcmp(p->dir, p->desktop) == 0) {
            return 0;           /* the Desktop is the top */
        }
        if (strcmp(p->dir, "/") == 0) {
            /* With the setting on, the root is below the Desktop, so Up
             * from it lands there (AW-061). */
            if (p->desktop[0] == '\0') {
                return 0;
            }
            snprintf(out, max, "%s", p->desktop);
            return 1;
        }
    } else if (strcmp(p->dir, "/") == 0) {
        return 0;
    }
    slash = strrchr(p->dir, '/');
    if (slash == NULL) {
        return 0;
    }
    if (slash == p->dir) {
        snprintf(out, max, "/");
    } else {
        int n = (int)(slash - p->dir);

        if (n >= (int)max) {
            n = (int)max - 1;
        }
        memcpy(out, p->dir, (size_t)n);
        out[n] = '\0';
    }
    return 1;
}

tiku_trk_panel_key_t
tiku_trk_panel_key(const tiku_trk_panel_t *p, int key, unsigned mods,
                   int pose_focus, int can_duplicate)
{
    if (p == NULL) {
        return TIKU_TRK_PANEL_KEY_NONE;
    }
    if (key == 27) {
        return TIKU_TRK_PANEL_KEY_CANCEL;       /* Escape */
    }
    if (key == '\t') {
        /* Tab moves the keyboard between the controls (PVK-022). */
        return TIKU_TRK_PANEL_KEY_FOCUS;
    }
    if (key == '\n' || key == '\r') {
        return TIKU_TRK_PANEL_KEY_DEFAULT;
    }
    if ((mods & TIKU_TRK_MOD_CMD) == 0u) {
        return TIKU_TRK_PANEL_KEY_NONE;
    }
    switch (key) {
    case 'w': case 'W':
        return TIKU_TRK_PANEL_KEY_CANCEL;
    case TIKU_TRK_KEY_UP:
        return TIKU_TRK_PANEL_KEY_PARENT;
    case TIKU_TRK_KEY_DOWN:
        return TIKU_TRK_PANEL_KEY_ENTER;
    case 'h': case 'H':
        return TIKU_TRK_PANEL_KEY_HOME;
    case 'd': case 'D':
        /* Cmd+D is the Desktop, except when the list has the keyboard and
         * the selection could be duplicated: then it duplicates, as in a
         * folder window (AW-058). */
        return (pose_focus && can_duplicate) ? TIKU_TRK_PANEL_KEY_DUPLICATE
                                             : TIKU_TRK_PANEL_KEY_DESKTOP;
    default:
        return TIKU_TRK_PANEL_KEY_NONE;
    }
}

/*---------------------------------------------------------------------------*/
/* Recent lists and favourites                                               */
/*---------------------------------------------------------------------------*/

/** @brief The attribute one recent list lives in. */
static const char *
recent_attr(tiku_trk_recent_t which)
{
    switch (which) {
    case TIKU_TRK_RECENT_FOLDERS: return "_trk/recent_folders";
    case TIKU_TRK_RECENT_APPS:    return "_trk/recent_apps";
    default:                      return "_trk/recent_docs";
    }
}

/** @brief Read a newline-separated list into @p out. */
static int
list_read(tiku_store_t *s, const char *attr,
          char out[][TIKU_PATH_MAX], int max)
{
    char buf[TIKU_PATH_MAX * TIKU_TRK_RECENT_MAX];
    int n = 0, len;
    char *line, *save;

    if (s == NULL || out == NULL || max <= 0) {
        return 0;
    }
    len = tiku_state_read(s, PANEL_SLOT, attr, buf, sizeof buf - 1u);
    if (len <= 0) {
        return 0;
    }
    buf[len] = '\0';
    for (line = strtok_r(buf, "\n", &save); line != NULL && n < max;
         line = strtok_r(NULL, "\n", &save)) {
        if (line[0] != '\0') {
            snprintf(out[n++], TIKU_PATH_MAX, "%s", line);
        }
    }
    return n;
}

/** @brief Write a list back. */
static void
list_write(tiku_store_t *s, const char *attr,
           char list[][TIKU_PATH_MAX], int n)
{
    char buf[TIKU_PATH_MAX * TIKU_TRK_RECENT_MAX];
    int i;
    size_t at = 0u;

    for (i = 0; i < n && at + 1u < sizeof buf; i++) {
        int w = snprintf(buf + at, sizeof buf - at, "%.*s\n",
                         (int)(TIKU_PATH_MAX - 1), list[i]);

        if (w <= 0 || (size_t)w >= sizeof buf - at) {
            break;
        }
        at += (size_t)w;
    }
    (void)tiku_state_write(s, PANEL_SLOT, attr, buf, at);
}

void
tiku_trk_recent_add(tiku_store_t *s, tiku_trk_recent_t which,
                    const char *path)
{
    char list[TIKU_TRK_RECENT_MAX][TIKU_PATH_MAX];
    char keep[TIKU_TRK_RECENT_MAX][TIKU_PATH_MAX];
    int n, i, k = 0;

    if (s == NULL || path == NULL || path[0] == '\0') {
        return;
    }
    n = list_read(s, recent_attr(which), list, TIKU_TRK_RECENT_MAX);
    snprintf(keep[k++], TIKU_PATH_MAX, "%.*s",
             (int)(TIKU_PATH_MAX - 1), path);
    for (i = 0; i < n && k < TIKU_TRK_RECENT_MAX; i++) {
        /* Used again is not a second entry: it moves to the front. */
        if (strcmp(list[i], path) != 0) {
            snprintf(keep[k++], TIKU_PATH_MAX, "%.*s",
                     (int)(TIKU_PATH_MAX - 1), list[i]);
        }
    }
    list_write(s, recent_attr(which), keep, k);
}

int
tiku_trk_recent_list(tiku_store_t *s, tiku_trk_recent_t which, int cap,
                     char out[][TIKU_PATH_MAX], int max)
{
    int n, shown;

    n = list_read(s, recent_attr(which), out, max);
    /* One fewer than the configured count (CW-058). */
    shown = cap - 1;
    if (shown < 0) {
        shown = 0;
    }
    return (n < shown) ? n : shown;
}

int
tiku_trk_panel_add_favorite(const tiku_trk_panel_t *p, tiku_store_t *s)
{
    char list[TIKU_TRK_RECENT_MAX][TIKU_PATH_MAX];
    int n, i;

    if (p == NULL || s == NULL || p->dir[0] == '\0') {
        return 0;
    }
    n = list_read(s, ATTR_FAVORITES, list, TIKU_TRK_RECENT_MAX);
    for (i = 0; i < n; i++) {
        if (strcmp(list[i], p->dir) == 0) {
            return 0;           /* already a favourite */
        }
    }
    if (n >= TIKU_TRK_RECENT_MAX) {
        return 0;
    }
    snprintf(list[n++], TIKU_PATH_MAX, "%s", p->dir);
    list_write(s, ATTR_FAVORITES, list, n);
    return 1;
}

int
tiku_trk_panel_favorites(tiku_store_t *s, char out[][TIKU_PATH_MAX],
                         int max)
{
    return list_read(s, ATTR_FAVORITES, out, max);
}

/*---------------------------------------------------------------------------*/
/* Geometry and name completion                                              */
/*---------------------------------------------------------------------------*/

/** @brief Clamp one dimension into the panel's limits. */
static int
clamp(int v, int lo, int hi)
{
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

int
tiku_trk_panel_restore_frame(tiku_store_t *s, int *x, int *y, int *w,
                             int *h)
{
    int32_t fr[4];

    if (s == NULL) {
        return 0;
    }
    if (tiku_state_read(s, PANEL_SLOT, ATTR_FRAME, fr, sizeof fr) !=
            (int)sizeof fr) {
        return 0;
    }
    if (x != NULL) { *x = (int)fr[0]; }
    if (y != NULL) { *y = (int)fr[1]; }
    /* Clamped on the way out as well as in, so a frame saved on another
     * screen still fits the limits (AW-063). */
    if (w != NULL) {
        *w = clamp((int)fr[2], TIKU_TRK_PANEL_MIN_W, TIKU_TRK_PANEL_MAX_W);
    }
    if (h != NULL) {
        *h = clamp((int)fr[3], TIKU_TRK_PANEL_MIN_H, TIKU_TRK_PANEL_MAX_H);
    }
    return 1;
}

void
tiku_trk_panel_save_frame(tiku_store_t *s, int x, int y, int w, int h)
{
    int32_t fr[4];

    if (s == NULL) {
        return;
    }
    fr[0] = (int32_t)x;
    fr[1] = (int32_t)y;
    fr[2] = (int32_t)clamp(w, TIKU_TRK_PANEL_MIN_W, TIKU_TRK_PANEL_MAX_W);
    fr[3] = (int32_t)clamp(h, TIKU_TRK_PANEL_MIN_H, TIKU_TRK_PANEL_MAX_H);
    (void)tiku_state_write(s, PANEL_SLOT, ATTR_FRAME, fr, sizeof fr);
}

/** @brief Collects the longest common extension during the list walk. */
struct complete_ctx {
    const char *prefix;
    size_t      plen;
    char        best[TIKU_NAME_MAX];
    int         matches;
};

/**
 * @brief Fold one entry into the completion: the first match is the best
 * so far, and each later one trims it to their common prefix.
 */
static int
complete_one(const tiku_model_t *m, void *ctx)
{
    struct complete_ctx *c = ctx;

    if (strncmp(m->name, c->prefix, c->plen) != 0) {
        return 0;
    }
    if (c->matches == 0) {
        snprintf(c->best, sizeof c->best, "%s", m->name);
    } else {
        size_t k = 0;

        while (c->best[k] != '\0' && c->best[k] == m->name[k]) {
            k++;
        }
        c->best[k] = '\0';
    }
    c->matches++;
    return 0;
}

int
tiku_trk_panel_complete(tiku_backend_t *b, const char *dir,
                        const char *prefix, char *out, size_t max)
{
    struct complete_ctx c;

    if (b == NULL || b->ops->list == NULL || dir == NULL ||
        prefix == NULL || out == NULL || max == 0u) {
        return 0;
    }
    memset(&c, 0, sizeof c);
    c.prefix = prefix;
    c.plen = strlen(prefix);
    if (c.plen == 0u) {
        return 0;               /* nothing typed: nothing to extend */
    }
    (void)b->ops->list(b, dir, complete_one, &c);
    if (c.matches > 0) {
        snprintf(out, max, "%s", c.best);
    }
    return c.matches;
}
