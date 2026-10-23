/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_open_with_window.c - the Open With window (AW-045, AW-047).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_strings.h"
#include "tiku_trk_gfx_dep.h"
#include "tiku_trk_open_with_window.h"
#include <stdio.h>
#include <string.h>

#define OW_WIDTH   420
#define OW_ROWS    10     /* rows the list shows */

/* The row height and the list's top, from the face; the drawing and the
 * click both use them. */
static int row_h(void)    { return tiku_font_plain()->height + 3; }
static int list_top(void) { return tiku_font_plain()->height + 11; }

/**
 * @brief The window's height: 260 at the 12 px face, grown by what the
 *        label line and the ten rows grow by with the face.
 */
static int ow_height(void)
{
    return 260 + (list_top() - 26) + OW_ROWS * (row_h() - 18);
}

/**
 * @brief Where the rows go: OW_ROWS rows of the face's height, inset eight
 *        from the window's sides.
 *
 * The drawing, the click and the keyboard all read this rectangle.
 */
static tiku_rect_t
list_body(tiku_rect_t content)
{
    tiku_rect_t body;

    body.x = content.x + 8;
    body.y = content.y + list_top();
    body.w = content.w - 16;
    body.h = OW_ROWS * row_h();
    return body;
}

/** @brief Set the shell's redraw flag, when it has one. */
static void
mark_dirty(tiku_trk_open_with_window_t *c)
{
    if (c->dirty != NULL) {
        *c->dirty = 1;
    }
}

/** @brief Where Open and the make-preferred button sit along the bottom. */
static void
buttons(tiku_trk_open_with_window_t *c, tiku_rect_t content,
        tiku_rect_t *open_b, tiku_rect_t *prefer_b)
{
    (void)c;
    *open_b = (tiku_rect_t){ content.x + content.w - 70,
                                  content.y + content.h - 30, 60, 22 };
    *prefer_b = (tiku_rect_t){ content.x + content.w - 260,
                                    content.y + content.h - 30, 182, 22 };
}

/**
 * @brief Whether Open and Open and make preferred can act on the selection
 *        (AW-045).
 */
static void
enables(const tiku_trk_open_with_window_t *c, int *can_open,
        int *can_prefer)
{
    int sel = tiku_list_chosen(&c->list);

    *can_open = (sel >= 0 && sel < c->count);
    /* An application with no relation cannot become preferred, and the
     * preferred one already is, so the button is off for both. */
    *can_prefer = *can_open && c->cand[sel].rel != TIKU_TRK_REL_NONE &&
                  !c->cand[sel].preferred;
}

/** @brief Draw the candidates, the chosen one marked, and the two buttons. */
static void
draw(tiku_window_t *window, tiku_surface_t *surface,
     tiku_rect_t content, void *context)
{
    tiku_trk_open_with_window_t *c = context;
    const tiku_font_t *font = tiku_font_plain();
    tiku_rect_t open_b, prefer_b;
    int can_open, can_prefer, i;

    (void)window;
    tiku_fill(surface, content, TIKU_C_PANEL);
    tiku_text(surface, font, content.x + 10, content.y + 16,
                   "Open with:", TIKU_C_TEXT);
    for (i = 0; i < OW_ROWS; i++) {
        int at = c->list.top + i;
        tiku_rect_t row = tiku_list_row_rect(&c->list,
                                                  list_body(content), at);
        int selected;

        if (at >= c->count) {
            break;
        }
        selected = tiku_list_selected(&c->list, at);
        tiku_fill(surface, row, selected ? TIKU_C_SELECT
                                              : TIKU_C_DOC);
        {
            char name[80];

            snprintf(name, sizeof name, "%s%s", c->cand[at].app->name,
                     c->cand[at].preferred ? "  (preferred)" : "");
            tiku_text(surface, font, row.x + 6, row.y + 13, name,
                           selected ? TIKU_C_SELTEXT
                                    : TIKU_C_TEXT);
        }
        {
            /* The relation in words, cut at its end to 30 characters
             * (MA-081). */
            char rel[48];

            snprintf(rel, sizeof rel, "%.30s",
                     tiku_trk_launch_relation_label(c->cand[at].rel));
            tiku_text(surface, font, row.x + row.w - 176, row.y + 13,
                           rel, selected ? TIKU_C_SELTEXT
                                         : tiku_dim(TIKU_C_TEXT,
                                                    TIKU_C_DOC));
        }
    }
    buttons(c, content, &open_b, &prefer_b);
    enables(c, &can_open, &can_prefer);
    tiku_ui_button(surface, prefer_b, "Open and make preferred",
                        (can_prefer ? 0u : TIKU_S_DISABLED) |
                        (tiku_press_held(&c->press, 2) ? TIKU_S_PRESSED : 0u));
    tiku_ui_button(surface, open_b, "Open",
                        (can_open ? 0u : TIKU_S_DISABLED) |
                        (tiku_press_held(&c->press, 1) ? TIKU_S_PRESSED : 0u));
}

