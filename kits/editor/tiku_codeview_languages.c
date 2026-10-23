/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_codeview_languages.c - what a language brings to a view besides its
 * colouring: the styles its runs are drawn in, and fold regions told by a
 * table of levels its parser supplies or, for JSON, read from the brackets.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_codeview_private.h"

#include <string.h>

/* The depth bits of a fold level. */
#define DEPTH 0xffffu

/* The flags a style may carry, and the colours it may set. */
#define STYLE_FLAGS (TIKU_CODEVIEW_BOLD | TIKU_CODEVIEW_ITALIC | \
                     TIKU_CODEVIEW_UNDERLINE | TIKU_CODEVIEW_HOTSPOT)
#define STYLE_COLORS (TIKU_CODEVIEW_STYLE_FORE | TIKU_CODEVIEW_STYLE_BACK)

tiku_ink_t
tiku_cv_style_role(const tiku_codeview_t *v, unsigned id)
{
    if (id < TIKU_CODEVIEW_STYLE_COUNT && v->style_defined[id])
        return v->style_defs[id].role;
    return id < TIKU_CV_INKS ? (tiku_ink_t)id : TIKU_INK_PLAIN;
}

tiku_rgb_t
tiku_cv_style_color(const tiku_codeview_t *v, unsigned id)
{
    if (id < TIKU_CODEVIEW_STYLE_COUNT && v->style_defined[id] &&
        (v->style_defs[id].colors & TIKU_CODEVIEW_STYLE_FORE))
        return v->style_defs[id].foreground;
    return tiku_ink(tiku_cv_style_role(v, id));
}

tiku_edit_status_t
tiku_codeview_set_style(tiku_codeview_t *view, unsigned id,
                        const tiku_codeview_style_t *style)
{
    if (view == NULL || id >= TIKU_CODEVIEW_STYLE_COUNT)
        return TIKU_EDIT_INVALID;
    if (style != NULL && ((unsigned)style->role >= TIKU_CV_INKS ||
                          (style->flags & ~STYLE_FLAGS) != 0 ||
                          (style->colors & ~STYLE_COLORS) != 0))
        return TIKU_EDIT_INVALID;
    if (view->lexing)
        return TIKU_EDIT_BUSY;
    view->style_defined[id] = style != NULL;
    view->ink_styles[id] = style != NULL ? style->flags : 0;
    if (style != NULL)
        view->style_defs[id] = *style;
    tiku_codeview_lexer_changed(view);
    return TIKU_EDIT_OK;
}

tiku_edit_status_t
tiku_codeview_get_style(const tiku_codeview_t *view, unsigned id,
                        tiku_codeview_style_t *style)
{
    if (view == NULL || style == NULL || id >= TIKU_CODEVIEW_STYLE_COUNT)
        return TIKU_EDIT_INVALID;
    if (!view->style_defined[id] && id >= TIKU_CV_INKS)
        return TIKU_EDIT_NOTHING;
    if (view->style_defined[id]) {
        *style = view->style_defs[id];
    } else {
        memset(style, 0, sizeof *style);
        style->role = (tiku_ink_t)id;
    }
    style->flags = view->ink_styles[id];
    return TIKU_EDIT_OK;
}

/** @brief TIKU_EDIT_OK when @p count levels can be folded by in the
 *         document at @p generation: one for each line, with no bits but
 *         the depth and the header flag, and each header followed by a
 *         deeper line; else the status saying which does not hold. */
static tiku_edit_status_t
levels_ok(const tiku_codeview_t *v, const uint32_t *levels, size_t count,
          uint64_t generation)
{
    size_t i;

    if (generation != tiku_editdoc_generation(v->doc))
        return TIKU_EDIT_STALE;
    if ((uint64_t)count != (uint64_t)tiku_editdoc_lines(v->doc))
        return TIKU_EDIT_INVALID_RANGE;
    if (count > SIZE_MAX / sizeof *levels)
        return TIKU_EDIT_LIMIT;
    for (i = 0; i < count; i++) {
        if (levels[i] & ~(TIKU_CODEVIEW_FOLD_HEADER | DEPTH))
            return TIKU_EDIT_INVALID;
        if ((levels[i] & TIKU_CODEVIEW_FOLD_HEADER) &&
            (i + 1 == count ||
             (levels[i + 1] & DEPTH) <= (levels[i] & DEPTH)))
            return TIKU_EDIT_INVALID_RANGE;
    }
    return TIKU_EDIT_OK;
}

