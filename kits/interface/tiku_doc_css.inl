/*
 * TikuOS Interface Kit.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_doc_css.inl - bounded text-style declarations and compound selectors.
 * SPDX-License-Identifier: Apache-2.0
 */

#define DOC_CSS_COLOR 1u
#define DOC_CSS_DECORATION 2u
#define DOC_CSS_SIZE 4u
#define DOC_CSS_FAMILY 8u
#define DOC_CSS_WEIGHT 16u
#define DOC_CSS_ITALIC 32u
#define DOC_CSS_ALIGN 64u
#define DOC_CSS_HEIGHT 128u
#define DOC_CSS_WHITE 256u
#define DOC_CSS_BACKGROUND (1u<<9)
#define DOC_CSS_MARGIN (1u<<10)
#define DOC_CSS_PADDING (1u<<14)
#define DOC_CSS_WIDTH (1u<<18)
#define DOC_CSS_MAX_WIDTH (1u<<19)
#define DOC_CSS_BORDER_WIDTH (1u<<20)
#define DOC_CSS_BORDER_COLOR (1u<<21)
#define DOC_CSS_BORDER_STYLE (1u<<22)
#define DOC_CSS_DISPLAY (1u<<23)
#define DOC_CSS_IMAGE_HEIGHT (1u<<24)
#define DOC_CSS_BOX_MASK (0xffffu<<9)
#define DOC_CSS_PROPERTIES 25
#define DOC_CSS_FLAGS (TIKU_DOC_RUN_COLOR | TIKU_DOC_RUN_UNDERLINE | \
                       TIKU_DOC_RUN_STRIKE | TIKU_DOC_RUN_LINK_PLAIN | TIKU_DOC_RUN_PRE | \
                       TIKU_DOC_RUN_BACKGROUND)
#define DOC_CSS_MATCH_MAX 1048576u
_Static_assert(TIKU_DOC_CSS_RULES_MAX >= 0 && TIKU_DOC_CSS_RULES_MAX <= 256,
               "CSS rules capacity is between zero and 256");
_Static_assert(TIKU_DOC_CSS_BYTES_MAX > 0 && TIKU_DOC_CSS_BYTES_MAX <= 1048576,
               "CSS source budget is between one byte and one MiB");

typedef struct {
    uint32_t color;
    uint32_t mask, important;
    unsigned char color_mode, decoration;
    int32_t size;
    unsigned char size_unit, family, weight, italic;
    int32_t height;
    unsigned char height_unit, align, whitespace;
    tiku_doc_box_t box;
    unsigned char display;
} doc_css_decl_t;

typedef struct {
    const char *selector;
    size_t length;
    int specificity;
    doc_css_decl_t declaration;
} doc_css_rule_t;

typedef struct {
    doc_css_rule_t rules[TIKU_DOC_CSS_RULES_MAX ? TIKU_DOC_CSS_RULES_MAX : 1];
    int count;
    size_t bytes;
    unsigned checks, notices;
} doc_css_t;

/** @brief Skip CSS whitespace and complete or unterminated comments. */
static const char *css_blank(const char *p, const char *end)
{
    while (p < end) {
        if (html_space(*p)) { p++; continue; }
        if (end-p < 2 || p[0] != '/' || p[1] != '*') break;
        p += 2;
        while (end-p >= 2 && !(p[0] == '*' && p[1] == '/')) p++;
        p = end-p >= 2 ? p+2 : end;
    }
    return p;
}

/** @brief Trim token whitespace from a bounded CSS value. */
static void css_trim(const char **p, const char **end)
{
    while (*p < *end && html_space(**p)) (*p)++;
    while (*end > *p && html_space((*end)[-1])) (*end)--;
}

/** @brief Hexadecimal digit value, or -1. */
static int css_hex(char c)
{
    if (c >= '0' && c <= '9') return c-'0';
    if (c >= 'a' && c <= 'f') return c-'a'+10;
    if (c >= 'A' && c <= 'F') return c-'A'+10;
    return -1;
}

