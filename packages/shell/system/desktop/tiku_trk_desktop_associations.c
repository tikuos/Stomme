/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_desktop_associations.c - the File Associations window: for each
 * file type, the application that opens it, edited as a draft that only
 * Save writes to the store.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_dialog.h"
#include "tiku_preferences_layout.h"
#include "tiku_text_layout_paint.h"
#include "tiku_trk_associations.h"
#include "tiku_trk_desktop_private.h"

#define ASSOC_PAGE 8
enum { ASSOC_APP = 200, ASSOC_PREV = 300, ASSOC_NEXT,
       ASSOC_APP_PREV, ASSOC_APP_NEXT, ASSOC_CANCEL, ASSOC_SAVE };

struct desktop_associations {
    desktop_t *app;
    tiku_window_t *window;
    tiku_trk_associations_t model;
    tiku_dialog_t dialog;
    tiku_trk_cand_t candidates[TIKU_TRK_APP_MAX];
    int selected, page, app_page, count, saving;
    unsigned generation, submitted_generation;
    char status[160];
};

typedef struct {
    tiku_rect_t types, apps, previous, next, app_previous, app_next;
    tiku_rect_t detail, status, cancel, save;
} assoc_layout_t;

/** @brief The window's layout, scaled to the font; painting and input
 *         share it. */
static assoc_layout_t assoc_layout(tiku_rect_t c)
{
    assoc_layout_t l;
    int row = tiku_font_plain()->height + 12, bh = tiku_ui_button_h();
    int half = (c.w - 52) / 2;
    l.types = (tiku_rect_t){c.x + 18, c.y + 76, half,
                           ASSOC_PAGE * row + 8};
    l.apps = l.types; l.apps.x += half + 16;
    int y = l.types.y + l.types.h + 8;
    l.previous = (tiku_rect_t){l.types.x + half - 64, y, 28, bh};
    l.next = l.previous; l.next.x += 36;
    l.app_previous = l.previous; l.app_previous.x += half + 16;
    l.app_next = l.next; l.app_next.x += half + 16;
    l.detail = (tiku_rect_t){c.x + 18, y + bh + 10, c.w - 36, row};
    l.status = l.detail; l.status.y += row;
    tiku_preferences_action_t actions[] = {{"Cancel", 82}, {"Save", 82}};
    tiku_rect_t buttons[2];
    (void)tiku_preferences_actions((tiku_rect_t){c.x + 18,
        l.status.y + row + 8, c.w - 36, bh}, NULL, actions, 2, 12,
        NULL, buttons);
    l.cancel = buttons[0]; l.save = buttons[1];
    return l;
}

/** @brief Row @p i of a list, inset from the list's bevel. */
static tiku_rect_t assoc_row(tiku_rect_t list, int i)
{
    return (tiku_rect_t){list.x + 4,
        list.y + 4 + i * (tiku_font_plain()->height + 12), list.w - 8,
        tiku_font_plain()->height + 12};
}

/** @brief Fill p->candidates with the applications that can open the
 *         selected type, from their declared capabilities, and go back to
 *         the first page of them. */
static void assoc_candidates(struct desktop_associations *p)
{
    p->count = 0; p->app_page = 0;
    if (!p->model.count) return;
    const char *type = p->model.row[p->selected].type;
    p->count = tiku_trk_launch_candidates(&p->app->apps, &type, 1, NULL,
                                         p->candidates, TIKU_TRK_APP_MAX);
}

