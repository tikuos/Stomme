/*
 * TikuOS Interface Kit.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_doc_tables.c - styled cells, row spans, captions and constrained table layout.
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tiku_doc_paint.h"
#include "tiku_font.h"

static tiku_doc_t doc;
static int failures;

/** @brief Record an assertion while retaining subsequent checks. */
static void check(int condition, const char *message)
{
    printf("[%s] tables %s\n",condition ? "PASS" : "FAIL",message); failures += !condition;
}

/** @brief Find the cell containing a word. */
static int cell(const char *word)
{
    for (int i = 0; i < doc.nblock; i++) {
        const tiku_doc_block_t *b = &doc.block[i];
        if (b->kind != TIKU_DOC_BLOCK_CELL) continue;
        for (int k = 0; k < b->nrun; k++)
            if (strstr(doc.text+doc.run[b->run0+k].off,word)) return i;
    }
    return -1;
}

/** @brief Parse and lay out a table at the requested width. */
static void read_table(const char *source, int width)
{
    tiku_doc_read(&doc,source);tiku_doc_layout(&doc,width);
}

/** @brief Test occupied columns, spanning geometry and retained link hits. */
static void spans(void)
{
    read_table("<table><tr><td rowspan='2'><a href='#end'>Spanning words over two rows</a></td>"
               "<td>First</td></tr><tr><td>Second</td></tr></table>",220);
    int a = cell("Spanning"), b = cell("First"), c = cell("Second");
    check(a >= 0 && b >= 0 && c >= 0 && doc.block[c].col == 1,
          "row spans reserve occupied columns in following rows");
    if (a >= 0 && b >= 0 && c >= 0) {
        tiku_rect_t ra = tiku_doc_block_rect(&doc,a), rb = tiku_doc_block_rect(&doc,b);
        tiku_rect_t rc = tiku_doc_block_rect(&doc,c);
        check(rc.x == rb.x && rc.y > rb.y && ra.y+ra.h >= rc.y+rc.h,
              "spanning cell height covers rows while adjacent cells align");
    }
    int hit = 0;
    for (int i = 0; i < doc.npiece; i++) if (doc.run[doc.piece[i].run].link >= 0) {
        tiku_rect_t r = tiku_doc_piece_rect(&doc,i);
        hit += tiku_doc_hit(&doc,r.x+r.w/2,r.y+r.h/2).kind == TIKU_DOC_HIT_LINK;
    }
    check(hit && tiku_doc_check(&doc),"spanning cell links hit at their painted positions");
    read_table("<table><tr><td rowspan='2' colspan='2'>Wide</td><td>Top</td></tr>"
               "<tr><td>Bottom</td></tr></table>",240);
    c = cell("Bottom");
    check(c >= 0 && doc.block[c].col == 2 && tiku_doc_check(&doc),
          "column spans reserve every covered column across rows");
    read_table("<table><tr><td rowspan='2147483647'>Bounded</td><td>A</td></tr>"
               "<tr><td>B</td></tr></table>",240);
    check(tiku_doc_notices(&doc) && tiku_doc_check(&doc),"out-of-profile row spans report conversion without losing cells");
    read_table("<table><tr><td rowspan='64' colspan='8'>Reserved</td></tr><tr><td>Recovered</td></tr></table>",300);
    a = cell("Reserved"); b = cell("Recovered");
    check(a >= 0 && b >= 0 && doc.block[b].index >= doc.block[a].index+64 && tiku_doc_check(&doc),
          "a fully occupied row advances beyond the spanning reservation");
}

/** @brief Exercise truncated tables and omitted cell/row closing scopes. */
static void malformed(void)
{
    const char source[] = "<table style='padding:3px'><caption>Caption</caption>"
        "<tr style='background:red'><th rowspan='2'>A<th>B<tr><td colspan='2' "
        "style='padding:2px;border:1px solid blue'>C</table><p>Tail</p>";
    int valid = 1;
    for (size_t n = 0; n < sizeof source; n++) {
        char *prefix = malloc(n+1);
        if (!prefix) { valid = 0; break; }
        memcpy(prefix,source,n);prefix[n] = 0;
        read_table(prefix,300);valid &= tiku_doc_check(&doc);
        tiku_doc_layout(&doc,19);valid &= tiku_doc_check(&doc);
        free(prefix);
    }
    check(valid,"every table input prefix retains valid wide and stacked geometry");
    read_table("<table><tr><td style='background:red'>A<td>B<tr><td>C<td>D</table>",300);
    int b = cell("B"), c = cell("C");
    check(b >= 0 && c >= 0 && !doc.block[b].box.has_background &&
          !doc.block[c].box.has_background,"omitted cell and row closes restore noninherited styles");
}

