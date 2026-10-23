/*
 * TikuDesktop Document Kit.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richruler.c - bounded selection ruler and atomic indentation edits.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richruler.h"
#include "tiku_richview_private.h"
#include "tiku_richlayout_private.h"
#include <math.h>
#include <string.h>

/** @brief Resolve one paragraph's indentation and its text-start positions. */
static void
paragraph(const tiku_richview_t *view, size_t index,
    tiku_rich_para_t *props, double position[3])
{
    const tiku_rl_paragraph_t *box = &view->layout->paragraphs[index];
    tiku_rich_chars_t chars;

    tiku_rd_resolve_value(view->doc->root,
        &view->doc->root->paragraphs[index]->value, NULL, &chars, props);
    position[TIKU_RICH_RULER_LEFT] = box->x + props->left + box->list_offset;
    position[TIKU_RICH_RULER_FIRST] = position[TIKU_RICH_RULER_LEFT] +
                                    props->first;
    position[TIKU_RICH_RULER_RIGHT] = box->x + box->width - props->right;
}

/** @brief Find the inclusive paragraph selection using command range rules. */
static void
range(const tiku_richview_t *view, size_t *first, size_t *last)
{
    tiku_richcmd_context_t context = tiku_rs_context(view->session);

    *first = tiku_rd_find(view->doc->root, context.first.paragraph);
    *last = tiku_rd_find(view->doc->root, context.last.paragraph);
    if (*last > *first && !context.last.byte) --*last;
}

/** @brief Intersect handle limits while retaining the first paragraph display. */
static void
accumulate(tiku_rich_ruler_t *ruler, const tiku_rich_para_t *props,
    const double position[3], int first)
{
    double minimum[3] = {ruler->origin,
        ruler->origin + fmax(0, -props->first),
        fmax(position[0], position[1]) + 1};
    double maximum[3] = {position[2] - 1,
        position[2] - 1 - fmax(0, props->first),
        ruler->origin + ruler->width};

    for (unsigned i = 0; i < 3; i++) {
        if (first) {
            ruler->position[i] = position[i];
            ruler->minimum[i] = minimum[i];
            ruler->maximum[i] = maximum[i];
        } else {
            if (fabs(ruler->position[i] - position[i]) > 0.001)
                ruler->mixed |= 1u << i;
            ruler->minimum[i] = fmax(ruler->minimum[i], minimum[i]);
            ruler->maximum[i] = fmin(ruler->maximum[i], maximum[i]);
        }
    }
}

/** @brief Gather bounded geometry without publishing a partially read result. */
static tiku_rich_status_t
read_selection(const tiku_richview_t *view, tiku_rich_ruler_t *out)
{
    size_t first, last;
    const tiku_rl_paragraph_t *box, *base;
    tiku_rich_cells_t cells;
    tiku_rich_para_t props;
    double position[3];

    if (!view) return TIKU_RICH_INVALID;
    if (view->doc->notifying || view->doc->initiator) return TIKU_RICH_BUSY;
    if (!tiku_richview_layout(view)) return tiku_richview_status(view);
    if (tiku_richview_cells(view, &cells) == TIKU_RICH_OK)
        return TIKU_RICH_UNSUPPORTED;
    range(view, &first, &last);
    if (last - first >= 1024) return TIKU_RICH_LIMIT;
    base = &view->layout->paragraphs[first];
    out->origin = base->x;
    out->width = base->width;
    out->paragraphs = last - first + 1;
    for (size_t i = first; i <= last; i++) {
        box = &view->layout->paragraphs[i];
        if (box->owner != base->owner || box->x != base->x ||
            box->width != base->width) return TIKU_RICH_UNSUPPORTED;
        paragraph(view, i, &props, position);
        accumulate(out, &props, position, i == first);
    }
    out->generation = view->doc->generation;
    out->session = tiku_richsession_id(view->session);
    out->anchor = view->session->anchor->revision;
    out->caret = view->session->caret->revision;
    out->viewport = view->width;
    out->zoom = view->zoom;
    out->view = view;
    if (!tiku_richsession_read_only(view->session))
        for (unsigned i = 0; i < 3; i++)
            if (out->minimum[i] <= out->maximum[i]) out->enabled |= 1u << i;
    return TIKU_RICH_OK;
}

