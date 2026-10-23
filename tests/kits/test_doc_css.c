/*
 * TikuOS Interface Kit.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_doc_css.c - CSS text cascade, inheritance and painted decorations.
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

/** @brief Record an assertion and retain subsequent checks. */
static void check(int condition, const char *message)
{
    printf("[%s] css %s\n", condition ? "PASS" : "FAIL", message);
    failures += !condition;
}

/** @brief Find a run containing the requested word. */
static const tiku_doc_run_t *word(const char *text)
{
    for (int i = 0; i < doc.nrun; i++)
        if (strstr(doc.text + doc.run[i].off, text)) return &doc.run[i];
    return NULL;
}

/** @brief Whether a word has the explicit expected foreground. */
static int colored(const char *text, uint32_t color)
{
    const tiku_doc_run_t *run = word(text);
    return run && (run->flags & TIKU_DOC_RUN_COLOR) && run->color == color;
}

/** @brief Parse and reflow a page. */
static void read_css(const char *html)
{
    tiku_doc_read(&doc, html);
    tiku_doc_layout(&doc, 400);
}

/** @brief Check stylesheet matching and property precedence. */
static void cascade(void)
{
    read_css("<style>p{color:red}.note{color:#123}#target{color:#123456}"
             "p.note#target{color:blue}</style><p id='target' class='note'>Combined</p>");
    check(colored("Combined", 0x0000ff), "compound selector matches tag, ID and class");
    read_css("<style>p{color:red}#target{color:blue}.note{color:green}</style>"
             "<p id='target' class='note'>Specific</p>");
    check(colored("Specific", 0x0000ff), "ID specificity wins over a later class");
    read_css("<style>.note{color:red}.note{color:blue}</style>"
             "<p class='other note active'>Later</p><p class='noteworthy'>Plain</p>");
    check(colored("Later", 0x0000ff), "equal specificity follows source order");
    check(word("Plain") && !(word("Plain")->flags & TIKU_DOC_RUN_COLOR),
          "class selectors match complete class names");
    read_css("<style>p{color:red!important}#target{color:blue}</style>"
             "<p id='target' style='color:green'>Important</p>"
             "<p style='color:orange !IMPORTANT'>Inline</p>");
    check(colored("Important", 0xff0000), "important stylesheet declaration beats normal inline");
    check(colored("Inline", 0xffa500), "important inline declaration wins its cascade");
    read_css("<style>h2, .note {color:#abc; text-decoration:underline}</style>"
             "<h2>Heading</h2><p class='note'>Paragraph</p>");
    check(colored("Heading", 0xaabbcc) && colored("Paragraph", 0xaabbcc),
          "selector lists share declarations and expand short hex colors");
    check(word("Heading") && (word("Heading")->flags & TIKU_DOC_RUN_UNDERLINE),
          "color and decoration cascade independently");
    read_css("<p>Earlier</p><style>p {color: #123456;}</style>");
    check(colored("Earlier", 0x123456), "later stylesheets affect earlier content");
    read_css("<STYLE>/* lead */ P { COLOR: red; /* middle */ COLOR: blue; }</STYLE>"
             "<P>Comments</P>");
    check(colored("Comments", 0x0000ff), "CSS comments and case-insensitive property names parse");
}

/** @brief Verify nested restoration and implicit list/paragraph boundaries. */
static void inheritance(void)
{
    read_css("<body style='color:red'><div>Outer <span style='color:blue'>Inner</span>"
             " After</div><p>Sibling</p></body><p>Default</p>");
    check(colored("Outer", 0xff0000) && colored("Inner", 0x0000ff) &&
          colored("After", 0xff0000) && colored("Sibling", 0xff0000),
          "foreground inherits and restores through nested containers");
    check(word("Default") && !(word("Default")->flags & TIKU_DOC_RUN_COLOR),
          "closed body does not color subsequent content");
    read_css("<div style='color:red'><p style='color:inherit'>Inherited</p>"
             "<p style='color:initial'>Theme</p></div>");
    check(colored("Inherited", 0xff0000) && word("Theme") &&
          !(word("Theme")->flags & TIKU_DOC_RUN_COLOR),
          "inherit and initial retain explicit and theme foreground semantics");
    read_css("<p style='color:red'>One<p>Two<ul><li style='color:blue'>Three<li>Four</ul>");
    check(colored("One", 0xff0000) && word("Two") &&
          !(word("Two")->flags & TIKU_DOC_RUN_COLOR) &&
          colored("Three", 0x0000ff) && word("Four") &&
          !(word("Four")->flags & TIKU_DOC_RUN_COLOR),
          "omitted paragraph and item closes restore parent styles");
    read_css("<script>var s = '<style>p{color:red}</style>';</script>"
             "<!-- <style>p{color:blue}</style> --><p>Unstyled</p>");
    check(word("Unstyled") && !(word("Unstyled")->flags & TIKU_DOC_RUN_COLOR),
          "styles inside scripts and HTML comments do not enter the cascade");
    read_css("<h2 style='color:red'>First<h3>Second</h3>");
    check(colored("First", 0xff0000) && word("Second") &&
          !(word("Second")->flags & TIKU_DOC_RUN_COLOR),
          "an omitted heading close restores the parent style");
    read_css("<meta style='color:red'><p>After metadata</p>");
    check(word("After metadata") && !(word("After metadata")->flags & TIKU_DOC_RUN_COLOR),
          "void elements cannot retain an inherited style scope");
}

