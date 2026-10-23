/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richio_values.c - locale-independent XML attributes and properties.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richio_private.h"
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

_Static_assert(sizeof(float) == 4 && FLT_RADIX == 2 &&
               FLT_MANT_DIG == 24 && FLT_MAX_EXP == 128,
               "Native document points require IEEE binary32");

void
tiku_rio_put(tiku_rio_writer_t *w, const char *text, size_t length)
{
    if (w->status != TIKU_RICHIO_OK) return;
    if (length > w->options.xml_bytes - w->used ||
        w->used >= w->options.memory_bytes / 2 ||
        length >= w->options.memory_bytes / 2 - w->used) {
        w->status = TIKU_RICHIO_LIMIT;
        return;
    }
    if (w->data && length) memcpy(w->data + w->used, text, length);
    w->used += length;
}

void
tiku_rio_literal(tiku_rio_writer_t *w, const char *text)
{
    tiku_rio_put(w, text, strlen(text));
}

void
tiku_rio_uint(tiku_rio_writer_t *w, const char *name, size_t value)
{
    char digits[32];

    snprintf(digits, sizeof digits, "%zu", value);
    tiku_rio_literal(w, " ");
    tiku_rio_literal(w, name);
    tiku_rio_literal(w, "=\"");
    tiku_rio_literal(w, digits);
    tiku_rio_literal(w, "\"");
}

