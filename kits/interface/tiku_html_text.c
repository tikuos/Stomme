/*
 * TikuOS Interface Kit.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_html_text.c - flattened document text and point/range geometry.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_html_text.h"
#include <string.h>
#include <limits.h>

int tiku_html_text(const tiku_doc_t *d, char *out, size_t cap, int *starts)
{
    if (!out || !cap) return 0;
    size_t n = 0; out[0] = 0;
    if (!d) return 0;
    for (int i = 0; i < d->nblock; i++) {
        const tiku_doc_block_t *b = &d->block[i];
        for (int k = b->run0; k < b->run0+b->nrun; k++) {
            const tiku_doc_run_t *run = &d->run[k];
            if (starts) starts[k] = (int)n;
            if (n+(size_t)run->len+2 >= cap) { out[n] = 0; return (int)n; }
            memcpy(out+n,d->text+run->off,(size_t)run->len); n += run->len;
        }
        if (n && out[n-1] != '\n' && n+1 < cap) out[n++] = '\n';
    }
    out[n] = 0; return (int)n;
}

int tiku_html_text_match(const char *text, const char *query)
{
    if (!text || !query) return 0;
    while (*query) {
        unsigned char a = (unsigned char)*text++, b = (unsigned char)*query++;
        if (a >= 'A' && a <= 'Z') a += 'a'-'A';
        if (b >= 'A' && b <= 'Z') b += 'a'-'A';
        if (!a || a != b) return 0;
    }
    return 1;
}

int tiku_html_text_width(int face, const char *text, int bytes)
{
    char part[256]; int total = 0;
    while (bytes > 0) {
        int n = bytes < (int)sizeof part-1 ? bytes : (int)sizeof part-1;
        while (n > 1 && ((unsigned char)text[n] & 0xc0) == 0x80) n--;
        memcpy(part,text,(size_t)n);part[n] = 0;
        total += tiku_measure_width(face,part);text += n;bytes -= n;
    }
    return total;
}

int tiku_html_text_offset(const tiku_doc_t *d, const int *starts, int length,
                            int x, int y, int *block)
{
    if (!d || !starts || !tiku_doc_laid(d)) return -1;
    if (y < 0) return 0;
    if (y >= d->height) return length;
    int line = -1, distance = INT_MAX;
    for (int i = 0; i < d->nline; i++) {
        const tiku_doc_line_t *ln = &d->line[i];
        if (!ln->npiece || y < ln->y || y >= ln->y+ln->h) continue;
        int gap = x < ln->x ? ln->x-x : x > ln->x+ln->w ? x-ln->x-ln->w : 0;
        if (gap < distance) { line = i; distance = gap; }
    }
    if (line < 0) return -1;
    const tiku_doc_line_t *ln = &d->line[line];
    for (int i = ln->piece0; i < ln->piece0+ln->npiece; i++) {
        const tiku_doc_piece_t *pc = &d->piece[i];
        const tiku_doc_run_t *run = &d->run[pc->run];
        if (x > ln->x+pc->x+pc->w && i+1 < ln->piece0+ln->npiece) continue;
        const char *text = d->text+pc->off;
        int n = 0, before = 0, target = x-ln->x-pc->x;
        while (n < pc->len) {
            int next = n+1;
            while (next < pc->len && ((unsigned char)text[next] & 0xc0) == 0x80) next++;
            int after = tiku_html_text_width(run->face,text,next);
            if (target < before+(after-before)/2) break;
            n = next; before = after;
        }
        if (block) *block = ln->block;
        return starts[pc->run]+pc->off-run->off+n;
    }
    return -1;
}

tiku_rect_t tiku_html_text_range(const tiku_doc_t *d, const int *starts,
                                  int piece, int first, int last)
{
    tiku_rect_t box = {0};
    if (!d || !starts || piece < 0 || piece >= d->npiece) return box;
    const tiku_doc_piece_t *pc = &d->piece[piece];
    const tiku_doc_run_t *run = &d->run[pc->run];
    int start = starts[pc->run]+pc->off-run->off;
    if (first < start) first = start;
    if (last > start+pc->len) last = start+pc->len;
    if (first >= last) return box;
    box = tiku_doc_piece_rect(d,piece);
    int x = tiku_html_text_width(run->face,d->text+pc->off,first-start);
    int end = tiku_html_text_width(run->face,d->text+pc->off,last-start);
    box.x += x; box.w = end-x; return box;
}
