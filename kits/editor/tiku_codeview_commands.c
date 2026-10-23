/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_codeview_commands.c - the view's commands by name: whether each
 * may run, running it for a menu or a key the host bound, and what has
 * changed since the host last asked.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_codeview_private.h"

#include <string.h>

_Static_assert(TIKU_CODEVIEW_CMD_MOVE < 32,
               "the commands enabled are kept a bit each in 32 bits");

/** @brief @p key with an ASCII capital as its small letter, so a letter
 *         is bound in either case. */
static unsigned
key_of(unsigned key)
{
    return key >= 'A' && key <= 'Z' ? key + ('a' - 'A') : key;
}

/** @brief Whether bindings @p a and @p b are the same keys held. */
static int
same_keys(const tiku_codeview_binding_t *a, const tiku_codeview_binding_t *b)
{
    return key_of(a->key) == key_of(b->key) &&
           (a->modifiers & ~TIKU_MOD_CAPS) == (b->modifiers & ~TIKU_MOD_CAPS);
}

tiku_edit_status_t
tiku_codeview_set_bindings(tiku_codeview_t *view,
                           const tiku_codeview_binding_t *bindings,
                           size_t count)
{
    tiku_codeview_binding_t *copy = NULL;
    size_t i, j;

    if (view == NULL || (count > 0 && bindings == NULL))
        return TIKU_EDIT_INVALID;
    if (count > SIZE_MAX / sizeof *copy)
        return TIKU_EDIT_LIMIT;
    for (i = 0; i < count; i++) {
        if ((unsigned)bindings[i].command > TIKU_CODEVIEW_CMD_MOVE ||
            (bindings[i].command == TIKU_CODEVIEW_CMD_MOVE &&
             (unsigned)bindings[i].move > TIKU_CODEVIEW_DOC_END))
            return TIKU_EDIT_INVALID;
        for (j = 0; j < i; j++)
            if (same_keys(&bindings[i], &bindings[j]))
                return TIKU_EDIT_INVALID;
    }
    if (count > 0) {
        copy = tiku_ed_alloc(count * sizeof *copy);
        if (copy == NULL)
            return TIKU_EDIT_NO_MEMORY;
        memcpy(copy, bindings, count * sizeof *copy);
    }
    tiku_ed_free(view->bindings);
    view->bindings = copy;
    view->nbindings = count;
    return TIKU_EDIT_OK;
}

int
tiku_codeview_command_enabled(const tiku_codeview_t *view,
                              tiku_codeview_command_t command)
{
    int writable;

    if (view == NULL)
        return 0;
    writable = !tiku_editdoc_read_only(view->doc);
    switch (command) {
    case TIKU_CODEVIEW_CMD_PASS:
        return 0;
    case TIKU_CODEVIEW_CMD_IGNORE:
        return 1;
    case TIKU_CODEVIEW_CMD_UNDO:
        return writable && tiku_editdoc_can_undo(view->doc);
    case TIKU_CODEVIEW_CMD_REDO:
        return writable && tiku_editdoc_can_redo(view->doc);
    case TIKU_CODEVIEW_CMD_COPY:
    case TIKU_CODEVIEW_CMD_CUT:
        return (command != TIKU_CODEVIEW_CMD_CUT || writable) &&
               view->host.clip_set != NULL &&
               tiku_codeview_has_selection(view);
    case TIKU_CODEVIEW_CMD_PASTE:
        return writable && view->host.clip_text != NULL;
    case TIKU_CODEVIEW_CMD_KEEP_MAIN:
        return tiku_cv_many(view);
    case TIKU_CODEVIEW_CMD_COMMENT:
        return writable && tiku_codeview_rules(view)->comment != NULL;
    case TIKU_CODEVIEW_CMD_SELECT_ALL:
    case TIKU_CODEVIEW_CMD_FOLD:
    case TIKU_CODEVIEW_CMD_UNFOLD:
    case TIKU_CODEVIEW_CMD_LINE_ENDS:
    case TIKU_CODEVIEW_CMD_MOVE:
        return 1;
    default:
        return command >= TIKU_CODEVIEW_CMD_NEWLINE &&
               command <= TIKU_CODEVIEW_CMD_DELETE_LINES && writable;
    }
}

/** @brief Run @p command, one of the edits from TIKU_CODEVIEW_CMD_NEWLINE
 *         to TIKU_CODEVIEW_CMD_DELETE_LINES; TIKU_EDIT_INVALID for any
 *         other. */
static tiku_edit_status_t
run_lines(tiku_codeview_t *v, tiku_codeview_command_t command)
{
    switch (command) {
    case TIKU_CODEVIEW_CMD_NEWLINE:
        return tiku_codeview_newline(v);
    case TIKU_CODEVIEW_CMD_BACKSPACE:
        return tiku_codeview_delete(v, 0);
    case TIKU_CODEVIEW_CMD_DELETE:
        return tiku_codeview_delete(v, 1);
    case TIKU_CODEVIEW_CMD_INDENT:
        return tiku_codeview_indent(v, 0);
    case TIKU_CODEVIEW_CMD_UNINDENT:
        return tiku_codeview_indent(v, 1);
    case TIKU_CODEVIEW_CMD_COMMENT:
        return tiku_codeview_toggle_comment(v);
    case TIKU_CODEVIEW_CMD_DUPLICATE:
        return tiku_codeview_duplicate_lines(v);
    case TIKU_CODEVIEW_CMD_DELETE_LINES:
        return tiku_codeview_delete_lines(v);
    default:
        return TIKU_EDIT_INVALID;
    }
}

