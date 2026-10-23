/*
 * TikuDesktop Translation Kit.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richmarkup_read.c - HTML document structure and editable rich import.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richmarkup_private.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/** @brief One open HTML element and its local table/list state. */
typedef struct {
    char name[32];
    tiku_rm_style_t style;
    size_t node, parent;        /**< owned table/cell and previous container */
    unsigned rows, columns, cells, headers;
    unsigned list_depth;
    uint32_t start;
    int block, row, header, list_item;
} frame_t;

/** @brief Fixed-depth markup traversal and its bounded document staging. */
typedef struct {
    tiku_rimp_t builder;
    frame_t stack[65];
    unsigned depth;
    int pending_space;
    size_t empty_breaks;        /**< leading BR elements pending visible text */
} reader_t;

/** @brief Whether a tag belongs to a space-separated, ASCII name list. */
static int
named(const char *name, const char *list)
{
    size_t length = strlen(name);

    while (*list) {
        const char *end = strchr(list, ' ');
        size_t n = end ? (size_t)(end - list) : strlen(list);

        if (n == length && !memcmp(name, list, n)) return 1;
        if (!end) break;
        list = end + 1;
    }
    return 0;
}

/** @brief Find the nearest open table without depending on node addresses. */
static frame_t *
table_frame(reader_t *h)
{
    for (unsigned i = h->depth; i; i--)
        if (!strcmp(h->stack[i].name, "table")) return &h->stack[i];
    return NULL;
}

/** @brief End the current paragraph and discard collapsible trailing space. */
static void
end_paragraph(reader_t *h)
{
    tiku_rimp_t *r = &h->builder;
    const tiku_rm_style_t *s = &h->stack[h->depth].style;

    while (h->empty_breaks > 1 && tiku_rimp_active(r)) {
        tiku_rimp_text(r, "\n", 1, &s->chars, s->link);
        h->empty_breaks--;
    }
    h->empty_breaks = 0;
    h->builder.current = 0;
    h->pending_space = 0;
}

/** @brief Start an implicit or explicit paragraph within a body or cell. */
static void
paragraph(reader_t *h, const tiku_rm_style_t *style)
{
    tiku_rimp_t *r = &h->builder;
    tiku_rich_paragraph_source_t p = {.chars = style->chars,
        .paragraph = style->paragraph, .list = style->list,
        .style = style->heading};
    frame_t *table = table_frame(h);

    if (r->current || !tiku_rimp_active(r)) return;
    if (table && r->parent == table->parent) {
        r->status = TIKU_RICHIO_UNSUPPORTED;
        return;
    }
    tiku_rimp_paragraph(r, &p);
}

/** @brief Append decoded text, applying HTML whitespace and inherited style. */
static void
text(reader_t *h, const char *value)
{
    tiku_rimp_t *r = &h->builder;
    const tiku_rm_style_t *style = &h->stack[h->depth].style;
    size_t at = 0, length = strlen(value);

    if (length && (style->pre || strspn(value, " \t\n\r\f") != length)) {
        while (h->empty_breaks && tiku_rimp_active(r)) {
            tiku_rimp_text(r, "\n", 1, &style->chars, style->link);
            h->empty_breaks--;
        }
    }
    if (style->pre) {
        if (length) paragraph(h, style);
        for (size_t start = 0; at <= length && tiku_rimp_active(r); at++) {
            if (at != length && value[at] != '\r') continue;
            tiku_rimp_text(r, value + start, at - start, &style->chars, style->link);
            if (at < length) {
                tiku_rimp_text(r, "\n", 1, &style->chars, style->link);
                if (at + 1 < length && value[at + 1] == '\n') at++;
            }
            start = at + 1;
        }
        return;
    }
    while (at < length && tiku_rimp_active(r)) {
        if (strchr(" \t\n\r\f", value[at])) {
            h->pending_space = 1;
            at++;
            continue;
        }
        size_t start = at;

        while (at < length && !strchr(" \t\n\r\f", value[at])) at++;
        paragraph(h, style);
        if (h->pending_space && r->current &&
            r->paragraphs[r->current - 1].value.length)
            tiku_rimp_text(r, " ", 1, &style->chars, style->link);
        h->pending_space = 0;
        tiku_rimp_text(r, value + start, at - start, &style->chars, style->link);
    }
}