/** @brief Close the window, if open, and ask for a redraw. */
static void
close_window(tiku_trk_open_with_window_t *c)
{
    if (c->window != NULL) {
        tiku_workspace_close(c->workspace, c->window);
        c->window = NULL;
    }
    mark_dirty(c);
}

/** @brief Open with the selected application, asking first if it must. */
static void
do_open(tiku_trk_open_with_window_t *c, int make_preferred)
{
    const tiku_trk_cand_t *pick;
    int can_open, can_prefer;

    enables(c, &can_open, &can_prefer);
    if (!can_open || (make_preferred && !can_prefer)) {
        return;
    }
    pick = &c->cand[tiku_list_chosen(&c->list)];
    if (make_preferred && c->store != NULL) {
        int t;

        for (t = 0; t < c->type_count; t++) {
            (void)tiku_trk_launch_set_preferred(c->store, c->types[t],
                                                pick->app->name);
        }
    }
    if (pick->rel == TIKU_TRK_REL_NONE && !c->generic_only &&
        c->alert != NULL) {
        /* The application does not claim the document's type: ask
         * first, with Cancel the default, unless every document is
         * untyped (AW-046). */
        char text[220];

        snprintf(text, sizeof text,
                 "\"%.48s\" does not claim this document's type.  Open "
                 "anyway?", pick->app->name);
        tiku_alert_open(c->alert, TIKU_ALERT_WARN,
                            TIKU_TRK_ALERT_TAG_OPEN_ANYWAY, text,
                            tiku_str("alert.btn.open"));
        /* The answer arrives at tiku_trk_open_with_window_confirmed(). */
        return;
    }
    if (c->open != NULL) {
        c->open(c->open_context, pick->app, c->path_ptr, c->path_count);
    }
    close_window(c);
}

void
tiku_trk_open_with_window_confirmed(tiku_trk_open_with_window_t *c)
{
    {
        int sel = tiku_list_chosen(&c->list);

        if (sel >= 0 && sel < c->count && c->open != NULL) {
            c->open(c->open_context, c->cand[sel].app, c->path_ptr,
                    c->path_count);
        }
    }
    close_window(c);
}

/**
 * @brief A press picks a row; Open and Open and make preferred act on a
 *        release over them.
 * @return 1 when taken.
 */
static int
event(tiku_window_t *window, const tiku_event_t *ev,
      void *context)
{
    tiku_trk_open_with_window_t *c = context;
    tiku_rect_t content, open_b, prefer_b;

    (void)window;
    if (c->window == NULL) {
        return 0;
    }
    content = tiku_window_content(c->window);
    buttons(c, content, &open_b, &prefer_b);
#define INSIDE(r) (ev->x >= (r).x && ev->x < (r).x + (r).w && \
                   ev->y >= (r).y && ev->y < (r).y + (r).h)
    {
        /* A button shows pressed while held and acts on a release over
         * it. */
        int can_open, can_prefer, under = 0;

        enables(c, &can_open, &can_prefer);
        if (can_open && INSIDE(open_b)) { under = 1; }
        if (can_prefer && INSIDE(prefer_b)) { under = 2; }
        if (ev->type == TIKU_EVENT_POINTER_MOVE) {
            if (tiku_press_track(&c->press, under)) { mark_dirty(c); }
            return c->press.id != 0;
        }
        if (ev->type == TIKU_EVENT_POINTER_UP) {
            int id = tiku_press_release(&c->press, under);

            mark_dirty(c);
            if (id != 0) { do_open(c, id - 1); }
            return 1;
        }
        if (ev->type != TIKU_EVENT_POINTER_DOWN) {
            return 0;
        }
        if (under != 0 && tiku_press_arm(&c->press, under)) {
            mark_dirty(c);
            return 1;
        }
    }
#undef INSIDE
    {
        int at = tiku_list_at(&c->list, list_body(content), ev->x, ev->y);

        if (at >= 0) {
            tiku_list_click(&c->list, at, ev->modifiers);
            mark_dirty(c);
            return 1;
        }
    }
    return 0;
}

void
tiku_trk_open_with_window_init(tiku_trk_open_with_window_t *c,
    tiku_workspace_t *workspace, int screen_w, int screen_h,
    int *dirty, tiku_trk_ow_open_fn open, void *open_context,
    tiku_alert_t *alert, struct tiku_store *store)
{
    memset(c, 0, sizeof *c);
    c->workspace = workspace;
    c->screen_w = screen_w;
    c->screen_h = screen_h;
    c->dirty = dirty;
    c->open = open;
    c->open_context = open_context;
    c->alert = alert;
    c->store = store;
    /* A single-selection list: the list keeps at most one row
     * selected. */
    tiku_list_init(&c->list, 1);
    tiku_list_set_row_h(&c->list, row_h());
}

