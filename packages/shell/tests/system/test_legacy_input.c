/* Real desktop text routing and legacy controllers, without a filesystem.
 * SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include "../../system/desktop/tiku_trk_desktop_events.c"
/** @brief The painter is not linked: a full frame is the flag, as before. */
void desktop_damage_all(desktop_t *app) { app->dirty = 1; }

static folder_t folder;

struct embed_win *embed_win_of_window(desktop_t *app, const tiku_window_t *w)
{ (void)app; (void)w; return NULL; }
int embed_deliver(struct embed_win *w, const tiku_event_t *e)
{ (void)w; (void)e; assert(0); return 0; }
void embed_stop(struct desktop_embed *slot)
{ (void)slot; assert(0); }
tiku_window_t *desktop_deviceprefs_handle(desktop_t *app)
{ (void)app; return NULL; }
int desktop_utility_owns(desktop_t *app, tiku_window_t *w)
{ (void)app; (void)w; return 0; }
tiku_trk_described_t *panel_of_window(desktop_t *app, const tiku_window_t *w)
{ return app->panels[0].window == w ? &app->panels[0] : NULL; }
tiku_trk_folder_window_t *tiku_trk_folder_lifecycle_find(
    tiku_trk_folder_lifecycle_t *c, tiku_window_t *w)
{ (void)c; return folder.win == w ? &folder : NULL; }

static void commit(desktop_t *app, const char *text)
{
    tiku_text_input_t input;
    tiku_event_t e;
    assert(tiku_text_input_set(&input, text, strlen(text)));
    while (tiku_text_input_next(&input, &e)) desktop_text_input(app, &e);
}

