/*
 * TikuOS Interface Kit.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_html_view.c - public HTML view interaction, ownership and failure recovery.
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tiku_html_view.h"
#include "tiku_clipboard.h"
#include "tiku_font.h"
#include "../fixtures/ui_font.h"

static int failures, calls, fail_at, live, opened;
static char link_text[128];

/** @brief Record an assertion while retaining subsequent checks. */
static void check(int condition, const char *message)
{
    printf("[%s] HTML view %s\n",condition ? "PASS" : "FAIL",message);failures += !condition;
}

/** @brief A tracked allocator with one deterministic refused call. */
static void *allocate(size_t bytes)
{
    if (++calls == fail_at) return NULL;
    void *p = malloc(bytes);if (p) live++;return p;
}

/** @brief Tracked zeroed allocation under the same fault counter. */
static void *zero(size_t count, size_t size)
{
    void *p = allocate(count*size);if (p) memset(p,0,count*size);return p;
}

/** @brief Release a tracked allocation. */
static void release(void *p)
{
    if (p) { live--;free(p); }
}

/** @brief Record links without opening a resource. */
static void link_open(void *ctx, const char *href)
{
    (void)ctx;opened++;snprintf(link_text,sizeof link_text,"%s",href);
}

/** @brief Return the piece whose run contains a word. */
static int piece(tiku_html_view_t *v, const char *word)
{
    for (int i = 0; i < v->doc->npiece; i++)
        if (strstr(v->doc->text+v->doc->piece[i].off,word)) return i;
    return -1;
}

/** @brief Drag between independently measured character boundaries in a table cell. */
static void interaction(void)
{
    tiku_html_view_t v;tiku_html_view_init(&v,NULL);
    check(tiku_html_view_load(&v,"<table><tr><td>First column</td>"
        "<td style='font-family:serif;font-size:14px;padding:8px'>ABCDEFGHIJKL</td>"
        "</tr></table><p><a href='local.html'>Open local page</a></p>","td{color:blue}"),
        "owned HTML loads with a default stylesheet and no application code");
    tiku_rect_t viewport = {10,10,300,160};tiku_html_view_resize(&v,viewport);
    int i = piece(&v,"ABCDEFGHIJKL");
    check(i >= 0,"styled table text has a measured piece");
    if (i >= 0) {
        tiku_rect_t r = tiku_doc_piece_rect(v.doc,i);
        int face = v.doc->run[v.doc->piece[i].run].face;
        tiku_event_t e = {.type=TIKU_EVENT_POINTER_DOWN,.button=1,
            .x=10+r.x+tiku_measure_width(face,"ABC"),.y=10+r.y+r.h/2};
        check(tiku_html_view_event(&v,&e),"pointer down begins selection in the second column");
        e.type = TIKU_EVENT_POINTER_MOVE;e.x = 10+r.x+tiku_measure_width(face,"ABCDEFG");
        tiku_html_view_event(&v,&e);e.type = TIKU_EVENT_POINTER_UP;tiku_html_view_event(&v,&e);
        char selected[32];tiku_html_view_selection(&v,selected,sizeof selected);
        check(!strcmp(selected,"DEFG"),"table selection uses the pointed column and styled glyph metrics");
        e = (tiku_event_t){.type=TIKU_EVENT_KEY_DOWN,.key='c',.modifiers=TIKU_MOD_CMD};
        check(tiku_html_view_event(&v,&e) && !strcmp(tiku_clipboard_text(),"DEFG"),
              "copy publishes selected UTF-8 text through Interface Kit");
        char tiny[3] = "ok";
        check(tiku_html_view_selection(&v,tiny,sizeof tiny) == 5 && !strcmp(tiny,"ok"),
              "an undersized selection buffer remains unchanged");
    }
    v.link = link_open;
    tiku_event_t e = {.type=TIKU_EVENT_KEY_DOWN,.key=TIKU_KEY_TAB};
    tiku_html_view_event(&v,&e);e.key = TIKU_KEY_RETURN;tiku_html_view_event(&v,&e);
    check(opened == 1 && !strcmp(link_text,"local.html"),"keyboard traversal delivers links through the host callback");
    check(tiku_html_view_find(&v,"column",0) && v.matches == 1 &&
          tiku_html_view_find(&v,"column",0),"find wraps across the visible flattened text");
    tiku_surface_t *surface = tiku_surface_new(330,190,TIKU_C_DOC);
    if (surface) { tiku_html_view_draw(&v,surface,viewport);tiku_surface_free(surface); }
    check(tiku_doc_check(v.doc),"painted view keeps valid shared document geometry");
    tiku_html_view_free(&v);
}

