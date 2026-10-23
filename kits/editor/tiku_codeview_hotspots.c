/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_codeview_hotspots.c - runs of text a click follows: which one is
 * under the pointer while the hotspot keys are held, and a press and
 * release on it handed to the host, the caret left where it is.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_codeview_private.h"

/* The bytes past the pointer's character that a long line's colouring
 * must reach, to find where the hotspot under the pointer ends; a hotspot
 * that runs further may be cut short. */
#define HOT_REACH 256

void
tiku_codeview_set_hotspot_keys(tiku_codeview_t *view, unsigned modifiers)
{
    if (view == NULL)
        return;
    view->hot_keys = modifiers & ~TIKU_MOD_CAPS;
    tiku_cv_hot_forget(view);
}

int
tiku_codeview_hotspot(const tiku_codeview_t *view, tiku_edit_range_t *range)
{
    if (view == NULL || !view->hot_shown)
        return 0;
    if (range != NULL)
        *range = view->hot;
    return 1;
}

int
tiku_cv_hot_shown(const tiku_codeview_t *v, tiku_edit_range_t *range)
{
    return tiku_codeview_hotspot(v, range);
}

void
tiku_cv_hot_forget(tiku_codeview_t *v)
{
    v->hot_shown = 0;
    v->hot_pressed = 0;
}

/** @brief Whether any style ID's flags make a hotspot. */
static int
any_hotspots(const tiku_codeview_t *v)
{
    int i;

    for (i = 0; i < (int)TIKU_CODEVIEW_STYLE_COUNT; i++) {
        if (v->ink_styles[i] & TIKU_CODEVIEW_HOTSPOT)
            return 1;
    }
    return 0;
}

/** @brief The character under surface @p x, @p y in a row of text, or -1
 *         when the point is past the rows, a row's text or the text's
 *         area; an annotation's row holds no text. */
static tiku_edit_pos_t
char_under(tiku_codeview_t *v, int x, int y, int64_t *line)
{
    tiku_rect_t area = tiku_cv_area(v);
    int64_t row, top = v->top, sub = v->top_sub;
    tiku_edit_pos_t pos;
    tiku_cv_row_t w;

    if (x < tiku_cv_text_x(v) || x >= area.x + area.w ||
        y < tiku_cv_text_y(v) || y >= area.y + area.h)
        return -1;
    tiku_cv_rows_ready(v);
    row = ((int64_t)y - tiku_cv_text_y(v)) / tiku_codeview_row_height(v);
    if (tiku_cv_rows_step(v, &top, &sub, row) != row)
        return -1;
    tiku_cv_row(v, top, sub, &w);
    pos = tiku_cv_char_at(v, w.line,
                          (int64_t)x - tiku_cv_text_x(v) + v->left + w.x0);
    if (pos < w.start || pos >= w.end)
        return -1;
    *line = w.line;
    return pos;
}

/** @brief The hotspot holding @p pos on @p line, into @p out: the run
 *         round it, when its ink's style makes hotspots.
 *         tiku_cv_style_line() joins neighbouring runs of one ink, so a
 *         run is the whole of its ink there; text past the runs, or on a
 *         line with none, is a plain run of its own. */
static int
hotspot_at(tiku_codeview_t *v, int64_t line, tiku_edit_pos_t pos,
           tiku_edit_range_t *out)
{
    tiku_edit_pos_t start = tiku_editdoc_line_start(v->doc, line);
    int64_t at = pos - start, from = 0;
    size_t n = 0, i;
    const tiku_span_t *runs = tiku_cv_style_line(v, line, pos + HOT_REACH,
                                                 &n);
    tiku_ink_t ink;

    if (runs == NULL)
        n = 0;
    for (i = 0; i < n && at >= from + runs[i].len; i++)
        from += runs[i].len;
    ink = i < n ? runs[i].ink : TIKU_INK_PLAIN;
    if ((unsigned)ink >= TIKU_CODEVIEW_STYLE_COUNT ||
        !(v->ink_styles[ink] & TIKU_CODEVIEW_HOTSPOT))
        return 0;
    out->start = start + from;
    out->end = i < n ? out->start + runs[i].len
                     : tiku_editdoc_line_end(v->doc, line);
    return 1;
}

