/*
 * TikuOS Interface Kit.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_html_view_event.c - HTML view pointer selection, links and keyboard input.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_html_view.h"
#include "tiku_clipboard.h"

/** @brief Whether a document viewport contains a pointer coordinate. */
static int view_inside(tiku_rect_t r, int x, int y)
{
    return x >= r.x && y >= r.y && x-r.x < r.w && y-r.y < r.h;
}

/** @brief Open an internal anchor or deliver a non-anchor link to the host. */
static int view_link(tiku_html_view_t *v, int link)
{
    if (link < 0 || link >= v->doc->nlink) return 0;
    const char *href = tiku_doc_link_href(v->doc,link);
    if (*href == '#') {
        int y = tiku_doc_anchor_y(v->doc,href+1);
        if (y >= 0) tiku_html_view_scroll(v,y);
    } else if (v->link) v->link(v->link_ctx,href);
    return 1;
}

/** @brief Copy the selected bytes without including block separator terminators. */
static int view_copy(tiku_html_view_t *v)
{
    int a = v->a < v->b ? v->a : v->b, b = v->a > v->b ? v->a : v->b;
    if (a < 0 || b > v->length || a >= b || !v->text) return 0;
    return tiku_clipboard_import(v->text+a,(size_t)(b-a));
}

/** @brief Handle keyboard scrolling, select/copy and focused-link traversal. */
static int view_key(tiku_html_view_t *v, const tiku_event_t *e)
{
    if ((e->modifiers & TIKU_MOD_CMD) && (e->key == 'a' || e->key == 'A')) {
        v->a = 0;v->b = v->length;return 1;
    }
    if ((e->modifiers & TIKU_MOD_CMD) && (e->key == 'c' || e->key == 'C')) return view_copy(v);
    if (e->key == TIKU_KEY_TAB) {
        v->focus_link = tiku_doc_link_next(v->doc,v->focus_link,e->modifiers & TIKU_MOD_SHIFT);
        int first, count;
        if (tiku_doc_link_pieces(v->doc,v->focus_link,&first,&count) && count)
            tiku_html_view_scroll(v,tiku_doc_piece_rect(v->doc,first).y);
        return 1;
    }
    if (e->key == TIKU_KEY_RETURN) return view_link(v,v->focus_link);
    if (e->key == TIKU_KEY_ESCAPE) { v->a = v->b;v->focus_link = -1;return 1; }
    int offset = v->scroll, line = tiku_measure_height(TIKU_MEASURE_PLAIN);
    switch (e->key) {
    case TIKU_KEY_UP: offset -= line;break;
    case TIKU_KEY_DOWN: offset += line;break;
    case TIKU_KEY_PAGE_UP: offset -= v->viewport.h;break;
    case TIKU_KEY_PAGE_DOWN: offset += v->viewport.h;break;
    case TIKU_KEY_HOME: offset = 0;break;
    case TIKU_KEY_END: offset = v->doc->height;break;
    default: return 0;
    }
    tiku_html_view_scroll(v,offset);return 1;
}

int tiku_html_view_event(tiku_html_view_t *v, const tiku_event_t *e)
{
    if (!v || !v->doc || !e) return 0;
    if (e->type == TIKU_EVENT_DEACTIVATED) { v->dragging = 0;return 0; }
    if (e->type == TIKU_EVENT_KEY_DOWN) return view_key(v,e);
    int inside = view_inside(v->viewport,e->x,e->y);
    if (e->type == TIKU_EVENT_WHEEL && inside) {
        int64_t offset = (int64_t)v->scroll-(int64_t)e->dy*tiku_measure_height(TIKU_MEASURE_PLAIN)*3;
        if (offset > INT32_MAX) offset = INT32_MAX;
        if (offset < 0) offset = 0;
        tiku_html_view_scroll(v,(int)offset);return 1;
    }
    int x = e->x-v->viewport.x, y = e->y-v->viewport.y+v->scroll;
    if (e->type == TIKU_EVENT_POINTER_DOWN && e->button == 1 && inside) {
        int at = tiku_html_text_offset(v->doc,v->starts,v->length,x,y,&v->block);
        v->down_link = tiku_doc_hit(v->doc,x,y).link;
        if (at >= 0) { v->a = v->b = at;v->dragging = 1;return 1; }
        return view_link(v,v->down_link);
    }
    if (e->type == TIKU_EVENT_POINTER_MOVE && v->dragging) {
        if (e->y < v->viewport.y) tiku_html_view_scroll(v,v->scroll-16);
        else if (e->y >= v->viewport.y+v->viewport.h) tiku_html_view_scroll(v,v->scroll+16);
        y = e->y-v->viewport.y+v->scroll;
        int at = tiku_html_text_offset(v->doc,v->starts,v->length,x,y,&v->block);
        if (at >= 0) v->b = at;
        return 1;
    }
    if (e->type == TIKU_EVENT_POINTER_UP && v->dragging) {
        v->dragging = 0;
        if (inside && v->a == v->b && v->down_link >= 0 && tiku_doc_hit(v->doc,x,y).link == v->down_link)
            return view_link(v,v->down_link);
        return 1;
    }
    return 0;
}