tiku_rich_status_t
tiku_richruler_read(const tiku_richview_t *view, tiku_rich_ruler_t *out)
{
    tiku_rich_ruler_t value = {0};
    tiku_rich_status_t status;

    if (!out) return TIKU_RICH_INVALID;
    status = read_selection(view, &value);
    *out = status == TIKU_RICH_OK ? value : (tiku_rich_ruler_t){0};
    return status;
}

tiku_rich_status_t
tiku_richruler_preview(const tiku_rich_ruler_t *source,
    tiku_rich_ruler_part_t part, double x, tiku_rich_ruler_t *out)
{
    tiku_rich_ruler_t value = source ? *source : (tiku_rich_ruler_t){0};

    if (!out) return TIKU_RICH_INVALID;
    memset(out, 0, sizeof *out);
    if (!source || (unsigned)part > TIKU_RICH_RULER_RIGHT || !isfinite(x))
        return TIKU_RICH_INVALID;
    if (!(value.enabled & (1u << part))) return TIKU_RICH_INVALID_RANGE;
    if (!isfinite(value.minimum[part]) || !isfinite(value.maximum[part]) ||
        value.minimum[part] > value.maximum[part]) return TIKU_RICH_INVALID;
    x = fmax(value.minimum[part], fmin(value.maximum[part], x));
    if (part == TIKU_RICH_RULER_LEFT)
        value.position[TIKU_RICH_RULER_FIRST] += x - value.position[part];
    value.position[part] = x;
    value.mixed &= ~(1u << part);
    *out = value;
    return TIKU_RICH_OK;
}

/** @brief Build changes preserving each paragraph's other indent components. */
static size_t
edits(const tiku_richview_t *view, tiku_rich_ruler_part_t part, double x,
    tiku_rich_edit_t *out)
{
    size_t first, last, count = 0;
    tiku_rich_para_t props, changed;
    double position[3];

    range(view, &first, &last);
    for (size_t i = first; i <= last; i++) {
        paragraph(view, i, &props, position);
        changed = props;
        if (part == TIKU_RICH_RULER_FIRST)
            changed.first = (float)(x - position[TIKU_RICH_RULER_LEFT]);
        else if (part == TIKU_RICH_RULER_LEFT)
            changed.left = (float)(props.left + x - position[part]);
        else changed.right = (float)(props.right + position[part] - x);
        if (props.left == changed.left && props.right == changed.right &&
            props.first == changed.first) continue;
        changed.present = TIKU_RICH_INDENT;
        out[count++] = (tiku_rich_edit_t){.kind = TIKU_RICH_PARAGRAPH_FORMAT,
            .paragraph = view->doc->root->paragraphs[i]->value.id,
            .props = changed};
    }
    return count;
}

tiku_rich_status_t
tiku_richruler_apply(tiku_richview_t *view, const tiku_rich_ruler_t *source,
    tiku_rich_ruler_part_t part, double x)
{
    tiku_rich_ruler_t current, preview;
    tiku_rich_status_t status;
    tiku_rich_edit_t *changes;
    tiku_rich_transaction_t tx = {0};

    if (!view || !source || (unsigned)part > TIKU_RICH_RULER_RIGHT ||
        !isfinite(x)) return TIKU_RICH_INVALID;
    if (view->doc->notifying || view->doc->initiator) return TIKU_RICH_BUSY;
    if (source->view != view || source->generation != view->doc->generation ||
        source->session != tiku_richsession_id(view->session) ||
        source->anchor != view->session->anchor->revision ||
        source->caret != view->session->caret->revision ||
        source->viewport != view->width || source->zoom != view->zoom)
        return TIKU_RICH_STALE;
    if (tiku_richsession_read_only(view->session)) return TIKU_RICH_READ_ONLY;
    status = tiku_richruler_read(view, &current);
    if (status != TIKU_RICH_OK) return status;
    if (memcmp(current.position, source->position, sizeof current.position) ||
        current.origin != source->origin || current.width != source->width)
        return TIKU_RICH_STALE;
    status = tiku_richruler_preview(&current, part, x, &preview);
    if (status != TIKU_RICH_OK) return status;
    changes = tiku_rd_alloc(view->doc->memory,
        current.paragraphs * sizeof *changes, &status);
    if (!changes) return status;
    tx.generation = current.generation;
    tx.edits = changes;
    tx.count = edits(view, part, preview.position[part], changes);
    view->doc->initiator = view->session;
    status = tx.count ? tiku_richdoc_apply(view->doc, &tx) : TIKU_RICH_NO_CHANGE;
    view->doc->initiator = NULL;
    tiku_rd_free(changes);
    return status;
}
