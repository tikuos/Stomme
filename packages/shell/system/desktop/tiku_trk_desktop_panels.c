/*
 * Desktop shell and file manager.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_desktop_panels.c - the panels and the opening of things: panels
 * a device describes (.panel files and a board's gui/ forms), opening an
 * entry, Open With and the Info window's Opens with, Find and its results,
 * Select, and the file panel's questions, answers and remote picks.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_shell_product.h"
#include "tiku_trk_desktop_private.h"

/* Where list_children() collects a directory's entries. */
struct collect_ctx { tiku_model_t *out; int n, max; };

/**
 * @brief Rebuild for @p fl the Open With rows for its selection, the
 *        destination pickers, and the add-ons menu for the types in its
 *        listing; with @p fl NULL, the empty ones.
 */
void
build_open_with(desktop_t *app, folder_t *fl)
{
    tiku_trk_folder_menu_build_open_with(&app->folder_menu, fl);
    build_pickers(app, fl);
    if (fl != NULL && fl->view != NULL) {
        const char *types[TIKU_TRK_APP_TYPES_MAX];
        int n = tiku_trk_poseview_types_present(fl->view, types,
                                                TIKU_TRK_APP_TYPES_MAX);
        (void)tiku_trk_addons_build_menu(&app->addons, types, n,
                                         &app->menus.addons,
                                         TIKU_TRK_ADDON_COMMAND_BASE);
    } else {
        (void)tiku_trk_addons_build_menu(&app->addons, NULL, 0,
                                         &app->menus.addons,
                                         TIKU_TRK_ADDON_COMMAND_BASE);
    }
}

/**
 * @brief Hand @p paths to @p application through the launch controller,
 *        which reports the result on the status line (AW-039).
 *
 * A double click on a document, a drop on an application and the Open With
 * window launch through here.
 *
 * @return the controller's result.
 */
int
launch_paths(desktop_t *app, const tiku_trk_app_t *application,
             const char *const *paths, int count)
{
    return tiku_trk_launch_controller_run(&app->launcher, application, paths,
                                          count);
}

/** @brief The application a document should open in, or NULL (AW-030). */
static const tiku_trk_app_t *
handler_for(desktop_t *app, const tiku_model_t *model)
{
    return tiku_trk_launch_controller_handler(&app->launcher, model);
}

/**
 * @brief The path @p row is bound to, in the namespace: a /gui/ bind of a
 *        board's panel is put under the board's mount root.
 */
const char *
bind_path(const tiku_trk_described_t *p, const tiku_form_row_t *row,
          char *buf, size_t max)
{
    if (p->root[0] != '\0' && strncmp(row->bind, "/gui/", 5) == 0) {
        snprintf(buf, max, "%.*s%s", (int)(max / 2), p->root, row->bind);
        return buf;
    }
    return row->bind;
}

/**
 * @brief Read every bound row's value from its place; a row whose place
 *        does not answer shows no value.
 */
static void
panel_refresh(tiku_trk_described_t *p)
{
    desktop_t *app = p->app;
    char path[TIKU_PATH_MAX];
    int i, asked = 0, answered = 0;

    for (i = 0; i < p->form.nrow; i++) {
        tiku_form_row_t *row = &p->form.row[i];

        if (row->bind[0] == '\0' || row->kind == TIKU_FORM_BUTTON) {
            continue;
        }
        asked++;
        if (tiku_backend_text(app->backend,
                              bind_path(p, row, path, sizeof path),
                              row->value, sizeof row->value)) {
            answered++;
            /* A device ends a value with a newline or blanks: they are
             * trimmed. */
            size_t n = strlen(row->value);

            while (n > 0u && (row->value[n - 1u] == '\n' ||
                              row->value[n - 1u] == '\r' ||
                              row->value[n - 1u] == ' ')) {
                row->value[--n] = '\0';
            }
            row->known = 1;
        } else {
            row->known = 0;     /* no answer: the row shows no value */
        }
    }
    panel_refreshed(p, asked, answered);
}

/**
 * @brief End a refresh of @p p, however its values were read: when no bound
 *        place answered, the panel's own status line says "not answering";
 *        that status clears once any place answers.
 */
void
panel_refreshed(tiku_trk_described_t *p, int asked, int answered)
{
    p->quiet = (asked > 0 && answered == 0);
    if (p->quiet) {
        snprintf(p->status, sizeof p->status, "%s", "not answering");
    } else if (strcmp(p->status, "not answering") == 0) {
        p->status[0] = '\0';
    }
}

/**
 * @brief Paint a described panel: its form, and at its foot its status (the
 *        last refusal or "not answering") or, with none, a note when the
 *        device sent rows this build cannot draw.
 */
static void
panel_draw(tiku_window_t *window, tiku_surface_t *surface,
           tiku_rect_t content, void *context)
{
    tiku_trk_described_t *p = context;
    const tiku_font_t *f = tiku_font_plain();
    char note[112];

    (void)window;
    tiku_fill(surface, content, TIKU_C_PANEL);
    tiku_form_draw(&p->form, surface, content);
    note[0] = '\0';
    if (p->status[0] != '\0') {
        snprintf(note, sizeof note, "%s", p->status);
    } else if (p->unknown > 0) {
        /* The rows this build cannot draw are counted in a note. */
        snprintf(note, sizeof note, "%d row(s) this build does not know",
                 p->unknown);
    }
    if (note[0] != '\0') {
        tiku_text(surface, f, content.x + 8, content.y + content.h - 6,
                  note, tiku_dim(TIKU_C_TEXT, TIKU_C_PANEL));
    }
}

/* Used before it is defined. */
static int panel_track(tiku_trk_described_t *p, tiku_window_t *window,
                       const tiku_event_t *event);
/* End of the forward declarations. */

/**
 * @brief A press on a described panel: the row under it is pressed, and
 *        what it writes goes to the device; a refusal shows in the panel's
 *        own status line.
 * @return 1 when a row took the press.
 */