tiku_edit_status_t
tiku_codeview_set_fold_levels(tiku_codeview_t *view, const uint32_t *levels,
                              size_t count, uint64_t generation)
{
    uint32_t *copy = NULL;
    tiku_edit_status_t st;

    if (view == NULL || (levels == NULL && count > 0))
        return TIKU_EDIT_INVALID;
    if (view->lexing || view->doc->busy)
        return TIKU_EDIT_BUSY;
    if (levels == NULL && view->fold_levels == NULL)
        return TIKU_EDIT_OK;
    if (levels != NULL) {
        st = levels_ok(view, levels, count, generation);
        if (st != TIKU_EDIT_OK)
            return st;
        if (view->fold_levels != NULL && count == view->fold_level_count &&
            generation == view->fold_generation &&
            memcmp(levels, view->fold_levels, count * sizeof *levels) == 0)
            return TIKU_EDIT_OK;
        copy = tiku_ed_alloc(count * sizeof *copy);
        if (copy == NULL)
            return TIKU_EDIT_NO_MEMORY;
        memcpy(copy, levels, count * sizeof *copy);
    }
    tiku_ed_free(view->fold_levels);
    view->fold_levels = copy;
    view->fold_level_count = count;
    view->fold_generation = generation;
    view->json_folds = 0;
    tiku_cv_folds_refit(view);
    return TIKU_EDIT_OK;
}

int64_t
tiku_cv_supplied_fold(const tiku_codeview_t *v, int64_t line)
{
    uint32_t depth;
    int64_t end;

    if (v->fold_levels == NULL ||
        v->fold_generation != tiku_editdoc_generation(v->doc) ||
        line < 0 || (uint64_t)line >= v->fold_level_count ||
        !(v->fold_levels[line] & TIKU_CODEVIEW_FOLD_HEADER))
        return -1;
    depth = v->fold_levels[line] & DEPTH;
    end = line + 1;
    while ((uint64_t)end < v->fold_level_count &&
           (v->fold_levels[end] & DEPTH) > depth)
        end++;
    return end - 1 > line ? end - 1 : -1;
}

/** @brief Read the levels of @p lines lines of JSON @p text into
 *         @p levels: a line's depth is the lowest its brackets reach, and
 *         it heads a region when its depth at its end is greater.
 *         Brackets in strings are not counted.  TIKU_EDIT_LIMIT when the
 *         brackets nest deeper than a level holds. */
static tiku_edit_status_t
json_levels(const char *text, size_t length, uint32_t *levels, size_t lines)
{
    uint32_t depth = 0, low = 0;
    int quoted = 0, escaped = 0;
    size_t i, line = 0;

    for (i = 0; i <= length && line < lines; i++) {
        unsigned char c = i < length ? (unsigned char)text[i] : 0;

        if (c == 0 || c == '\r' || c == '\n') {
            levels[line++] = low |
                             (depth > low ? TIKU_CODEVIEW_FOLD_HEADER : 0);
            if (c == '\r' && i + 1 < length && text[i + 1] == '\n')
                i++;
            low = depth;
            quoted = 0;
            escaped = 0;
        } else if (quoted) {
            if (escaped)
                escaped = 0;
            else if (c == '\\')
                escaped = 1;
            else if (c == '"')
                quoted = 0;
        } else if (c == '"') {
            quoted = 1;
        } else if (c == '{' || c == '[') {
            if (depth == DEPTH)
                return TIKU_EDIT_LIMIT;
            depth++;
        } else if (c == '}' || c == ']') {
            if (depth > 0)
                depth--;
            if (depth < low)
                low = depth;
        }
    }
    /* A bracket left open on the last line, or closed on the next, heads
     * no line. */
    for (i = 0; i < lines; i++)
        if (i + 1 == lines || (levels[i + 1] & DEPTH) <= (levels[i] & DEPTH))
            levels[i] &= DEPTH;
    return TIKU_EDIT_OK;
}

tiku_edit_status_t
tiku_codeview_json_folds(tiku_codeview_t *view)
{
    tiku_edit_snapshot_t snapshot;
    tiku_edit_status_t st;
    uint32_t *levels;
    size_t lines;

    if (view == NULL)
        return TIKU_EDIT_INVALID;
    if (view->json_folds &&
        view->fold_generation == tiku_editdoc_generation(view->doc))
        return TIKU_EDIT_OK;
    st = tiku_editdoc_snapshot(view->doc, &snapshot);
    if (st != TIKU_EDIT_OK)
        return st;
    lines = (size_t)tiku_editdoc_lines(view->doc);
    levels = lines <= SIZE_MAX / sizeof *levels ?
             tiku_ed_alloc(lines * sizeof *levels) : NULL;
    if (levels == NULL) {
        tiku_edit_snapshot_free(&snapshot);
        return TIKU_EDIT_NO_MEMORY;
    }
    st = json_levels(snapshot.text, snapshot.length, levels, lines);
    if (st == TIKU_EDIT_OK)
        st = tiku_codeview_set_fold_levels(view, levels, lines,
                                           snapshot.generation);
    if (st == TIKU_EDIT_OK)
        view->json_folds = 1;
    tiku_ed_free(levels);
    tiku_edit_snapshot_free(&snapshot);
    return st;
}