/** @brief Register all visible controls for release-to-activate and Tab. */
static void assoc_controls(struct desktop_associations *p, assoc_layout_t l)
{
    tiku_dialog_begin(&p->dialog, ASSOC_SAVE, ASSOC_CANCEL);
    for (int i = p->page; i < p->model.count && i < p->page + ASSOC_PAGE; i++)
        tiku_dialog_add(&p->dialog, i, assoc_row(l.types, i - p->page),
                        TIKU_DIALOG_TOGGLE, 1);
    for (int i = p->app_page; i <= p->count &&
         i < p->app_page + ASSOC_PAGE && p->model.count; i++)
        tiku_dialog_add(&p->dialog, ASSOC_APP + i,
            assoc_row(l.apps, i - p->app_page), TIKU_DIALOG_TOGGLE, 1);
    tiku_dialog_add(&p->dialog, ASSOC_PREV, l.previous,
                    TIKU_DIALOG_BUTTON, p->page > 0);
    tiku_dialog_add(&p->dialog, ASSOC_NEXT, l.next, TIKU_DIALOG_BUTTON,
                    p->page + ASSOC_PAGE < p->model.count);
    tiku_dialog_add(&p->dialog, ASSOC_APP_PREV, l.app_previous,
                    TIKU_DIALOG_BUTTON, p->app_page > 0);
    tiku_dialog_add(&p->dialog, ASSOC_APP_NEXT, l.app_next,
                    TIKU_DIALOG_BUTTON, p->app_page + ASSOC_PAGE <= p->count);
    tiku_dialog_add(&p->dialog, ASSOC_CANCEL, l.cancel, TIKU_DIALOG_BUTTON, 1);
    tiku_dialog_add(&p->dialog, ASSOC_SAVE, l.save, TIKU_DIALOG_BUTTON,
                    tiku_trk_associations_changed(&p->model));
    tiku_dialog_end(&p->dialog);
}

/** @brief A control's drawing flags: focused, pressed, disabled. */
static unsigned assoc_flags(struct desktop_associations *p, int id)
{
    unsigned flags = tiku_dialog_focused(&p->dialog, id) ? TIKU_S_FOCUS : 0;
    if (tiku_dialog_pressed(&p->dialog, id)) flags |= TIKU_S_PRESSED;
    for (int i = 0; i < p->dialog.count; i++)
        if (p->dialog.control[i].id == id && !p->dialog.control[i].enabled)
            flags |= TIKU_S_DISABLED;
    return flags;
}

/** @brief Paint one line of text, clipped to @p r. */
static void assoc_line(tiku_surface_t *s, tiku_rect_t r, const char *text,
                        tiku_rgb_t color)
{
    tiku_text_line_draw(s, r, TIKU_MEASURE_PLAIN, text, color);
}

/** @brief Draw the selected type's MIME type, and a note on Save, or on its
 *         preferred application being unavailable. */
static void assoc_detail(struct desktop_associations *p, tiku_surface_t *s,
                         assoc_layout_t l)
{
    char text[180];
    if (!p->model.count) {
        assoc_line(s, l.detail, "No document applications installed.",
                   TIKU_C_TEXT);
        return;
    }
    tiku_trk_association_t *r = &p->model.row[p->selected];
    snprintf(text, sizeof text, "%s", r->type);
    assoc_line(s, l.detail, text, TIKU_C_TEXT);
    int available = !r->chosen[0];
    for (int i = 0; i < p->count; i++)
        available |= !strcmp(r->chosen, p->candidates[i].app->name);
    snprintf(text, sizeof text, available ?
        "Save applies your choices to future file opens." :
        "Preferred app unavailable: %.47s. Automatic is used until it returns.",
        r->chosen);
    assoc_line(s, l.status, p->status[0] ? p->status : text,
               tiku_dim(TIKU_C_TEXT, TIKU_C_PANEL));
}

/** @brief Reconcile saved values for rows without a pending draft change. */
static void assoc_sync(struct desktop_associations *p);

/**
 * @brief Draw the window: the file types and the applications that open the
 *        selected one side by side, the paging buttons, Cancel and Save.
 */
