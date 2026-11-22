/* Real desktop text routing and legacy controllers, without a filesystem.
 * SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include "../../system/desktop/tiku_trk_desktop_events.c"
#include "../../system/shared/ui/tiku_trk_responder.c"
/** @brief The painter is not linked: a full frame is the flag, as before. */
void desktop_damage_all(desktop_t *app) { app->dirty = 1; }

static folder_t folder;

/** @brief The damage moves asked for, joined as the painter joins it. */
static tiku_rect_t moved;
void desktop_damage(desktop_t *app, tiku_rect_t rect)
{ (void)app; moved = tiku_rect_union(moved, rect); }
void desktop_take_damage(desktop_t *app, tiku_rect_t damage)
{ (void)app; (void)damage; }
void desktop_pointer_says(desktop_t *app, int x, int y)
{ (void)app; (void)x; (void)y; }
#if TIKU_SHELL_BOARD_SERVICES
void desktop_devtray_hover(desktop_t *app, int was_x, int was_y)
{ (void)app; (void)was_x; (void)was_y; }
#endif

/** @brief A folder's move handler: the drag goes where the pointer is. */
static int follow(void *self, const tiku_event_t *ev, tiku_rect_t *damage)
{
    desktop_t *app = self;

    (void)tiku_trk_drag_moved(&app->drag, ev->x, ev->y);
    *damage = tiku_trk_damage_none();
    return 1;
}

/* The presses and releases that reach the surfaces, for the lens test. */
static int lens_downs, lens_ups, lens_x, lens_y;
/** @brief The Deskbar is not linked: it has no frame here. */
tiku_rect_t tiku_trk_deskbar_frame(const tiku_trk_deskbar_t *db)
{ (void)db; return (tiku_rect_t){ 0, 0, 0, 0 }; }
int tiku_trk_deskbar_raised(const tiku_trk_deskbar_t *db)
{ (void)db; return 0; }
int tiku_trk_deskbar_menu_open(const tiku_trk_deskbar_t *db)
{ (void)db; return 0; }
int tiku_trk_deskbar_popup_at(const tiku_trk_deskbar_t *db, int x, int y,
                              tiku_rect_t *frame)
{ (void)db; (void)x; (void)y; (void)frame; return 0; }
/** @brief The global menu bar is not linked: it is off here. */
tiku_rect_t global_bar_rect(desktop_t *app)
{ (void)app; return (tiku_rect_t){ 0, 0, 0, 0 }; }
/** @brief A surface that takes every press. */
static int everywhere(void *self, int x, int y)
{ (void)self; (void)x; (void)y; return 1; }
/** @brief Count a press and where it landed. */
static int seen_down(void *self, const tiku_event_t *ev, tiku_rect_t *damage)
{
    (void)self;
    lens_downs++; lens_x = ev->x; lens_y = ev->y;
    *damage = tiku_trk_damage_none();
    return 1;
}
/** @brief Count a release and where it landed. */
static int seen_up(void *self, const tiku_event_t *ev, tiku_rect_t *damage)
{
    (void)self;
    lens_ups++; lens_x = ev->x; lens_y = ev->y;
    *damage = tiku_trk_damage_none();
    return 1;
}