/** @brief Verify decorations reach both flags and raster output. */
static void painting(void)
{
    read_css("<p style='color:#123456;text-decoration:underline line-through'>Painted</p>");
    check(word("Painted") && (word("Painted")->flags & TIKU_DOC_RUN_UNDERLINE) &&
          (word("Painted")->flags & TIKU_DOC_RUN_STRIKE),
          "underline and line-through resolve together");
    tiku_surface_t *s = tiku_surface_new(440, 140, TIKU_C_DOC);
    check(s != NULL, "paint surface allocated");
    if (!s) return;
    tiku_doc_draw(s, &doc, (tiku_rect_t){10,10,400,120}, NULL);
    int n = 0;
    for (int y = 0; y < s->h; y++) for (int x = 0; x < s->w; x++)
        n += (s->px[(size_t)y*s->w+x] & 0xffffffu) == 0x123456;
    check(n > 0, "explicit RGB foreground reaches the surface");
    if (doc.nline && doc.npiece) {
        tiku_rect_t r = tiku_doc_piece_rect(&doc, 0);
        int x = 10+r.x+r.w/2, y = 10+r.y+doc.line[0].ascent+2;
        check((s->px[(size_t)y*s->w+x] & 0xffffffu) == 0x123456,
              "underline follows the laid-out piece and foreground");
        y = 10+r.y+doc.line[0].ascent/2;
        check((s->px[(size_t)y*s->w+x] & 0xffffffu) == 0x123456,
              "strikethrough follows the laid-out baseline");
    }
    tiku_surface_free(s);
    read_css("<u>Underline</u><s>Strike</s><del>Deleted</del> Plain");
    check(word("Underline") && (word("Underline")->flags & TIKU_DOC_RUN_UNDERLINE) &&
          word("Strike") && (word("Strike")->flags & TIKU_DOC_RUN_STRIKE) &&
          word("Deleted") && (word("Deleted")->flags & TIKU_DOC_RUN_STRIKE) &&
          word("Plain") && !(word("Plain")->flags &
          (TIKU_DOC_RUN_UNDERLINE | TIKU_DOC_RUN_STRIKE)),
          "semantic decoration tags restore after closing");
    read_css("<a href='next.html' style='color:#123456;text-decoration:none'>Quiet link</a>");
    check(!tiku_doc_notices(&doc), "link underline removal is a supported text style");
    s = tiku_surface_new(440,140,TIKU_C_DOC);
    if (s && doc.nline && doc.npiece) {
        tiku_doc_draw(s,&doc,(tiku_rect_t){10,10,400,120},NULL);
        tiku_rect_t r = tiku_doc_piece_rect(&doc,0);
        int x = 10+r.x+r.w/2, y = 10+r.y+doc.line[0].ascent+2;
        check((s->px[(size_t)y*s->w+x] & 0xffffffu) == TIKU_C_DOC,
              "CSS none removes the automatic link underline");
        tiku_doc_paint_t how = {.focus_link=0};
        tiku_doc_draw(s,&doc,(tiku_rect_t){10,10,400,120},&how);
        x = 10+r.x+r.w+1; y = 10+r.y+1;
        check((s->px[(size_t)y*s->w+x] & 0xffffffu) == TIKU_C_FOCUS,
              "links without underline retain their keyboard focus frame");
    }
    tiku_surface_free(s);
}