int main(void)
{
    desktop_t *app = calloc(1, sizeof *app);
    assert(app);
    app->surf = tiku_surface_new(800, 600, TIKU_C_PANEL);
    app->desk = tiku_workspace_new(app->surf);
    assert(app->surf && app->desk);
    for (int i = 0; i < TIKU_REMOTE_SESSIONS; i++) app->remote.session[i].fd = -1;
    app->info_windows.workspace = app->desk;
    app->info_windows.renaming = -1;
    app->file_panel.workspace = app->desk;
    app->find_window.workspace = app->desk;
    app->select_window.workspace = app->desk;
    tiku_window_t *one = tiku_workspace_open(app->desk, "One",
        (tiku_rect_t){30,30,400,300}, NULL, NULL, NULL);
    tiku_window_t *two = tiku_workspace_open(app->desk, "Two",
        (tiku_rect_t){40,40,400,300}, NULL, NULL, NULL);
    assert(one && two);

    /* Committing text must edit the Save name, not confirm the save. */
    tiku_trk_panel_window_t *p = &app->file_panel;
    p->window = one; p->dirty = &app->dirty;
    p->model.mode = TIKU_TRK_PANEL_SAVE;
    p->model.focus = TIKU_TRK_PANEL_FOCUS_NAME;
    tiku_field_start(&p->name, "original");
    snprintf(p->model.name, sizeof p->model.name, "original");
    tiku_workspace_activate(app->desk, one);
    commit(app, "café 日本語.txt");
    assert(!strcmp(p->name.text, "café 日本語.txt"));
    assert(!strcmp(p->model.name, p->name.text) && one->open);
    tiku_field_key(&p->name, TIKU_FIELD_SELECT_ALL, 0, 0);
    char too_long[160]; memset(too_long, 'x', sizeof too_long - 1);
    too_long[sizeof too_long - 1] = 0;
    commit(app, too_long);
    assert(!strcmp(p->model.name, "café 日本語.txt"));
    assert(p->name.sel_a == 0 && p->name.sel_b == p->name.len);
    tiku_clipboard_set(too_long);
    assert(tiku_trk_panel_window_key(p, 'v', TIKU_MOD_CMD, 0));
    /* A refused paste is not a key that did nothing: the panel marks it
     * so the shell owning the status line can say so, once. */
    assert(tiku_trk_panel_window_refused(p));
    assert(!tiku_trk_panel_window_refused(p));
    assert(!strcmp(p->name.text, "café 日本語.txt"));
    /* A clipboard the FIELD refuses is marked the same way. */
    tiku_clipboard_set("two\nlines");
    assert(tiku_trk_panel_window_key(p, 'v', TIKU_MOD_CMD, 0));
    assert(tiku_trk_panel_window_refused(p));
    assert(!strcmp(p->name.text, "café 日本語.txt"));
    assert(!strcmp(p->model.name, p->name.text));
    assert(p->name.sel_a == 0 && p->name.sel_b == p->name.len);
    p->model.focus = TIKU_TRK_PANEL_FOCUS_LIST;
    commit(app, "ignored");
    assert(!strcmp(p->name.text, "café 日本語.txt"));
    p->model.focus = TIKU_TRK_PANEL_FOCUS_NAME;

    /* Changing windows between chunks must never insert the tail elsewhere. */
    tiku_text_input_t input;
    tiku_event_t e;
    assert(tiku_text_input_set(&input, "long composed café", 19));
    assert(tiku_text_input_next(&input, &e)); desktop_text_input(app, &e);
    tiku_workspace_activate(app->desk, two);
    while (tiku_text_input_next(&input, &e)) desktop_text_input(app, &e);
    assert(!strcmp(p->name.text, "café 日本語.txt"));
    tiku_workspace_activate(app->desk, one);
    app->status_overlay.alert.open = 1;
    commit(app, "ignored");
    assert(!strcmp(p->name.text, "café 日本語.txt"));
    app->status_overlay.alert.open = 0;
    p->window = NULL;
    puts("PASS: Save-name routing, capacity, focus changes and modal isolation");

    tiku_trk_find_init(&app->find_window.model);
    app->find_window.window = one;
    app->find_window.model.focus = TIKU_TRK_FIND_FOCUS_NAME;
    commit(app, "café 日本語");
    assert(!strcmp(app->find_window.model.name_text, "café 日本語"));
    tiku_trk_find_backspace(&app->find_window.model);
    assert(!strcmp(app->find_window.model.name_text, "café 日本"));
    app->find_window.model.focus = TIKU_TRK_FIND_FOCUS_FORMULA;
    commit(app, "name == café");
    assert(!strcmp(app->find_window.model.formula, "name == café"));
    app->find_window.model.focus = TIKU_TRK_FIND_FOCUS_ROW;
    app->find_window.model.focus_row = 0;
    commit(app, "é");
    assert(!strcmp(app->find_window.model.row[0].value, "é"));
    app->find_window.window = NULL;
    app->select_window.window = one;
    app->select_window.field = 1; /* Expression hit target. */
    tiku_field_start(&app->select_window.model.expr, "*");
    commit(app, "café*");
    assert(!strcmp(app->select_window.model.expr.text, "café*") && one->open);
    app->select_window.window = NULL;
    puts("PASS: Find name/formula/attribute and Select expressions accept Unicode");

    app->info_windows.window[0] = one;
    app->info_windows.model[0].used = 1;
    app->info_windows.renaming = 0;
    tiku_field_start(&app->info_windows.name, "old");
    tiku_field_deny(&app->info_windows.name, "/");
    commit(app, "café");
    assert(!strcmp(app->info_windows.name.text, "café"));
    commit(app, "/bad");
    assert(!strcmp(app->info_windows.name.text, "café"));
    app->info_windows.window[0] = NULL;

    folder.win = one;
    tiku_trk_folder_edit_t *f = &app->folder_edit;
    f->dirty = &app->dirty; f->renaming = 1; f->rename_folder = &folder;
    tiku_field_start(&f->rename_edit, "old");
    commit(app, "日本語");
    assert(!strcmp(f->rename_edit.text, "日本語") && f->renaming);
    tiku_field_set_editable(&f->rename_edit, 0);
    commit(app, "read-only");
    assert(!strcmp(f->rename_edit.text, "日本語"));
    tiku_workspace_activate(app->desk, two);
    commit(app, "wrong window");
    assert(!strcmp(f->rename_edit.text, "日本語"));
    f->renaming = 0; f->editing = 1; f->value_folder = &folder;
    tiku_field_start(&f->value_edit, "keep");
    commit(app, "wrong value");
    assert(!strcmp(f->value_edit.text, "keep"));
    assert(!tiku_trk_folder_edit_key(f, NULL, 'x', 0, 'x'));
    assert(!strcmp(f->value_edit.text, "keep"));
    tiku_workspace_activate(app->desk, one);
    commit(app, "é");
    assert(!strcmp(f->value_edit.text, "é") && f->editing);
    f->editing = 0;
    app->panels[0].window = one;
    assert(tiku_form_parse(&app->panels[0].form, "entry Name /data/name", NULL) == 1);
    tiku_form_focus(&app->panels[0].form, 0);
    commit(app, "café");
    assert(!strcmp(app->panels[0].form.row[0].edit, "café"));
    assert(!*app->panels[0].form.row[0].value);
    tiku_form_key(&app->panels[0].form, TIKU_KEY_BACKSPACE, 0);
    assert(!strcmp(app->panels[0].form.row[0].edit, "caf"));
    puts("PASS: rename/value ownership and described entries edit without external writes");
    tiku_workspace_free(app->desk);
    tiku_surface_free(app->surf);
    free(app);
    return 0;
}
