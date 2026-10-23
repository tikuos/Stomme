/*
 * TikuDesktop Translation Kit.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richmarkup_css.c - bounded inline CSS conversion to document properties.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richmarkup_private.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/** @brief Trim ASCII whitespace from a writable declaration fragment. */
static char *
trim(char *p)
{
    size_t length;

    while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') p++;
    length = strlen(p);
    while (length && (p[length-1] == ' ' || p[length-1] == '\n' ||
           p[length-1] == '\r' || p[length-1] == '\t')) p[--length] = 0;
    return p;
}

int
tiku_rm_length(const char *text, float *value)
{
    double number = 0, fraction = 0.1;
    int sign = 1, digits = 0;

    if (*text == '-' || *text == '+') { if (*text == '-') sign = -1; text++; }
    while (*text >= '0' && *text <= '9') {
        number = number * 10 + *text++ - '0';
        if (number > 1000000) return 0;
        digits++;
    }
    if (*text == '.') {
        text++;
        while (*text >= '0' && *text <= '9') {
            number += (*text++ - '0') * fraction;
            fraction *= 0.1;
            digits++;
        }
    }
    if (!digits) return 0;
    if (!strcasecmp(text, "px")) number *= .75;
    else if (*text && strcasecmp(text, "pt")) return 0;
    *value = (float)(number * sign);
    return isfinite(*value);
}

int
tiku_rm_color(const char *text, uint32_t *value)
{
    static const struct { const char *name; uint32_t value; } colors[] = {
        {"black", 0xff000000}, {"white", 0xffffffff}, {"red", 0xffff0000},
        {"green", 0xff008000}, {"blue", 0xff0000ff}, {"gray", 0xff808080},
        {"grey", 0xff808080}, {"yellow", 0xffffff00}, {"transparent", 0}
    };
    size_t length;
    uint32_t result = 0;

    for (size_t i = 0; i < sizeof colors / sizeof *colors; i++)
        if (!strcasecmp(text, colors[i].name)) { *value = colors[i].value; return 1; }
    if (*text++ != '#') return 0;
    length = strlen(text);
    if (length != 3 && length != 4 && length != 6 && length != 8) return 0;
    for (size_t i = 0; i < length; i++) {
        unsigned char c = (unsigned char)text[i];
        unsigned digit = c >= '0' && c <= '9' ? c-'0' :
            c >= 'a' && c <= 'f' ? c-'a'+10 : c >= 'A' && c <= 'F' ? c-'A'+10 : 16;

        if (digit == 16) return 0;
        result = length <= 4 ? (result << 8) | (digit * 17) :
                              (result << 4) | digit;
    }
    *value = length == 3 || length == 6 ? 0xff000000 | result :
             (result >> 8) | (result << 24);
    return 1;
}

/** @brief Decode a CSS quoted family, including ASCII hexadecimal escapes. */
static int
family(const char *value, char out[64])
{
    size_t length = strlen(value), used = 0, at = 0;
    char quote = *value == '"' || *value == '\'' ? value[at++] : 0;

    if (quote && (length < 2 || value[length-1] != quote)) return 0;
    if (quote) length--;
    while (at < length) {
        unsigned c = (unsigned char)value[at++];

        if (c == '\\') {
            c = 0;
            size_t digits = 0;

            while (at < length && digits < 6) {
                char b = value[at];
                unsigned d = b >= '0' && b <= '9' ? (unsigned)(b-'0') :
                    b >= 'a' && b <= 'f' ? (unsigned)(b-'a'+10) :
                    b >= 'A' && b <= 'F' ? (unsigned)(b-'A'+10) : 16;

                if (d == 16) break;
                c = c * 16 + d;
                at++; digits++;
            }
            if (!digits || !c || c > 127) return 0;
            if (at < length && value[at] == ' ') at++;
        }
        if (used == 63 || (!quote && c == ',')) return 0;
        out[used++] = (char)c;
    }
    out[used] = 0;
    return used != 0;
}