/** @brief Decode a named attribute, limiting its encoded source size. */
static char *
attribute(reader_t *h, const tiku_rm_tag_t *tag, const char *name, size_t max)
{
    const tiku_rm_attr_t *a = tiku_rm_attr(tag, name);

    if (!a) return NULL;
    if (a->length > max) { h->builder.status = TIKU_RICHIO_LIMIT; return NULL; }
    return tiku_rm_decode(&h->builder, a->value, a->length);
}

/** @brief Read a positive decimal attribute without truncation or overflow. */
static unsigned
number(reader_t *h, const tiku_rm_tag_t *tag, const char *name, unsigned fallback)
{
    const tiku_rm_attr_t *a = tiku_rm_attr(tag, name);
    unsigned value = 0;

    if (!a) return fallback;
    if (!a->length) goto bad;
    for (size_t i = 0; i < a->length; i++) {
        unsigned digit = (unsigned char)a->value[i] - '0';

        if (digit > 9 || value > (UINT32_MAX - digit) / 10) goto bad;
        value = value * 10 + digit;
    }
    if (value) return value;
bad:
    h->builder.notices |= TIKU_RICHMARKUP_STRUCTURE;
    return fallback;
}

/** @brief Close an element and validate its completed row or table. */
static void
pop(reader_t *h)
{
    tiku_rimp_t *r = &h->builder;
    frame_t *f = &h->stack[h->depth];

    if (named(f->name, "td th")) {
        if (r->block_count == f->node) paragraph(h, &f->style);
        end_paragraph(h);
        r->parent = f->parent;
    } else if (f->row) {
        frame_t *t = table_frame(h);

        if (!t || !t->cells || (t->columns && t->columns != t->cells))
            r->status = TIKU_RICHIO_UNSUPPORTED;
        else {
            t->columns = t->cells;
            t->cells = 0;
            t->rows++;
            if (f->header && t->headers + 1 == t->rows) t->headers++;
            if (t->rows > 256 || t->rows * t->columns > 4096)
                r->status = TIKU_RICHIO_LIMIT;
        }
    } else if (!strcmp(f->name, "table") && f->node) {
        tiku_rich_table_t *t = &r->blocks[f->node - 1].table;

        t->rows = f->rows;
        t->columns = f->columns;
        t->header_rows = f->headers;
        if (!f->rows || !f->columns) r->status = TIKU_RICHIO_UNSUPPORTED;
        end_paragraph(h);
    } else if (f->block) end_paragraph(h);
    h->depth--;
}

/** @brief Close through a named ancestor, reporting mismatched inline nesting. */
static void
close_tag(reader_t *h, const char *name)
{
    unsigned found = h->depth;

    while (found && strcmp(h->stack[found].name, name)) found--;
    if (!found) { h->builder.notices |= TIKU_RICHMARKUP_STRUCTURE; return; }
    while (h->depth >= found && tiku_rimp_active(&h->builder)) pop(h);
}

/** @brief Apply HTML's optional paragraph, item, row and cell end tags. */
static void
implicit_close(reader_t *h, const char *name)
{
    const char *targets = named(name, "td th") ? "td th" :
        !strcmp(name, "tr") ? "tr" : !strcmp(name, "li") ? "li" :
        named(name, "p h1 h2 h3 h4 h5 h6 div table ul ol pre blockquote") ? "p" : "";

    for (unsigned i = h->depth; i; i--) {
        if (named(h->stack[i].name, targets)) {
            while (h->depth >= i && tiku_rimp_active(&h->builder)) pop(h);
            break;
        }
        if (named(h->stack[i].name, "table td th ul ol")) break;
    }
}