void
tiku_rio_escape(tiku_rio_writer_t *w, const char *text, size_t length)
{
    size_t i, start = 0;

    for (i = 0; i < length && w->status == TIKU_RICHIO_OK; i++) {
        const char *escaped = NULL;

        if (!(i % 4096) && w->options.cancel &&
            w->options.cancel(w->options.ctx)) {
            w->status = TIKU_RICHIO_CANCELLED;
            break;
        }
        switch (text[i]) {
        case '%': escaped = "%25"; break;
        case '&': escaped = "&amp;"; break;
        case '<': escaped = "&lt;"; break;
        case '>': escaped = "&gt;"; break;
        case '"': escaped = "&quot;"; break;
        case '\t': escaped = "&#9;"; break;
        case '\n': escaped = "&#10;"; break;
        case '\r': escaped = "&#13;"; break;
        }
        if ((unsigned char)text[i] == 0xef && length - i >= 3 &&
            (unsigned char)text[i + 1] == 0xbf &&
            ((unsigned char)text[i + 2] == 0xbe ||
             (unsigned char)text[i + 2] == 0xbf)) {
            tiku_rio_put(w, text + start, i - start);
            tiku_rio_literal(w, (unsigned char)text[i + 2] == 0xbe
                ? "%EF%BF%BE" : "%EF%BF%BF");
            i += 2;
            start = i + 1;
            continue;
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
tiku_rio_write_float(tiku_rio_writer_t *w, const char *name, float value)
{
    uint32_t bits;
    char digits[9];

    memcpy(&bits, &value, sizeof bits);
    snprintf(digits, sizeof digits, "%08x", (unsigned)bits);
    tiku_rio_literal(w, " ");
    tiku_rio_literal(w, name);
    tiku_rio_literal(w, "=\"");
    tiku_rio_literal(w, digits);
    tiku_rio_literal(w, "\"");
}

void
tiku_rio_write_chars(tiku_rio_writer_t *w, const tiku_rich_chars_t *c)
{
    tiku_rio_literal(w, "<chars");
    tiku_rio_uint(w, "mask", c->present);
    tiku_rio_uint(w, "bold", c->bold);
    tiku_rio_uint(w, "italic", c->italic);
    tiku_rio_uint(w, "underline", c->underline);
    tiku_rio_uint(w, "strike", c->strike);
    tiku_rio_write_float(w, "size-f32", c->size);
    tiku_rio_uint(w, "foreground", c->foreground);
    tiku_rio_uint(w, "highlight", c->highlight);
    tiku_rio_uint(w, "baseline", (size_t)(c->baseline + 1));
    tiku_rio_literal(w, " font=\"");
    tiku_rio_escape(w, c->font, strlen(c->font));
    tiku_rio_literal(w, "\"/>");
}

void
tiku_rio_write_para(tiku_rio_writer_t *w, const tiku_rich_para_t *p)
{
    tiku_rio_literal(w, "<para");
    tiku_rio_uint(w, "mask", p->present);
    tiku_rio_uint(w, "align", p->align);
    tiku_rio_write_float(w, "left-f32", p->left);
    tiku_rio_write_float(w, "right-f32", p->right);
    tiku_rio_write_float(w, "first-f32", p->first);
    tiku_rio_write_float(w, "before-f32", p->before);
    tiku_rio_write_float(w, "after-f32", p->after);
    tiku_rio_uint(w, "keep-next", p->keep_next);
    tiku_rio_uint(w, "keep-lines", p->keep_lines);
    if (w->break_feature) tiku_rio_uint(w, "break-before", p->break_before);
    if (w->outline_feature) tiku_rio_uint(w, "outline", p->outline);
    tiku_rio_literal(w, "/>");
}

int
tiku_rio_element(tiku_rio_reader_t *r, const tiku_xml_node_t *n,
    const char *name, const char *const *attrs, size_t count)
{
    size_t i, j;

    if (r->status != TIKU_RICHIO_OK) return 0;
    if (!n || !n->name) { r->status = TIKU_RICHIO_DAMAGED; return 0; }
    if (strcmp(n->name, name)) {
        r->status = TIKU_RICHIO_UNSUPPORTED;
        return 0;
    }
    for (i = 0; i < (size_t)n->nattr; i++) {
        for (j = 0; j < count; j++)
            if (!strcmp(n->attr[2 * i], attrs[j])) break;
        if (j == count) { r->status = TIKU_RICHIO_UNSUPPORTED; return 0; }
    }
    if ((size_t)n->nattr != count) {
        r->status = TIKU_RICHIO_DAMAGED;
        return 0;
    }
    if (r->options.cancel && r->options.cancel(r->options.ctx)) {
        r->status = TIKU_RICHIO_CANCELLED;
        return 0;
    }
    return 1;
}

size_t
tiku_rio_number(tiku_rio_reader_t *r, const tiku_xml_node_t *n,
    const char *name, size_t maximum)
{
    const char *s = tiku_xml_attr(n, name);
    size_t value = 0;

    if (!s || !*s) { r->status = TIKU_RICHIO_DAMAGED; return 0; }
    for (; *s; s++) {
        unsigned digit = (unsigned char)*s - '0';

        if (digit > 9 || digit > maximum || value > (maximum - digit) / 10) {
            r->status = TIKU_RICHIO_DAMAGED;
            return 0;
        }
        value = value * 10 + digit;
    }
    return value;
}

const tiku_xml_node_t *
tiku_rio_next(tiku_rio_reader_t *r, const tiku_xml_node_t *n)
{
    while (n && !n->name) {
        if (n->text[strspn(n->text, " \t\r\n")])
            r->status = TIKU_RICHIO_DAMAGED;
        n = n->next;
    }
    return n;
}

float
tiku_rio_read_float(tiku_rio_reader_t *r, const tiku_xml_node_t *n,
                    const char *name)
{
    const char *s = tiku_xml_attr(n, name);
    uint32_t bits = 0;
    float value = 0;
    size_t i;

    if (!s || strlen(s) != 8) {
        r->status = TIKU_RICHIO_DAMAGED;
        return 0;
    }
    for (i = 0; i < 8; i++) {
        unsigned c = (unsigned char)s[i];
        unsigned digit = c >= '0' && c <= '9' ? c - '0' :
            c >= 'a' && c <= 'f' ? c - 'a' + 10 : 16;

        if (digit == 16) { r->status = TIKU_RICHIO_DAMAGED; return 0; }
        bits = (bits << 4) | digit;
    }
    memcpy(&value, &bits, sizeof value);
    if (!isfinite(value)) r->status = TIKU_RICHIO_DAMAGED;
    return value;
}

void
tiku_rio_read_chars(tiku_rio_reader_t *r, const tiku_xml_node_t *n,
    tiku_rich_chars_t *c)
{
    static const char *const attrs[] = {"mask", "bold", "italic",
        "underline", "strike", "size-f32", "foreground", "highlight",
        "baseline", "font"};
    const char *font;

    if (!tiku_rio_element(r, n, "chars", attrs, 10)) return;
    c->present = (unsigned)tiku_rio_number(r, n, "mask", 511);
    c->bold = (unsigned char)tiku_rio_number(r, n, "bold", 1);
    c->italic = (unsigned char)tiku_rio_number(r, n, "italic", 1);
    c->underline = (unsigned char)tiku_rio_number(r, n, "underline", 1);
    c->strike = (unsigned char)tiku_rio_number(r, n, "strike", 1);
    c->size = tiku_rio_read_float(r, n, "size-f32");
    c->foreground = (uint32_t)tiku_rio_number(r, n, "foreground", UINT32_MAX);
    c->highlight = (uint32_t)tiku_rio_number(r, n, "highlight", UINT32_MAX);
    c->baseline = (int)tiku_rio_number(r, n, "baseline", 2) - 1;
    font = tiku_xml_attr(n, "font");
    tiku_rio_string(r, font, c->font, sizeof c->font);
    if (tiku_rio_next(r, n->child)) r->status = TIKU_RICHIO_UNSUPPORTED;
    if ((!(c->present & TIKU_RICH_BOLD) && c->bold) ||
        (!(c->present & TIKU_RICH_ITALIC) && c->italic) ||
        (!(c->present & TIKU_RICH_UNDERLINE) && c->underline) ||
        (!(c->present & TIKU_RICH_STRIKE) && c->strike) ||
        (!(c->present & TIKU_RICH_SIZE) && c->size) ||
        (!(c->present & TIKU_RICH_FONT) && c->font[0]) ||
        (!(c->present & TIKU_RICH_FOREGROUND) && c->foreground) ||
        (!(c->present & TIKU_RICH_HIGHLIGHT) && c->highlight) ||
        (!(c->present & TIKU_RICH_BASELINE) && c->baseline))
        r->status = TIKU_RICHIO_DAMAGED;
}

void
tiku_rio_read_para(tiku_rio_reader_t *r, const tiku_xml_node_t *n,
    tiku_rich_para_t *p)
{
    const char *attrs[] = {"mask", "align", "left-f32",
        "right-f32", "first-f32", "before-f32", "after-f32",
        "keep-next", "keep-lines", NULL, NULL};
    size_t count = 9;
    unsigned mask = 15;

    if (r->break_feature) { attrs[count++] = "break-before"; mask |= 16; }
    if (r->outline_feature) { attrs[count++] = "outline"; mask |= 32; }
    if (!tiku_rio_element(r, n, "para", attrs, count)) return;
    p->present = (unsigned)tiku_rio_number(r, n, "mask", mask);
    if (p->present & ~mask) r->status = TIKU_RICHIO_DAMAGED;
    p->align = (tiku_rich_align_t)tiku_rio_number(r, n, "align", 3);
    p->left = tiku_rio_read_float(r, n, "left-f32");
    p->right = tiku_rio_read_float(r, n, "right-f32");
    p->first = tiku_rio_read_float(r, n, "first-f32");
    p->before = tiku_rio_read_float(r, n, "before-f32");
    p->after = tiku_rio_read_float(r, n, "after-f32");
    p->keep_next = (unsigned char)tiku_rio_number(r, n, "keep-next", 1);
    p->keep_lines = (unsigned char)tiku_rio_number(r, n, "keep-lines", 1);
    if (r->break_feature)
        p->break_before = (unsigned char)tiku_rio_number(r, n,
                                                        "break-before", 1);
    if (r->outline_feature)
        p->outline = (unsigned char)tiku_rio_number(r, n, "outline", 6);
    if (tiku_rio_next(r, n->child)) r->status = TIKU_RICHIO_UNSUPPORTED;
    if ((!(p->present & TIKU_RICH_ALIGNMENT) && p->align) ||
        (!(p->present & TIKU_RICH_INDENT) &&
         (p->left || p->right || p->first)) ||
        (!(p->present & TIKU_RICH_SPACING) && (p->before || p->after)) ||
        (!(p->present & TIKU_RICH_KEEP) && (p->keep_next || p->keep_lines)) ||
        (!(p->present & TIKU_RICH_PAGE_BREAK) && p->break_before) ||
        (!(p->present & TIKU_RICH_OUTLINE) && p->outline))
        r->status = TIKU_RICHIO_DAMAGED;
}

void
tiku_rio_write_page(tiku_rio_writer_t *w, const tiku_rich_page_setup_t *p)
{
    tiku_rio_literal(w, "<page");
    tiku_rio_write_float(w, "width-f32", p->width);
    tiku_rio_write_float(w, "height-f32", p->height);
    tiku_rio_write_float(w, "top-f32", p->top);
    tiku_rio_write_float(w, "right-f32", p->right);
    tiku_rio_write_float(w, "bottom-f32", p->bottom);
    tiku_rio_write_float(w, "left-f32", p->left);
    tiku_rio_literal(w, "/>\n");
}

void
tiku_rio_read_page(tiku_rio_reader_t *r, const tiku_xml_node_t *n)
{
    static const char *const attrs[] = {"width-f32", "height-f32",
        "top-f32", "right-f32", "bottom-f32", "left-f32"};
    tiku_rich_page_setup_t p;

    if (!tiku_rio_element(r, n, "page", attrs, 6)) return;
    p.width = tiku_rio_read_float(r, n, "width-f32");
    p.height = tiku_rio_read_float(r, n, "height-f32");
    p.top = tiku_rio_read_float(r, n, "top-f32");
    p.right = tiku_rio_read_float(r, n, "right-f32");
    p.bottom = tiku_rio_read_float(r, n, "bottom-f32");
    p.left = tiku_rio_read_float(r, n, "left-f32");
    if (tiku_rio_next(r, n->child) || !p.width || !p.height ||
        tiku_rich_page_setup(&p, &r->source.page) != TIKU_RICH_OK)
        r->status = TIKU_RICHIO_DAMAGED;
}