/** @brief Convert one declaration; return zero for an unsupported value. */
static int
declaration(const char *name, const char *value, tiku_rm_style_t *s)
{
    tiku_rich_chars_t *c = &s->chars;
    tiku_rich_para_t *p = &s->paragraph;
    float length;
    uint32_t color;

    if (s->image && (!strcmp(name, "width") || !strcmp(name, "height"))) {
        if (!tiku_rm_length(value, &length) || length < 1 || length > 14400)
            return 0;
        if (*name == 'w') s->image_width = length;
        else s->image_height = length;
    } else if (!strcmp(name, "font-weight")) {
        if (strcasecmp(value, "bold") && strcasecmp(value, "normal") &&
            strcmp(value, "700") && strcmp(value, "400")) return 0;
        c->present |= TIKU_RICH_BOLD;
        c->bold = !strcasecmp(value, "bold") || !strcmp(value, "700");
    } else if (!strcmp(name, "font-style")) {
        if (strcasecmp(value, "italic") && strcasecmp(value, "normal") &&
            strcasecmp(value, "oblique")) return 0;
        c->present |= TIKU_RICH_ITALIC;
        c->italic = strcasecmp(value, "normal") != 0;
    } else if (!strcmp(name, "text-decoration")) {
        c->present |= TIKU_RICH_UNDERLINE | TIKU_RICH_STRIKE;
        c->underline = strstr(value, "underline") != NULL;
        c->strike = strstr(value, "line-through") != NULL;
        return c->underline || c->strike || !strcmp(value, "none");
    } else if (!strcmp(name, "font-size")) {
        if (!tiku_rm_length(value, &length) || length < 1 || length > 4096) return 0;
        c->present |= TIKU_RICH_SIZE; c->size = length;
    } else if (!strcmp(name, "font-family")) {
        char namebuf[64];

        if (!family(value, namebuf)) return 0;
        c->present |= TIKU_RICH_FONT;
        memcpy(c->font, namebuf, strlen(namebuf) + 1);
    } else if (!strcmp(name, "color") || !strcmp(name, "background-color")) {
        if (!tiku_rm_color(value, &color)) return 0;
        if (*name == 'c') { c->present |= TIKU_RICH_FOREGROUND; c->foreground = color; }
        else { c->present |= TIKU_RICH_HIGHLIGHT; c->highlight = color; }
    } else if (!strcmp(name, "vertical-align")) {
        if (strcmp(value, "super") && strcmp(value, "sub") &&
            strcmp(value, "baseline")) return 0;
        c->present |= TIKU_RICH_BASELINE;
        c->baseline = !strcmp(value, "super") ? 1 : !strcmp(value, "sub") ? -1 : 0;
    } else if (!strcmp(name, "text-align")) {
        static const char *const align[] = {"left", "center", "right", "justify"};
        unsigned i;

        for (i = 0; i < 4 && strcmp(value, align[i]); i++) {}
        if (i == 4) return 0;
        p->present |= TIKU_RICH_ALIGNMENT; p->align = (tiku_rich_align_t)i;
    } else if (!strcmp(name, "white-space")) {
        if (strcmp(value, "pre") && strcmp(value, "pre-wrap") &&
            strcmp(value, "normal")) return 0;
        s->pre = strcmp(value, "normal") != 0;
    } else if (!strncmp(name, "margin-", 7) || !strcmp(name, "text-indent")) {
        if (!tiku_rm_length(value, &length) || length < -14400 || length > 14400)
            return 0;
        if (!strcmp(name, "margin-left")) { p->present |= TIKU_RICH_INDENT; p->left = length; }
        else if (!strcmp(name, "margin-right")) { p->present |= TIKU_RICH_INDENT; p->right = length; }
        else if (!strcmp(name, "text-indent")) { p->present |= TIKU_RICH_INDENT; p->first = length; }
        else if (length < 0) return 0;
        else if (!strcmp(name, "margin-top")) { p->present |= TIKU_RICH_SPACING; p->before = length; }
        else if (!strcmp(name, "margin-bottom")) { p->present |= TIKU_RICH_SPACING; p->after = length; }
        else return 0;
    } else if (!strcmp(name, "break-before") && !strcmp(value, "page")) {
        p->present |= TIKU_RICH_PAGE_BREAK; p->break_before = 1;
    } else if (!strcmp(name, "break-after") && !strcmp(value, "avoid")) {
        p->present |= TIKU_RICH_KEEP; p->keep_next = 1;
    } else if (!strcmp(name, "break-inside") && !strcmp(value, "avoid")) {
        p->present |= TIKU_RICH_KEEP; p->keep_lines = 1;
    } else return 0;
    return 1;
}

void
tiku_rm_css(tiku_rimp_t *r, const char *text, tiku_rm_style_t *style)
{
    char buffer[4097];
    size_t length = strlen(text), start = 0;
    char quote = 0;

    if (length >= sizeof buffer) { r->status = TIKU_RICHIO_LIMIT; return; }
    memcpy(buffer, text, length + 1);
    for (size_t i = 0; i <= length; i++) {
        if (i < length && buffer[i] == '\\' && i + 1 < length) { i++; continue; }
        if (i < length && (buffer[i] == '\'' || buffer[i] == '"')) {
            if (quote == buffer[i]) quote = 0;
            else if (!quote) quote = buffer[i];
        }
        if (i != length && (buffer[i] != ';' || quote)) continue;
        buffer[i] = 0;
        char *name = trim(buffer + start), *colon = strchr(name, ':');

        if (colon) {
            *colon++ = 0;
            name = trim(name);
            for (char *p = name; *p; p++) if (*p >= 'A' && *p <= 'Z') *p += 'a'-'A';
            tiku_rm_style_t candidate = *style;

            if (declaration(name, trim(colon), &candidate)) *style = candidate;
            else r->notices |= TIKU_RICHMARKUP_STYLE;
        } else if (*name) r->notices |= TIKU_RICHMARKUP_STYLE;
        start = i + 1;
    }
}