/** @brief Parse a six/three-digit hex or supported named RGB colour. */
static int css_color(const char *p, size_t length, uint32_t *rgb)
{
    static const struct { const char *name; uint32_t rgb; } names[] = {
        {"black",0x000000}, {"white",0xffffff}, {"red",0xff0000},
        {"green",0x008000}, {"blue",0x0000ff}, {"yellow",0xffff00},
        {"gray",0x808080}, {"grey",0x808080}, {"orange",0xffa500},
        {"purple",0x800080}, {"lime",0x00ff00}, {"aqua",0x00ffff},
        {"teal",0x008080}, {"navy",0x000080}, {"maroon",0x800000},
        {"silver",0xc0c0c0}, {"fuchsia",0xff00ff}, {"olive",0x808000}
    };
    if (length && p[0] == '#' && (length == 4 || length == 7)) {
        uint32_t value = 0;
        for (size_t i = 1; i < length; i++) {
            int digit = css_hex(p[i]);
            if (digit < 0) return 0;
            value = length == 4 ? (value << 8) | (unsigned)digit*17u :
                                 (value << 4) | (unsigned)digit;
        }
        *rgb = value; return 1;
    }
    for (size_t i = 0; i < sizeof names/sizeof names[0]; i++)
        if (html_name(p, length, names[i].name)) {
            *rgb = names[i].rgb; return 1;
        }
    return 0;
}

/** @brief Parse underline, line-through and none into run flags. */
static int css_decoration(const char *p, const char *end, unsigned char *flags)
{
    unsigned char value = 0;
    int count = 0, none = 0;
    while (p < end) {
        p = css_blank(p, end);
        const char *start = p;
        while (p < end && !html_space(*p)) p++;
        size_t n = (size_t)(p-start);
        if (!n) break;
        if (html_name(start,n,"underline")) value |= TIKU_DOC_RUN_UNDERLINE;
        else if (html_name(start,n,"line-through")) value |= TIKU_DOC_RUN_STRIKE;
        else if (html_name(start,n,"none")) none = 1;
        else return 0;
        count++;
    }
    if (!count || (none && count != 1)) return 0;
    *flags = value; return 1;
}

/** @brief Copy a declaration value with comments replaced by spaces. */
static int css_value(const char *p, const char *end, char *out, size_t cap)
{
    size_t at = 0;
    while (p < end) {
        if (at + 1 >= cap) return 0;
        if (end-p >= 2 && p[0] == '/' && p[1] == '*') {
            out[at++] = ' '; p = css_blank(p,end);
        } else out[at++] = *p++;
    }
    out[at] = 0; return 1;
}

#include "tiku_doc_css_fonts.inl"
#include "tiku_doc_css_flow.inl"
#include "tiku_doc_css_boxes.inl"

/** @brief Copy one declaration value and its importance bit. */
static void css_copy_property(doc_css_decl_t *to, const doc_css_decl_t *from,
                               unsigned mask)
{
    to->mask |= mask;
    to->important = (to->important & ~mask) | (from->important & mask);
    if (mask == DOC_CSS_COLOR) { to->color = from->color; to->color_mode = from->color_mode; }
    else if (mask == DOC_CSS_DECORATION) to->decoration = from->decoration;
    else if (mask == DOC_CSS_SIZE) { to->size = from->size; to->size_unit = from->size_unit; }
    else if (mask == DOC_CSS_FAMILY) to->family = from->family;
    else if (mask == DOC_CSS_WEIGHT) to->weight = from->weight;
    else if (mask == DOC_CSS_ITALIC) to->italic = from->italic;
    else if (mask == DOC_CSS_ALIGN) to->align = from->align;
    else if (mask == DOC_CSS_WHITE) to->whitespace = from->whitespace;
    else if (mask == DOC_CSS_HEIGHT) { to->height = from->height; to->height_unit = from->height_unit; }
    else if (mask == DOC_CSS_BACKGROUND) { to->box.background = from->box.background; to->box.has_background = from->box.has_background; }
    else if (mask == DOC_CSS_WIDTH) to->box.width = from->box.width;
    else if (mask == DOC_CSS_MAX_WIDTH) to->box.max_width = from->box.max_width;
    else if (mask == DOC_CSS_BORDER_WIDTH) to->box.border = from->box.border;
    else if (mask == DOC_CSS_BORDER_COLOR) to->box.border_color = from->box.border_color;
    else if (mask == DOC_CSS_BORDER_STYLE) to->box.border_style = from->box.border_style;
    else if (mask == DOC_CSS_DISPLAY) to->display = from->display;
    else if (mask == DOC_CSS_IMAGE_HEIGHT) to->box.height = from->box.height;
    else for (int i = 0; i < 4; i++) {
        if (mask == (DOC_CSS_MARGIN<<i)) to->box.margin[i] = from->box.margin[i];
        if (mask == (DOC_CSS_PADDING<<i)) to->box.padding[i] = from->box.padding[i];
    }
}

