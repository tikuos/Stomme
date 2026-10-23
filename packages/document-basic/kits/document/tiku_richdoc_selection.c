/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richdoc_selection.c - uniform and mixed selection-format queries.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richdoc_private.h"

#include <string.h>

/** @brief Retain uniform character values and mark differing property bits. */
static void
merge_chars(tiku_rich_selection_t *out, const tiku_rich_chars_t *value)
{
    tiku_rich_chars_t a, b;
    unsigned bit;

    for (bit = 1; bit <= TIKU_RICH_BASELINE; bit <<= 1) {
        a = out->chars;
        b = *value;
        tiku_rd_chars_reset(&a, ~bit);
        tiku_rd_chars_reset(&b, ~bit);
        if (!tiku_rd_chars_equal(&a, &b))
            out->mixed_chars |= bit;
    }
    tiku_rd_chars_reset(&out->chars, out->mixed_chars);
}

/** @brief Retain uniform paragraph values and mark differing groups. */
static void
merge_props(tiku_rich_selection_t *out, const tiku_rich_para_t *value)
{
    tiku_rich_para_t a, b, kept;
    unsigned bit;

    for (bit = 1; bit <= TIKU_RICH_OUTLINE; bit <<= 1) {
        kept = out->props;
        kept.present &= bit;
        tiku_rd_props(&kept, &a);
        kept = *value;
        kept.present &= bit;
        tiku_rd_props(&kept, &b);
        if (!tiku_rd_props_equal(&a, &b))
            out->mixed_props |= bit;
    }
    kept = out->props;
    kept.present &= ~out->mixed_props;
    tiku_rd_props(&kept, &out->props);
}

/** @brief Merge one paragraph's resolved properties and structural settings. */
static void
paragraph_sample(tiku_rich_selection_t *out,
    const tiku_rich_paragraph_t *p, const tiku_rich_para_t *props)
{
    if (!out->paragraphs++) {
        out->props = *props;
        out->paragraph_style = p->style;
        out->list = p->list;
        return;
    }
    merge_props(out, props);
    if (out->paragraph_style != p->style) {
        out->mixed |= TIKU_RICH_MIXED_PARAGRAPH_STYLE;
        out->paragraph_style = 0;
    }
    if (!tiku_rd_list_equal(&out->list, &p->list)) {
        out->mixed |= TIKU_RICH_MIXED_LIST;
        memset(&out->list, 0, sizeof out->list);
    }
}

/** @brief Merge one selected run's resolved appearance, style and target. */
static tiku_rich_status_t
character_sample(const tiku_rd_root_t *root, const tiku_rich_paragraph_t *p,
    const tiku_rich_run_t *run, tiku_rich_selection_t *out, int *sampled)
{
    tiku_rich_chars_t chars;
    tiku_rich_para_t props;
    tiku_rich_status_t status;
    uint64_t style = run ? run->style : 0;
    const char *link = run ? run->link : NULL;

    status = tiku_rd_resolve_value(root, p, run, &chars, &props);
    if (status != TIKU_RICH_OK)
        return status;
    if (!*sampled) {
        out->chars = chars;
        out->character_style = style;
        out->link = link;
        *sampled = 1;
        return TIKU_RICH_OK;
    }
    merge_chars(out, &chars);
    if (out->character_style != style) {
        out->mixed |= TIKU_RICH_MIXED_CHARACTER_STYLE;
        out->character_style = 0;
    }
    if (!tiku_rd_link_equal(out->link, link)) {
        out->mixed |= TIKU_RICH_MIXED_LINK;
        out->link = NULL;
    }
    return TIKU_RICH_OK;
}

/** @brief Sample selected runs, or the preceding insertion run for a caret. */
static tiku_rich_status_t
sample_runs(const tiku_rd_root_t *root, const tiku_rich_paragraph_t *p,
    size_t first, size_t last, tiku_rich_selection_t *out, int *sampled)
{
    size_t i, at = 0;
    tiku_rich_status_t status;
    const tiku_rich_run_t *run;

    for (i = 0; i < p->run_count; i++) {
        run = &p->runs[i];
        if ((out->empty && at < first && first <= at + run->length) ||
            (!out->empty && at < last && first < at + run->length)) {
            status = character_sample(root, p, run, out, sampled);
            if (status != TIKU_RICH_OK)
                return status;
        }
        at += run->length;
    }
    return TIKU_RICH_OK;
}

tiku_rich_status_t
tiku_richsnapshot_selection(const tiku_richsnapshot_t *snapshot,
    tiku_rich_pos_t first, tiku_rich_pos_t last, tiku_rich_selection_t *out)
{
    tiku_rich_selection_t result = {0};
    tiku_rich_status_t status;
    const tiku_rich_paragraph_t *p;
    tiku_rich_chars_t chars;
    tiku_rich_para_t props;
    size_t begin, end, i;
    int order, sampled = 0;

    if (!out)
        return TIKU_RICH_INVALID;
    memset(out, 0, sizeof *out);
    if (!snapshot)
        return TIKU_RICH_INVALID;
    status = tiku_richsnapshot_compare(snapshot, first, last, &order);
    if (status != TIKU_RICH_OK)
        return status;
    if (order > 0)
        return TIKU_RICH_INVALID_RANGE;
    result.empty = order == 0;
    begin = tiku_rd_find(snapshot->root, first.paragraph);
    end = tiku_rd_find(snapshot->root, last.paragraph);
    if (end > begin && !last.byte)
        end--;
    for (i = begin; i <= end; i++) {
        p = &snapshot->root->paragraphs[i]->value;
        status = tiku_rd_resolve_value(snapshot->root, p, NULL, &chars, &props);
        if (status != TIKU_RICH_OK)
            return status;
        paragraph_sample(&result, p, &props);
        status = sample_runs(snapshot->root, p, i == begin ? first.byte : 0,
            p->id == last.paragraph ? last.byte : p->length,
            &result, &sampled);
        if (status != TIKU_RICH_OK)
            return status;
    }
    p = &snapshot->root->paragraphs[begin]->value;
    if (!sampled) {
        status = character_sample(snapshot->root, p, NULL, &result, &sampled);
        if (status != TIKU_RICH_OK)
            return status;
    }
    if (result.empty)
        result.link = tiku_rd_typing_link(p, first.byte, first.byte);
    *out = result;
    return TIKU_RICH_OK;
}
