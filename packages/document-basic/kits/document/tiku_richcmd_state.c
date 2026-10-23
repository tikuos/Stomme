/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richcmd_state.c - live command availability and formatting indicators.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richcmd_private.h"

#include <string.h>

/** @brief Disable a known command with a stable explanatory reason. */
static void
disable(tiku_richcmd_state_t *out, tiku_rich_status_t status,
        const char *reason)
{
    out->enabled = 0;
    out->status = status;
    out->reason = reason;
}

/** @brief Resolve a toggle's explicit boolean value. */
static int
boolean_value(const tiku_rich_chars_t *chars, unsigned bit)
{
    switch (bit) {
    case TIKU_RICH_BOLD: return chars->bold;
    case TIKU_RICH_ITALIC: return chars->italic;
    case TIKU_RICH_UNDERLINE: return chars->underline;
    default: return chars->strike;
    }
}

tiku_richcmd_check_t
tiku_rc_checked(tiku_richcmd_kind_t kind,
                  const tiku_rich_selection_t *selection)
{
    unsigned bit = tiku_rc_toggle_bit(kind);

    if (bit) {
        if (selection->mixed_chars & bit)
            return TIKU_RICHCMD_CHECK_MIXED;
        if (!(selection->chars.present & bit))
            return TIKU_RICHCMD_CHECK_UNSPECIFIED;
        return boolean_value(&selection->chars, bit) ?
            TIKU_RICHCMD_CHECK_ON : TIKU_RICHCMD_CHECK_OFF;
    }
    if (kind >= TIKU_RICHCMD_ALIGN_LEFT && kind <= TIKU_RICHCMD_ALIGN_JUSTIFY) {
        if (selection->mixed_props & TIKU_RICH_ALIGNMENT)
            return TIKU_RICHCMD_CHECK_MIXED;
        if (!(selection->props.present & TIKU_RICH_ALIGNMENT))
            return TIKU_RICHCMD_CHECK_UNSPECIFIED;
        return selection->props.align == tiku_rc_alignment(kind) ?
            TIKU_RICHCMD_CHECK_ON : TIKU_RICHCMD_CHECK_OFF;
    }
    if (kind == TIKU_RICHCMD_LINK_SET || kind == TIKU_RICHCMD_LINK_REMOVE) {
        if (selection->mixed & TIKU_RICH_MIXED_LINK)
            return TIKU_RICHCMD_CHECK_MIXED;
        return selection->link ? TIKU_RICHCMD_CHECK_ON :
                                 TIKU_RICHCMD_CHECK_OFF;
    }
    return TIKU_RICHCMD_CHECK_NONE;
}

size_t
tiku_rc_count(const tiku_richdoc_t *doc,
    const tiku_richcmd_context_t *context, tiku_richcmd_scope_t scope)
{
    size_t begin = tiku_rd_find(doc->root, context->first.paragraph);
    size_t end = tiku_rd_find(doc->root, context->last.paragraph);
    size_t i, count = 0, first, last;

    if (scope == TIKU_RICHCMD_PARAGRAPH_SCOPE)
        return end - begin + 1 - (end > begin && !context->last.byte);
    for (i = begin; i <= end; i++) {
        first = i == begin ? context->first.byte : 0;
        last = i == end ? context->last.byte :
               doc->root->paragraphs[i]->value.length;
        count += first < last;
    }
    return count;
}