/** @brief Resolve one declaration into its property and importance slot. */
static void css_property(doc_css_t *css, doc_css_decl_t *d, const char *key,
                          size_t n, const char *value, const char *end)
{
    char buffer[160];
    if (!css_value(value,end,buffer,sizeof buffer)) {
        css->notices |= TIKU_DOC_CSS_UNSUPPORTED; return;
    }
    value = buffer; end = buffer+strlen(buffer); css_trim(&value,&end);
    int important = 0;
    for (const char *p = end; p > value; p--) if (p[-1] == '!') {
        const char *word = p; const char *last = end; css_trim(&word,&last);
        if (html_name(word,(size_t)(last-word),"important")) {
            important = 1; end = p-1; css_trim(&value,&end);
        }
        break;
    }
    doc_css_decl_t parsed = {0};
    doc_css_decl_t *target = d; d = &parsed;
    unsigned mask;
    if (html_name(key,n,"color")) {
        mask = DOC_CSS_COLOR;
        if ((d->important & mask) && !important) return;
        uint32_t rgb = 0; int mode = 0;
        if (html_name(value,(size_t)(end-value),"inherit") ||
            html_name(value,(size_t)(end-value),"unset")) mode = 1;
        else if (html_name(value,(size_t)(end-value),"initial")) mode = 2;
        else if (!css_color(value,(size_t)(end-value),&rgb)) {
            css->notices |= TIKU_DOC_CSS_UNSUPPORTED; return;
        }
        d->color = rgb; d->color_mode = (unsigned char)mode;
    } else if (html_name(key,n,"text-decoration") ||
               html_name(key,n,"text-decoration-line")) {
        mask = DOC_CSS_DECORATION;
        if ((d->important & mask) && !important) return;
        unsigned char decoration;
        if (!css_decoration(value,end,&decoration)) {
            css->notices |= TIKU_DOC_CSS_UNSUPPORTED; return;
        }
        d->decoration = decoration;
    } else {
        mask = css_flow_property(key,n);
        if (mask) {
            if ((d->important & mask) && !important) return;
            if (!css_flow_value(d,mask,value,end)) {
                css->notices |= TIKU_DOC_CSS_UNSUPPORTED; return;
            }
        } else if ((mask = css_box_property(d,key,n,value,end)) != 0) {
            if (mask == UINT32_MAX) { css->notices |= TIKU_DOC_CSS_UNSUPPORTED; return; }
        } else {
            mask = css_font_property(key,n);
            if (!mask) { css->notices |= TIKU_DOC_CSS_UNSUPPORTED; return; }
            if ((d->important & mask) && !important) return;
            if (!css_font_value(d,mask,value,end)) {
                css->notices |= TIKU_DOC_CSS_UNSUPPORTED; return;
            }
        }
    }
    d->mask = mask; d->important = important ? mask : 0;
    for (int i = 0; i < DOC_CSS_PROPERTIES; i++) {
        unsigned bit = 1u<<i;
        if (!(mask & bit) || ((target->important & bit) && !important)) continue;
        css_copy_property(target,d,bit);
    }
}