void
tiku_trk_open_with_window_open(tiku_trk_open_with_window_t *c,
    const tiku_trk_apps_t *apps, const char *const *types, int ntypes,
    const char *const *paths, int npaths)
{
    int i, j;

    if (apps == NULL) {
        return;
    }
    c->count = 0;
    tiku_list_init(&c->list, 1);
    tiku_list_set_row_h(&c->list, row_h());
    c->type_count = 0;
    c->generic_only = 1;
    for (i = 0; i < ntypes && c->type_count < TIKU_TRK_APP_TYPES_MAX; i++) {
        if (types[i] == NULL) {
            continue;
        }
        snprintf(c->types[c->type_count], sizeof c->types[0], "%s",
                 types[i]);
        if (strcmp(types[i], "application/octet-stream") != 0 &&
            strcmp(types[i], "*") != 0) {
            c->generic_only = 0;
        }
        c->type_count++;
    }
    c->path_count = 0;
    for (i = 0; i < npaths && c->path_count < TIKU_TRK_OW_PATHS; i++) {
        if (paths[i] == NULL) {
            continue;
        }
        snprintf(c->paths[c->path_count], sizeof c->paths[0], "%s",
                 paths[i]);
        c->path_ptr[c->path_count] = c->paths[c->path_count];
        c->path_count++;
    }
    /* Every application that opens documents, with its best relation to
     * the selection's types (AW-046). */
    for (i = 0; i < apps->count && c->count < TIKU_TRK_APP_MAX; i++) {
        /* A bundle with no types advertises no document-opening API. */
        if (apps->app[i].module[0] && !apps->app[i].ntype &&
            !apps->app[i].takes_anything) continue;
        tiku_trk_cand_t *k = &c->cand[c->count++];

        k->app = &apps->app[i];
        k->rel = TIKU_TRK_REL_NONE;
        k->preferred = 0;
        for (j = 0; j < c->type_count; j++) {
            tiku_trk_relation_t r =
                tiku_trk_launch_relation(&apps->app[i], c->types[j]);

            if (r > k->rel) {
                k->rel = r;
            }
        }
        for (j = 0; j < c->type_count; j++) {
            char pref[TIKU_TRK_APP_NAME_MAX];
            const char *p = tiku_trk_launch_preferred(c->store,
                                c->types[j], pref, sizeof pref);

            if (p != NULL && strcmp(p, apps->app[i].name) == 0) {
                k->preferred = 1;
            }
        }
    }
    /* Better claims first, in the order tiku_trk_launch_cand_before()
     * gives. */
    for (i = 0; i < c->count; i++) {
        for (j = i + 1; j < c->count; j++) {
            if (tiku_trk_launch_cand_before(&c->cand[j], &c->cand[i])) {
                tiku_trk_cand_t t = c->cand[i];

                c->cand[i] = c->cand[j];
                c->cand[j] = t;
            }
        }
    }
    (void)tiku_list_set_count(&c->list, c->count);
    {
        tiku_rect_t frame = { (c->screen_w - OW_WIDTH) / 2,
                                   (c->screen_h - ow_height()) / 3,
                                   OW_WIDTH, ow_height() };

        if (c->window != NULL && c->window->open) {
            tiku_workspace_activate(c->workspace, c->window);
        } else {
            c->window = tiku_workspace_open(c->workspace, "Open with",
                frame, draw, c, NULL);
            if (c->window != NULL) {
                tiku_window_set_handlers(c->window, event, NULL, NULL);
            }
        }
    }
    mark_dirty(c);
}

tiku_window_t *
tiku_trk_open_with_window_handle(tiku_trk_open_with_window_t *c)
{
    return c->window;
}

int
tiku_trk_open_with_window_key(tiku_trk_open_with_window_t *c, int key,
                              unsigned modifiers)
{
    if (c->window == NULL || !c->window->open ||
        tiku_workspace_focused(c->workspace) != c->window) {
        return 0;
    }
    if (c->alert != NULL && c->alert->open) {
        /* The confirm is up: its keys belong to the alert, later in the
         * shell's chain (AW-046). */
        return 0;
    }
    switch (key) {
    case TIKU_KEY_ESCAPE:
        /* Escape closes the window without opening anything
         * (AW-047). */
        close_window(c);
        return 1;
    case TIKU_KEY_UP:
    case TIKU_KEY_DOWN:
    case TIKU_KEY_PAGE_UP:
    case TIKU_KEY_PAGE_DOWN:
    case TIKU_KEY_HOME:
    case TIKU_KEY_END:
        (void)tiku_list_key(&c->list, key, modifiers,
                            list_body(tiku_window_content(c->window)));
        mark_dirty(c);
        return 1;
    case TIKU_KEY_RETURN:
        do_open(c, 0);
        return 1;
    default:
        return 1;               /* the window keeps every other key */
    }
}