tiku_edit_status_t
tiku_codeview_execute(tiku_codeview_t *view, tiku_codeview_command_t command,
                      tiku_codeview_move_t move, int extend)
{
    tiku_edit_status_t st;

    if (view == NULL)
        return TIKU_EDIT_INVALID;
    switch (command) {
    case TIKU_CODEVIEW_CMD_PASS:
    case TIKU_CODEVIEW_CMD_IGNORE:
        st = TIKU_EDIT_NOTHING;
        break;
    case TIKU_CODEVIEW_CMD_UNDO:
        st = tiku_codeview_undo(view);
        break;
    case TIKU_CODEVIEW_CMD_REDO:
        st = tiku_codeview_redo(view);
        break;
    case TIKU_CODEVIEW_CMD_COPY:
        st = tiku_codeview_copy(view);
        break;
    case TIKU_CODEVIEW_CMD_CUT:
        st = tiku_codeview_cut(view);
        break;
    case TIKU_CODEVIEW_CMD_PASTE:
        st = tiku_codeview_paste(view);
        break;
    case TIKU_CODEVIEW_CMD_SELECT_ALL:
        st = tiku_codeview_select_all(view);
        break;
    case TIKU_CODEVIEW_CMD_FOLD:
        st = tiku_codeview_fold_at(view, 0);
        break;
    case TIKU_CODEVIEW_CMD_UNFOLD:
        st = tiku_codeview_fold_at(view, 1);
        break;
    case TIKU_CODEVIEW_CMD_LINE_ENDS:
        st = tiku_codeview_carets_to_line_ends(view);
        break;
    case TIKU_CODEVIEW_CMD_KEEP_MAIN:
        st = tiku_codeview_keep_main(view);
        break;
    case TIKU_CODEVIEW_CMD_MOVE:
        st = (unsigned)move > TIKU_CODEVIEW_DOC_END ?
             TIKU_EDIT_INVALID : tiku_codeview_move(view, move, extend);
        break;
    default:
        st = run_lines(view, command);
        break;
    }
    view->status = st;
    return st;
}

int
tiku_cv_binding_event(tiku_codeview_t *v, const tiku_event_t *e)
{
    tiku_codeview_binding_t pressed;
    size_t i;

    if (e->type != TIKU_EVENT_KEY_DOWN)
        return -1;
    memset(&pressed, 0, sizeof pressed);
    pressed.key = e->key;
    pressed.modifiers = e->modifiers;
    for (i = 0; i < v->nbindings; i++) {
        const tiku_codeview_binding_t *b = &v->bindings[i];

        if (!same_keys(b, &pressed))
            continue;
        if (b->command == TIKU_CODEVIEW_CMD_PASS)
            return 0;
        (void)tiku_codeview_execute(v, b->command, b->move, b->extend);
        return 1;
    }
    return -1;
}

/** @brief The commands that may run now, a bit each. */
static uint32_t
commands_enabled(const tiku_codeview_t *v)
{
    uint32_t enabled = 0;
    int c;

    for (c = TIKU_CODEVIEW_CMD_PASS; c <= TIKU_CODEVIEW_CMD_MOVE; c++)
        if (tiku_codeview_command_enabled(v, (tiku_codeview_command_t)c))
            enabled |= UINT32_C(1) << c;
    return enabled;
}

unsigned
tiku_codeview_changes(tiku_codeview_t *view)
{
    const tiku_ed_view_t *sel;
    uint64_t generation;
    uint32_t commands;
    unsigned flags = 0;
    int modified, read_only;

    if (view == NULL)
        return 0;
    sel = tiku_ed_view_get(view->doc, view->view);
    generation = tiku_editdoc_generation(view->doc);
    modified = tiku_editdoc_modified(view->doc);
    read_only = tiku_editdoc_read_only(view->doc);
    commands = commands_enabled(view);
    if (generation != view->seen_doc || modified != view->seen_modified ||
        read_only != view->seen_read_only)
        flags |= TIKU_CODEVIEW_CHANGE_DOCUMENT;
    if (generation != view->seen_doc ||
        sel->selection_serial != view->seen_selection)
        flags |= TIKU_CODEVIEW_CHANGE_SELECTION;
    if (view->top != view->seen_top || view->top_sub != view->seen_sub ||
        view->left != view->seen_left)
        flags |= TIKU_CODEVIEW_CHANGE_SCROLL;
    if (commands != view->seen_commands)
        flags |= TIKU_CODEVIEW_CHANGE_COMMANDS;
    if (view->status != view->seen_status)
        flags |= TIKU_CODEVIEW_CHANGE_STATUS;
    if (!view->observed)
        flags = TIKU_CODEVIEW_CHANGE_ALL;
    view->observed = 1;
    view->seen_doc = generation;
    view->seen_selection = sel->selection_serial;
    view->seen_modified = modified;
    view->seen_read_only = read_only;
    view->seen_top = view->top;
    view->seen_sub = view->top_sub;
    view->seen_left = view->left;
    view->seen_commands = commands;
    view->seen_status = view->status;
    return flags;
}