static int
panel_event(tiku_window_t *window, const tiku_event_t *event, void *context)
{
    tiku_trk_described_t *p = context;
    desktop_t *app = p->app;
    tiku_rect_t content = tiku_window_content(window);
    const char *write;
    int row;

    if (p->tracking) {
        return panel_track(p, window, event);
    }
    if (event->type != TIKU_EVENT_POINTER_DOWN) {
        return 0;
    }
    row = tiku_form_at(&p->form, content, event->x, event->y);
    if (row < 0) {
        return 0;
    }
    if (p->form.row[row].kind == TIKU_FORM_ENTRY) {
        /* An entry takes the caret; nothing is written until Return. */
        tiku_form_focus(&p->form, row);
        desktop_damage_all(app);
        return 1;
    }
    if (tiku_form_editing(&p->form) >= 0) {
        tiku_form_focus(&p->form, -1);      /* a press on another row
                                             * takes the entry's caret */
    }
    if (p->form.row[row].kind == TIKU_FORM_SLIDER) {
        /* The knob follows the pointer, and the value is written once,
         * when the button is released. */
        (void)tiku_form_press_at(&p->form, content, row, event->x);
        p->tracking = 1;
        p->pending = row;
        desktop_damage_all(app);
        return 1;
    }
    write = tiku_form_press(&p->form, row);
    if (write != NULL) {
        /*
         * A control writes its value to the place it is bound to, on a
         * later turn by panels_pump(), so a slow cable does not hold the
         * press.
         */
        p->pending = row;
        desktop_damage_all(app);
    }
    return 1;
}

/** @brief The slider's knob follows a move; after the release,
 *         panels_pump() writes its value. */
static int
panel_track(tiku_trk_described_t *p, tiku_window_t *window,
            const tiku_event_t *event)
{
    tiku_rect_t content = tiku_window_content(window);
    int row = p->pending;

    if (row < 0 || row >= p->form.nrow) {
        p->tracking = 0;
        return 0;
    }
    (void)tiku_form_press_at(&p->form, content, row, event->x);
    if (event->type == TIKU_EVENT_POINTER_UP) {
        p->tracking = 0;        /* pending stays: the pump writes it */
    }
    desktop_damage_all(p->app);
    return 1;
}

/**
 * @brief Write a row's value to the place it is bound to, and read the
 *        panel back, on a worker: a board's write can take seconds.
 */
static void
panel_write(tiku_trk_described_t *p, int row)
{
    if (!desktop_wire_panel_write(p->app, p, row)) {
        snprintf(p->status, sizeof p->status, "%s: %s",
                 p->form.row[row].label, "nothing serves it");
        tiku_sound_play(TIKU_SOUND_BEEP);
        desktop_damage_all(p->app);
    }
}

int
panel_key(desktop_t *app, unsigned key, char ch)
{
    tiku_trk_described_t *p =
        panel_of_window(app, tiku_workspace_focused(app->desk));
    int row, took;

    if (p == NULL) {
        return 0;
    }
    row = tiku_form_editing(&p->form);
    if (row < 0) {
        return 0;
    }
    took = tiku_form_key(&p->form, key, ch);
    if (took == 2) {
        p->pending = row;
    }
    desktop_damage_all(app);
    return took != 0;
}

void
panels_pump(desktop_t *app)
{
    int i;

    for (i = 0; i < TIKU_TRK_PANELS_MAX; i++) {
        tiku_trk_described_t *p = &app->panels[i];

        if (p->window != NULL && p->window->open && p->pending >= 0 &&
            !p->tracking) {
            int row = p->pending;

            p->pending = -1;
            panel_write(p, row);
            return;             /* one write per turn */
        }
    }
}

tiku_trk_described_t *
panel_tracking(desktop_t *app)
{
    int i;

    for (i = 0; i < TIKU_TRK_PANELS_MAX; i++) {
        if (app->panels[i].tracking && app->panels[i].window != NULL &&
            app->panels[i].window->open) {
            return &app->panels[i];
        }
    }
    return NULL;
}

tiku_trk_described_t *
panel_of_window(desktop_t *app, const tiku_window_t *w)
{
    int i;

    for (i = 0; w != NULL && i < TIKU_TRK_PANELS_MAX; i++) {
        if (app->panels[i].window == w && w->open) {
            return &app->panels[i];
        }
    }
    return NULL;
}

/** @brief The open panel describing @p path, or NULL. */
static tiku_trk_described_t *
panel_of_path(desktop_t *app, const char *path)
{
    int i;

    for (i = 0; i < TIKU_TRK_PANELS_MAX; i++) {
        tiku_trk_described_t *p = &app->panels[i];

        if (p->window != NULL && p->window->open &&
            strcmp(p->path, path) == 0) {
            return p;
        }
    }
    return NULL;
}

/** @brief A free panel slot, else the first, whose panel then closes for
 *         the new one. */
static tiku_trk_described_t *
panel_slot(desktop_t *app)
{
    int i;

    for (i = 0; i < TIKU_TRK_PANELS_MAX; i++) {
        tiku_trk_described_t *p = &app->panels[i];

        if (p->window == NULL || !p->window->open) {
            return p;
        }
    }
    return &app->panels[0];
}
#if TIKU_SHELL_BOARD_SERVICES


/**
 * @brief The prefix a board's binds are relative to: for
 *        .../gui/<form>/panel the part before /gui/, else "" (a .panel
 *        file binds absolutely).
 */
static void
root_of(const char *path, char *root, size_t max)
{
    const char *gui = strstr(path, "/gui/");
    size_t n = strlen(path);

    root[0] = '\0';
    if (gui != NULL && n >= 6 && strcmp(path + n - 6, "/panel") == 0) {
        snprintf(root, max, "%.*s", (int)(gui - path), path);
    }
}
#endif