/** @brief Check endpoints and determine selection-specific availability. */
static void
selection_state(const tiku_richdoc_t *doc,
    const tiku_richcmd_context_t *context, const tiku_richcmd_info_t *info,
    tiku_richcmd_state_t *out)
{
    tiku_richsnapshot_t snapshot = {.root = doc->root};
    tiku_rich_selection_t selection;
    tiku_rich_status_t status;
    size_t count;

    status = tiku_richsnapshot_selection(&snapshot, context->first,
                                         context->last, &selection);
    if (status != TIKU_RICH_OK) {
        disable(out, status, "The selection is not valid in this document.");
        return;
    }
    out->check = tiku_rc_checked(info->kind, &selection);
    if (info->kind >= TIKU_RICHCMD_ROW_BEFORE &&
        info->kind <= TIKU_RICHCMD_COLUMN_REMOVE) {
        tiku_rich_edit_t edit;

        if (!selection.empty) {
            disable(out, TIKU_RICH_INVALID_RANGE, "Place the caret first.");
            return;
        }
        status = tiku_rc_grid_edit(doc->root, context->first.paragraph,
                                   info->kind, &edit);
        if (status != TIKU_RICH_OK)
            disable(out, status, status == TIKU_RICH_LIMIT ?
                "The table dimension limit has been reached." :
                "Place the caret in a cell; keep at least one row and column.");
        return;
    }
    if (info->kind == TIKU_RICHCMD_TABLE_INSERT ||
        info->kind == TIKU_RICHCMD_TABLE_REMOVE ||
        info->kind == TIKU_RICHCMD_TABLE_SET ||
        info->kind == TIKU_RICHCMD_CELL_SET) {
        if (!selection.empty)
            disable(out, TIKU_RICH_INVALID_RANGE, "Place the caret first.");
        else if (info->kind != TIKU_RICHCMD_TABLE_INSERT &&
                 !tiku_rd_table_for(doc->root, context->first.paragraph))
            disable(out, TIKU_RICH_INVALID_RANGE, "Place the caret in a cell.");
        return;
    }
    if (info->kind == TIKU_RICHCMD_IMAGE_INSERT ||
        info->kind == TIKU_RICHCMD_IMAGE_SET) {
        const tiku_rich_paragraph_t *p = &doc->root->paragraphs[
            tiku_rd_find(doc->root, context->first.paragraph)]->value;
        size_t at = 0, i = 0;

        if (context->first.paragraph != context->last.paragraph) {
            disable(out, TIKU_RICH_INVALID_RANGE,
                    "Place the caret or selection in one paragraph.");
            return;
        }
        if (info->kind == TIKU_RICHCMD_IMAGE_INSERT) return;
        while (i < p->run_count && at < context->first.byte)
            at += p->runs[i++].length;
        if (at != context->first.byte || i == p->run_count ||
            !p->runs[i].image || context->last.byte - at != 3)
            disable(out, TIKU_RICH_INVALID_RANGE, "Select one picture.");
        return;
    }
    if (info->kind == TIKU_RICHCMD_DELETE && selection.empty) {
        disable(out, TIKU_RICH_NO_CHANGE, "Select content to delete.");
        return;
    }
    if (info->scope == TIKU_RICHCMD_RANGE_SCOPE) {
        if (!tiku_rd_same_container(doc->root,
            tiku_rd_find(doc->root, context->first.paragraph),
            tiku_rd_find(doc->root, context->last.paragraph))) {
            disable(out, TIKU_RICH_INVALID_RANGE,
                    "This edit cannot cross table or cell boundaries.");
            return;
        }
        if (info->kind == TIKU_RICHCMD_PASTE_FRAGMENT)
            return;
        count = tiku_rd_find(doc->root, context->last.paragraph) -
                tiku_rd_find(doc->root, context->first.paragraph) + 1;
        if (info->kind == TIKU_RICHCMD_PARAGRAPH_BREAK)
            count += 2;
        if (count > 1024)
            disable(out, TIKU_RICH_LIMIT,
                "This range exceeds the plain-text replacement limit.");
        return;
    }
    count = tiku_rc_count(doc, context, info->scope);
    if (!count) {
        disable(out, TIKU_RICH_NO_CHANGE,
                "Select text; caret formatting belongs to a view session.");
        return;
    }
    if (count > 1024) {
        disable(out, TIKU_RICH_LIMIT,
                "This command exceeds the 1024-operation transaction limit.");
        return;
    }
    if (info->kind == TIKU_RICHCMD_LINK_REMOVE && !selection.link &&
        !(selection.mixed & TIKU_RICH_MIXED_LINK))
        disable(out, TIKU_RICH_NO_CHANGE, "The selected text has no link.");
}

tiku_rich_status_t
tiku_richcmd_query(const tiku_richdoc_t *doc,
    const tiku_richcmd_context_t *context, tiku_richcmd_kind_t kind,
    tiku_richcmd_state_t *out)
{
    const tiku_richcmd_info_t *info = tiku_richcmd_info(kind);

    if (!out)
        return TIKU_RICH_INVALID;
    memset(out, 0, sizeof *out);
    if (!doc || !context)
        return TIKU_RICH_INVALID;
    if (!info)
        return TIKU_RICH_UNSUPPORTED;
    out->generation = doc->generation;
    out->enabled = 1;
    if (info->scope != TIKU_RICHCMD_DOCUMENT_SCOPE)
        selection_state(doc, context, info, out);
    else if (kind == TIKU_RICHCMD_UNDO && !doc->undo)
        disable(out, TIKU_RICH_NO_CHANGE, "There is no change to undo.");
    else if (kind == TIKU_RICHCMD_REDO && !doc->redo)
        disable(out, TIKU_RICH_NO_CHANGE, "There is no change to redo.");
    if (doc->generation == UINT64_MAX)
        disable(out, TIKU_RICH_LIMIT, "The document generation is exhausted.");
    if (context->generation && context->generation != doc->generation) {
        out->check = TIKU_RICHCMD_CHECK_NONE;
        disable(out, TIKU_RICH_STALE,
                "The document changed; refresh the selection.");
    }
    if (doc->read_only || context->read_only)
        disable(out, TIKU_RICH_READ_ONLY, "Editing is disabled for this host.");
    if (doc->notifying)
        disable(out, TIKU_RICH_BUSY, "A document change is being delivered.");
    return TIKU_RICH_OK;
}