/** @brief Note the pointer at @p x, @p y with @p mods held, and find the
 *         live hotspot under it; @return whether what is shown changed. */
static int
note(tiku_codeview_t *v, int x, int y, unsigned mods)
{
    int was = v->hot_shown;
    tiku_edit_range_t before = v->hot;
    tiku_edit_pos_t pos;
    int64_t line = 0;

    v->hot_seen = 1;
    v->hot_x = x;
    v->hot_y = y;
    v->hot_mods = mods & ~TIKU_MOD_CAPS;
    v->hot_shown = 0;
    if (v->hot_mods == v->hot_keys && any_hotspots(v) &&
        (pos = char_under(v, x, y, &line)) >= 0)
        v->hot_shown = hotspot_at(v, line, pos, &v->hot);
    return was != v->hot_shown || (was && (before.start != v->hot.start ||
                                           before.end != v->hot.end));
}

void
tiku_cv_hot_after(tiku_codeview_t *v, const tiku_event_t *e)
{
    if (v->hot_pressed || v->dragging)
        return;
    if (e->type == TIKU_EVENT_WHEEL && v->hot_seen)
        (void)note(v, v->hot_x, v->hot_y, v->hot_mods);
    else if (e->type == TIKU_EVENT_POINTER_UP && e->button == 1)
        (void)note(v, e->x, e->y, e->modifiers);
}

/** @brief The modifier a modifier key holds, or 0 for any other key. */
static unsigned
modifier_of(unsigned key)
{
    switch (key) {
    case TIKU_KEY_SHIFT:  return TIKU_MOD_SHIFT;
    case TIKU_KEY_CMD:    return TIKU_MOD_CMD;
    case TIKU_KEY_OPTION: return TIKU_MOD_OPTION;
    default:              return 0;
    }
}

/** @brief Find the live hotspot again where the pointer was last seen,
 *         with modifier key @p e gone down or up; @return whether what is
 *         shown changed. */
static int
modifier_key(tiku_codeview_t *v, const tiku_event_t *e)
{
    unsigned bit = modifier_of(e->key);
    unsigned mods = e->modifiers;

    if (bit == 0 || !v->hot_seen || v->dragging || v->hot_pressed)
        return 0;
    mods = e->type == TIKU_EVENT_KEY_DOWN ? mods | bit : mods & ~bit;
    return note(v, v->hot_x, v->hot_y, mods);
}

/** @brief Button 1 came up: call the host's hotspot() when the pointer is
 *         over the hotspot the press went down on. */
static void
release(tiku_codeview_t *v, const tiku_event_t *e)
{
    tiku_edit_range_t pressed = v->hot_press;

    v->hot_pressed = 0;
    (void)note(v, e->x, e->y, e->modifiers);
    if (v->hot_shown && v->hot.start == pressed.start &&
        v->hot.end == pressed.end && v->host.hotspot != NULL)
        v->host.hotspot(v->host.ctx, v, pressed, e->modifiers);
}

int
tiku_cv_hot_event(tiku_codeview_t *v, const tiku_event_t *e)
{
    switch (e->type) {
    case TIKU_EVENT_POINTER_MOVE:
        if (v->dragging || v->held_bar || v->arrow_bar)
            return 0;
        return note(v, e->x, e->y, e->modifiers) || v->hot_pressed;
    case TIKU_EVENT_POINTER_DOWN:
        if (e->button != 1 || v->dragging || v->held_bar || v->arrow_bar)
            return 0;
        (void)note(v, e->x, e->y, e->modifiers);
        if (!v->hot_shown)
            return 0;
        v->hot_pressed = 1;
        v->hot_press = v->hot;
        return 1;
    case TIKU_EVENT_POINTER_UP:
        if (e->button != 1 || !v->hot_pressed)
            return 0;
        release(v, e);
        return 1;
    case TIKU_EVENT_KEY_DOWN:
    case TIKU_EVENT_KEY_UP:
        return modifier_key(v, e);
    case TIKU_EVENT_DEACTIVATED:
        tiku_cv_hot_forget(v);
        v->hot_seen = 0;
        return 0;
    default:
        return 0;
    }
}