int
open_panel_path(desktop_t *app, const char *path)
{
    char text[4096];
    tiku_rect_t frame;
    tiku_trk_described_t *p;
    const char *name;
    int rows, unknown;
    tiku_form_t form;

    if (path == NULL ||
        !tiku_backend_text(app->backend, path, text, sizeof text)) {
        return 0;
    }
    rows = tiku_form_parse(&form, text, &unknown);
    if (rows == 0 && unknown == 0) {
        return 0;               /* no rows: not a panel */
    }
    p = panel_of_path(app, path);
    if (p != NULL) {
        /* Open already: it is brought forward with what it says now. */
        p->form = form;
        p->unknown = unknown;
        panel_refresh(p);
        tiku_workspace_activate(app->desk, p->window);
        desktop_damage_all(app);
        return 1;
    }
    p = panel_slot(app);
    if (p->window != NULL && p->window->open) {
        tiku_workspace_close(app->desk, p->window);
    }
    memset(p, 0, sizeof *p);
    p->app = app;
    p->form = form;
    p->unknown = unknown;
    p->pending = -1;
    snprintf(p->path, sizeof p->path, "%s", path);
#if TIKU_SHELL_BOARD_SERVICES

    root_of(path, p->root, sizeof p->root);
#endif

    panel_refresh(p);
    name = strrchr(path, '/');
    name = (name != NULL) ? name + 1 : path;
    frame.w = 320;
    frame.h = tiku_form_height(&p->form) + tiku_window_tab_h() +
              2 * TIKU_WINDOW_BORDER + 14;
    frame.x = (app->surf->w - frame.w) / 2 +
              (int)(p - app->panels) * 24;
    frame.y = (app->surf->h - frame.h) / 3 + (int)(p - app->panels) * 24;
    p->window = tiku_workspace_open(app->desk,
        p->form.title[0] != '\0' ? p->form.title : name, frame,
        panel_draw, p, p);
    if (p->window != NULL) {
        tiku_window_set_handlers(p->window, panel_event, NULL, NULL);
    }
    desktop_damage_all(app);
    return p->window != NULL;
#if TIKU_SHELL_BOARD_SERVICES

}

/** @brief One listing callback: open the panel of each form under gui/. */
static int
open_form_entry(const tiku_model_t *m, void *ctx)
{
    desktop_t *app = ctx;
    char path[TIKU_PATH_MAX];

    if (m == NULL || m->name[0] == '.' || !tiku_model_is_container(m)) {
        return 0;
    }
    snprintf(path, sizeof path, "%.*s/panel", (int)sizeof path - 8, m->path);
    (void)open_panel_path(app, path);
    return 0;
}

/**
 * @brief Keep one record per mount but the root, with the identity of its
 *        gui/ node looked up once; a mount with no gui/ node gets ~0.
 */
static void
boards_refresh(desktop_t *app)
{
    int slots = tiku_mount_slots(app->backend), i;

    for (i = 0; i < slots && i < TIKU_MOUNT_MAX; i++) {
        const char *prefix = tiku_mount_prefix_at(app->backend, i);
        tiku_model_t m;
        char dir[TIKU_PATH_MAX];

        if (prefix == NULL || (prefix[0] == '/' && prefix[1] == '\0')) {
            app->boards[i].root[0] = '\0';
            continue;
        }
        if (strcmp(app->boards[i].root, prefix) != 0) {
            memset(&app->boards[i], 0, sizeof app->boards[i]);
            snprintf(app->boards[i].root, TIKU_PATH_MAX, "%s", prefix);
        }
        if (app->boards[i].gui_id != 0 || app->backend->ops->stat == NULL) {
            continue;
        }
        snprintf(dir, sizeof dir, "%s/gui", prefix);
        app->boards[i].gui_id =
            (app->backend->ops->stat(app->backend, dir, &m) == 0 &&
             m.node_id != 0) ? m.node_id : ~(uint64_t)0;
    }
}

/**
 * @brief Board @p b's gui/ changed: re-read its open panels, or open its
 *        forms the first time it is seen.
 * @return how many panels changed.
 */
static int
panels_apply_board(desktop_t *app, int b)
{
    const char *root = app->boards[b].root;
    int changed = 0, any = 0, i;

    if (root[0] == '\0') {
        return 0;
    }
    for (i = 0; i < TIKU_TRK_PANELS_MAX; i++) {
        tiku_trk_described_t *p = &app->panels[i];

        if (p->window != NULL && p->window->open &&
            strcmp(p->root, root) == 0) {
            panel_refresh(p);
            any = 1;
            changed++;
        }
    }
    if (!any && !app->boards[b].announced &&
        app->backend->ops->list != NULL) {
        char dir[TIKU_PATH_MAX];

        app->boards[b].announced = 1;
        snprintf(dir, sizeof dir, "%s/gui", root);
        (void)app->backend->ops->list(app->backend, dir, open_form_entry,
                                      app);
        changed++;
    }
    return changed;
}

/** @brief Apply a record whose id names a board's gui/ node. */
static int
panels_apply(desktop_t *app, const tiku_model_change_t *c)
{
    int b;

    for (b = 0; b < TIKU_MOUNT_MAX; b++) {
        if (app->boards[b].root[0] != '\0' &&
            app->boards[b].gui_id == c->id) {
            return panels_apply_board(app, b);
        }
    }
    return 0;
}

int
panels_poll(desktop_t *app, const tiku_model_change_t *ch, int nch)
{
    int changed = 0, k, b;

    boards_refresh(app);
    for (k = 0; k < nch; k++) {
        changed += panels_apply(app, &ch[k]);
    }
    if (nch >= 0) {
        return changed;
    }
    /*
     * The namespace could not say what changed (a local root with no
     * change ring beside a board with one): each board's own last answer,
     * as its worker brought it, says whether its gui/ changed.
     */
    for (b = 0; b < TIKU_MOUNT_MAX; b++) {
        if (app->boards[b].root[0] == '\0' ||
            app->boards[b].gui_id == ~(uint64_t)0) {
            continue;
        }
        if (desktop_wire_changed(app, b)) {
            changed += panels_apply_board(app, b);
        }
    }
    return changed;
#endif

}

/**
 * @brief Open the panel a file describes: a .panel file, or a board's
 *        gui/<form>/panel node.
 * @return 1 when a panel opened.
 */
static int
open_described_panel(desktop_t *app, const tiku_model_t *model)
{
    const char *dot = strrchr(model->name, '.');

    if (dot != NULL && strcmp(dot, ".panel") == 0) {
        return open_panel_path(app, model->path);
    }
    if (strcmp(model->name, "panel") == 0 &&
        strstr(model->path, "/gui/") != NULL) {
        return open_panel_path(app, model->path);
    }
    return 0;
}

/**
 * @brief Open @p model: a described panel as a panel; a document type in
 *        its handler; an installed application, or a link to one, started;
 *        an executable run as itself; anything else in its type's handler.
 * @return 1 when something opened.
 */