/** @brief Parse bounded declarations, retaining valid properties. */
static doc_css_decl_t css_declarations(doc_css_t *css, const char *p,
                                        const char *end)
{
    doc_css_decl_t d = {0};
    if ((size_t)(end-p) > TIKU_DOC_CSS_BYTES_MAX-css->bytes) {
        css->notices |= TIKU_DOC_CSS_LIMIT; return d;
    }
    css->bytes += (size_t)(end-p);
    while ((p = css_blank(p,end)) < end) {
        if (*p == ';') { p++; continue; }
        const char *key = p;
        while (p < end && *p != ':' && *p != ';') p++;
        const char *key_end = p; css_trim(&key,&key_end);
        if (p == end || *p != ':') {
            css->notices |= TIKU_DOC_CSS_SYNTAX;
            if (p < end) p++;
            continue;
        }
        const char *value = ++p; char quote = 0;
        while (p < end) {
            if (quote) { if (*p == quote) quote = 0; p++; }
            else if (*p == '\'' || *p == '"') quote = *p++;
            else if (end-p >= 2 && p[0] == '/' && p[1] == '*')
                p = css_blank(p,end);
            else if (*p == ';') break;
            else p++;
        }
        css_property(css,&d,key,(size_t)(key_end-key),value,p);
        if (p < end) p++;
    }
    return d;
}

#include "tiku_doc_css_inline.inl"

/** @brief Whether a byte belongs to a supported ASCII selector name. */
static int css_ident(char c)
{
    return html_letter(c) || (c >= '0' && c <= '9') || c == '_' || c == '-';
}

/** @brief Match an ID or a complete token in a class attribute. */
static int css_attribute_match(const char *attrs, const char *name,
                                const char *value, size_t length, int classes)
{
    const char *p; size_t n;
    if (!html_attribute(attrs,name,&p,&n)) return 0;
    const char *end = p+n;
    if (!classes) return n == length && !memcmp(p,value,n);
    while (p < end) {
        while (p < end && html_space(*p)) p++;
        const char *start = p;
        while (p < end && !html_space(*p)) p++;
        if ((size_t)(p-start) == length && !memcmp(start,value,length)) return 1;
    }
    return 0;
}

/** @brief Selector specificity; -1 is unsupported, -2 is unmatched. */
static int css_selector(const char *p, size_t length, const char *tag,
                         size_t tag_length, const char *attrs)
{
    const char *end = p+length; css_trim(&p,&end);
    if (end-p > 256 || p == end) return -1;
    int score = 0, matched = 1;
    if (*p == '*') p++;
    else if (html_letter(*p)) {
        const char *start = p++;
        while (p < end && css_ident(*p)) p++;
        if (tag && !html_same(start,(size_t)(p-start),tag,tag_length)) matched = 0;
        score++;
    }
    while (p < end) {
        char kind = *p++;
        if (kind != '.' && kind != '#') return -1;
        const char *start = p;
        while (p < end && css_ident(*p)) p++;
        if (p == start) return -1;
        if (attrs && !css_attribute_match(attrs,kind == '#' ? "id" : "class",
                                          start,(size_t)(p-start),kind == '.')) matched = 0;
        score += kind == '#' ? 65536 : 256;
    }
    return matched ? score : -2;
}

/** @brief Skip a CSS block or statement, including nested braces. */
static const char *css_block_end(const char *p, const char *end, int depth)
{
    char quote = 0;
    while (p < end) {
        if (quote) {
            if (*p == '\\' && end-p >= 2) p += 2;
            else { if (*p == quote) quote = 0; p++; }
        } else if (end-p >= 2 && p[0] == '/' && p[1] == '*') p = css_blank(p,end);
        else if (*p == '\'' || *p == '"') quote = *p++;
        else if (*p == '{') { depth++; p++; }
        else if (*p == '}') { p++; if (--depth <= 0) return p; }
        else if (*p == ';' && !depth) return p+1;
        else p++;
    }
    return end;
}

