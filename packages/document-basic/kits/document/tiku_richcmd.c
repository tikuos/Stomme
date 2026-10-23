/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richcmd.c - checked command dispatch through document operations.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richcmd_private.h"

tiku_rich_status_t
tiku_richcmd_execute(tiku_richdoc_t *doc,
    const tiku_richcmd_context_t *context, const tiku_richcmd_t *command)
{
    tiku_richcmd_state_t state;
    tiku_rich_status_t status;

    if (!command)
        return TIKU_RICH_INVALID;
    status = tiku_richcmd_query(doc, context, command->kind, &state);
    if (status != TIKU_RICH_OK)
        return status;
    if (!state.enabled)
        return state.status;
    switch (command->kind) {
    case TIKU_RICHCMD_CELL_SET: {
        tiku_rich_edit_t edit = {.kind = TIKU_RICH_CELL_SET,
            .object = tiku_rd_container(doc->root, context->first.paragraph),
            .background = command->value.color};
        tiku_rich_transaction_t tx = {context->generation, &edit, 1};

        return tiku_richdoc_apply(doc, &tx);
    }
    case TIKU_RICHCMD_UNDO:
        return tiku_richdoc_undo(doc);
    case TIKU_RICHCMD_REDO:
        return tiku_richdoc_redo(doc);
    case TIKU_RICHCMD_REPLACE_TEXT:
        return tiku_richdoc_replace_range(doc, context->generation,
            context->first, context->last, command->value.text.data,
            command->value.text.length);
    case TIKU_RICHCMD_DELETE:
        return tiku_richdoc_replace_range(doc, context->generation,
            context->first, context->last, NULL, 0);
    case TIKU_RICHCMD_PARAGRAPH_BREAK:
        return tiku_richdoc_replace_range(doc, context->generation,
            context->first, context->last, "\n", 1);
    case TIKU_RICHCMD_PASTE_FRAGMENT:
        return tiku_richdoc_paste_fragment(doc, context->generation,
            context->first, context->last, command->value.fragment);
    case TIKU_RICHCMD_ROW_BEFORE:
    case TIKU_RICHCMD_ROW_AFTER:
    case TIKU_RICHCMD_ROW_REMOVE:
    case TIKU_RICHCMD_COLUMN_BEFORE:
    case TIKU_RICHCMD_COLUMN_AFTER:
    case TIKU_RICHCMD_COLUMN_REMOVE: {
        tiku_rich_edit_t edit;
        tiku_rich_transaction_t tx = {context->generation, &edit, 1};

        status = tiku_rc_grid_edit(doc->root, context->first.paragraph,
                                   command->kind, &edit);
        return status == TIKU_RICH_OK ? tiku_richdoc_apply(doc, &tx) : status;
    }
    case TIKU_RICHCMD_TABLE_INSERT:
    case TIKU_RICHCMD_TABLE_SET:
    case TIKU_RICHCMD_TABLE_REMOVE: {
        tiku_rich_edit_t edit = {
            .kind = command->kind == TIKU_RICHCMD_TABLE_INSERT ?
                TIKU_RICH_TABLE_INSERT :
                command->kind == TIKU_RICHCMD_TABLE_SET ?
                TIKU_RICH_TABLE_SET : TIKU_RICH_TABLE_REMOVE,
            .paragraph = context->first.paragraph,
            .object = tiku_rd_table_for(doc->root, context->first.paragraph)};
        tiku_rich_transaction_t tx = {context->generation, &edit, 1};

        if (command->kind != TIKU_RICHCMD_TABLE_REMOVE)
            edit.table = command->value.table;
        return tiku_richdoc_apply(doc, &tx);
    }
    case TIKU_RICHCMD_IMAGE_INSERT:
    case TIKU_RICHCMD_IMAGE_SET: {
        tiku_rich_edit_t edit = {
            .kind = command->kind == TIKU_RICHCMD_IMAGE_INSERT ?
                TIKU_RICH_IMAGE_INSERT : TIKU_RICH_IMAGE_SET,
            .paragraph = context->first.paragraph,
            .start = context->first.byte, .end = context->last.byte,
            .image = command->value.image};
        tiku_rich_transaction_t tx = {context->generation, &edit, 1};

        if (context->first.paragraph != context->last.paragraph)
            return TIKU_RICH_INVALID_RANGE;
        return tiku_richdoc_apply(doc, &tx);
    }
    default:
        return tiku_rc_apply(doc, context, command, &state);
    }
}