int
open_one(desktop_t *app, const tiku_model_t *model)
{
    tiku_trk_app_t itself;
    const char *one[1];

    if (model == NULL) {
        return 0;
    }
    if (model->name[0] != '\0' && open_described_panel(app, model)) {
        return 1;
    }
    one[0] = model->path;
    {
        /* A document opens for editing even with its executable bit set:
         * the type database says which types are documents. */
        const tiku_trk_typerec_t *rec = tiku_trk_types_find(&app->types,
                                                            model->type);

        if (rec != NULL && rec->document) {
            return launch_paths(app, handler_for(app, model), one, 1);
        }
    }
    {
        /* An installed application, or a link to one: the runner starts
         * it, as the Deskbar's row would; its module is not a program
         * of its own to exec. */
        const tiku_trk_app_t *installed = desktop_app_at(app, model->path);

        if (installed != NULL) {
            return tiku_trk_launch_run(installed, NULL, 0) ==
                   TIKU_TRK_LAUNCH_OK;
        }
    }
    if (tiku_trk_launch_self(model, &itself)) {
        /* The application itself: it is started with no documents. */
        return launch_paths(app, &itself, NULL, 0);
    }
    return launch_paths(app, handler_for(app, model), one, 1);
}

/**
 * @brief Fill an Info window's "Opens with" row (AW-071): the application
 *        stored as preferred for the type, else "Default application";
 *        empty for a folder or an executable.
 */
void
info_opens_with_fill(desktop_t *app, tiku_trk_info_t *info)
{
    const char *pref;

    info->opens_with[0] = '\0';
    if (info == NULL || tiku_model_is_container(&info->model) ||
        (info->model.facts.perm & TIKU_P_EXEC) != 0u) {
        return;
    }
    char prefbuf[48];

    pref = tiku_trk_launch_preferred(app->store, info->model.type, prefbuf,
                                     sizeof prefbuf);
    if (pref != NULL && pref[0] != '\0') {
        snprintf(info->opens_with, sizeof info->opens_with, "%s", pref);
    } else {
        snprintf(info->opens_with, sizeof info->opens_with,
                 "Default application");
    }
}

/** @brief Refresh open Info views after a committed or failed preference save. */
static void info_preferred_saved(void *context, tiku_resource_error_t error)
{
    desktop_t *app = context;
    int i;
    for (i = 0; i < TIKU_TRK_INFO_WINDOW_MAX; i++)
        if (app->info_windows.window[i] && app->info_windows.window[i]->open)
            info_opens_with_fill(app, &app->info_windows.model[i]);
    snprintf(app->status, sizeof app->status, "%s", error == TIKU_RESOURCE_STALE ?
        "Associations changed. Retry your choice." : error == TIKU_RESOURCE_UNKNOWN_OUTCOME ?
        "Save outcome unknown. Check File Associations." : error ?
        "Could not save the preferred application." : "File association saved.");
    desktop_damage_all(app);
}

/** @brief Walk "Opens with" to the next candidate and keep it (AW-071). */
void
info_opens_with_next(desktop_t *app, tiku_trk_info_t *info)
{
    const char *types[1];
    tiku_trk_cand_t cand[16];
    const char *current;
    int count, i, at = -1;

    types[0] = info->model.type;
    /* The candidates are ranked without the stored preference (no
     * store), so the walk visits each in turn and then the default. */
    count = tiku_trk_launch_candidates(&app->apps, types, 1, NULL,
                                       cand, 16);
    if (count == 0) {
        snprintf(app->status, sizeof app->status,
                 "no application claims %s", info->model.type);
        return;
    }
    char curbuf[48];

    current = tiku_trk_launch_preferred(app->store, info->model.type,
                                        curbuf, sizeof curbuf);
    for (i = 0; i < count; i++) {
        if (current != NULL && strcmp(cand[i].app->name, current) == 0) {
            at = i;
        }
    }
    if (app->catalog_settings) {
        tiku_resource_error_t error = tiku_trk_catalog_settings_prefer(app->catalog_settings,
            types, 1, at + 1 < count ? cand[at + 1].app->name : "", info_preferred_saved, app);
        snprintf(app->status, sizeof app->status, "%s", error ?
            "Could not start saving. Retry after other settings finish." :
            "Saving the preferred application...");
        desktop_damage_all(app);
        return;
    }
    if (at + 1 < count) {
        (void)tiku_trk_launch_set_preferred(app->store, info->model.type,
                                            cand[at + 1].app->name);
        snprintf(app->status, sizeof app->status, "%s opens with %s",
                 info->model.name, cand[at + 1].app->name);
    } else {
        /* Past the last candidate the choice returns to the ranking's
         * own: "Default application". */
        (void)tiku_trk_launch_set_preferred(app->store, info->model.type,
                                            "");
        snprintf(app->status, sizeof app->status,
                 "%s opens with the default application",
                 info->model.name);
    }
    (void)tiku_store_flush(app->store);
    info_opens_with_fill(app, info);
}

/** @brief The Open With window's launch, through launch_paths()
 *         (AW-039). */
void
open_with_window_launch(void *context, const tiku_trk_app_t *application,
                        const char *const *paths, int count)
{
    desktop_t *app = context;

    (void)launch_paths(app, application, paths, count);
}

/** @brief Open the selection with the candidate at @p at (AW-040). */
void
open_with_pick(desktop_t *app, folder_t *fl, int at)
{
    tiku_trk_launch_controller_pick(&app->launcher, fl, at);
}

/** @brief One listed entry copied into the collector; full stops the list. */
static int
collect_one(const tiku_model_t *m, void *ctx)
{
    struct collect_ctx *c = ctx;

    if (c->n >= c->max) {
        return 1;
    }
    c->out[c->n++] = *m;
    return 0;
}

/**
 * @brief The entries of @p path from backend @p b, at most @p max, into
 *        @p out.  @return how many.
 */
int
list_children(tiku_backend_t *b, const char *path, tiku_model_t *out,
              int max)
{
    struct collect_ctx c = { out, 0, max };

    if (b == NULL || b->ops->list == NULL) {
        return 0;
    }
    (void)b->ops->list(b, path, collect_one, &c);
    return c.n;
}

/**
 * @brief Open a live results window titled @p title on @p run, which the
 *        window takes over; when no window opens, the run is stopped.
 * @return the window's folder, or NULL.
 */
static folder_t *
open_results(desktop_t *app, const char *title, tiku_qrun_t *run)
{
    return tiku_trk_folder_lifecycle_open_results(&app->folder_lifecycle,
                                                   title, run);
}

