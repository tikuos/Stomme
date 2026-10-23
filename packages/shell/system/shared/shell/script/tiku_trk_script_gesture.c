/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_script_gesture.c - the gestures a script makes (keys, presses,
 * clicks and drags) and the steps between them: waits, reports, menu
 * picks, shots and resizes.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_script_private.h"
#include <signal.h>

/**
 * @brief Run a gesture step: fill @p out with the event to inject, or start
 *        a wait, raise a report, take a note or ask for a resize.
 *
 * A target that cannot be found (a Desktop icon, a window, a row or a
 * cell) records a failed check instead.
 *
 * @return 1 for a step of this group, 0 for another group's.
 */
int
sc_pump_gesture(tiku_trk_script_t *s, const tiku_trk_script_state_t *st,
                tiku_trk_script_step_t *out, sc_step_t *cur)
{
    switch (cur->op) {

    case OP_KEY:
        out->what = TIKU_TRK_SC_KEY;
        out->key = cur->key;
        out->modifiers = cur->modifiers;
        return 1;
    case OP_CLICK_DESK:
    case OP_PRESS_DESK:
    case OP_MOVE_DESK:
    case OP_RELEASE_DESK: {
        int px = 0, py = 0;
        int (*at)(void *, const char *, int *, int *) =
            (st != NULL && st->pose_desk_at != NULL) ? st->pose_desk_at
            : (st != NULL) ? st->pose_at : NULL;

        if (at == NULL || !at(st->pose_ctx, cur->text, &px, &py)) {
            sc_verdict(s, 0, "no desktop icon named:", cur->text);
            return 1;
        }
        /* The reported point is the icon's corner; the press goes 16
         * pixels in, inside the smallest icon the Desktop draws. */
        cur->x = px + 16;
        cur->y = py + 16;
        out->what = (cur->op == OP_RELEASE_DESK) ? TIKU_TRK_SC_RELEASE
                  : (cur->op == OP_MOVE_DESK) ? TIKU_TRK_SC_MOVE
                                              : TIKU_TRK_SC_PRESS;
        out->x = cur->x;
        out->y = cur->y;
        out->button = cur->button;
        out->modifiers = cur->modifiers;
        s->pending_release = (cur->op == OP_CLICK_DESK);
        return 1;
    }
    case OP_PRESS:
    case OP_CLICK:
        out->what = TIKU_TRK_SC_PRESS;
        out->x = cur->x;
        out->y = cur->y;
        out->button = cur->button;
        out->modifiers = cur->modifiers;
        s->pending_release = (cur->op == OP_CLICK);
        return 1;
    case OP_CLICK_WINDOW:
        if (st == NULL || st->tab_point == NULL ||
            !st->tab_point(st->pose_ctx, cur->text, &cur->button_x,
                           &cur->button_y)) {
            sc_verdict(s, 0, "no window titled:", cur->text);
            return 1;
        }
        cur->x = cur->button_x;
        cur->y = cur->button_y;
        out->what = TIKU_TRK_SC_PRESS;
        out->x = cur->x;
        out->y = cur->y;
        out->button = 1u;
        s->pending_release = 1;
        return 1;
    case OP_DRAG_WINDOW:
        if (st == NULL || st->tab_point == NULL ||
            !st->tab_point(st->pose_ctx, cur->text, &cur->button_x,
                           &cur->button_y)) {
            sc_verdict(s, 0, "no window titled:", cur->text);
            return 1;
        }
        /* Press on the tab; the move and the release follow on the next
         * two turns, so the shell sees a real gesture. */
        out->what = TIKU_TRK_SC_PRESS;
        out->x = cur->button_x;
        out->y = cur->button_y;
        out->button = 1u;
        s->drag_to_x = cur->x;
        s->drag_to_y = cur->y;
        s->dragging_window = 1;
        return 1;
    case OP_PRESS_ROW:
    case OP_MOVE_ROW:
    case OP_RELEASE_ROW:
        if (st == NULL || st->row_point == NULL ||
            !st->row_point(st->pose_ctx, cur->count, &cur->x, &cur->y)) {
            char row[32];

            snprintf(row, sizeof row, "%d", cur->count);
            sc_verdict(s, 0, "cannot reach row:", row);
            return 1;
        }
        out->what = (cur->op == OP_PRESS_ROW) ? TIKU_TRK_SC_PRESS
                  : (cur->op == OP_MOVE_ROW) ? TIKU_TRK_SC_MOVE
                                             : TIKU_TRK_SC_RELEASE;
        out->x = cur->x;
        out->y = cur->y;
        out->button = cur->button;
        out->modifiers = cur->modifiers;
        return 1;
    case OP_CLICK_CELL:
        if (st == NULL || st->cell_point == NULL ||
            !st->cell_point(st->pose_ctx, cur->count, cur->text, &cur->x,
                            &cur->y)) {
            sc_verdict(s, 0, "cannot click cell:", cur->text);
            return 1;
        }
        out->what = TIKU_TRK_SC_PRESS;
        out->x = cur->x;
        out->y = cur->y;
        out->button = cur->button;
        s->pending_release = 1;
        return 1;
    case OP_CLICK_ROW:
        if (st == NULL || st->row_point == NULL ||
            !st->row_point(st->pose_ctx, cur->count, &cur->x, &cur->y)) {
            char row[32];

            snprintf(row, sizeof row, "%d", cur->count);
            sc_verdict(s, 0, "cannot click row:", row);
            return 1;
        }
        out->what = TIKU_TRK_SC_PRESS;
        out->x = cur->x;
        out->y = cur->y;
        out->button = cur->button;
        out->modifiers = cur->modifiers;
        s->pending_release = 1;
        return 1;
    case OP_MOVE:
        out->what = TIKU_TRK_SC_MOVE;
        out->x = cur->x;
        out->y = cur->y;
        out->modifiers = cur->modifiers;
        out->button = cur->button;
        return 1;
    case OP_RELEASE:
        out->what = TIKU_TRK_SC_RELEASE;
        out->x = cur->x;
        out->y = cur->y;
        out->button = cur->button;
        out->modifiers = cur->modifiers;
        return 1;
        case OP_WAIT:
        s->waiting = (cur->count > 0) ? cur->count : 1;
        return 1;
    case OP_REPORT:
        /* SIGUSR2, as a person sends it from a terminal; the shell writes
         * its report on the next turn, which the wait gives it. */
        (void)raise(SIGUSR2);
        s->waiting = 1;
        return 1;
    case OP_MENU_PICK:
    case OP_MENU_PRESS:
    case OP_MENU_RELEASE:
        out->what = cur->op == OP_MENU_PRESS ? TIKU_TRK_SC_MENU_PRESS
                  : cur->op == OP_MENU_RELEASE ? TIKU_TRK_SC_MENU_RELEASE
                                              : TIKU_TRK_SC_MENU_PICK;
        out->path = cur->text;
        return 1;
    case OP_SHOT:
        out->what = TIKU_TRK_SC_SHOT;
        out->path = cur->text;
        return 1;
    case OP_RESIZE:
        if (st == NULL || st->resize == NULL) {
            sc_verdict(s, 0, "resize:", "this shell cannot");
        } else if (!st->resize(st->resize_ctx, cur->x, cur->y)) {
            sc_verdict(s, 0, "resize:", "refused");
        }
        return 1;
    case OP_NOTE:
        snprintf(s->note, sizeof s->note, "%s", cur->text);
        return 1;
    default:
        return 0;
    }
}

