/*
 * TikuDesktop Translation Kit.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richhtml_style.c - HTML escaping and locale-independent CSS values.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richhtml_private.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

void
tiku_rhtml_escape(tiku_rio_writer_t *w, const char *text, size_t length)
{
    size_t start = 0;

    for (size_t i = 0; i < length && w->status == TIKU_RICHIO_OK; i++) {
        const char *escaped = NULL;

        if (!(i % 4096) && w->options.cancel &&
            w->options.cancel(w->options.ctx)) {
            w->status = TIKU_RICHIO_CANCELLED;
            return;
        }
        switch (text[i]) {
        case '&': escaped = "&amp;"; break;
        case '<': escaped = "&lt;"; break;
        case '>': escaped = "&gt;"; break;
        case '"': escaped = "&quot;"; break;
        case '\'': escaped = "&#39;"; break;
        case '\r': escaped = "&#13;"; break;
        }
        if (escaped) {
            tiku_rio_put(w, text + start, i - start);
            tiku_rio_literal(w, escaped);
            start = i + 1;
        }
    }
    tiku_rio_put(w, text + start, length - start);
}

void
tiku_rhtml_number(tiku_rio_writer_t *w, double value)
{
    char text[48];
    long long scaled = llround(value * 1000);
    unsigned long long magnitude = scaled < 0 ? -scaled : scaled;

    snprintf(text, sizeof text, "%s%llu.%03llu", scaled < 0 ? "-" : "",
             magnitude / 1000, magnitude % 1000);
    tiku_rio_literal(w, text);
}

void
tiku_rhtml_color(tiku_rio_writer_t *w, uint32_t color)
{
    char text[10];

    snprintf(text, sizeof text, "#%08x", (unsigned)((color << 8) |
                                                   (color >> 24)));
    tiku_rio_literal(w, text);
}

/** @brief Emit a quoted CSS family with escaped ASCII delimiters. */
static void
font(tiku_rio_writer_t *w, const char *name)
{
    char escape[8];

    tiku_rio_literal(w, "font-family:&quot;");
    for (const unsigned char *p = (const unsigned char *)name; *p; p++) {
        if (*p < 128) {
            snprintf(escape, sizeof escape, "\\%02x ", *p);
            tiku_rio_literal(w, escape);
        } else tiku_rio_put(w, (const char *)p, 1);
    }
    tiku_rio_literal(w, "&quot;;");
}

void
tiku_rhtml_chars(tiku_rio_writer_t *w, const tiku_rich_chars_t *c)
{
    unsigned mask = c->present;

    tiku_rio_literal(w, "font-weight:");
    tiku_rio_literal(w, mask & TIKU_RICH_BOLD && c->bold ? "bold;" : "normal;");
    tiku_rio_literal(w, "font-style:");
    tiku_rio_literal(w, mask & TIKU_RICH_ITALIC && c->italic ?
                     "italic;" : "normal;");
    tiku_rio_literal(w, "text-decoration:");
    if (mask & TIKU_RICH_UNDERLINE && c->underline)
        tiku_rio_literal(w, "underline ");
    if (mask & TIKU_RICH_STRIKE && c->strike)
        tiku_rio_literal(w, "line-through ");
    if (!(mask & TIKU_RICH_UNDERLINE && c->underline) &&
        !(mask & TIKU_RICH_STRIKE && c->strike)) tiku_rio_literal(w, "none");
    tiku_rio_literal(w, ";font-size:");
    tiku_rhtml_number(w, mask & TIKU_RICH_SIZE ? c->size : 12);
    tiku_rio_literal(w, "pt;");
    if (mask & TIKU_RICH_FONT) font(w, c->font);
    else tiku_rio_literal(w, "font-family:sans-serif;");
    tiku_rio_literal(w, "color:");
    tiku_rhtml_color(w, mask & TIKU_RICH_FOREGROUND ?
                      c->foreground : 0xff000000);
    tiku_rio_literal(w, ";background-color:");
    tiku_rhtml_color(w, mask & TIKU_RICH_HIGHLIGHT ? c->highlight : 0);
    tiku_rio_literal(w, ";vertical-align:");
    tiku_rio_literal(w, mask & TIKU_RICH_BASELINE && c->baseline ?
        (c->baseline > 0 ? "super;" : "sub;") : "baseline;");
}

/** @brief Emit a point-valued property from validated model geometry. */
static void
points(tiku_rio_writer_t *w, const char *name, double value)
{
    tiku_rio_literal(w, name);
    tiku_rhtml_number(w, value);
    tiku_rio_literal(w, "pt;");
}

void
tiku_rhtml_props(tiku_rio_writer_t *w, const tiku_rich_para_t *p)
{
    static const char *const align[] = {"left;", "center;", "right;",
                                       "justify;"};

    tiku_rio_literal(w, "text-align:");
    tiku_rio_literal(w, align[p->present & TIKU_RICH_ALIGNMENT ? p->align : 0]);
    if (p->present & TIKU_RICH_INDENT) {
        points(w, "margin-left:", p->left);
        points(w, "margin-right:", p->right);
        points(w, "text-indent:", p->first);
    }
    if (p->present & TIKU_RICH_SPACING) {
        points(w, "margin-top:", p->before);
        points(w, "margin-bottom:", p->after);
    }
    if (p->present & TIKU_RICH_KEEP) {
        if (p->keep_next) tiku_rio_literal(w, "break-after:avoid;");
        if (p->keep_lines) tiku_rio_literal(w, "break-inside:avoid;");
    }
    if (p->present & TIKU_RICH_PAGE_BREAK && p->break_before)
        tiku_rio_literal(w, "break-before:page;");
}

void
tiku_rhtml_paragraph(tiku_rio_writer_t *w,
    const tiku_richsnapshot_t *snapshot, size_t index)
{
    tiku_rich_paragraph_t p;
    tiku_rich_chars_t c;
    tiku_rich_para_t props;
    size_t at = 0;
    char tag[3] = "p";

    tiku_richsnapshot_paragraph(snapshot, index, &p);
    tiku_richsnapshot_resolve(snapshot, index, 0, &c, &props);
    if (props.outline) { tag[0] = 'h'; tag[1] = (char)('0' + props.outline); }
    tiku_rio_literal(w, "<");
    tiku_rio_literal(w, tag);
    tiku_rio_literal(w, " style=\"white-space:pre-wrap;");
    tiku_rhtml_props(w, &props);
    if (!p.length) tiku_rhtml_chars(w, &c);
    tiku_rio_literal(w, "\">");
    for (size_t i = 0; i < p.run_count && w->status == TIKU_RICHIO_OK; i++) {
        const tiku_rich_run_t *run = &p.runs[i];

        tiku_richsnapshot_resolve(snapshot, index, i, &c, &props);
        if (run->link) {
            tiku_rio_literal(w, "<a href=\"");
            tiku_rhtml_escape(w, run->link, strlen(run->link));
            tiku_rio_literal(w, "\" style=\"text-decoration:none\">");
        }
        tiku_rio_literal(w, "<span style=\"");
        tiku_rhtml_chars(w, &c);
        tiku_rio_literal(w, "\">");
        if (run->image) tiku_rhtml_image(w, run->image);
        else tiku_rhtml_escape(w, p.text + at, run->length);
        at += run->length;
        tiku_rio_literal(w, "</span>");
        if (run->link) tiku_rio_literal(w, "</a>");
    }
    if (!p.length) tiku_rio_literal(w, "<br>");
    tiku_rio_literal(w, "</");
    tiku_rio_literal(w, tag);
    tiku_rio_literal(w, ">\n");
}