/**
 * @brief Start a re-walk of a live results window's query; the turn steps
 *        it and applies the difference when it ends.
 * @return nonzero when a re-walk began.
 */
int
refresh_results(desktop_t *app, folder_t *fl)
{
    return tiku_trk_folder_lifecycle_refresh_results(
        &app->folder_lifecycle, fl);
}

/**
 * @brief Apply a Select expression to the folder it was opened from.
 * @return the folder's window; NULL when the window has gone or the
 *         expression is bad.
 */
tiku_window_t *
apply_select_model(void *context, void *target,
                   const tiku_trk_select_t *selection)
{
    desktop_t *app = context;
    folder_t *folder = target;
    char error[96];
    int count;

    if (folder == NULL || folder->win == NULL || !folder->win->open) {
        snprintf(app->status, sizeof app->status,
                 "the window this was opened from has gone");
        return NULL;
    }
    count = tiku_trk_poseview_select_matching(folder->view,
        selection->expr.text, tiku_trk_select_rule(selection->rule),
        selection->ignore_case, selection->invert, error, sizeof error);
    if (count < 0) {
        /* A bad expression is a stop alert, closeable with Escape, and it
         * selects nothing (PVK-047). */
        tiku_sound_play(TIKU_SOUND_BEEP);
        tiku_alert_open(alert_model(app), TIKU_ALERT_STOP,
                            TIKU_TRK_ALERT_TAG_REFUSAL, error, "Cancel");
        return NULL;
    }
    snprintf(app->status, sizeof app->status, "selected %d item%s", count,
             count == 1 ? "" : "s");
    return folder->win;
}

/** @brief Open the Find panel, or bring it forward. */
void
open_panel(desktop_t *app)
{
    tiku_trk_find_window_open(&app->find_window);
}

/**
 * @brief Start the Find panel's query and open its live results window; the
 *        query is saved as a record, temporary unless it replaces a named
 *        one.
 * @return 1 when a results window opened.
 */
int
panel_search(void *context, tiku_trk_find_t *panel)
{
    desktop_t *app = context;
    tiku_query_t q;
    tiku_qrun_t *run;
    char title[TIKU_PATH_MAX];
    char scope[64];
    char record_name[TIKU_NAME_MAX];
    int replacing;

    tiku_trk_find_build(panel, &q);
    if (q.invalid) {
        snprintf(app->status, sizeof app->status,
                 "invalid query formula");
        desktop_damage_all(app);
        return 0;
    }
    /* The Trash path is resolved at run time: it belongs to this machine
     * and volume, not to the saved record (Q-036). */
    (void)tiku_fs_trash_dir(app->fs, desktop_folder(app)->path,
                                q.trash_path,
                                sizeof q.trash_path);
    tiku_query_bind_scopes(&q, app->backend);
    run = tiku_query_start(app->backend, &q);
    if (run == NULL) {
        return 0;
    }
    /* The run is stepped one turn at a time by
     * tiku_trk_folder_lifecycle_step_results(): the window opens empty
     * and fills as the run finds entries. */
    (void)tiku_trk_find_scope_label(panel, scope, sizeof scope);
    replacing = panel->loaded_kind == TIKU_QREC_QUERY &&
                panel->loaded_name[0] != '\0';
    record_name[0] = '\0';
    {
        /* Every run is saved as a record, so it can be reopened and
         * edited; a temporary one is reaped later by desktop_sweep()
         * (Q-023, MA-053). */
        tiku_query_t previous;
        int i;

        q.temporary = 1;
        if (replacing) {
            snprintf(record_name, sizeof record_name, "%s",
                     panel->loaded_name);
            if (tiku_query_load(&previous, record_name, app->store) ==
                    0) {
                q.temporary = previous.temporary;
            }
            /* The old window owns the layout being inherited.  Close it
             * first so its frame and columns reach the store before the
             * replacement reads them (Q-025). */
            for (i = 0; i < app->folder_count; i++) {
                if (app->folders[i].win != NULL &&
                    app->folders[i].win->open &&
                    strcmp(app->folders[i].qname, record_name) == 0) {
                    close_window(app, app->folders[i].win);
                }
            }
        }
        /* Wall-clock time, so a record's age holds across a restart; its
         * default name carries the date too. */
        q.saved_at = (int64_t)time(NULL);
        if (record_name[0] != '\0' ||
            tiku_trk_find_default_name(panel, (long)q.saved_at,
                                       record_name,
                                       sizeof record_name) > 0) {
            (void)tiku_query_save(&q, record_name, app->store);
            snprintf(app->last_query, sizeof app->last_query, "%s",
                     record_name);
        }
    }
    snprintf(title, sizeof title, "find #%d", ++app->find_serial);
    {
        folder_t *fl = open_results(app, title, run);

        if (fl != NULL) {
            snprintf(fl->qname, sizeof fl->qname, "%s", app->last_query);
            snprintf(fl->qscope, sizeof fl->qscope, "%s", scope);
            /* Set once: the results window's count bar shows the walk's
             * progress. */
            snprintf(app->status, sizeof app->status,
                     "searching %.30s -- 0 results so far", scope);
        } else {
            return 0;
        }
    }
    desktop_damage_all(app);
    return 1;
}

/**
 * @brief Run the typed search, walked to the end here, and open its results
 *        in a window of their own.  The scope is the folder the search
 *        started from.
 */
void
run_find(desktop_t *app, folder_t *from)
{
    tiku_query_t q;
    tiku_qrun_t *run;
    char title[TIKU_PATH_MAX];
    folder_t *fl;

    tiku_query_init(&q);
    tiku_query_set_name(&q, app->find_buf);
    if (from != NULL) {
        (void)tiku_query_add_scope(&q, from->path);
    }
    (void)tiku_fs_trash_dir(app->fs,
                                (from != NULL) ? from->path
                                               : desktop_folder(app)->path,
                                q.trash_path, sizeof q.trash_path);
    run = tiku_query_start(app->backend, &q);
    if (run == NULL) {
        return;
    }
    while (!tiku_query_done(run)) {
        if (tiku_query_step(run, 8) < 0) {
            break;
        }
    }
    /* The window is titled with the search and a serial number, so two
     * searches open two windows. */
    snprintf(title, sizeof title, "find:%.40s#%d", app->find_buf,
             ++app->find_serial);
    fl = open_folder(app, title);
    if (fl != NULL) {
        int i, n = tiku_query_count(run);
        tiku_model_t *set = calloc((size_t)(n > 0 ? n : 1), sizeof *set);

        if (set != NULL) {
            for (i = 0; i < n; i++) {
                set[i] = *tiku_query_at(run, i);
            }
            (void)tiku_trk_poseview_set_results(fl->view, set, n);
            free(set);
        }
        snprintf(app->status, sizeof app->status,
                 "%d result%s for \"%.30s\" in %.40s", n, (n == 1) ? "" : "s",
                 app->find_buf, (from != NULL) ? from->path : "/");
    }
    tiku_query_stop(run);
    desktop_damage_all(app);
}

