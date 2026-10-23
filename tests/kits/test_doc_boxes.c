/*
 * TikuOS Interface Kit.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_doc_boxes.c - bounded CSS boxes, display and nested container painting.
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
static void check(int value, const char *message)
{
    printf("[%s] boxes %s\n",value ? "PASS" : "FAIL",message); failures += !value;
}

/** @brief Read and lay out a document at a fixed test width. */
static void read_boxes(const char *source)
{
    tiku_doc_read(&doc,source); tiku_doc_layout(&doc,300);
}

/** @brief Find a laid-out piece containing a word. */
static tiku_rect_t word_rect(const char *word)
{
    for (int i = 0; i < doc.npiece; i++)
        if (strstr(doc.text+doc.piece[i].off,word)) return tiku_doc_piece_rect(&doc,i);
    return (tiku_rect_t){0,0,0,0};
}

/** @brief Test content widths, four-sided spacing and nested box coordinates. */
static void boxes(void)
{
    read_boxes("<div style='width:120px;padding:10px;border:2px solid red;margin:5px 7px'>"
               "Inside words that wrap across the constrained content width.</div><p>After</p>");
    tiku_rect_t in = word_rect("Inside"), after = word_rect("After");
    check(in.x == 19 && in.y == 17 && in.w <= 120 && after.y > in.y+in.h &&
          doc.nline > 2 && tiku_doc_check(&doc),"width, border, margin and padding define the content box");
    read_boxes("<div style='width:50%;margin-left:auto;margin-right:auto;padding:10px'>Centered</div>");
    in = word_rect("Centered");
    check(in.x == 75,"percentage content width and automatic margins center a padded box");
    read_boxes("<div style='width:280px;max-width:100px;padding-left:11px'><p>Maximum</p></div>");
    in = word_rect("Maximum");
    check(in.x == 11 && doc.nline == 1 && tiku_doc_check(&doc),"max-width constrains a block independently of padding");
    read_boxes("<div style='padding:10px'><div style='padding:7px'><p>Nested</p></div></div>");
    in = word_rect("Nested");
    check(in.x == 17 && in.y == 17,"nested containers accumulate padding once per ancestor");
    read_boxes("<style>div{padding-left:20px!important}</style>"
               "<div style='padding:5px'>Important</div>");
    in = word_rect("Important");
    check(in.x == 20 && in.y == 5,"spacing shorthand preserves per-side importance");
    read_boxes("<div style='font-size:14px;padding:1em 1rem'>Units</div>");
    in = word_rect("Units");
    check(in.x == 12 && in.y == 14,"box em and rem units use element and root font sizes");
    read_boxes("<div style='width:200px;padding:10px'><table><tr><td>Left</td><td>Right</td></tr></table></div>");
    tiku_rect_t right = word_rect("Right");
    in = word_rect("Left");
    check(in.x >= 10 && right.x > in.x && right.x+right.w <= 210 && tiku_doc_check(&doc),
          "table cells retain the surrounding box origin and available width");
    read_boxes("<div style='width:100000px;padding:999999px'>Kept</div>");
    check(word_rect("Kept").w > 0 && (tiku_doc_notices(&doc) & TIKU_DOC_CSS_UNSUPPORTED),
          "out-of-profile dimensions retain text and report unsupported values");
}