/**
 * @brief Parse a gesture line: key, click, press, move, release,
 *        click-window, drag-window, press-row, move-row, release-row,
 *        click-cell, click-row, wait, report, menu gestures, shot or resize.
 *
 * @return 1 for a verb of this group, -1 for one whose line is bad and has
 *         been reported, 0 for a verb of another group.
 */
int
sc_parse_gesture(sc_step_t *st, char **w, int n, int lineno)
{
    if (strcmp(w[0], "key") == 0 && n >= 2) {
        st->op = OP_KEY;
        st->key = sc_key_of(w[1]);
        if (st->key == 0u) {
            fprintf(stderr, "script:%d: unknown key '%s'\n", lineno,
                    w[1]);
            return -1;
        }
        st->modifiers = sc_mods_of(w, n, 2);
    } else if ((strcmp(w[0], "click") == 0 ||
                strcmp(w[0], "press") == 0 ||
                strcmp(w[0], "move") == 0 ||
                strcmp(w[0], "release") == 0) && n >= 3) {
        st->op = (w[0][0] == 'c') ? OP_CLICK
               : (w[0][0] == 'p') ? OP_PRESS
               : (w[0][0] == 'm') ? OP_MOVE : OP_RELEASE;
        st->x = atoi(w[1]);
        st->y = atoi(w[2]);
        if (n >= 4) {
            st->button = (unsigned)atoi(w[3]);
        }
        st->modifiers = sc_mods_of(w, n, 4);
    } else if (strcmp(w[0], "click-window") == 0 && n >= 2) {
        /* A click on the tab of the window with this title, wherever the
         * window is. */
        st->op = OP_CLICK_WINDOW;
        snprintf(st->text, sizeof st->text, "%s", w[1]);
        st->button = 1u;
    } else if (strcmp(w[0], "drag-window") == 0 && n >= 4) {
        /* By title: the shell reports where the window's tab is. */
        st->op = OP_DRAG_WINDOW;
        snprintf(st->text, sizeof st->text, "%s", w[1]);
        st->x = atoi(w[2]);
        st->y = atoi(w[3]);
        st->button = 1u;
    } else if ((strcmp(w[0], "press-row") == 0 ||
                strcmp(w[0], "move-row") == 0 ||
                strcmp(w[0], "release-row") == 0) && n >= 2) {
        /* The parts of a drag, aimed at a row of the focused list; the
         * shell reports where the row is. */
        st->op = (w[0][0] == 'p') ? OP_PRESS_ROW
               : (w[0][0] == 'm') ? OP_MOVE_ROW : OP_RELEASE_ROW;
        st->count = atoi(w[1]);
        if (n >= 3) {
            st->button = (unsigned)atoi(w[2]);
        }
        st->modifiers = sc_mods_of(w, n, 3);
    } else if (strcmp(w[0], "click-cell") == 0 && n >= 3) {
        st->op = OP_CLICK_CELL;
        st->count = atoi(w[1]);
        sc_rest(w, n, 2, st->text, sizeof st->text);
        st->button = 1u;
    } else if (strcmp(w[0], "click-row") == 0 && n >= 2) {
        st->op = OP_CLICK_ROW;
        st->count = atoi(w[1]);
        if (n >= 3) {
            st->button = (unsigned)atoi(w[2]);
        }
        st->modifiers = sc_mods_of(w, n, 3);
        } else if (strcmp(w[0], "wait") == 0 && n >= 2) {
        st->op = OP_WAIT;
        st->count = atoi(w[1]);
    } else if (strcmp(w[0], "report") == 0) {
        st->op = OP_REPORT;
    } else if (strcmp(w[0], "menu-release") == 0 && n == 1) {
        st->op = OP_MENU_RELEASE;
    } else if ((strcmp(w[0], "menu-pick") == 0 ||
                strcmp(w[0], "menu-press") == 0) && n >= 2) {
        st->op = strcmp(w[0], "menu-press") == 0 ? OP_MENU_PRESS
                                               : OP_MENU_PICK;
        sc_rest(w, n, 1, st->text, sizeof st->text);
    } else if (strcmp(w[0], "shot") == 0 && n >= 2) {
        st->op = OP_SHOT;
        snprintf(st->text, sizeof st->text, "%s", w[1]);
    } else if (strcmp(w[0], "resize") == 0 && n >= 3) {
        st->op = OP_RESIZE;
        st->x = atoi(w[1]);
        st->y = atoi(w[2]);
    } else {
        return 0;
    }
    return 1;
}