/** @brief Check cell box geometry, preferred widths and semantic captions. */
static void styles(void)
{
    read_table("<style>td{padding:8px;border:2px solid red;text-align:right}</style>"
               "<table><caption>Inventory caption</caption><tr><td style='width:200px'>Wide</td>"
               "<td>Narrow</td></tr></table>",300);
    int a = cell("Wide"), b = cell("Narrow");
    if (a >= 0 && b >= 0) {
        tiku_rect_t ra = tiku_doc_block_rect(&doc,a), rb = tiku_doc_block_rect(&doc,b);
        const tiku_doc_line_t *line = &doc.line[doc.block[a].line0];
        check(ra.w > rb.w && line->x+line->w == ra.x+ra.w-10 &&
              line->y == ra.y+10,"cell width preferences, padding, border and alignment share geometry");
    } else check(0,"styled cells remain present");
    int captions = 0;
    for (int i = 0; i < doc.nblock; i++) captions += doc.block[i].kind == TIKU_DOC_BLOCK_CAPTION;
    check(captions == 1 && tiku_doc_check(&doc),"table captions retain their own semantic block");
    read_table("<table><tr style='background:#123456'><td>Row</td><td>Other</td></tr>"
               "<tr><td>Plain</td><td>Restored</td></tr></table>",300);
    a = cell("Row");b = cell("Plain");
    check(a >= 0 && b >= 0 && doc.block[a].box.has_background &&
          doc.block[a].box.background == 0x123456 && !doc.block[b].box.has_background,
          "row backgrounds cover cells and restore before the next row");
    read_table("<table><tr><td>First table</td></tr></table>"
               "<table><tr><td>Second table</td></tr></table>",300);
    a = cell("First"); b = cell("Second");
    check(a >= 0 && b >= 0 && doc.block[b].y >= doc.block[a].y+doc.block[a].h,
          "adjacent tables retain separate row groups");
    read_table("<table><tr><td style='border:none'>No border</td></tr></table>",300);
    tiku_surface_t *surface = tiku_surface_new(320,100,TIKU_C_DOC);
    if (surface) {
        tiku_doc_draw(surface,&doc,(tiku_rect_t){10,10,300,80},NULL);
        a = cell("No border");tiku_rect_t r = tiku_doc_block_rect(&doc,a);
        check((surface->px[(10+r.y+r.h-1)*320+10+r.x] & 0xffffffu) == TIKU_C_DOC,
              "explicit border none suppresses the default table row rule");
        tiku_surface_free(surface);
    }
}

/** @brief Check narrow fitting, row/cell backgrounds and solid border pixels. */
static void paint(void)
{
    read_table("<style>td{padding:6px;border:2px solid red;background:#123456}</style>"
               "<div style='width:180px;padding:10px'><table><tr><td>Left cell words</td>"
               "<td><a href='#x'>Right cell words</a></td></tr></table></div>",300);
    tiku_doc_layout(&doc,90);
    int valid = tiku_doc_check(&doc);
    for (int i = 0; i < doc.nline; i++) valid &= doc.line[i].x+doc.line[i].w <= 90;
    check(valid,"narrow cell boxes retain padding bounds and wrap their text");
    tiku_doc_layout(&doc,300);
    tiku_surface_t *s = tiku_surface_new(320,180,TIKU_C_DOC);
    if (!s) { check(0,"table paint surface allocated"); return; }
    tiku_doc_draw(s,&doc,(tiku_rect_t){10,10,300,160},NULL);
    int a = cell("Left");
    if (a >= 0) {
        tiku_rect_t r = tiku_doc_block_rect(&doc,a);
        check((s->px[(10+r.y)*320+10+r.x] & 0xffffffu) == 0xff0000 &&
              (s->px[(14+r.y)*320+14+r.x] & 0xffffffu) == 0x123456,
              "cell background and border pixels match the laid-out cell box");
    }
    const char *path = getenv("TIKU_TABLE_SNAPSHOT");
    if (path) check(!tiku_surface_png(s,path),"table snapshot written");
    tiku_surface_free(s);
}

int main(void)
{
    tiku_font_set_size(12); tiku_font_set_family(0); tiku_doc_init(&doc);
    spans(); styles(); paint(); malformed();printf("Tables: %d failure(s)\n",failures);return failures != 0;
}