/**
 * @brief Host directory @p path as a path of the desktop's namespace,
 *        through the local (posix) mount, into @p out.
 * @return nonzero when there is a local mount and it fits.
 */
static int
local_picker_start(desktop_t *app, const char *path, char *out, size_t max)
{
    if (!path || path[0] != '/') path = "/";
    if (!tiku_mount_is(app->backend)) {
        if (!app->backend || !app->backend->ops ||
            strcmp(app->backend->ops->name, "posix")) return 0;
        return snprintf(out, max, "%s", path) < (int)max;
    }
    for (int i = 0; i < tiku_mount_slots(app->backend); i++) {
        tiku_backend_t *b = tiku_mount_at(app->backend, i);
        const char *prefix = tiku_mount_prefix_at(app->backend, i);
        if (!b || !b->ops || strcmp(b->ops->name, "posix") || !prefix)
            continue;
        return snprintf(out, max, "%s%s",
            !strcmp(prefix, "/") ? "" : prefix, path) < (int)max;
    }
    return 0;
}

/** @brief Whether the question up is one @p window of @p session put. */
static int
pick_is_from(const desktop_t *app, int session, uint32_t window)
{
    return app->pending_pick.kind == 2 &&
           app->pending_pick.session == session &&
           app->pending_pick.generation ==
               app->remote.session[session].generation &&
           app->pending_pick.window == window;
}

/**
 * @brief A remote application asks for the file panel: note who asked (a
 *        reused session is never answered) and show the panel, in the
 *        local mount for a local pick.
 *
 * One application question at a time: the window whose question is up may
 * ask a newer one, which replaces it; another application's ask is
 * ignored while one is up, and every ask is ignored while the desktop's
 * own question is up.
 */
void
desktop_remote_pick(desktop_t *app, int session, const tiku_msg_t *msg)
{
    int32_t window = 0, mode = TIKU_APP_PICK_OPEN;
    const char *start = tiku_msg_find_string(msg, "start", 0);
    const char *name = tiku_msg_find_string(msg, "name", 0);
    char mapped[TIKU_PATH_MAX];

    if (session < 0 || session >= TIKU_REMOTE_SESSIONS ||
        !app->remote.session[session].used)
        return;
    (void)tiku_msg_find_int32(msg, "window", 0, &window);
    (void)tiku_msg_find_int32(msg, "mode", 0, &mode);
    if (panel_asks_desktop(app) ||
        (app->pending_pick.kind &&
         !pick_is_from(app, session, (uint32_t)window)))
        return;
    if (mode < TIKU_APP_PICK_OPEN || mode > TIKU_APP_PICK_LOCAL_FOLDER)
        return;
    if (mode == TIKU_APP_PICK_LOCAL_OPEN ||
        mode == TIKU_APP_PICK_LOCAL_FOLDER) {
        if (!local_picker_start(app, start, mapped, sizeof mapped)) {
            snprintf(app->status, sizeof app->status,
                     "No local filesystem is available for this application");
            desktop_damage_all(app);
            return;
        }
        start = mapped;
    }
    app->pending_pick.kind = 2;
    app->pending_pick.session = session;
    app->pending_pick.generation = app->remote.session[session].generation;
    app->pending_pick.slot = NULL;
    app->pending_pick.window = (uint32_t)window;
    app->pending_pick.local = mode == TIKU_APP_PICK_LOCAL_OPEN ||
                              mode == TIKU_APP_PICK_LOCAL_FOLDER;
    if (mode == TIKU_APP_PICK_SAVE)
        tiku_trk_panel_window_open_save(&app->file_panel,
            &app->pending_pick, TIKU_TRK_CMD_APP_PICK, start, name);
    else if (mode == TIKU_APP_PICK_LOCAL_FOLDER)  /* a folder question */
        tiku_trk_panel_window_open(&app->file_panel, &app->pending_pick,
                                   TIKU_TRK_CMD_APP_PICK, start);
    else
        tiku_trk_panel_window_open_file(&app->file_panel,
            &app->pending_pick, TIKU_TRK_CMD_APP_PICK, start, name);
    desktop_damage_all(app);
}

/**
 * @brief Tell whoever asked what the person chose, and forget the ask: an
 *        embedded application through its descriptor, a remote one over its
 *        session unless the session was reused.
 *
 * The note is cleared even when the asker has gone, and a @p path of NULL
 * tells nobody.  The panel's Cancel does not call here: pick_sweep()
 * clears the note once the panel has closed.
 */
static void
deliver_pick(desktop_t *app, const char *path)
{
    int kind = app->pending_pick.kind;
    struct desktop_embed *slot = app->pending_pick.slot;
    int session = app->pending_pick.session;
    uint32_t window = app->pending_pick.window;
    uint64_t generation = app->pending_pick.generation;
    int local_only = app->pending_pick.local;
    char local[TIKU_PATH_MAX];

    app->pending_pick.kind = 0;
    app->pending_pick.slot = NULL;
    if (path == NULL) {
        return;
    }
    if (local_only) {
        tiku_backend_t *b = tiku_backend_serving(app->backend, path,
                                                local, sizeof local);
        if (!b || !b->ops || strcmp(b->ops->name, "posix")) {
            snprintf(app->status, sizeof app->status,
                     "This app opens local files; copy the device file first");
            desktop_damage_all(app);
            return;
        }
        path = local;
    }
    if (kind == 1 && slot != NULL && slot->running &&
        slot->descriptor != NULL && slot->descriptor->picked != NULL) {
        slot->descriptor->picked(slot->state, window, path);
        desktop_damage_all(app);
    } else if (kind == 2 && session >= 0 && session < TIKU_REMOTE_SESSIONS &&
               app->remote.session[session].used &&
               app->remote.session[session].generation == generation) {
        tiku_msg_t *m = tiku_msg_new(TIKU_MSG_PICKED);

        if (m != NULL) {
            (void)tiku_msg_add_int32(m, "window", (int32_t)window);
            (void)tiku_msg_add_string(m, "path", path);
            (void)tiku_remote_tell(&app->remote, session, m);
            tiku_msg_free(m);
        }
    }
}

