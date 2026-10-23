/*
 * TikuOS Interface Kit.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_html_view_paint.c - view selection and find highlights on document pieces.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_html_view.h"
#include "tiku_ui.h"
#include "tiku_font.h"
#include <string.h>

/** @brief Mix a highlight into the document background while retaining text contrast. */
static tiku_rgb_t highlight_color(tiku_rgb_t accent)
{
    tiku_rgb_t page = TIKU_C_DOC;
    unsigned r = (((page>>16)&255)*3+((accent>>16)&255))/4;
    unsigned g = (((page>>8)&255)*3+((accent>>8)&255))/4;
    unsigned b = ((page&255)*3+(accent&255))/4;
    return TIKU_RGB(r,g,b);
}

/** @brief Highlight the intersection of a flattened range and one visible piece. */
static void view_range(tiku_html_view_t *v, tiku_surface_t *s, int piece,
                          tiku_rect_t painted, int first, int last, int selected, int active)
{
    tiku_rect_t range = tiku_html_text_range(v->doc,v->starts,piece,first,last);
    if (range.w <= 0) return;
    tiku_rect_t full = tiku_doc_piece_rect(v->doc,piece);
    range.x += painted.x-full.x;range.y = painted.y;
    tiku_fill(s,range,selected ? TIKU_C_SELECT : highlight_color(TIKU_C_NOTE));
    if (active) tiku_frame(s,range,TIKU_C_FOCUS);
}

/** @brief Find matches followed by the current selection, before piece text. */
static void view_mark(void *ctx, tiku_surface_t *s, int piece, tiku_rect_t box)
{
    tiku_html_view_t *v = ctx;
    const tiku_doc_piece_t *pc = &v->doc->piece[piece];
    int start = v->starts[pc->run]+pc->off-v->doc->run[pc->run].off;
    if (v->match_len > 0 && v->matches) {
        int from = start-v->match_len+1;
        if (from < 0) from = 0;
        for (int i = from; i < start+pc->len && i+v->match_len <= v->length; i++)
            if (tiku_html_text_match(v->text+i,v->query))
                view_range(v,s,piece,box,i,i+v->match_len,0,i == v->match);
    }
    view_range(v,s,piece,box,v->a < v->b ? v->a : v->b,
               v->a > v->b ? v->a : v->b,1,0);
}

/** @brief Draw a bounded text segment once, followed by its resolved decorations. */
static void view_span(tiku_surface_t *s, const tiku_doc_run_t *run, const char *text,
                         int bytes, int x, int width, tiku_rect_t box, int baseline, tiku_rgb_t ink)
{
    if (bytes <= 0) return;
    int start = x;char part[256];
    while (bytes > 0 && x < s->clip.x+s->clip.w) {
        int n = bytes < (int)sizeof part-1 ? bytes : (int)sizeof part-1;
        while (n > 1 && ((unsigned char)text[n] & 0xc0) == 0x80) n--;
        memcpy(part,text,(size_t)n);part[n] = 0;
        tiku_text(s,tiku_font_for_measure(run->face),x,baseline,part,ink);
        x += tiku_html_text_width(run->face,text,n);text += n;bytes -= n;
    }
    if ((run->flags & TIKU_DOC_RUN_UNDERLINE) ||
        (run->link >= 0 && !(run->flags & TIKU_DOC_RUN_LINK_PLAIN)))
        tiku_hline(s,start,baseline+2,width,ink);
    if (run->flags & TIKU_DOC_RUN_STRIKE)
        tiku_hline(s,start,box.y+(baseline-box.y)/2,width,ink);
}

/** @brief Split selected text at its measured boundaries and apply selection ink. */
static void view_text(void *ctx, tiku_surface_t *s, const tiku_doc_t *doc,
                          int piece, tiku_rect_t box, int baseline, tiku_rgb_t ink)
{
    tiku_html_view_t *v = ctx;const tiku_doc_piece_t *pc = &doc->piece[piece];
    const tiku_doc_run_t *run = &doc->run[pc->run];const char *text = doc->text+pc->off;
    int start = v->starts[pc->run]+pc->off-run->off;
    int first = (v->a < v->b ? v->a : v->b)-start;
    int last = (v->a > v->b ? v->a : v->b)-start;
    if (first < 0) first = 0;
    if (last > pc->len) last = pc->len;
    if (first >= last) view_span(s,run,text,pc->len,box.x,pc->w,box,baseline,ink);
    else {
        int left = tiku_html_text_width(run->face,text,first);
        int right = tiku_html_text_width(run->face,text,last);
        view_span(s,run,text,first,box.x,left,box,baseline,ink);
        view_span(s,run,text+first,last-first,box.x+left,right-left,box,baseline,TIKU_C_SELTEXT);
        view_span(s,run,text+last,pc->len-last,box.x+right,pc->w-right,box,baseline,ink);
    }
    if (run->link >= 0 && run->link == v->focus_link)
        tiku_frame(s,(tiku_rect_t){box.x-2,box.y,box.w+4,box.h},TIKU_C_FOCUS);
}

void tiku_html_view_draw(tiku_html_view_t *v, tiku_surface_t *s, tiku_rect_t viewport)
{
    if (!s || viewport.w < 1 || viewport.h < 1) return;
    if (v->a < 0) v->a = 0;
    if (v->a > v->length) v->a = v->length;
    if (v->b < 0) v->b = 0;
    if (v->b > v->length) v->b = v->length;
    tiku_html_view_resize(v,viewport);
    if (!v->doc) { tiku_fill(s,viewport,TIKU_C_DOC);return; }
    tiku_doc_paint_t how = v->paint;
    how.scroll = v->scroll;how.focus_link = v->focus_link;
    if (v->text && v->starts) { how.text = view_text;how.text_ctx = v; }
    tiku_doc_draw_marked(s,v->doc,viewport,&how,
                         v->text && v->starts ? view_mark : NULL,v);
}