/** @brief Apply semantic inline tags and supported attribute formatting. */
static void
style(reader_t *h, const tiku_rm_tag_t *tag, frame_t *f)
{
    tiku_rimp_t *r = &h->builder;
    tiku_rich_chars_t *c = &f->style.chars;
    const char *name = tag->name;
    char *css;

    if (named(name, "b strong")) { c->present |= TIKU_RICH_BOLD; c->bold = 1; }
    if (named(name, "i em")) { c->present |= TIKU_RICH_ITALIC; c->italic = 1; }
    if (!strcmp(name, "u")) { c->present |= TIKU_RICH_UNDERLINE; c->underline = 1; }
    if (named(name, "s strike del")) { c->present |= TIKU_RICH_STRIKE; c->strike = 1; }
    if (named(name, "sub sup")) {
        c->present |= TIKU_RICH_BASELINE;
        c->baseline = !strcmp(name, "sup") ? 1 : -1;
    }
    if (named(name, "code pre kbd samp")) {
        c->present |= TIKU_RICH_FONT;
        strcpy(c->font, "monospace");
    }
    if (!strcmp(name, "pre")) f->style.pre = 1;
    if (strlen(name) == 2 && name[0] == 'h' && name[1] >= '1' && name[1] <= '6')
        f->style.heading = (unsigned)(name[1] - '0');
    if (!strcmp(name, "blockquote")) {
        f->style.paragraph.present |= TIKU_RICH_INDENT;
        f->style.paragraph.left += 24;
    }
    if (!strcmp(name, "a")) {
        char *link = attribute(h, tag, "href", 8192);

        if (link && tiku_rich_link_validate(link) == TIKU_RICH_OK)
            f->style.link = *link ? link : NULL;
        else if (link) r->notices |= TIKU_RICHMARKUP_CONTENT;
    }
    f->style.image = !strcmp(name, "img");
    f->style.image_width = f->style.image_height = 0;
    css = attribute(h, tag, "style", 16384);
    if (css) tiku_rm_css(r, css, &f->style);
    if (tiku_rm_attr(tag, "class") || tiku_rm_attr(tag, "align"))
        r->notices |= TIKU_RICHMARKUP_STYLE;
    if (tiku_rm_attr(tag, "id")) r->notices |= TIKU_RICHMARKUP_STRUCTURE;
    for (size_t i = 0; i < tag->count; i++) {
        if (!strncmp(tag->attributes[i].name, "on", 2))
            r->notices |= TIKU_RICHMARKUP_CONTENT;
        else if (!named(tag->attributes[i].name,
            "style class id href src alt start value rowspan colspan") &&
            !(f->style.image && named(tag->attributes[i].name, "width height")))
            r->notices |= TIKU_RICHMARKUP_STYLE;
    }
}

/** @brief Add a table, row or cell to the depth-first import hierarchy. */
static void
grid(reader_t *h, const tiku_rm_tag_t *tag, frame_t *f)
{
    tiku_rimp_t *r = &h->builder;
    frame_t *t = table_frame(h);

    if (!strcmp(tag->name, "table")) {
        unsigned tables = 0;

        for (unsigned i = 1; i <= h->depth; i++)
            tables += !strcmp(h->stack[i].name, "table");
        if (tables >= 8) { r->status = TIKU_RICHIO_LIMIT; return; }
        if (t && r->parent == t->parent) { r->status = TIKU_RICHIO_UNSUPPORTED; return; }
        f->node = tiku_rimp_block(r, TIKU_RICH_BLOCK_TABLE, r->parent);
        f->parent = r->parent;
        if (f->node) r->blocks[f->node - 1].table = (tiku_rich_table_t){
            .padding = 4, .border = 1, .color = 0xff808080};
    } else if (!strcmp(tag->name, "tr")) {
        if (!t) { r->status = TIKU_RICHIO_DAMAGED; return; }
        f->row = 1;
        f->header = 1;
        for (unsigned i = h->depth; i && &h->stack[i] != t; i--)
            if (!strcmp(h->stack[i].name, "thead")) f->header = 2;
    } else if (named(tag->name, "td th")) {
        if (!t || !h->depth || !h->stack[h->depth].row) {
            r->status = TIKU_RICHIO_UNSUPPORTED;
            return;
        }
        if (number(h, tag, "colspan", 1) != 1 ||
            number(h, tag, "rowspan", 1) != 1) {
            r->status = TIKU_RICHIO_UNSUPPORTED;
            return;
        }
        if (++t->cells > 64) { r->status = TIKU_RICHIO_LIMIT; return; }
        if (!strcmp(tag->name, "td") && h->stack[h->depth].header == 1)
            h->stack[h->depth].header = 0;
        f->node = tiku_rimp_block(r, TIKU_RICH_BLOCK_CELL, t->node);
        f->parent = r->parent;
        r->parent = f->node;
        if (f->node && (f->style.chars.present & TIKU_RICH_HIGHLIGHT)) {
            r->blocks[f->node - 1].background = f->style.chars.highlight;
            f->style.chars.present &= ~TIKU_RICH_HIGHLIGHT;
        }
    }
}

