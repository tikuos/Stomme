/*
 * TikuOS Interface Kit.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_html_view.c - HTML view ownership, reflow and public interaction state.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_html_view.h"
#include <stdlib.h>
#include <string.h>

void tiku_html_view_init(tiku_html_view_t *v, const tiku_html_options_t *options)
{
    memset(v,0,sizeof *v);
    v->valid_options = !options || (!(options->allocate || options->zero || options->release) ||
        (options->allocate && options->zero && options->release));
    if (options) v->options = *options;
    if (!v->options.source_bytes) v->options.source_bytes = TIKU_HTML_SOURCE_MAX;
    if (v->options.source_bytes > INT32_MAX/4-4096) v->options.source_bytes = INT32_MAX/4-4096;
    if (!v->options.memory_bytes) v->options.memory_bytes = TIKU_HTML_MEMORY_MAX;
    if (!v->options.allocate) v->options.allocate = malloc;
    if (!v->options.zero) v->options.zero = calloc;
    if (!v->options.release) v->options.release = free;
    v->viewport = (tiku_rect_t){0,0,256,256};
    v->focus_link = v->down_link = v->match = v->block = TIKU_DOC_NONE;
}

void tiku_html_view_free(tiku_html_view_t *v)
{
    if (v->owned) {
        tiku_html_document_free(v->doc,&v->options);
        if (v->text) v->options.release(v->text);
        if (v->starts) v->options.release(v->starts);
    }
    v->doc = NULL;v->text = NULL;v->starts = NULL;v->length = v->owned = 0;
    v->tools_bytes = 0;
    v->a = v->b = v->dragging = v->scroll = v->matches = v->match_len = 0;
    v->query[0] = 0;v->focus_link = v->down_link = v->match = v->block = TIKU_DOC_NONE;
}

int tiku_html_view_load(tiku_html_view_t *v, const char *html, const char *css)
{
    if (!v->valid_options) return 0;
    if (!html) html = "";
    size_t n = strnlen(html,v->options.source_bytes+1);
    size_t styles = css ? strnlen(css,TIKU_DOC_CSS_BYTES_MAX+1) : 0;
    if (n > v->options.source_bytes || styles > TIKU_DOC_CSS_BYTES_MAX) return 0;
    char *combined = NULL;
    if (styles) {
        if (n+styles+16 > v->options.source_bytes || n+styles+16 > v->options.memory_bytes) return 0;
        combined = v->options.allocate(n+styles+16);
        if (!combined) return 0;
        memcpy(combined,"<style>",7);memcpy(combined+7,css,styles);
        memcpy(combined+7+styles,"</style>",8);memcpy(combined+15+styles,html,n+1);
    }
    tiku_doc_t *doc = tiku_html_document_load(combined ? combined : html,0,&v->options);
    if (combined) v->options.release(combined);
    if (!doc) return 0;
    size_t text_bytes = (size_t)doc->ntext+doc->nblock+1;
    size_t starts_bytes = (size_t)(doc->nrun ? doc->nrun : 1)*sizeof(int);
    if (text_bytes+starts_bytes+doc->storage_bytes+sizeof *doc > v->options.memory_bytes) {
        tiku_html_document_free(doc,&v->options);return 0;
    }
    char *text = v->options.allocate(text_bytes);
    int *starts = v->options.allocate(starts_bytes);
    if (!text || !starts) {
        if (text) v->options.release(text);
        if (starts) v->options.release(starts);
        tiku_html_document_free(doc,&v->options);return 0;
    }
    int length = tiku_html_text(doc,text,text_bytes,starts);
    tiku_html_view_free(v);
    v->doc = doc;v->text = text;v->starts = starts;v->length = length;v->owned = 1;
    v->tools_bytes = text_bytes+starts_bytes;
    tiku_doc_images(doc,v->image_size,v->paint.image_ctx);
    tiku_html_view_resize(v,v->viewport);return 1;
}

void tiku_html_view_bind(tiku_html_view_t *v, tiku_doc_t *doc,
                           char *text, int length, int *starts)
{
    if (v->doc == doc && !v->owned) {
        v->text = text;v->length = length;v->starts = starts;return;
    }
    tiku_html_view_free(v);
    v->doc = doc;v->text = text;v->length = length;v->starts = starts;
}

void tiku_html_view_resources(tiku_html_view_t *v, tiku_doc_image_fn size,
                                tiku_doc_draw_image_fn draw, void *ctx)
{
    v->image_size = size;v->paint.image = draw;v->paint.image_ctx = ctx;
    if (v->doc && (v->doc->image_fn != size || v->doc->image_ctx != ctx)) {
        tiku_doc_images(v->doc,size,ctx);v->doc->laid_width = TIKU_DOC_NONE;
    }
}

int tiku_html_view_scroll(tiku_html_view_t *v, int offset)
{
    int maximum = v->doc ? v->doc->height-v->viewport.h : 0;
    if (maximum < 0) maximum = 0;
    v->scroll = offset < 0 ? 0 : offset > maximum ? maximum : offset;
    return v->scroll;
}

void tiku_html_view_resize(tiku_html_view_t *v, tiku_rect_t viewport)
{
    int had_layout = v->doc && tiku_doc_laid(v->doc);
    tiku_doc_place_t place = had_layout ? tiku_doc_place(v->doc,v->scroll) : (tiku_doc_place_t){0};
    int changed = viewport.w != v->viewport.w || (v->doc &&
        (v->doc->laid_height != tiku_measure_height(TIKU_MEASURE_PLAIN) ||
         v->doc->laid_font_stamp != tiku_measure_font_stamp()));
    if (viewport.h < 0) viewport.h = 0;
    v->viewport = viewport;
    if (!v->doc || viewport.w < 1) return;
    if (v->owned && v->options.memory_bytes > v->tools_bytes) {
        tiku_html_options_t options = v->options;
        options.memory_bytes -= v->tools_bytes;
        (void)tiku_html_document_reflow(&v->doc,viewport.w,&options);
    } else tiku_doc_layout(v->doc,viewport.w);
    if (changed && had_layout) v->scroll = tiku_doc_place_y(v->doc,place);
    tiku_html_view_scroll(v,v->scroll);
}

size_t tiku_html_view_selection(const tiku_html_view_t *v, char *out, size_t cap)
{
    int a = v->a < v->b ? v->a : v->b, b = v->a > v->b ? v->a : v->b;
    if (a < 0) a = 0;
    if (b > v->length) b = v->length;
    if (!v->text || a >= b) { if (out && cap) out[0] = 0; return 1; }
    while (a > 0 && ((unsigned char)v->text[a] & 0xc0) == 0x80) a--;
    while (b > 0 && b < v->length && ((unsigned char)v->text[b] & 0xc0) == 0x80) b--;
    size_t required = (size_t)(b-a)+1;
    if (out && cap >= required) { memmove(out,v->text+a,required-1);out[required-1] = 0; }
    return required;
}

int tiku_html_view_find(tiku_html_view_t *v, const char *query, int backwards)
{
    if (!v->text || !query || strlen(query) >= sizeof v->query) return 0;
    int prior = strcmp(query,v->query) ? -1 : v->match;
    int length = (int)strlen(query);
    memmove(v->query,query,(size_t)length+1);v->match_len = length;
    v->matches = 0;int first = -1, last = -1, chosen = -1;
    for (int i = 0; v->match_len && i+v->match_len <= v->length; i++) {
        if (!tiku_html_text_match(v->text+i,v->query)) continue;
        v->matches++;if (first < 0) first = i;last = i;
        if ((!backwards && i > prior && chosen < 0) || (backwards && prior >= 0 && i < prior)) chosen = i;
    }
    v->match = chosen >= 0 ? chosen : backwards ? last : first;
    for (int i = 0; v->match >= 0 && i < v->doc->npiece; i++) {
        const tiku_doc_piece_t *pc = &v->doc->piece[i];
        int start = v->starts[pc->run]+pc->off-v->doc->run[pc->run].off;
        if (v->match >= start && v->match < start+pc->len) {
            tiku_html_view_scroll(v,tiku_doc_piece_rect(v->doc,i).y);break;
        }
    }
    return v->match >= 0;
}