static void assoc_draw(tiku_window_t *w, tiku_surface_t *s,
                       tiku_rect_t c, void *ctx)
{
    struct desktop_associations *p = ctx;
    assoc_layout_t l = assoc_layout(c);
    const tiku_font_t *f = tiku_font_plain();
    (void)w;
    assoc_sync(p);
    assoc_controls(p, l);
    tiku_fill(s, c, TIKU_C_PANEL);
    tiku_icon_paint(s, "folder_prefs", c.x + 18, c.y + 12, 32);
    tiku_text(s, tiku_font_bold(), c.x + 62, c.y + 12 + f->ascent,
              "File Associations", TIKU_C_TEXT);
    assoc_line(s, (tiku_rect_t){c.x + 62, c.y + 32, c.w - 80, f->height},
        "Choose a file type, then the application that opens it.",
        tiku_dim(TIKU_C_TEXT, TIKU_C_PANEL));
    tiku_text(s, tiku_font_bold(), l.types.x, l.types.y - 8,
              "File type", TIKU_C_TEXT);
    tiku_text(s, tiku_font_bold(), l.apps.x, l.apps.y - 8,
              "Opens with", TIKU_C_TEXT);
    tiku_ui_sunken(s, l.types, TIKU_C_DOC);
    tiku_ui_sunken(s, l.apps, TIKU_C_DOC);
    for (int i = p->page; i < p->model.count && i < p->page + ASSOC_PAGE; i++) {
        tiku_rect_t r = assoc_row(l.types, i - p->page);
        if (i == p->selected) tiku_fill(s, r, TIKU_C_SELECT);
        assoc_line(s, tiku_inset(r, 4), p->model.row[i].label,
                   i == p->selected ? TIKU_C_SELTEXT : TIKU_C_TEXT);
        if (tiku_dialog_focused(&p->dialog, i))
            tiku_frame(s, tiku_inset(r, 1), TIKU_C_FOCUS);
    }
    for (int i = p->app_page; i <= p->count &&
         i < p->app_page + ASSOC_PAGE && p->model.count; i++) {
        tiku_rect_t r = assoc_row(l.apps, i - p->app_page);
        const char *chosen = p->model.row[p->selected].chosen;
        const char *name = i ? p->candidates[i - 1].app->name : "";
        char label[80];
        snprintf(label, sizeof label, i ? "%s" : "Automatic (%s)",
            i ? name : p->count ? p->candidates[0].app->name : "none");
        tiku_ui_radio(s, r, NULL, (!strcmp(chosen, name) ? TIKU_S_ON : 0) |
                                   assoc_flags(p, ASSOC_APP + i));
        r.x += 26; r.w -= 30;
        assoc_line(s, r, label, TIKU_C_TEXT);
    }
    tiku_ui_button(s, l.previous, "<", assoc_flags(p, ASSOC_PREV));
    tiku_ui_button(s, l.next, ">", assoc_flags(p, ASSOC_NEXT));
    tiku_ui_button(s, l.app_previous, "<", assoc_flags(p, ASSOC_APP_PREV));
    tiku_ui_button(s, l.app_next, ">", assoc_flags(p, ASSOC_APP_NEXT));
    assoc_detail(p, s, l);
    tiku_ui_button(s, l.cancel, "Cancel", assoc_flags(p, ASSOC_CANCEL));
    tiku_ui_button(s, l.save, "Save", TIKU_S_DEFAULT |
                    assoc_flags(p, ASSOC_SAVE));
}

/** @brief Reconcile clean rows with saved values while preserving unsaved draft preconditions. */
static void assoc_sync(struct desktop_associations *p)
{
    int i;
    if (p->saving) return;
    for (i = 0; i < p->model.count; i++) {
        tiku_trk_association_t *row = &p->model.row[i];
        if (!strcmp(row->saved, row->chosen)) {
            tiku_trk_launch_preferred(p->app->store, row->type, row->saved, sizeof row->saved);
            strcpy(row->chosen, row->saved);
        }
    }
}

/** @brief Close the submitted draft after persistence, or retain it with its error. */
static void assoc_saved(void *context, tiku_resource_error_t error)
{
    struct desktop_associations *p = context;
    p->saving = 0;
    if (p->generation != p->submitted_generation) return;
    if (!error) desktop_associations_close(p->app);
    else snprintf(p->status, sizeof p->status, error == TIKU_RESOURCE_STALE ?
        "Associations changed. Reopen this window before saving." :
        error == TIKU_RESOURCE_UNKNOWN_OUTCOME ?
        "The save outcome is uncertain. Reopen to check the saved associations." :
        "Could not save. Your previous associations are unchanged.");
    desktop_damage_all(p->app);
}