/** @brief Check subtree hiding and explicit block/inline boundaries. */
static void display(void)
{
    read_boxes("<p>Before<span style='display:none'><a id='hidden' href='x'>SECRET</a>"
               "<img src='secret.bmp'><b>hidden</b></span>After</p>");
    int hidden = 0;
    for (int i = 0; i < doc.nrun; i++) hidden |= strstr(doc.text+doc.run[i].off,"SECRET") != NULL;
    check(!hidden && !doc.nlink && !doc.nimage && tiku_doc_anchor_y(&doc,"hidden") == TIKU_DOC_NONE &&
          word_rect("Before").w > 0 && word_rect("After").w > 0,
          "display none omits hidden text, links, images and anchors");
    read_boxes("<p>Before<div style='display:none'><p>SECRET</p></div>After</p>");
    check(doc.nline == 1 && word_rect("After").w > 0,"hidden block descendants do not split visible paragraph flow");
    read_boxes("<span style='display:block'>One</span><span style='display:block'>Two</span>");
    check(doc.nline == 2 && word_rect("Two").y > word_rect("One").y,"block display gives inline elements separate flow boxes");
    read_boxes("<p style='display:inline'>One</p><p style='display:inline'>Two</p>");
    check(doc.nline == 1,"inline display removes paragraph boundaries");
    read_boxes("<h1 style='display:inline'>Heading</h1><p style='display:inline'>Body</p>");
    check(doc.nline == 2 && doc.nrun > 1 &&
          (tiku_doc_notices(&doc) & TIKU_DOC_CSS_UNSUPPORTED) &&
          tiku_measure_height(doc.run[0].face) > tiku_measure_height(doc.run[1].face),
          "unsupported inline headings retain heading structure and report conversion");
    char source[2048]; int n = sprintf(source,"<p>Before<span style='display:none'>");
    for (int i = 0; i < 40; i++) n += sprintf(source+n,"<span>");
    for (int i = 0; i < 40; i++) n += sprintf(source+n,"</span>");
    strcpy(source+n,"SECRET</span>After</p>"); read_boxes(source);
    hidden = 0;
    for (int i = 0; i < doc.nrun; i++) hidden |= strstr(doc.text+doc.run[i].off,"SECRET") != NULL;
    check(!hidden && (doc.full & TIKU_DOC_FULL_DEPTH) && tiku_doc_check(&doc),
          "hidden ancestors remain hidden when nested scopes exceed the stack budget");
}

/** @brief Exercise malformed and truncated styles without sentinel bytes. */
static void malformed(void)
{
    const char source[] = "<style>.box{margin:2px 3px;padding:1em;border:2px solid red}</style>"
        "<div class='box' style='width:70%;background:#123456'>A"
        "<span style='display:none'><p>Hidden</p></span>"
        "<div style='padding:4px;max-width:80px'>B</div>C</div>";
    int valid = 1;
    for (size_t n = 0; n < sizeof source; n++) {
        char *prefix = malloc(n+1);
        if (!prefix) { valid = 0; break; }
        memcpy(prefix,source,n); prefix[n] = 0;
        read_boxes(prefix); valid &= tiku_doc_check(&doc);
        tiku_doc_layout(&doc,23); valid &= tiku_doc_check(&doc);
        free(prefix);
    }
    check(valid,"every styled input prefix and narrow reflow retains valid box ranges");
    read_boxes("<div style='padding:2px'><p style='border:1px solid red'>A"
               "<p style='padding:3px'>B</div><p>C");
    check(tiku_doc_check(&doc) && word_rect("C").x == 0,
          "omitted closes restore box origins before following content");
}

/** @brief Inspect background and border pixels, including a scrolled container. */
static void pixels(void)
{
    read_boxes("<div style='background-color:#123456;padding:10px;border:2px solid #ff0000'>"
               "<p>Top</p><p>Bottom</p></div>");
    tiku_surface_t *s = tiku_surface_new(320,160,TIKU_C_DOC);
    if (!s) { check(0,"box paint surface allocated"); return; }
    tiku_doc_draw(s,&doc,(tiku_rect_t){10,10,300,140},NULL);
    check((s->px[10*320+10] & 0xffffffu) == 0xff0000 &&
          (s->px[14*320+14] & 0xffffffu) == 0x123456,
          "container background and solid border paint around their children");
    tiku_doc_paint_t paint = {.scroll=20,.focus_link=TIKU_DOC_NONE};
    tiku_doc_draw(s,&doc,(tiku_rect_t){10,10,300,140},&paint);
    check((s->px[12*320+14] & 0xffffffu) == 0x123456,
          "a container paints its background when its opening block is above the viewport");
    read_boxes("<p>Before <span style='background:#00ff00'>Marked</span> after</p>");
    tiku_rect_t r = word_rect("Marked");
    tiku_doc_draw(s,&doc,(tiku_rect_t){10,10,300,140},NULL);
    check(r.w > 0 && (s->px[(10+r.y)*320+10+r.x] & 0xffffffu) == 0x00ff00,
          "inline backgrounds use the same piece rectangle as text selection");
    const char *path = getenv("TIKU_BOX_SNAPSHOT");
    if (path) check(!tiku_surface_png(s,path),"box snapshot written");
    tiku_surface_free(s);
}

int main(void)
{
    tiku_font_set_size(12);tiku_font_set_family(0);tiku_doc_init(&doc);
    boxes(); display(); pixels(); malformed();
    printf("Boxes: %d failure(s)\n",failures); return failures != 0;
}