/**
 * @brief The file panel's answer for @p command: open in the reader or the
 *        image application, an application's pick, a sound, a Find scope, a
 *        saved query, a replacement link target, or the selection of
 *        @p target's folder moved or copied to @p destination.
 */
void
fpanel_commit(void *context, void *target, int command,
              const char *destination)
{
    desktop_t *app = context;
    folder_t *folder = target;
    char paths[16][TIKU_PATH_MAX];
    const char *selection[16];
    int count;

    if (command == TIKU_TRK_CMD_READER_OPEN) {
        if (destination != NULL) (void)desktop_reader_file(app, destination);
        return;
    }
    if (command == TIKU_TRK_CMD_IMAGE_OPEN) {
        if (destination != NULL) (void)desktop_image_file(app, destination);
        return;
    }
    if (command == TIKU_TRK_CMD_APP_PICK) {
        /*
         * An application's question: the panel carried an opaque target,
         * and deliver_pick() reads the desktop's note of who asked.
         */
        deliver_pick(app, destination);
        return;
    }
    if (command == TIKU_TRK_CMD_CHOOSE_SOUND && destination != NULL) {
        tiku_trk_sounds_window_chosen(&app->sounds_window, app->sound_pick,
                                      destination);
        return;
    }
    if (command == TIKU_TRK_CMD_ADD_SCOPE && destination != NULL) {
        /* The chosen folder becomes a place and the search is scoped to
         * it at once (Q-019). */
        if (tiku_trk_find_scope_to_folder(&app->find_window.model,
                                          destination)) {
            snprintf(app->status, sizeof app->status,
                     "scoped to %.100s", destination);
        }
        return;
    }
    if (command == TIKU_TRK_CMD_SAVE_QUERY && destination != NULL) {
        /* The panel already checked the destination (AW-052); what is
         * left is the writing: the query's saved text as a real file,
         * and the same record under the file's name so the Queries menu
         * knows it too. */
        char text[TIKU_QUERY_PRED_MAX * 2];
        char err[160];
        const char *base = strrchr(destination, '/');
        int n = tiku_query_serialise(&app->pending_query, text,
                                         sizeof text);

        if (n < 0 || app->backend->ops->write == NULL ||
            app->backend->ops->write(app->backend, destination, text,
                                     (size_t)n, err, sizeof err) != 0) {
            snprintf(app->status, sizeof app->status,
                     "could not save the query");
            return;
        }
        (void)tiku_query_save(&app->pending_query,
                                  base != NULL ? base + 1 : destination,
                                  app->store);
        (void)tiku_store_flush(app->store);
        snprintf(app->status, sizeof app->status, "query saved");
        return;
    }
    if (command == TIKU_TRK_CMD_GET_INFO) {
        int slot;
        char err[160];

        /* The answer is for the link the picker was opened for: its Info
         * window is found by path, so a window closed or replaced while
         * the picker was up re-points nothing. */
        for (slot = 0; slot < TIKU_TRK_INFO_WINDOW_MAX; slot++) {
            if (app->info_windows.model[slot].used &&
                app->info_relink_path[0] != '\0' &&
                strcmp(app->info_windows.model[slot].path,
                       app->info_relink_path) == 0) {
                break;
            }
        }
        app->info_relink_path[0] = '\0';
        if (destination == NULL || slot >= TIKU_TRK_INFO_WINDOW_MAX) {
            return;
        }
        err[0] = '\0';
        if (tiku_trk_info_relink(&app->info_windows.model[slot],
                                 app->backend, destination, err,
                                 sizeof err) != 0) {
            snprintf(app->status, sizeof app->status, "%s",
                     err[0] != '\0' ? err : "could not set the link target");
        } else {
            snprintf(app->status, sizeof app->status,
                     "link now points to %.120s", destination);
        }
        desktop_damage_all(app);
        return;
    }
    if (folder == NULL || destination == NULL) {
        return;
    }
    count = folder_selection(app, folder, paths, selection, 16);
    if (count > 0) {
        if (command == TIKU_TRK_CMD_COPY_TO) {
            (void)tiku_fs_copy(app->fs, selection, count, destination,
                                   TIKU_CONFLICT_ASK);
            snprintf(app->status, sizeof app->status,
                     "copying %d item%s", count, count == 1 ? "" : "s");
        } else {
            (void)tiku_fs_move(app->fs, selection, count, destination,
                                   TIKU_CONFLICT_ASK);
            snprintf(app->status, sizeof app->status,
                     "moving %d item%s", count, count == 1 ? "" : "s");
        }
        tiku_trk_recent_add(app->store, TIKU_TRK_RECENT_FOLDERS,
                            destination);
    }
}

/** @brief Find's 'Select folders...': the panel chooses a scope (Q-019). */
void
find_select_folder(void *context)
{
    desktop_t *app = context;

    tiku_trk_panel_window_open(&app->file_panel, app,
                               TIKU_TRK_CMD_ADD_SCOPE, "/");
}

/** @brief The Sounds window's "…": any file at all for @p event. */
void
sound_choose(void *context, tiku_sound_event_t event)
{
    desktop_t *app = context;
    const char *home = getenv("HOME");

    app->sound_pick = event;
    tiku_trk_panel_window_open_file(&app->file_panel, &app->sounds_window,
                                    TIKU_TRK_CMD_CHOOSE_SOUND,
                                    (home != NULL && home[0] != '\0')
                                        ? home : "/",
                                    NULL);
}

/**
 * @brief Open @p path's local file in TikuReader; a path not on the local
 *        mount is refused.
 * @return 1 when the reader started.
 */
