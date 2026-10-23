/*
 * TikuOS Interface Kit.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_doc_fonts.c - CSS font metrics, inheritance and painted geometry.
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tiku_doc.h"
#include "tiku_doc_paint.h"
#include "tiku_font.h"

static tiku_doc_t doc;
static int failures;

/** @brief Record an assertion while retaining later checks. */
static void check(int condition, const char *message)
{
    printf("[%s] fonts %s\n", condition ? "PASS" : "FAIL", message);
    failures += !condition;
}

/** @brief Find a visible run containing a word. */
static const tiku_doc_run_t *word(const char *text)
{
    for (int i = 0; i < doc.nrun; i++)
        if (strstr(doc.text+doc.run[i].off,text)) return &doc.run[i];
    return NULL;
}

/** @brief Compare the run's metrics with a known desktop face. */
static int measured(const char *text, const tiku_font_t *font)
{
    const tiku_doc_run_t *run = word(text);
    return run && font &&
        tiku_measure_width(run->face,text) == tiku_text_width(font,text) &&
        tiku_measure_height(run->face) == font->height &&
        tiku_measure_ascent(run->face) == font->ascent;
}

/** @brief Parse and lay out a page without changing desktop font settings. */
static void read_fonts(const char *html)
{
    int family = tiku_font_family(), size = tiku_font_size();
    tiku_doc_read(&doc,html); tiku_doc_layout(&doc,600);
    check(tiku_font_family() == family && tiku_font_size() == size,
          "document fonts preserve desktop font settings");
}

/** @brief Verify supported sizes, weights, families and style precedence. */
static void variants(void)
{
    read_fonts("<p style='font-size:14px'>MeasuredWWiii</p>");
    check(measured("MeasuredWWiii",tiku_font_at(14)), "CSS pixels select measured proportional size");
    read_fonts("<p style='font-size:14px;font-weight:700'>WeightWWiii</p>");
    check(measured("WeightWWiii",tiku_font_styled(tiku_font_at(14),TIKU_FONT_BOLD)),
          "CSS size and bold weight select the same face");
    read_fonts("<p style='font-size:24px;font-weight:bold'>LargeWWiii</p>");
    check(measured("LargeWWiii",tiku_font_styled(tiku_font_at(12),TIKU_FONT_BOLD)->hi),
          "large document fonts reuse the measured high resolution faces");
    read_fonts("<p style='font-family:monospace;font-size:15px;font-style:italic;font-weight:bold'>MonoWWiii</p>");
    check(measured("MonoWWiii",tiku_font_styled(tiku_font_mono_at(15,0),
                   TIKU_FONT_BOLD | TIKU_FONT_ITALIC)),
          "monospace has sized bold italic metrics");
    int family = tiku_font_family(); tiku_font_set_family(1);
    const tiku_font_t *serif = tiku_font_at(14); tiku_font_set_family(family);
    read_fonts("<p style='font-family:Missing, serif;font-size:14px'>SerifWWiii</p>");
    check(measured("SerifWWiii",serif), "font fallback lists select an available serif family");
    read_fonts("<style>p {font-size:14px;font-weight:bold!important}</style>"
               "<p style='font-weight:normal'>PriorityWWiii</p>");
    check(measured("PriorityWWiii",tiku_font_styled(tiku_font_at(14),TIKU_FONT_BOLD)),
          "font properties respect importance independently");
    read_fonts("<h1 style='font-size:12px;font-weight:normal'>HeadingWWiii</h1>");
    check(measured("HeadingWWiii",tiku_font_at(12)), "heading defaults retain explicit CSS overrides");
}

