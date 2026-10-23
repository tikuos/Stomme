/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richview_input.c - shared host input and complete text commits.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richview_private.h"
#include <string.h>

/** @brief Execute a parameter-free editing command for this view. */
static tiku_rich_status_t
command(tiku_richview_t *view, tiku_richcmd_kind_t kind)
{
    tiku_richcmd_t cmd = {.kind = kind};

    return tiku_richview_execute(view, 0, &cmd);
}

/** @brief Dispatch standard command-modified shortcuts; leave host actions. */
static tiku_rich_status_t
shortcut(tiku_richview_t *view, const tiku_event_t *e, int *handled)
{
    unsigned key = e->key;
    int shift = !!(e->modifiers & TIKU_MOD_SHIFT);

    if (key >= 'A' && key <= 'Z') key += 'a' - 'A';
    switch (key) {
    case 'a': return tiku_richview_select_all(view);
    case 'b': return command(view, TIKU_RICHCMD_BOLD);
    case 'i': return command(view, TIKU_RICHCMD_ITALIC);
    case 'u': return command(view, TIKU_RICHCMD_UNDERLINE);
    case 'z': return command(view, shift ? TIKU_RICHCMD_REDO :
                                         TIKU_RICHCMD_UNDO);
    case 'y': return command(view, TIKU_RICHCMD_REDO);
    case TIKU_KEY_HOME:
        return tiku_richview_move(view, TIKU_RICH_MOVE_START, shift);
    case TIKU_KEY_END:
        return tiku_richview_move(view, TIKU_RICH_MOVE_FINISH, shift);
    default: *handled = 0; return TIKU_RICH_NO_CHANGE;
    }
}

/** @brief Move between cell owners, or exit to a surrounding paragraph. */
static tiku_rich_status_t
cell_tab(tiku_richview_t *view, int backward)
{
    const tiku_rd_root_t *root = view->doc->root;
    tiku_rich_pos_t position = view->session->caret->position;
    uint64_t owner = tiku_rd_container(root, position.paragraph);
    size_t index = tiku_rd_find(root, position.paragraph);
    tiku_rich_status_t status;

    do {
        if ((backward && !index) || (!backward && index + 1 == root->count))
            return TIKU_RICH_NO_CHANGE;
        index = backward ? index - 1 : index + 1;
    } while (tiku_rd_container(root, root->paragraphs[index]->value.id) ==
             owner);
    owner = tiku_rd_container(root, root->paragraphs[index]->value.id);
    if (backward && owner != root->body)
        while (index && tiku_rd_container(root,
            root->paragraphs[index - 1]->value.id) == owner) index--;
    position.paragraph = root->paragraphs[index]->value.id;
    position.byte = 0;
    position.affinity = TIKU_RICH_AFTER;
    status = tiku_richsession_select(view->session, 0, position, position);
    if (status == TIKU_RICH_OK) (void)tiku_richview_reveal(view);
    return status;
}

/** @brief Dispatch text keys, grapheme deletion and visual navigation. */
static tiku_rich_status_t
key_event(tiku_richview_t *view, const tiku_event_t *e, int *handled)
{
    int shift = !!(e->modifiers & TIKU_MOD_SHIFT);
    tiku_rich_motion_t motion;
    size_t length;

    if (e->modifiers & TIKU_MOD_OPTION) {
        *handled = 0;
        return TIKU_RICH_NO_CHANGE;
    }
    if (e->modifiers & TIKU_MOD_CMD) return shortcut(view, e, handled);
    switch (e->key) {
    case TIKU_KEY_LEFT: motion = TIKU_RICH_MOVE_LEFT; break;
    case TIKU_KEY_RIGHT: motion = TIKU_RICH_MOVE_RIGHT; break;
    case TIKU_KEY_UP: motion = TIKU_RICH_MOVE_UP; break;
    case TIKU_KEY_DOWN: motion = TIKU_RICH_MOVE_DOWN; break;
    case TIKU_KEY_HOME: motion = TIKU_RICH_MOVE_HOME; break;
    case TIKU_KEY_END: motion = TIKU_RICH_MOVE_END; break;
    case TIKU_KEY_DELETE: return tiku_richview_delete(view, 0);
    case TIKU_KEY_BACKSPACE: return tiku_richview_delete(view, 1);
    case TIKU_KEY_RETURN: return command(view, TIKU_RICHCMD_PARAGRAPH_BREAK);
    case TIKU_KEY_TAB:
        if (tiku_rd_table_for(view->doc->root,
            view->session->caret->position.paragraph))
            return cell_tab(view, shift);
        if (shift) { *handled = 0; return TIKU_RICH_NO_CHANGE; }
        return tiku_richsession_type(view->session, 0, "\t", 1);
    default:
        if (e->key < 32 || (unsigned char)e->text[0] < 32) {
            *handled = 0;
            return TIKU_RICH_NO_CHANGE;
        }
        for (length = 0; length < sizeof e->text && e->text[length]; length++)
            {}
        if (length == sizeof e->text) return TIKU_RICH_INVALID_TEXT;
        return tiku_richsession_type(view->session, 0, e->text, length);
    }
    return tiku_richview_move(view, motion, shift);
}

