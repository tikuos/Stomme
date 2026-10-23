/* tiku_editor_context.c -- the document's right-click menu.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_editor_private.h"

/** Add the published menu row for @p command to the context menu, with
 *  the label, shortcut and enabled state the menu bar shows. */
static void add_published(edit_state_t *st, int command)
{
    for (int m = 0; m < st->last_menus.nmenu; m++) {
        const tiku_menu_list_t *menu = &st->last_menus.menu[m];

        for (int j = 0; j < menu->nitem; j++) {
            const tiku_menu_entry_t *e = &menu->item[j];

            if (e->command == command) {
                (void)tiku_menu_add(&st->context, e->label, e->command,
                                    e->sc, e->mods, e->enabled);
            }
        }
    }
}

/** Open the context menu at @p x, @p y in the document window: edit and
 *  search commands as the menus offer them, the selection kept. */
void tiku_editor_context_open(edit_state_t *st, int x, int y)
{
    static const int commands[] = {
        CMD_UNDO, CMD_REDO, 0, CMD_CUT, CMD_COPY, CMD_PASTE, CMD_ALL, 0,
        CMD_COMMENT, CMD_TRIM, CMD_FIND, CMD_SELECT_MATCHES, CMD_PREFS};

    tiku_editor_publish(st);
    memset(&st->context, 0, sizeof st->context);
    st->context.hot = -1;
    for (size_t i = 0; i < sizeof commands / sizeof commands[0]; i++) {
        if (commands[i] == 0) {
            (void)tiku_menu_add_separator(&st->context);
        } else {
            add_published(st, commands[i]);
        }
    }
    st->context_frame = tiku_menu_place(&st->context, x, y,
                                        (tiku_rect_t){0, 0, st->w, st->h});
    st->context_open = 1;
}

/** Move the context menu's highlight one enabled row up or down. */
static void context_step(edit_state_t *st, int direction)
{
    tiku_menu_t *m = &st->context;

    if (m->hot < 0 && direction < 0) {
        m->hot = 0;
    }
    for (int j = 0; j < m->count; j++) {
        m->hot = (m->hot + direction + m->count) % m->count;
        if (!m->item[m->hot].separator && m->item[m->hot].enabled) {
            break;
        }
    }
}

/** Close the context menu and run row @p i's command when it is
 *  enabled; -1 runs nothing. */
static void context_choose(edit_state_t *st, int i)
{
    st->context_open = 0;
    if (i >= 0 && st->context.item[i].enabled) {
        (void)tiku_editor_pick(st, st->id, st->context.item[i].command);
    }
}

/**
 * @brief Give @p e to the open context menu: a move highlights a row, a
 *        press chooses one or dismisses the menu, Up and Down step,
 *        Return chooses and Escape dismisses.
 *
 * @return 1 when the menu took the event, 0 when none is open or the
 *         event is the window's close, deactivation or resize, which
 *         dismiss the menu and then go on to the window
 */
int tiku_editor_context_event(edit_state_t *st, const tiku_event_t *e)
{
    if (!st->context_open) {
        return 0;
    }
    if (e->type == TIKU_EVENT_CLOSE || e->type == TIKU_EVENT_DEACTIVATED ||
        e->type == TIKU_EVENT_RESIZE) {
        st->context_open = 0;
        return 0;
    }
    if (e->type == TIKU_EVENT_POINTER_MOVE) {
        (void)tiku_menu_track(&st->context, st->context_frame, e->x, e->y);
    } else if (e->type == TIKU_EVENT_POINTER_DOWN) {
        int i = tiku_menu_item_at(&st->context, st->context_frame, e->x,
                                  e->y);

        context_choose(st, e->button <= 1 ? i : -1);
    } else if (e->type == TIKU_EVENT_KEY_DOWN) {
        if (e->key == TIKU_KEY_ESCAPE) {
            st->context_open = 0;
        } else if (e->key == TIKU_KEY_UP || e->key == TIKU_KEY_DOWN) {
            context_step(st, e->key == TIKU_KEY_UP ? -1 : 1);
        } else if (e->key == TIKU_KEY_RETURN) {
            context_choose(st, st->context.hot);
        }
    }
    tiku_editor_paint(st);
    return 1;
}