/** @brief Relative sizes remain logical through resizing and UI text changes. */
static void inheritance(void)
{
    read_fonts("<div style='font-size:14px'><span style='font-size:1em'>ParentWWiii</span>"
               "<span style='font-size:1rem'>RootWWiii</span>"
               "<span style='font-size:80%'>PercentWWiii</span>"
               "<b style='font-weight:normal'>RegularWWiii</b></div><p>PlainWWiii</p>");
    check(measured("ParentWWiii",tiku_font_at(14)) && measured("RootWWiii",tiku_font_at(12)) &&
          measured("PercentWWiii",tiku_font_at(10)), "em, rem and percent sizes resolve against their parent or root");
    check(measured("RegularWWiii",tiku_font_at(14)) &&
          measured("PlainWWiii",tiku_font_plain()), "font inheritance and semantic overrides restore at boundaries");
    read_fonts("<div style='font-size:12px'><h1 style='font-size:1em;font-weight:inherit'>"
               "RelativeHeading</h1><b style='font-weight:inherit'>InheritedWeight</b></div>");
    check(measured("RelativeHeading",tiku_font_at(12)) &&
          measured("InheritedWeight",tiku_font_at(12)),
          "CSS inherited values use the parent before semantic defaults");
    read_fonts("<p style='font-size:14px;font-style:italic'>Fallback</p>");
    check(tiku_doc_notices(&doc) & TIKU_DOC_FONT_FALLBACK,
          "unavailable proportional italics report font approximation");
    read_fonts("<p style='font-size:1.5em'>TemporaryFallback</p>");
    check(tiku_doc_notices(&doc) & TIKU_DOC_FONT_FALLBACK,
          "sizes between available faces report approximation");
    tiku_font_set_size(16); tiku_doc_layout(&doc,600);
    check(!(tiku_doc_notices(&doc) & TIKU_DOC_FONT_FALLBACK),
          "reflow clears approximation notices when the requested face is available");
    tiku_font_set_size(12);
    read_fonts("<p style='font-size:1em'>DynamicWWiii</p>");
    tiku_font_set_size(16); tiku_doc_layout(&doc,600);
    check(measured("DynamicWWiii",tiku_font_at(16)), "relative CSS sizes reflow with the Reader text size");
    tiku_font_set_size(12);
    read_fonts("<ol style='font-family:monospace;font-size:15px'><li>Item words</li></ol>");
    check(doc.nrun > 1 && measured("1.",tiku_font_mono_at(15,0)),
          "list markers inherit the document font");
    int consistent = 1;
    for (int i = 0; i < doc.npiece; i++) {
        const tiku_doc_piece_t *pc = &doc.piece[i];
        char text[256];
        if (pc->len >= (int)sizeof text) { consistent = 0; continue; }
        memcpy(text,doc.text+pc->off,(size_t)pc->len); text[pc->len] = 0;
        consistent &= pc->w == tiku_measure_width(doc.run[pc->run].face,text);
    }
    check(consistent && tiku_doc_check(&doc), "marker and body pieces use their painted font metrics");
}

/** @brief Font changes remain valid through wrapping, links and raster painting. */
static void geometry(void)
{
    read_fonts("<p style='font-family:serif;font-size:14px;font-weight:bold'>A wide paragraph "
               "<a href='#end'>with a link and wrapping text</a> before its end.</p>");
    int wide = doc.nline; tiku_doc_layout(&doc,140);
    check(doc.nline > wide && tiku_doc_check(&doc), "styled text reflows at narrow widths");
    int hits = 0, correct = 1;
    for (int i = 0; i < doc.npiece; i++) {
        const tiku_doc_piece_t *pc = &doc.piece[i];
        if (doc.run[pc->run].link == TIKU_DOC_NONE || pc->w < 1) continue;
        tiku_rect_t r = tiku_doc_piece_rect(&doc,i);
        correct &= tiku_doc_hit(&doc,r.x+r.w/2,r.y+r.h/2).kind == TIKU_DOC_HIT_LINK;
        hits++;
    }
    check(hits && correct, "styled links hit at their laid-out positions");
    tiku_surface_t *surface = tiku_surface_new(200,400,TIKU_C_DOC);
    check(surface != NULL, "styled page paint surface allocated");
    if (surface) {
        tiku_doc_draw(surface,&doc,(tiku_rect_t){10,10,140,380},NULL);
        const char *path = getenv("TIKU_FONT_SNAPSHOT");
        if (path) check(!tiku_surface_png(surface,path), "styled font snapshot written");
        tiku_surface_free(surface);
    }
    read_fonts("<p style='font-size:24px;font-weight:bold'>Pixels</p>");
    surface = tiku_surface_new(240,140,TIKU_C_DOC);
    tiku_surface_t *reference = tiku_surface_new(240,140,TIKU_C_DOC);
    if (surface && reference && doc.nline && doc.npiece) {
        tiku_doc_draw(surface,&doc,(tiku_rect_t){10,10,220,120},NULL);
        tiku_rect_t box = tiku_doc_piece_rect(&doc,0);
        const tiku_font_t *font = tiku_font_styled(tiku_font_at(12),TIKU_FONT_BOLD)->hi;
        tiku_text(reference,font,10+box.x,10+box.y+doc.line[0].ascent,
                  "Pixels",tiku_doc_ink(TIKU_DOC_INK_PLAIN));
        check(!memcmp(surface->px,reference->px,(size_t)240*140*sizeof *surface->px),
              "styled text pixels match the face used for measurement");
    } else check(0,"font comparison surfaces allocated");
    tiku_surface_free(surface); tiku_surface_free(reference);
}

int main(void)
{
    tiku_font_set_size(12); tiku_font_set_family(0); tiku_doc_init(&doc);
    variants(); inheritance(); geometry();
    printf("Fonts: %d failure(s)\n",failures);
    return failures != 0;
}
