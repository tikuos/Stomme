/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richcmd_apply.c - range command expansion into atomic edit groups.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richcmd_private.h"

/** @brief Translate a command payload into a paragraph-independent edit. */
static tiku_rich_edit_t
edit_template(const tiku_richcmd_t *command, const tiku_richcmd_state_t *state)
{
    tiku_rich_edit_t edit = {.kind = TIKU_RICH_FORMAT};
    unsigned bit = tiku_rc_toggle_bit(command->kind);
    unsigned char enabled = state->check != TIKU_RICHCMD_CHECK_ON;

    if (bit) {
        edit.chars = (tiku_rich_chars_t){.present = bit, .bold = enabled,
            .italic = enabled, .underline = enabled, .strike = enabled};
        return edit;
    }
    if (command->kind >= TIKU_RICHCMD_ALIGN_LEFT &&
        command->kind <= TIKU_RICHCMD_ALIGN_JUSTIFY) {
        edit.kind = TIKU_RICH_PARAGRAPH_FORMAT;
        edit.props.present = TIKU_RICH_ALIGNMENT;
        edit.props.align = tiku_rc_alignment(command->kind);
        return edit;
    }
    switch (command->kind) {
    case TIKU_RICHCMD_CHARACTER_FORMAT:
        edit.chars = command->value.character.values;
        edit.clear_chars = command->value.character.clear;
        break;
    case TIKU_RICHCMD_CLEAR_CHARACTER_OVERRIDES:
        edit.clear_chars = 511u;
        break;
    case TIKU_RICHCMD_PARAGRAPH_FORMAT:
        edit.kind = TIKU_RICH_PARAGRAPH_FORMAT;
        edit.props = command->value.paragraph.values;
        edit.chars = command->value.paragraph.chars;
        edit.clear_props = command->value.paragraph.clear_props;
        edit.clear_chars = command->value.paragraph.clear_chars;
        break;
    case TIKU_RICHCMD_PARAGRAPH_STYLE:
    case TIKU_RICHCMD_CHARACTER_STYLE:
        edit.kind = command->kind == TIKU_RICHCMD_PARAGRAPH_STYLE ?
            TIKU_RICH_PARAGRAPH_STYLE_SET : TIKU_RICH_CHARACTER_STYLE_SET;
        edit.style = command->value.style;
        break;
    case TIKU_RICHCMD_LIST_SET:
        edit.kind = TIKU_RICH_LIST_SET;
        edit.list = command->value.list;
        break;
    case TIKU_RICHCMD_LINK_SET:
    case TIKU_RICHCMD_LINK_REMOVE:
        edit.kind = TIKU_RICH_LINK_SET;
        edit.link = command->kind == TIKU_RICHCMD_LINK_SET ?
                    command->value.link : NULL;
        break;
    case TIKU_RICHCMD_STYLE_DEFINE:
        edit.kind = TIKU_RICH_STYLE_DEFINE;
        edit.style = command->value.definition.style;
        edit.definition = &command->value.definition.value;
        break;
    case TIKU_RICHCMD_STYLE_REMOVE:
        edit.kind = TIKU_RICH_STYLE_REMOVE;
        edit.style = command->value.style;
        break;
    default:
        break;
    }
    return edit;
}

/** @brief Validate range payloads before allocating command expansion. */
static tiku_rich_status_t
validate(const tiku_rd_root_t *root, const tiku_rich_edit_t *edit)
{
    tiku_rich_chars_t chars;
    tiku_rich_para_t props;
    tiku_rich_list_t list;

    if ((edit->clear_chars & ~511u) || (edit->clear_props & ~63u) ||
        tiku_rd_chars(&edit->chars, &chars) != TIKU_RICH_OK ||
        tiku_rd_props(&edit->props, &props) != TIKU_RICH_OK)
        return TIKU_RICH_INVALID_STYLE;
    if (edit->kind == TIKU_RICH_LIST_SET)
        return tiku_rd_list(&edit->list, &list);
    if (edit->kind == TIKU_RICH_LINK_SET)
        return tiku_rich_link_validate(edit->link);
    if (edit->kind == TIKU_RICH_PARAGRAPH_STYLE_SET &&
        !tiku_rd_style_ref(root, edit->style, TIKU_RICH_PARAGRAPH_STYLE))
        return TIKU_RICH_INVALID_STYLE;
    if (edit->kind == TIKU_RICH_CHARACTER_STYLE_SET &&
        !tiku_rd_style_ref(root, edit->style, TIKU_RICH_CHARACTER_STYLE))
        return TIKU_RICH_INVALID_STYLE;
    return TIKU_RICH_OK;
}

/** @brief Fill an exactly sized array for a previously checked selection. */
static void
fill(const tiku_richdoc_t *doc, const tiku_richcmd_context_t *context,
    tiku_richcmd_scope_t scope, const tiku_rich_edit_t *base,
    tiku_rich_edit_t *edits)
{
    size_t begin = tiku_rd_find(doc->root, context->first.paragraph);
    size_t end = tiku_rd_find(doc->root, context->last.paragraph);
    size_t i, n = 0, first, last;
    const tiku_rich_paragraph_t *p;

    for (i = begin; i <= end; i++) {
        p = &doc->root->paragraphs[i]->value;
        first = i == begin ? context->first.byte : 0;
        last = i == end ? context->last.byte : p->length;
        if (scope == TIKU_RICHCMD_CHARACTER_SCOPE && first == last)
            continue;
        if (scope == TIKU_RICHCMD_PARAGRAPH_SCOPE &&
            end > begin && i == end && !last)
            continue;
        edits[n] = *base;
        edits[n].paragraph = p->id;
        if (scope == TIKU_RICHCMD_CHARACTER_SCOPE) {
            edits[n].start = first;
            edits[n].end = last;
        }
        if (n && base->kind == TIKU_RICH_LIST_SET)
            edits[n].list.restart = 0;
        n++;
    }
}

tiku_rich_status_t
tiku_rc_apply(tiku_richdoc_t *doc, const tiku_richcmd_context_t *context,
    const tiku_richcmd_t *command, const tiku_richcmd_state_t *state)
{
    tiku_rich_edit_t edit = edit_template(command, state), *edits;
    tiku_rich_transaction_t tx = {context->generation, &edit, 1};
    tiku_richcmd_scope_t scope = tiku_richcmd_info(command->kind)->scope;
    tiku_rich_status_t status;

    if (scope == TIKU_RICHCMD_DOCUMENT_SCOPE)
        return tiku_richdoc_apply(doc, &tx);
    status = validate(doc->root, &edit);
    if (status != TIKU_RICH_OK)
        return status;
    tx.count = tiku_rc_count(doc, context, scope);
    edits = tiku_rd_alloc(doc->memory, tx.count * sizeof *edits, &status);
    if (!edits)
        return status;
    fill(doc, context, scope, &edit, edits);
    tx.edits = edits;
    status = tiku_richdoc_apply(doc, &tx);
    tiku_rd_free(edits);
    return status;
}