/** @brief Whether two rectangles are the same. */
static int same(tiku_rect_t a, tiku_rect_t b)
{
    return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}

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

    /* A move in a file drag damages the drag image's old and new boxes;
     * the move that starts the drag damages the new one. */
    tiku_trk_responder_t folders = { .name = "folders", .self = app,
                                     .move = follow };
    tiku_trk_responders_init(&app->responders);
    assert(tiku_trk_responders_push(&app->responders, &folders) == 0);
    tiku_trk_drag_arm(&app->drag, 100, 100, 1u, 0u, 1, NULL, "/tmp");
    assert(tiku_trk_drag_add(&app->drag, "/tmp/a.txt") == 0);
    tiku_event_t mv = { .type = TIKU_EVENT_POINTER_MOVE, .x = 120, .y = 100 };
    desktop_pointer_move(app, &mv, NULL);
    assert(tiku_trk_drag_active(&app->drag));
    assert(same(moved, (tiku_rect_t){ 104, 84, 50, 50 }));
    moved = (tiku_rect_t){ 0, 0, 0, 0 };
    mv.x = 160; mv.y = 130;
    desktop_pointer_move(app, &mv, NULL);
    assert(same(moved, (tiku_rect_t){ 104, 84, 90, 80 }));
    moved = (tiku_rect_t){ 0, 0, 0, 0 };
    desktop_pointer_move(app, &mv, NULL);
    assert(moved.w == 0);
    puts("PASS: a file drag's moves damage the drag image where it was and is");

    /* An open Select window left with a smaller face's height takes the
     * current face's again, its foot kept on the screen. */
    tiku_trk_select_window_init(&app->select_window, app->desk, 800, 600,
                                &app->dirty, NULL, NULL);
    tiku_trk_select_window_open(&app->select_window, &folder, 100, 100);
    tiku_window_t *sel = tiku_trk_select_window_handle(&app->select_window);
    assert(sel);
    int fits = sel->frame.h;
    sel->frame.h = fits - 20;
    tiku_trk_select_window_refit(&app->select_window);
    assert(sel->frame.h == fits && sel->frame.y == app->select_window.model.y);
    sel->frame.h = fits - 20;
    sel->frame.y = 600 - sel->frame.h;
    tiku_trk_select_window_refit(&app->select_window);
    assert(sel->frame.h == fits && sel->frame.y + sel->frame.h == 600);
    puts("PASS: an open Select window follows the face's height");

    /* The touch lens opens on a tap near a window's close box and stays
     * open when the finger lifts; a second press in its view picks the
     * point under it, the aim follows the finger, and the release clicks
     * there. */
    folder.win = NULL;   /* the stub folder has no view to measure */
    tiku_trk_responder_t clicks = { .name = "clicks", .self = app,
                                    .hit = everywhere, .press = seen_down,
                                    .release = seen_up };
    tiku_trk_responders_init(&app->responders);
    assert(tiku_trk_responders_push(&app->responders, &clicks) == 0);
    app->lens.used = 1;
    int bx = -1, by = -1, rx = -1, ry = -1;
    for (int y = 0; y < 480; y++) {
        for (int x = 0; x < 800; x++) {
            tiku_window_t *hw = NULL;
            tiku_hit_t hit = tiku_workspace_hit(app->desk, x, y, &hw);

            if (hit == TIKU_HIT_CLOSE && bx < 0) {
                bx = x;
                by = y;
            }
            if (hit == TIKU_HIT_RESIZE && rx < 0) {
                rx = x;
                ry = y;
            }
        }
    }
    assert(bx >= 0 && rx >= 0);
    tiku_event_t tap = { .type = TIKU_EVENT_POINTER_DOWN, .button = 1,
                         .x = bx + 6, .y = by + 6 };
    moved = (tiku_rect_t){ 0, 0, 0, 0 };
    desktop_pointer_down(app, &tap, NULL);
    assert(app->lens.shown && lens_downs == 0);
    assert(same(moved, app->lens.box) && moved.w > 0);
    /* A wander inside the slop leaves it a tap. */
    tap.type = TIKU_EVENT_POINTER_MOVE;
    tap.x += DESKTOP_LENS_SLOP;
    desktop_pointer_move(app, &tap, NULL);
    assert(app->lens.shown && lens_downs == 0);
    tap.type = TIKU_EVENT_POINTER_UP;
    desktop_pointer_up(app, &tap, NULL);
    assert(app->lens.shown && lens_downs == 0 && lens_ups == 0);
    /* A second tap on the control leaves the lens open. */
    tap = (tiku_event_t){ .type = TIKU_EVENT_POINTER_DOWN, .button = 1,
                          .x = bx + 6 + DESKTOP_LENS_AGAIN, .y = by + 6 };
    desktop_pointer_down(app, &tap, NULL);
    tap.type = TIKU_EVENT_POINTER_UP;
    desktop_pointer_up(app, &tap, NULL);
    assert(app->lens.shown && lens_downs == 0 && lens_ups == 0);
    tiku_rect_t view = tiku_inset(app->lens.box, 2);
    int lens_s = DESKTOP_LENS_SCALE;
    /* The pick lands three view pixels to the right of the close box and
     * slides back onto it. */
    tap = (tiku_event_t){ .type = TIKU_EVENT_POINTER_DOWN, .button = 1,
        .x = view.x + (bx + 3 - app->lens.area.x) * lens_s + 1,
        .y = view.y + (by - app->lens.area.y) * lens_s + 1 };
    desktop_pointer_down(app, &tap, NULL);
    assert(app->lens.picking && app->lens.ax == bx + 3 &&
           app->lens.ay == by && lens_downs == 0);
    tap.type = TIKU_EVENT_POINTER_MOVE;
    tap.x -= 3 * lens_s;
    desktop_pointer_move(app, &tap, NULL);
    assert(app->lens.ax == bx && app->lens.ay == by);
    tap.type = TIKU_EVENT_POINTER_UP;
    tap.time_us = 1000000;
    desktop_pointer_up(app, &tap, NULL);
    assert(!app->lens.shown && !app->lens.held);
    assert(lens_downs == 1 && lens_ups == 1 && lens_x == bx && lens_y == by);
    /* A second tap on the lens's place, soon after its click, reaches
     * nothing; a later one does. */
    tap.type = TIKU_EVENT_POINTER_DOWN;
    tap.time_us = 1000000 + DESKTOP_LENS_GUARD_US - 1;
    desktop_pointer_down(app, &tap, NULL);
    tap.type = TIKU_EVENT_POINTER_UP;
    desktop_pointer_up(app, &tap, NULL);
    assert(!app->lens.shown && lens_downs == 1 && lens_ups == 1);
    tap.type = TIKU_EVENT_POINTER_DOWN;
    tap.time_us = 1000000 + DESKTOP_LENS_GUARD_US;
    desktop_pointer_down(app, &tap, NULL);
    tap.type = TIKU_EVENT_POINTER_UP;
    desktop_pointer_up(app, &tap, NULL);
    assert(!app->lens.shown && lens_downs == 2 && lens_ups == 2);
    /* A pick that slides out of the view clicks nothing; the lens stays. */
    tap = (tiku_event_t){ .type = TIKU_EVENT_POINTER_DOWN, .button = 1,
                          .x = bx, .y = by };
    desktop_pointer_down(app, &tap, NULL);
    tap.type = TIKU_EVENT_POINTER_UP;
    desktop_pointer_up(app, &tap, NULL);
    view = tiku_inset(app->lens.box, 2);
    tap = (tiku_event_t){ .type = TIKU_EVENT_POINTER_DOWN, .button = 1,
                          .x = view.x + 4, .y = view.y + 4 };
    desktop_pointer_down(app, &tap, NULL);
    assert(app->lens.picking);
    tap.type = TIKU_EVENT_POINTER_MOVE;
    tap.x = view.x + view.w + 20;
    desktop_pointer_move(app, &tap, NULL);
    assert(!app->lens.picking);
    tap.type = TIKU_EVENT_POINTER_UP;
    desktop_pointer_up(app, &tap, NULL);
    assert(app->lens.shown && lens_downs == 2 && lens_ups == 2);
    /* A press away from the lens and from the control closes it and
     * reaches nothing. */
    tap = (tiku_event_t){ .type = TIKU_EVENT_POINTER_DOWN, .button = 1,
                          .x = view.x + view.w + 20, .y = view.y + 4 };
    desktop_pointer_down(app, &tap, NULL);
    assert(!app->lens.shown);
    tap.type = TIKU_EVENT_POINTER_UP;
    desktop_pointer_up(app, &tap, NULL);
    assert(!app->lens.held && lens_downs == 2 && lens_ups == 2);
    /* A press that opens the lens and then travels is a drag: the lens
     * closes and the press reaches the surfaces where it landed. */
    tap = (tiku_event_t){ .type = TIKU_EVENT_POINTER_DOWN, .button = 1,
                          .x = bx + 6, .y = by + 6 };
    desktop_pointer_down(app, &tap, NULL);
    assert(app->lens.shown && lens_downs == 2);
    tap.type = TIKU_EVENT_POINTER_MOVE;
    tap.x += DESKTOP_LENS_SLOP + 1;
    desktop_pointer_move(app, &tap, NULL);
    assert(!app->lens.shown && !app->lens.held);
    assert(lens_downs == 3 && lens_x == bx + 6 && lens_y == by + 6);
    tap.type = TIKU_EVENT_POINTER_UP;
    desktop_pointer_up(app, &tap, NULL);
    assert(lens_ups == 3);
    /* The resize corner is dragged, not clicked: no lens.  Nor one for a
     * press away from small controls. */
    tap = (tiku_event_t){ .type = TIKU_EVENT_POINTER_DOWN, .button = 1,
                          .x = rx, .y = ry };
    desktop_pointer_down(app, &tap, NULL);
    assert(!app->lens.shown && !app->lens.held && lens_downs == 4);
    tap.type = TIKU_EVENT_POINTER_UP;
    desktop_pointer_up(app, &tap, NULL);
    tiku_rect_t inside = tiku_window_content(two);
    tap = (tiku_event_t){ .type = TIKU_EVENT_POINTER_DOWN, .button = 1,
                          .x = inside.x + inside.w / 2,
                          .y = inside.y + inside.h / 2 };
    desktop_pointer_down(app, &tap, NULL);
    assert(!app->lens.shown && !app->lens.held && lens_downs == 5);
    tap.type = TIKU_EVENT_POINTER_UP;
    desktop_pointer_up(app, &tap, NULL);
    /* While a menu shows, a press on it opens the lens from the menu's
     * left edge, where the rows' names begin; a press off the menu, a
     * close box included, opens none and reaches the menu's dismissal. */
    static tiku_menu_t rows;
    app->popup = &rows;
    app->popup_frame = (tiku_rect_t){ 300, 200, 150, 100 };
    tap = (tiku_event_t){ .type = TIKU_EVENT_POINTER_DOWN, .button = 1,
                          .x = 400, .y = 250 };
    desktop_pointer_down(app, &tap, NULL);
    assert(app->lens.shown && app->lens.area.x == 300 && lens_downs == 5);
    assert(app->lens.scale == DESKTOP_LENS_MENU_SCALE &&
           app->lens.area.w == DESKTOP_LENS_VIEW_W / DESKTOP_LENS_MENU_SCALE);
    tap.type = TIKU_EVENT_POINTER_UP;
    desktop_pointer_up(app, &tap, NULL);
    tap = (tiku_event_t){ .type = TIKU_EVENT_POINTER_DOWN, .button = 1,
                          .x = 700, .y = 450 };
    desktop_pointer_down(app, &tap, NULL);
    tap.type = TIKU_EVENT_POINTER_UP;
    desktop_pointer_up(app, &tap, NULL);
    assert(!app->lens.shown && lens_downs == 5);
    tap = (tiku_event_t){ .type = TIKU_EVENT_POINTER_DOWN, .button = 1,
                          .x = bx + 6, .y = by + 6 };
    desktop_pointer_down(app, &tap, NULL);
    assert(!app->lens.shown && !app->lens.held && lens_downs == 6);
    app->popup = NULL;
    puts("PASS: the touch lens stays open for a second press that picks the "
         "point under it");
    tiku_workspace_free(app->desk);
    tiku_surface_free(app->surf);
    free(app);
    return 0;
}