/** @brief Check transactional replacement and paired allocation cleanup. */
static void ownership(void)
{
    tiku_html_options_t options = {.allocate=allocate,.zero=zero,.release=release};
    tiku_html_view_t v;tiku_html_view_init(&v,&options);
    check(tiku_html_view_load(&v,"<p>Keep this page</p>",NULL),"tracked view page allocated");
    int valid = 1;
    for (int fault = 1; fault <= 4; fault++) {
        tiku_doc_t *old = v.doc;char *text = v.text;int before = live;
        calls = 0;fail_at = fault;
        int result = tiku_html_view_load(&v,"<p>Replacement page</p>","p{color:red}");
        valid &= !result && v.doc == old && v.text == text && strstr(v.text,"Keep") && live == before;
    }
    fail_at = 0;check(valid,"allocation failure at each replacement stage preserves the old page without leaks");
    tiku_html_view_free(&v);check(live == 0,"owned view releases model, text and run indices");
    options = (tiku_html_options_t){.allocate=allocate};
    tiku_html_view_init(&v,&options);
    check(!tiku_html_view_load(&v,"<p>Invalid allocator</p>",NULL),"partial custom allocators are refused before allocation");
    tiku_html_view_free(&v);
    options = (tiku_html_options_t){.source_bytes=4};tiku_html_view_init(&v,&options);
    check(!tiku_html_view_load(&v,"12345",NULL),"source-byte budget is enforced before parsing");
    tiku_html_view_free(&v);
    options = (tiku_html_options_t){.memory_bytes=1};tiku_html_view_init(&v,&options);
    check(!tiku_html_view_load(&v,"<p>Budget</p>",NULL),"model-memory budget includes its inline tables");
    tiku_html_view_free(&v);
}

/** @brief Check borrowed lifetime, resizing, find reveal and scroll clamping. */
static void borrowed(void)
{
    static tiku_doc_t doc;static int starts[TIKU_DOC_RUNS_MAX];static char text[32768];
    tiku_doc_init(&doc);tiku_doc_read(&doc,"<p>Borrowed words that wrap at narrow widths.</p><p id='tail'>Tail needle</p>");
    int length = tiku_html_text(&doc,text,sizeof text,starts);
    tiku_html_view_t v;tiku_html_view_init(&v,NULL);tiku_html_view_bind(&v,&doc,text,length,starts);
    tiku_html_view_resize(&v,(tiku_rect_t){0,0,70,20});
    check(v.scroll == 0,"first layout of a borrowed page begins at the document top");
    check(tiku_html_view_find(&v,"Tail needle",0) && v.scroll > 0,"find reveals a match below the viewport");
    int a = v.a, b = v.b;tiku_html_view_resize(&v,(tiku_rect_t){0,0,200,30});
    check(v.doc == &doc && v.a == a && v.b == b && tiku_doc_check(&doc),"borrowed model reflows without changing ownership or selection");
    tiku_event_t e = {.type=TIKU_EVENT_WHEEL,.x=1,.y=1,.dy=INT32_MIN};
    tiku_html_view_event(&v,&e);
    check(v.scroll >= 0 && v.scroll <= doc.height,"extreme wheel travel clamps without coordinate overflow");
    tiku_html_view_free(&v);
    check(doc.nrun && strstr(doc.text+doc.run[0].off,"Borrowed"),"freeing a borrowed view retains its caller's document tables");
}

/** @brief Compare partial selection colours against native text under each theme. */
static void selection_pixels(void)
{
    const tiku_theme_t *themes[] = {NULL,tiku_theme_dark(),tiku_theme_mono()};
    for (size_t i = 0; i < sizeof themes/sizeof themes[0]; i++) {
        tiku_theme_set(themes[i]);
        tiku_html_view_t v;tiku_html_view_init(&v,NULL);
        check(tiku_html_view_load(&v,"<p style='color:white;background:black'>ABCD</p>",NULL),
              "selection theme fixture loaded");
        tiku_surface_t *actual = tiku_surface_new(100,40,TIKU_C_DOC);
        tiku_surface_t *expected = tiku_surface_new(100,40,TIKU_C_DOC);
        if (actual && expected) {
            v.a = 1;v.b = 3;tiku_html_view_draw(&v,actual,(tiku_rect_t){0,0,100,40});
            const tiku_font_t *font = tiku_font_plain();
            int a = tiku_text_width(font,"A"), b = tiku_text_width(font,"ABC");
            tiku_fill(expected,(tiku_rect_t){0,0,100,font->height},0);
            tiku_fill(expected,(tiku_rect_t){a,0,b-a,font->height},TIKU_C_SELECT);
            tiku_text(expected,font,0,font->ascent,"A",0xffffff);
            tiku_text(expected,font,a,font->ascent,"BC",TIKU_C_SELTEXT);
            tiku_text(expected,font,b,font->ascent,"D",0xffffff);
            check(!memcmp(actual->px,expected->px,(size_t)100*40*sizeof *actual->px),
                  "partial selection uses theme background and text colours without repainting glyph edges");
        } else check(0,"selection comparison surfaces allocated");
        tiku_surface_free(actual);tiku_surface_free(expected);tiku_html_view_free(&v);
    }
    tiku_theme_set(NULL);
}