/** @brief Open one non-active element, retaining a bounded inherited state. */
static void
open_tag(reader_t *h, const tiku_rm_tag_t *tag)
{
    static const char *const blocks = "p h1 h2 h3 h4 h5 h6 div section article "
        "header footer main aside pre blockquote li ul ol table tr td th hr";
    const char *name = tag->name;
    tiku_rimp_t *r = &h->builder;
    frame_t f = {0};

    implicit_close(h, name);
    if (!tiku_rimp_active(r)) return;
    if (h->depth == 64) { r->status = TIKU_RICHIO_LIMIT; return; }
    strcpy(f.name, name);
    f.style = h->stack[h->depth].style;
    f.list_depth = h->stack[h->depth].list_depth;
    f.block = named(name, blocks);
    if (f.block) end_paragraph(h);
    else if (strcmp(name, "br") && h->pending_space && r->current &&
             r->paragraphs[r->current - 1].value.length) {
        tiku_rimp_text(r, " ", 1, &f.style.chars, f.style.link);
        h->pending_space = 0;
    }
    style(h, tag, &f);
    if (named(name, "ul ol")) {
        if (f.list_depth >= 9) { r->status = TIKU_RICHIO_LIMIT; return; }
        f.list_depth++;
        f.style.list = (tiku_rich_list_t){0};
        f.start = number(h, tag, "start", 1);
    }
    if (!strcmp(name, "li")) {
        for (unsigned i = h->depth; i; i--)
            if (named(h->stack[i].name, "ul ol")) {
                frame_t *list = &h->stack[i];

                f.style.list = (tiku_rich_list_t){
                    .kind = !strcmp(list->name, "ol") ? TIKU_RICH_LIST_DECIMAL :
                                                       TIKU_RICH_LIST_BULLET,
                    .level = list->list_depth - 1};
                if (f.style.list.kind == TIKU_RICH_LIST_DECIMAL)
                    f.style.list.restart = number(h, tag, "value", list->start);
                list->start = 0;
                f.list_item = 1;
                break;
            }
        if (!f.list_item) r->notices |= TIKU_RICHMARKUP_STRUCTURE;
    }
    grid(h, tag, &f);
    if (!tiku_rimp_active(r)) return;
    h->stack[++h->depth] = f;
    if (named(name, "p h1 h2 h3 h4 h5 h6 pre")) paragraph(h, &f.style);
    if (!strcmp(name, "br")) {
        paragraph(h, &f.style);
        h->pending_space = 0;
        if (r->current && !r->paragraphs[r->current - 1].value.length)
            h->empty_breaks++;
        else tiku_rimp_text(r, "\n", 1, &f.style.chars, f.style.link);
    } else if (!strcmp(name, "img")) {
        char *alt = attribute(h, tag, "alt", 8192);

        paragraph(h, &f.style);
        h->pending_space = 0;
        while (h->empty_breaks && tiku_rimp_active(r)) {
            tiku_rimp_text(r, "\n", 1, &f.style.chars, f.style.link);
            h->empty_breaks--;
        }
        if (!tiku_rm_picture(r, tag, &f.style, alt ? alt : "")) {
            r->notices |= TIKU_RICHMARKUP_IMAGES;
            text(h, "[Image: ");
            text(h, alt && *alt ? alt : "picture");
            text(h, "]");
        }
    } else if (!strcmp(name, "hr")) {
        text(h, "\xe2\x80\x94\xe2\x80\x94\xe2\x80\x94");
        r->notices |= TIKU_RICHMARKUP_STRUCTURE;
    } else if (!f.block && !named(name, "html body span a b strong i em u s "
               "strike del sub sup code kbd samp thead tbody tfoot colgroup "
               "col br meta link wbr")) r->notices |= TIKU_RICHMARKUP_STRUCTURE;
    if (named(name, "meta link col colgroup")) r->notices |= TIKU_RICHMARKUP_STYLE;
    if (tag->empty || named(name, "br img hr meta link col input wbr")) pop(h);
}