/** @brief Decode HTML references in inline CSS before CSS tokenization. */
static void attribute_references(void)
{
    read_css("<p style='font-size:14&#112;x;color&#58;#123456;"
             "text-decoration:under&#108;ine'>Encoded</p>");
    const tiku_doc_run_t *run = word("Encoded");
    check(colored("Encoded",0x123456) && run &&
          (run->flags & TIKU_DOC_RUN_UNDERLINE) &&
          tiku_measure_width(run->face,"Encoded") == tiku_text_width(tiku_font_at(14),"Encoded"),
          "HTML references decode in CSS property names, units and values");
    read_css("<p style='font-family:&#34;Mono&#34;;font-size:15px'>Quoted</p>");
    run = word("Quoted");
    check(run && tiku_measure_width(run->face,"Quoted") ==
          tiku_text_width(tiku_font_mono_at(15,0),"Quoted"),
          "encoded quotes preserve CSS family strings");
    read_css("<p style='color:red!important&#59;color:blue'>Priority</p>");
    check(colored("Priority",0xff0000), "decoded declaration separators retain important precedence");
}

/** @brief Unsupported CSS and limits leave document text available. */
static void limits(void)
{
    read_css("<style>@import url('https://invalid/remote.css');"
             "p { unknown-property: value; color:red; }</style><p>Retained</p>");
    check(colored("Retained", 0xff0000) && (doc.notices & TIKU_DOC_CSS_UNSUPPORTED),
          "unsupported rules and declarations report loss without dropping text");
    read_css("<style>p span {color:red} p {color:blue}</style><p>Kept</p>");
    check(colored("Kept", 0x0000ff) && (doc.notices & TIKU_DOC_CSS_UNSUPPORTED),
          "unsupported selectors do not prevent following rules");
    read_css("<p style='color:not-a-color;text-decoration:blink'>Visible</p>");
    check(word("Visible") && doc.notices && tiku_doc_check(&doc),
          "unsupported values retain consistent document text");
    char html[12000]; size_t at = 0;
    at += (size_t)snprintf(html+at, sizeof html-at, "<style>");
    for (int i = 0; i < TIKU_DOC_CSS_RULES_MAX+5; i++)
        at += (size_t)snprintf(html+at, sizeof html-at, ".n%d{color:red}", i);
    snprintf(html+at, sizeof html-at, "</style><p>Tail</p>");
    read_css(html);
    check(word("Tail") && (doc.notices & TIKU_DOC_CSS_LIMIT) && !doc.full,
          "CSS rule limits report style loss separately from text capacity");
    char *large = malloc(60000);
    check(large != NULL, "inline budget fixture allocated");
    if (large) {
        at = 0;
        for (int i = 0; i < 200; i++)
            at += (size_t)snprintf(large+at, 60000-at,
                    "<p style='color:red;%0190d'>X</p>", i);
        read_css(large);
        check(doc.nblock == 200 && !doc.full && (doc.notices & TIKU_DOC_CSS_LIMIT),
              "inline CSS byte limits preserve document text");
        free(large);
    }
    read_css("<p>Reset</p>");
    check(!tiku_doc_notices(&doc), "rereading clears earlier conversion notices");
}

/** @brief Exercise exact-length truncated CSS and HTML buffers. */
static void truncated(void)
{
    static const char *samples[] = {
        "<style>/*comment*/p{color:red!important;text-decoration:underline}</style><p>x</p>",
        "<style>@media x {p{color:red}} p {color:blue}</style><p>x</p>",
        "<style>p{unknown:'a;{x}';color:red;/*unterminated",
        "<p style='color:#12;text-decoration:underline line-through'>x</p>",
        "<style>p,,.x{color:blue}#x {color:inherit}</style>"
    };
    int consistent = 1;
    for (size_t i = 0; i < sizeof samples/sizeof samples[0]; i++) {
        size_t length = strlen(samples[i]);
        for (size_t n = 0; n <= length; n++) {
            char *prefix = malloc(n+1);
            if (!prefix) { consistent = 0; break; }
            memcpy(prefix,samples[i],n); prefix[n] = 0;
            read_css(prefix); consistent &= tiku_doc_check(&doc);
            free(prefix);
        }
    }
    check(consistent, "truncated CSS and quoted values preserve table invariants");
}

int main(void)
{
    tiku_doc_init(&doc); tiku_font_set_size(12);
    cascade(); inheritance(); painting(); attribute_references(); limits(); truncated();
    printf("CSS: %d failure(s)\n", failures);
    return failures != 0;
}