/** @brief Mouse and keyboard edit a draft; only Save changes associations. */
static int assoc_event(tiku_window_t *w, const tiku_event_t *ev, void *ctx)
{
    struct desktop_associations *p = ctx;
    assoc_controls(p, assoc_layout(tiku_window_content(w)));
    int id = tiku_dialog_event(&p->dialog, ev).activated;
    if (ev->type == TIKU_EVENT_KEY_DOWN && (ev->modifiers & TIKU_MOD_CMD)) {
        if (ev->key == 's') id = ASSOC_SAVE;
        if (ev->key == 'w' || ev->key == 'q') id = ASSOC_CANCEL;
    }
    if (p->saving) {
        if (id == ASSOC_CANCEL) desktop_associations_close(p->app);
        return 1;
    }
    if (id >= 0 && id < p->model.count) {
        p->selected = id; assoc_candidates(p);
    } else if (id >= ASSOC_APP && id <= ASSOC_APP + p->count &&
               p->model.count) {
        int i = id - ASSOC_APP;
        snprintf(p->model.row[p->selected].chosen,
            sizeof p->model.row[p->selected].chosen, "%s",
            i ? p->candidates[i - 1].app->name : "");
        p->status[0] = 0;
    }
    if (id == ASSOC_PREV) p->page -= ASSOC_PAGE;
    if (id == ASSOC_NEXT) p->page += ASSOC_PAGE;
    if (id == ASSOC_APP_PREV) p->app_page -= ASSOC_PAGE;
    if (id == ASSOC_APP_NEXT) p->app_page += ASSOC_PAGE;
    if (id == ASSOC_CANCEL) desktop_associations_close(p->app);
    if (id == ASSOC_SAVE && tiku_trk_associations_changed(&p->model)) {
        if (p->app->catalog_settings) {
            if (!tiku_trk_catalog_settings_submit(p->app->catalog_settings, &p->model, assoc_saved, p)) {
                p->saving = 1; p->submitted_generation = p->generation;
                snprintf(p->status, sizeof p->status, "Saving associations...");
            } else snprintf(p->status, sizeof p->status, "Could not start saving. Retry after other settings finish.");
        } else if (tiku_trk_associations_save(&p->model, p->app->store))
            desktop_associations_close(p->app);
        else snprintf(p->status, sizeof p->status,
            "Could not save. Your previous associations are unchanged.");
    }
    desktop_damage_all(p->app);
    return 1;
}

/** @brief The File Associations window, or NULL. */
tiku_window_t *desktop_associations_handle(desktop_t *app)
{
    return app->associations ? app->associations->window : NULL;
}

/** @brief Close the window; unsaved choices are dropped and the store is
 *         not touched. */
void desktop_associations_close(desktop_t *app)
{
    tiku_window_t *w = desktop_associations_handle(app);
    if (app->prefs_track == w) app->prefs_track = NULL;
    if (w && w->open) tiku_workspace_close(app->desk, w);
    if (app->associations) { app->associations->window = NULL; app->associations->generation++; }
    desktop_damage_all(app);
}

/** @brief Open the window with the saved associations loaded, or raise it
 *         when it is open. */
void desktop_associations_open(desktop_t *app)
{
    if (!app->associations) {
        app->associations = calloc(1, sizeof *app->associations);
        if (!app->associations) return;
        app->associations->app = app;
    }
    struct desktop_associations *p = app->associations;
    if (p->window && p->window->open) {
        tiku_workspace_activate(app->desk, p->window); return;
    }
    tiku_trk_associations_load(&p->model, &app->apps, &app->types, app->store);
    p->generation++;
    p->selected = p->page = 0; p->status[0] = 0;
    tiku_dialog_init(&p->dialog);
    assoc_candidates(p);
    tiku_rect_t r = {0, 0, 720, 0};
    assoc_layout_t l = assoc_layout(r);
    r.h = l.save.y + l.save.h + 44;
    r.x = (app->surf->w - r.w) / 2;
    r.y = (app->surf->h - r.h) / 3;
    p->window = tiku_workspace_open(app->desk, "File Associations", r,
                                    assoc_draw, p, NULL);
    if (p->window) {
        tiku_window_set_size_controls(p->window, 0, 0);
        tiku_window_set_handlers(p->window, assoc_event, NULL, NULL);
    }
    desktop_damage_all(app);
}

/** @brief Release the window and its unsaved draft at desktop shutdown. */
void desktop_associations_destroy(desktop_t *app)
{
    desktop_associations_close(app);
    free(app->associations); app->associations = NULL;
}