/** @brief Reflow, paint and hit-test across themes, font sources and surface scales. */
static void profiles(void)
{
    const tiku_theme_t *themes[] = {NULL,tiku_theme_dark(),tiku_theme_mono()};
    const int sizes[] = {10,12,14,16}, widths[] = {60,300,700};
    tiku_html_view_t v;tiku_html_view_init(&v,NULL);
    check(tiku_html_view_load(&v,"<div style='padding:4px;border:1px solid blue'>"
        "<h2>Profiles</h2><p style='line-height:1.5'>Words <a href='#tail'>linked words</a> "
        "with <span style='font-family:serif;font-size:14px'>Serif</span> and "
        "<span style='font-family:monospace;font-size:15px;font-style:italic'>Mono</span>.</p>"
        "<table><tr><td rowspan='2'>Span</td><td><a href='#tail'>Cell link</a></td></tr>"
        "<tr><td>More</td></tr></table><p id='tail'>Tail</p></div>",NULL),"profile matrix source loaded");
    int valid = 1;
    for (int family = 0; family < 2; family++) {
        tiku_font_set_family(family);
        for (size_t theme = 0; theme < sizeof themes/sizeof themes[0]; theme++) {
            tiku_theme_set(themes[theme]);
            for (size_t size = 0; size < sizeof sizes/sizeof sizes[0]; size++) {
                tiku_font_set_size(sizes[size]);
                for (size_t w = 0; w < sizeof widths/sizeof widths[0]; w++) {
                    tiku_surface_t *s = tiku_surface_new(widths[w]+20,300,TIKU_C_DOC);
                    if (!s) { valid = 0; continue; }
                    valid &= !tiku_surface_rescale(s,2,TIKU_C_DOC);
                    tiku_html_view_draw(&v,s,(tiku_rect_t){10,10,widths[w],280});
                    valid &= tiku_doc_check(v.doc);
                    for (int i = 0; i < v.doc->npiece; i++) {
                        const tiku_doc_piece_t *pc = &v.doc->piece[i];
                        if (v.doc->run[pc->run].link < 0) continue;
                        tiku_rect_t r = tiku_doc_piece_rect(v.doc,i);
                        const tiku_doc_block_t *b = NULL;
                        for (int n = 0; n < v.doc->nline; n++)
                            if (i >= v.doc->line[n].piece0 && i < v.doc->line[n].piece0+v.doc->line[n].npiece)
                                b = &v.doc->block[v.doc->line[n].block];
                        if (b && b->kind == TIKU_DOC_BLOCK_CELL) {
                            int right = r.x+r.w;
                            if (r.x < b->content_x) r.x = b->content_x;
                            if (right > b->content_x+b->content_w) right = b->content_x+b->content_w;
                            r.w = right-r.x;
                        }
                        if (r.w > 0) valid &= tiku_doc_hit(v.doc,r.x+r.w/2,r.y+r.h/2).kind == TIKU_DOC_HIT_LINK;
                    }
                    tiku_surface_free(s);
                }
            }
        }
    }
    check(valid,"72 theme/family/size/width combinations retain geometry and link hits at 2x scale");
    check(tiku_font_set_files(ui_test_font(),NULL),"native file font source opened for view validation");
    tiku_font_set_size(12);tiku_html_view_resize(&v,(tiku_rect_t){0,0,300,100});
    unsigned file_stamp = v.doc->laid_font_stamp;
    tiku_font_use_baked();tiku_html_view_resize(&v,(tiku_rect_t){0,0,300,100});
    check(file_stamp != v.doc->laid_font_stamp && tiku_doc_check(v.doc),
          "changing native font source invalidates cached document layout");
    tiku_html_view_free(&v);tiku_font_set_size(12);tiku_font_set_family(0);tiku_theme_set(NULL);
}

/** @brief Keep owned reading tools inside the budget when layout tables grow. */
static void reflow_budget(void)
{
    char *source = malloc(50100);check(source != NULL,"reflow budget source allocated");
    if (!source) return;
    strcpy(source,"<p>");for (int i = 0; i < 10000; i++) memcpy(source+3+i*5,"word ",5);
    strcpy(source+50003,"</p>");
    tiku_html_view_t v;tiku_html_view_init(&v,NULL);
    check(tiku_html_view_load(&v,source,NULL),"large view loaded before constrained reflow");free(source);
    if (v.doc) {
        tiku_doc_limits_t next = v.doc->limits;next.lines *= 2;
        v.options.memory_bytes = sizeof *v.doc+tiku_doc_storage_size(&next);
        tiku_html_view_resize(&v,(tiku_rect_t){0,0,1,40});
        size_t required = sizeof *v.doc+v.doc->storage_bytes+strlen(v.text)+1+
            (size_t)v.doc->nrun*sizeof *v.starts;
        check(required <= v.options.memory_bytes && tiku_doc_check(v.doc),
              "reflow reserves the flattened text and run mapping within the owned-view budget");
        check(strstr(v.text,"word") != NULL,"budget-limited reflow retains readable source text");
    }
    tiku_html_view_free(&v);
}

int main(void)
{
    tiku_font_set_size(12);tiku_font_set_family(0);
    interaction();ownership();borrowed();selection_pixels();profiles();reflow_budget();
    printf("HTML view: %d failure(s)\n",failures);return failures != 0;
}