/** @brief Assemble a bounded commit and reject movement during its transfer. */
static tiku_rich_status_t
text_event(tiku_richview_t *view, const tiku_event_t *e)
{
    tiku_richcmd_t cmd = {.kind = TIKU_RICHCMD_REPLACE_TEXT};
    tiku_rich_status_t status;
    int result;

    if (e->button & TIKU_TEXT_BEGIN) {
        view->input_generation = view->doc->generation;
        view->input_anchor = view->session->anchor->revision;
        view->input_caret = view->session->caret->revision;
    }
    result = tiku_text_input_event(&view->input, e);
    if (result == TIKU_TEXT_REJECTED) return TIKU_RICH_INVALID_TEXT;
    if (result != TIKU_TEXT_COMPLETE) return TIKU_RICH_PENDING;
    if (view->input_generation != view->doc->generation ||
        view->input_anchor != view->session->anchor->revision ||
        view->input_caret != view->session->caret->revision) {
        tiku_text_input_reset(&view->input);
        return TIKU_RICH_STALE;
    }
    cmd.value.text.data = view->input.text;
    cmd.value.text.length = (size_t)view->input.length;
    status = tiku_richsession_execute(view->session,
        view->input_generation, &cmd);
    tiku_text_input_reset(&view->input);
    return status;
}

tiku_rich_status_t
tiku_richview_event(tiku_richview_t *view, const tiku_event_t *e, int *handled)
{
    tiku_rich_status_t status;
    uint64_t cells;

    if (handled) *handled = 0;
    if (!view || !e || !handled) return TIKU_RICH_INVALID;
    if (view->doc->notifying || view->doc->initiator) return TIKU_RICH_BUSY;
    cells = view->cells.table;
    *handled = 1;
    if (e->type != TIKU_EVENT_TEXT_INPUT)
        (void)tiku_text_input_event(&view->input, e);
    status = tiku_rv_cells_event(view, e, handled);
    if (*handled) return status;
    status = tiku_rv_table_event(view, e, handled);
    if (*handled) return status;
    *handled = 1;
    if (e->type == TIKU_EVENT_TEXT_INPUT) return text_event(view, e);
    status = tiku_rv_picture_event(view, e, handled);
    if (*handled) return status;
    *handled = 1;
    switch (e->type) {
    case TIKU_EVENT_KEY_DOWN:
        status = key_event(view, e, handled);
        if (status == TIKU_RICH_NO_CHANGE && cells && !view->cells.table) {
            *handled = 1;
            return TIKU_RICH_OK;
        }
        return status;
    case TIKU_EVENT_POINTER_DOWN:
        if (e->button != 1) break;
        status = tiku_richview_point(view, e->x, e->y,
                                   !!(e->modifiers & TIKU_MOD_SHIFT));
        view->dragging = status == TIKU_RICH_OK;
        return status;
    case TIKU_EVENT_POINTER_MOVE:
        if (!view->dragging) break;
        status = tiku_richview_point(view, e->x, e->y, 1);
        if (status == TIKU_RICH_OK) (void)tiku_richview_reveal(view);
        return status;
    case TIKU_EVENT_POINTER_UP:
        if (e->button != 1) break;
        view->dragging = 0;
        return TIKU_RICH_OK;
    case TIKU_EVENT_WHEEL:
        return tiku_richview_scroll(view, view->scroll + e->dy * 24.0);
    case TIKU_EVENT_DEACTIVATED:
        view->dragging = 0;
        return tiku_richsession_break_undo(view->session);
    default: break;
    }
    *handled = 0;
    return TIKU_RICH_NO_CHANGE;
}