int desktop_reader_file(desktop_t *app, const char *path)
{
    char local[TIKU_PATH_MAX];
    tiku_backend_t *b = tiku_backend_serving(app->backend, path, local,
                                            sizeof local);
    if (!b || !b->ops || strcmp(b->ops->name, "posix")) return 0;
    return desktop_launch_app(app, "reader", local);
}

/**
 * @brief The launcher's document opener: run @p application on the local
 *        file of @p path, when that is a readable regular file on the local
 *        mount and the application is installed.
 * @return 1 when it started.
 */
int desktop_document_open(void *ctx, const tiku_trk_app_t *application,
                           const char *path)
{
    desktop_t *app = ctx;
    char local[TIKU_PATH_MAX];
    struct stat sb;
    tiku_backend_t *b = tiku_backend_serving(app->backend, path,
                                            local, sizeof local);
    if (!b || !b->ops || strcmp(b->ops->name, "posix") ||
        local[0] != '/' || access(local, R_OK) ||
        access(application->cmd, X_OK) || access(application->module, R_OK) ||
        stat(local, &sb) || !S_ISREG(sb.st_mode))
        return 0;
    const char *paths[] = {local};
    return tiku_trk_launch_run(application, paths, 1) == TIKU_TRK_LAUNCH_OK;
}

/**
 * @brief Open host file @p path through the local mount, whatever is at the
 *        namespace's root.
 * @return 1 when something opened.
 */
int desktop_document_local(desktop_t *app, const char *path)
{
    if (!path || path[0] != '/' || strlen(path) >= TIKU_PATH_MAX) return 0;
    char mapped[TIKU_PATH_MAX];
    if (!tiku_mount_is(app->backend)) {
        if (!app->backend || strcmp(app->backend->ops->name, "posix"))
            return 0;
        return desktop_app_open_path(app, path);
    }
    for (int i = 0; i < tiku_mount_slots(app->backend); i++) {
        tiku_backend_t *b = tiku_mount_at(app->backend, i);
        const char *prefix = tiku_mount_prefix_at(app->backend, i);
        if (!b || !b->ops || strcmp(b->ops->name, "posix") || !prefix)
            continue;
        if (!strcmp(prefix, "/")) return desktop_app_open_path(app, path);
        if (snprintf(mapped, sizeof mapped, "%s%s", prefix, path) >=
            (int)sizeof mapped) return 0;
        return desktop_app_open_path(app, mapped);
    }
    return 0;
}

/**
 * @brief Open @p path's local file in the image application; a path not on
 *        the local mount is refused.
 * @return 1 when it started.
 */
int desktop_image_file(desktop_t *app, const char *path)
{
    char local[TIKU_PATH_MAX];
    tiku_backend_t *b = tiku_backend_serving(app->backend, path, local,
                                            sizeof local);
    if (!b || !b->ops || strcmp(b->ops->name, "posix")) return 0;
    return desktop_launch_app(app, "image", local);
}

/** @brief Find's Save with no destination: ask for one with the file
 *         panel. */
void
find_save_as(void *context)
{
    desktop_t *app = context;
    const char *home = getenv("HOME");

    tiku_trk_find_build(&app->find_window.model, &app->pending_query);
    tiku_trk_panel_window_open_save(&app->file_panel, app,
                                    TIKU_TRK_CMD_SAVE_QUERY,
                                    (home != NULL && home[0] != '\0')
                                        ? home : "/",
                                    "query");
}

/** @brief Ask with the file panel where @p command (Move to, Copy to)
 *         puts @p folder's selection. */
void
fpanel_open(desktop_t *app, folder_t *folder, int command)
{
    if (folder != NULL) {
        tiku_trk_panel_window_open(&app->file_panel, folder, command,
                                   folder->path);
    }
}

/**
 * @brief Select the row called @p name in @p fl, and scroll it into view.
 * @return the row's index, or -1.
 */
int
select_named(desktop_t *app, folder_t *fl, const char *name)
{
    int i, n;

    if (fl == NULL || name == NULL || name[0] == '\0') {
        return -1;
    }
    n = tiku_trk_poseview_count(fl->view);
    for (i = 0; i < n; i++) {
        tiku_trk_pose_t *p = tiku_trk_poseview_at(fl->view, i);

        if (p != NULL && strcmp(p->model.name, name) == 0) {
            tiku_trk_poseview_click(fl->view, i, 0u);
            /* Scrolled as little as needed: a row already on screen does
             * not move (NeXT UIG ch3). */
            tiku_trk_poseview_scroll_into_view(fl->view, i,
                tiku_trk_poseview_visible_rows(fl->view));
            desktop_damage_all(app);
            return i;
        }
    }
    return -1;
}

/**
 * @brief Make the user's icon folder (TIKU_ICONS, else
 *        ~/.config/tracker/icons) and load its icons, and make the
 *        desktop's Desktop folder.
 */
void
prepare_places(desktop_t *app)
{
    const char *home = getenv("HOME");
    char path[TIKU_PATH_MAX];
    const char *icons = getenv("TIKU_ICONS");

    (void)app;
    if (icons == NULL || icons[0] == '\0') {
        if (home == NULL || home[0] == '\0') {
            return;
        }
        snprintf(path, sizeof path, TIKU_SHELL_USER_ICONS_PATH, home);
        icons = path;
    }
    (void)tiku_state_mkparents(icons);
    (void)mkdir(icons, 0755);
    /* An icon there replaces the built-in one of the same name. */
    (void)tiku_icons_load_dir(icons);

    {
        /* The desktop's own Desktop folder (tiku_desktop_dir()), not the
         * login session's. */
        char desktop[TIKU_PATH_MAX];

        (void)tiku_desktop_dir(desktop, sizeof desktop);
        (void)tiku_state_mkparents(desktop);
        (void)mkdir(desktop, 0755);
    }
}
#if TIKU_SHELL_BOARD_SERVICES

#else


int
panels_poll(desktop_t *app, const tiku_model_change_t *changes, int count)
{
    int refreshed = 0;
    (void)changes;
    if (count == 0) return 0;
    for (int i = 0; i < TIKU_TRK_PANELS_MAX; i++) {
        tiku_trk_described_t *p = &app->panels[i];
        if (p->window != NULL && p->window->open) {
            panel_refresh(p);
            refreshed++;
        }
    }
    return refreshed;
}
#endif