/** @brief Collect supported rules while bounding source bytes and rule count. */
static void css_stylesheet(doc_css_t *css, const char *p, const char *end)
{
    if ((size_t)(end-p) > TIKU_DOC_CSS_BYTES_MAX-css->bytes) {
        css->notices |= TIKU_DOC_CSS_LIMIT; return;
    }
    size_t after_bytes = css->bytes + (size_t)(end-p);
    while ((p = css_blank(p,end)) < end) {
        if (*p == '@') {
            css->notices |= TIKU_DOC_CSS_UNSUPPORTED;
            p = css_block_end(p,end,0); continue;
        }
        const char *selector = p;
        while (p < end && *p != '{') p++;
        if (p == end) { css->notices |= TIKU_DOC_CSS_SYNTAX; break; }
        const char *declarations = p+1;
        const char *after = css_block_end(declarations,end,1);
        if (after == declarations || after[-1] != '}') {
            css->notices |= TIKU_DOC_CSS_SYNTAX; break;
        }
        doc_css_decl_t d = css_declarations(css,declarations,after-1);
        while (selector < p) {
            const char *last = selector;
            while (last < p && *last != ',') last++;
            int score = css_selector(selector,(size_t)(last-selector),NULL,0,NULL);
            if (score < 0) css->notices |= TIKU_DOC_CSS_UNSUPPORTED;
            else if (css->count == TIKU_DOC_CSS_RULES_MAX)
                css->notices |= TIKU_DOC_CSS_LIMIT;
            else css->rules[css->count++] = (doc_css_rule_t){selector,
                    (size_t)(last-selector),score,d};
            selector = last < p ? last+1 : p;
        }
        p = after;
    }
    css->bytes = after_bytes;
}

/** @brief Locate the exact closing name of raw HTML content. */
static const char *css_raw_end(const char *p, const char *name, size_t length)
{
    while (*p) {
        if (p[0] == '<' && p[1] == '/') {
            const char *end = p+2;
            while (*end && !html_space(*end) && *end != '>' && *end != '/' &&
                   *end != '<') end++;
            if (html_same(p+2,(size_t)(end-p-2),name,length)) return p;
        }
        p++;
    }
    return p;
}

/** @brief Collect embedded stylesheets before the document text is parsed. */
static void css_collect(doc_css_t *css, const char *p)
{
    while (*p) {
        if (p[0] != '<') { p++; continue; }
        if (p[1] == '!' && p[2] == '-' && p[3] == '-') {
            const char *end = strstr(p+4,"-->");
            p = end ? end+3 : p+strlen(p); continue;
        }
        const char *name = p+1;
        const char *body = name;
        while (*body && !html_space(*body) && *body != '/' && *body != '>' &&
               *body != '<') body++;
        size_t length = (size_t)(body-name);
        const char *after = html_tag_end(body);
        if (after == body || after[-1] != '>') { p = after; continue; }
        if (html_name(name,length,"style") || html_name(name,length,"script") ||
            html_name(name,length,"title")) {
            const char *end = css_raw_end(after,name,length);
            if (html_name(name,length,"style")) css_stylesheet(css,after,end);
            p = *end ? html_tag_end(end+2+length) : end;
        } else p = after;
    }
}

/** @brief Resolve cascade winners independently for supported properties. */
static doc_css_decl_t css_resolve(doc_css_t *css, const char *tag, size_t n,
                                   const char *attrs)
{
    doc_css_decl_t result = {0};
    int winner[DOC_CSS_PROPERTIES];
    for (int i = 0; i < DOC_CSS_PROPERTIES; i++) winner[i] = -1;
    for (int i = 0; i <= css->count; i++) {
        doc_css_decl_t d; int specificity;
        if (i == css->count) {
            const char *value; size_t length;
            if (!html_attribute(attrs,"style",&value,&length)) break;
            d = css_inline_declarations(css,value,length); specificity = 1<<24;
        } else {
            if (css->checks >= DOC_CSS_MATCH_MAX) {
                css->notices |= TIKU_DOC_CSS_LIMIT; i = css->count-1; continue;
            }
            css->checks++;
            doc_css_rule_t *rule = &css->rules[i];
            if (css_selector(rule->selector,rule->length,tag,n,attrs) < 0) continue;
            d = rule->declaration; specificity = rule->specificity;
        }
        for (int property = 0; property < DOC_CSS_PROPERTIES; property++) {
            unsigned mask = 1u << property;
            int score = specificity + ((d.important & mask) ? 1<<28 : 0);
            if (!(d.mask & mask) || score < winner[property]) continue;
            winner[property] = score;
            css_copy_property(&result,&d,mask);
        }
    }
    return result;
}