/** @brief Locate raw-text closing markup with bounded, cancellable scanning. */
static size_t
skip_raw(reader_t *h, const char *data, size_t length, size_t at,
         const char *name)
{
    size_t n = strlen(name);

    for (; at + n + 2 <= length; at++) {
        if (!(at % 4096) && !tiku_rimp_active(&h->builder)) return length;
        if (data[at] == '<' && data[at+1] == '/' &&
            !strncasecmp(data + at + 2, name, n) &&
            (at + n + 2 == length || strchr(" \t\n\r>", data[at+n+2]))) {
            while (at < length && data[at] != '>') at++;
            return at < length ? at + 1 : length;
        }
    }
    return length;
}

tiku_richio_status_t
tiku_richmarkup_read_html(const void *input, size_t length,
    const tiku_richio_options_t *options, tiku_richdoc_t **out, unsigned *notices)
{
    reader_t h = {0};
    tiku_rimp_t *r = &h.builder;
    const char *data = input;
    size_t at = 0;

    if (out) *out = NULL;
    if (notices) *notices = 0;
    if (!out || (!input && length)) return TIKU_RICHIO_INVALID;
    r->status = tiku_rio_options(options, &r->options);
    if (r->status != TIKU_RICHIO_OK) return r->status;
    if (length > r->options.xml_bytes) return TIKU_RICHIO_LIMIT;
    if (length && memchr(data, 0, length)) return TIKU_RICHIO_DAMAGED;
    if (length >= 3 && !memcmp(data, "\xef\xbb\xbf", 3)) at = 3;
    while (at < length && tiku_rimp_active(r)) {
        if (data[at] != '<') {
            size_t start = at;

            while (at < length && data[at] != '<') at++;
            char *decoded = tiku_rm_decode(r, data + start, at - start);

            if (decoded) text(&h, decoded);
        } else if (length - at >= 4 && !memcmp(data + at, "<!--", 4)) {
            at += 4;
            while (length - at >= 3 && memcmp(data + at, "-->", 3)) {
                if (!(at % 4096) && !tiku_rimp_active(r)) break;
                at++;
            }
            if (length - at < 3) r->status = TIKU_RICHIO_DAMAGED;
            else at += 3;
        } else if (length - at >= 9 && !strncasecmp(data + at, "<!doctype", 9)) {
            while (at < length && data[at] != '>' && data[at] != '[') at++;
            if (at == length || data[at] == '[') r->status = TIKU_RICHIO_DAMAGED;
            else at++;
        } else {
            tiku_rm_tag_t tag;

            if (!tiku_rm_tag(r, data, length, &at, &tag)) break;
            if (tag.closing) close_tag(&h, tag.name);
            else if (named(tag.name, "head script style title iframe object svg canvas")) {
                r->notices |= named(tag.name, "head style") ?
                    TIKU_RICHMARKUP_STYLE | TIKU_RICHMARKUP_PAGES :
                    TIKU_RICHMARKUP_CONTENT;
                if (!tag.empty) at = skip_raw(&h, data, length, at, tag.name);
            } else open_tag(&h, &tag);
        }
    }
    while (h.depth && tiku_rimp_active(r)) pop(&h);
    tiku_rimp_finish(r, out);
    if (r->status == TIKU_RICHIO_OK && notices) *notices = r->notices;
    tiku_rimp_dispose(r);
    return r->status;
}
