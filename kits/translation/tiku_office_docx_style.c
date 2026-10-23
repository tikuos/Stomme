/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_office_docx_style.c - how a .docx says text looks.
 *
 * Properties are read as WordprocessingML states them (ECMA-376 part 1,
 * 17.3 and 17.7) and laid over each other in Word's order: the
 * document's defaults, the table's style, the paragraph's style and what
 * it is based on, a list level's indents, the run's style, then direct
 * formatting.  Lists are counted as Word counts them; a bullet drawn in
 * Symbol or Wingdings is the Unicode character it looks like.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_office_docx.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* -------------------------------------------------------------- values  */

float
dx_twips(const char *v, float dflt)
{
    char *end;
    double n;

    if (v == NULL || *v == '\0') {
        return dflt;
    }
    n = strtod(v, &end);
    if (end == v) {
        return dflt;
    }
    if (strcmp(end, "pt") == 0) {
        return (float)n;
    }
    if (strcmp(end, "in") == 0) {
        return (float)(n * 72);
    }
    if (strcmp(end, "cm") == 0) {
        return (float)(n * 72 / 2.54);
    }
    if (strcmp(end, "mm") == 0) {
        return (float)(n * 72 / 25.4);
    }
    if (strcmp(end, "pc") == 0 || strcmp(end, "pi") == 0) {
        return (float)(n * 12);
    }
    return (float)(n / 20);
}

int
dx_on(const tiku_xml_node_t *e)
{
    const char *v = tiku_xml_attr(e, "w:val");

    if (e == NULL) {
        return 0;
    }
    return v == NULL || !(strcmp(v, "0") == 0 || strcmp(v, "false") == 0 ||
                          strcmp(v, "off") == 0 || strcmp(v, "none") == 0);
}

uint32_t
dx_color(const char *v)
{
    char *end;
    unsigned long c;

    if (v == NULL || strlen(v) != 6) {
        return 0;
    }
    c = strtoul(v, &end, 16);
    return *end == '\0' ? 0xff000000u | (uint32_t)c : 0;
}

/** @brief Whether @p hay holds @p needle, ASCII case aside. */
static int
has(const char *hay, const char *needle)
{
    size_t n = strlen(needle);

    for (; hay != NULL && *hay; hay++) {
        if (strncasecmp(hay, needle, n) == 0) {
            return 1;
        }
    }
    return 0;
}

/** @brief A highlight's name as ARGB; 0 for "none". */
static uint32_t
highlight(const char *v)
{
    static const struct { const char *name; uint32_t argb; } h[] = {
        {"yellow", 0xffffff00}, {"green", 0xff00ff00}, {"cyan", 0xff00ffff},
        {"magenta", 0xffff00ff}, {"blue", 0xff0000ff}, {"red", 0xffff0000},
        {"darkBlue", 0xff000080}, {"darkCyan", 0xff008080},
        {"darkGreen", 0xff008000}, {"darkMagenta", 0xff800080},
        {"darkRed", 0xff800000}, {"darkYellow", 0xff808000},
        {"darkGray", 0xff808080}, {"lightGray", 0xffc0c0c0},
        {"black", 0xff000000}, {"white", 0xffffffff}
    };
    size_t i;

    for (i = 0; v != NULL && i < sizeof h / sizeof h[0]; i++) {
        if (strcmp(v, h[i].name) == 0) {
            return h[i].argb;
        }
    }
    return 0;
}

/** @brief A number attribute of @p e, @p dflt when absent. */
static long
num(const tiku_xml_node_t *e, const char *name, long dflt)
{
    const char *v = tiku_xml_attr(e, name);

    return v != NULL && *v ? strtol(v, NULL, 10) : dflt;
}

/** @brief A list's first number, from attribute @p name of @p e: from 0
 *         to 32767, Word's own ceiling, so counting on cannot overflow. */
static int
start_of(const tiku_xml_node_t *e, const char *name)
{
    long v = num(e, name, 1);

    return v < 0 ? 0 : v > 32767 ? 32767 : (int)v;
}

/** @brief A shading's fill: its colour, or none for "auto". */
static uint32_t
shading(const tiku_xml_node_t *shd)
{
    return shd != NULL ? dx_color(tiku_xml_attr(shd, "w:fill")) : 0;
}

void
dx_border(const tiku_xml_node_t *e, of_border_t *b)
{
    const char *v = tiku_xml_attr(e, "w:val");

    memset(b, 0, sizeof *b);
    if (e == NULL || v == NULL || strcmp(v, "nil") == 0 ||
        strcmp(v, "none") == 0) {
        return;
    }
    b->width = (float)num(e, "w:sz", 4) / 8.0f;
    b->width = b->width < 0.25f ? 0.25f : b->width;
    b->space = (float)num(e, "w:space", 0);
    b->color = dx_color(tiku_xml_attr(e, "w:color"));
    b->style = strstr(v, "double") || strstr(v, "triple") ? OF_LS_DOUBLE :
               strstr(v, "dot") && !strstr(v, "Dash") ? OF_LS_DOTTED :
               strstr(v, "ash") ? OF_LS_DASHED : OF_LS_SINGLE;
}

/* --------------------------------------------------------- properties  */

/** @brief A w:rFonts: the Latin and East Asian families, or the theme's
 *         ("+minor", "+major") when it names a theme font. */
static void
fonts(const tiku_xml_node_t *e, dx_rpr_t *r)
{
    const char *theme = tiku_xml_attr(e, "w:asciiTheme");
    const char *name = tiku_xml_attr(e, "w:ascii");
    const char *ea_theme = tiku_xml_attr(e, "w:eastAsiaTheme");
    const char *ea = tiku_xml_attr(e, "w:eastAsia");

    if (theme == NULL) {
        theme = tiku_xml_attr(e, "w:hAnsiTheme");
    }
    if (name == NULL) {
        name = tiku_xml_attr(e, "w:hAnsi");
    }
    if (theme != NULL || name != NULL) {
        snprintf(r->font, sizeof r->font, "%s", theme == NULL ? name :
                 strncmp(theme, "major", 5) == 0 ? "+major" : "+minor");
        r->set |= RP_FONT;
    }
    if (ea_theme != NULL || ea != NULL) {
        snprintf(r->font_ea, sizeof r->font_ea, "%s", ea_theme == NULL ? ea
                 : strncmp(ea_theme, "major", 5) == 0 ? "+major" : "+minor");
        r->set |= RP_FONT_EA;
    }
}

/** @brief An underline's kind from w:u's value. */
static unsigned char
underline(const char *v)
{
    if (v == NULL || strcmp(v, "none") == 0) {
        return v == NULL ? OF_UL_SINGLE : OF_UL_NONE;
    }
    return strcmp(v, "double") == 0 ? OF_UL_DOUBLE :
           strncmp(v, "thick", 5) == 0 ? OF_UL_THICK :
           strstr(v, "otted") || strcmp(v, "dotted") == 0 ? OF_UL_DOTTED :
           strstr(v, "ash") ? OF_UL_DASHED :
           strstr(v, "wav") ? OF_UL_WAVE : OF_UL_SINGLE;
}

/** @brief The on/off run properties of @p c, a child of a w:rPr. */
static void
toggle(const tiku_xml_node_t *c, dx_rpr_t *r)
{
    static const struct { const char *name; unsigned bit; } t[] = {
        {"w:b", RP_BOLD}, {"w:i", RP_ITALIC}, {"w:caps", RP_CAPS},
        {"w:smallCaps", RP_SMALLCAPS}, {"w:vanish", RP_HIDDEN},
        {"w:strike", RP_STRIKE}, {"w:dstrike", RP_STRIKE}
    };
    size_t i;
    int on = dx_on(c);

    for (i = 0; i < sizeof t / sizeof t[0]; i++) {
        if (strcmp(c->name, t[i].name) != 0) {
            continue;
        }
        r->set |= t[i].bit;
        switch (t[i].bit) {
        case RP_BOLD: r->bold = (unsigned char)on; break;
        case RP_ITALIC: r->italic = (unsigned char)on; break;
        case RP_CAPS: r->caps = (unsigned char)on; break;
        case RP_SMALLCAPS: r->smallcaps = (unsigned char)on; break;
        case RP_HIDDEN: r->hidden = (unsigned char)on; break;
        default: r->strike = (unsigned char)(on ? (c->name[2] == 'd' ? 2 : 1)
                                                : 0); break;
        }
    }
}

void
dx_rpr_read(dx_t *x, const tiku_xml_node_t *e, dx_rpr_t *r)
{
    const tiku_xml_node_t *c;

    (void)x;
    for (c = tiku_xml_child(e, NULL); c != NULL; c = tiku_xml_next(c, NULL)) {
        const char *v = tiku_xml_attr(c, "w:val");

        if (tiku_xml_is(c, "w:rFonts")) {
            fonts(c, r);
        } else if (tiku_xml_is(c, "w:sz") && v != NULL) {
            r->size = (float)atof(v) / 2;
            r->set |= RP_SIZE;
        } else if (tiku_xml_is(c, "w:color")) {
            r->color = dx_color(v);
            r->set |= RP_COLOR;
        } else if (tiku_xml_is(c, "w:highlight")) {
            r->back = highlight(v);
            r->set |= RP_BACK;
        } else if (tiku_xml_is(c, "w:shd") && !(r->set & RP_BACK)) {
            r->back = shading(c);
            r->set |= r->back ? RP_BACK : 0;
        } else if (tiku_xml_is(c, "w:u")) {
            r->underline = underline(v);
            r->set |= RP_UNDERLINE;
        } else if (tiku_xml_is(c, "w:vertAlign")) {
            r->vert = (signed char)(v == NULL ? 0 : strcmp(v, "superscript")
                      == 0 ? 1 : strcmp(v, "subscript") == 0 ? -1 : 0);
            r->set |= RP_VERT;
        } else if (tiku_xml_is(c, "w:spacing")) {
            r->spacing = dx_twips(v, 0);
            r->set |= RP_SPACING;
        } else if (tiku_xml_is(c, "w:position")) {
            r->rise = v != NULL ? (float)atof(v) / 2 : 0;
            r->set |= RP_RISE;
        } else if (tiku_xml_is(c, "w:w")) {
            r->scale = v != NULL && atof(v) > 0 ? (float)atof(v) / 100 : 1;
            r->set |= RP_SCALE;
        } else {
            toggle(c, r);
        }
    }
}

/** @brief A w:tab stop added to @p p: a clear, or a stop. */
static void
tab(const tiku_xml_node_t *c, dx_ppr_t *p)
{
    const char *v = tiku_xml_attr(c, "w:val");
    const char *l = tiku_xml_attr(c, "w:leader");
    of_tab_t *t;

    if (v == NULL || strcmp(v, "bar") == 0 || p->ntab >= DX_TABS) {
        return;
    }
    t = &p->tab[p->ntab++];
    t->pos = dx_twips(tiku_xml_attr(c, "w:pos"), 0);
    t->align = strcmp(v, "clear") == 0 ? 0xff :
               strcmp(v, "center") == 0 ? OF_TAB_CENTER :
               strcmp(v, "right") == 0 || strcmp(v, "end") == 0
               ? OF_TAB_RIGHT : strcmp(v, "decimal") == 0 ? OF_TAB_DECIMAL
                                                          : OF_TAB_LEFT;
    t->leader = (unsigned char)(l == NULL ? 0 : strcmp(l, "dot") == 0 ? '.' :
                strcmp(l, "hyphen") == 0 ? '-' : strcmp(l, "underscore") == 0
                || strcmp(l, "heavy") == 0 ? '_' : strcmp(l, "middleDot") == 0
                ? 0xb7 : 0);
    p->set |= PP_TABS;
}

/** @brief w:spacing's space above, below and between lines. */
static void
spacing(const tiku_xml_node_t *c, dx_ppr_t *p)
{
    const char *rule = tiku_xml_attr(c, "w:lineRule");
    const char *line = tiku_xml_attr(c, "w:line");

    if (tiku_xml_attr(c, "w:before") != NULL) {
        p->before = dx_twips(tiku_xml_attr(c, "w:before"), 0);
        p->set |= PP_BEFORE;
    }
    if (dx_on(c) && tiku_xml_attr(c, "w:beforeAutospacing") != NULL &&
        strcmp(tiku_xml_attr(c, "w:beforeAutospacing"), "1") == 0) {
        p->before = 14;
        p->set |= PP_BEFORE;
    }
    if (tiku_xml_attr(c, "w:after") != NULL) {
        p->after = dx_twips(tiku_xml_attr(c, "w:after"), 0);
        p->set |= PP_AFTER;
    }
    if (line != NULL) {
        p->line_rule = rule != NULL && strcmp(rule, "exact") == 0
                       ? OF_LINE_EXACT : rule != NULL && strcmp(rule,
                       "atLeast") == 0 ? OF_LINE_LEAST : OF_LINE_AUTO;
        p->line = p->line_rule == OF_LINE_AUTO ? (float)atof(line) / 240
                                               : dx_twips(line, 12);
        p->set |= PP_LINE;
    }
}

/** @brief w:ind's indents, as Word's own and its right-to-left names. */
static void
indents(const tiku_xml_node_t *c, dx_ppr_t *p)
{
    const char *l = tiku_xml_attr(c, "w:left"), *r = tiku_xml_attr(c,
                                                                  "w:right");
    const char *h = tiku_xml_attr(c, "w:hanging");
    const char *f = tiku_xml_attr(c, "w:firstLine");

    l = l ? l : tiku_xml_attr(c, "w:start");
    r = r ? r : tiku_xml_attr(c, "w:end");
    if (l != NULL) {
        p->left = dx_twips(l, 0);
        p->set |= PP_LEFT;
    }
    if (r != NULL) {
        p->right = dx_twips(r, 0);
        p->set |= PP_RIGHT;
    }
    if (h != NULL || f != NULL) {
        p->first = h != NULL ? -dx_twips(h, 0) : dx_twips(f, 0);
        p->set |= PP_FIRST;
    }
}

/** @brief One child of a w:pPr other than the few with their own
 *         readers. */
static void
ppr_child(const tiku_xml_node_t *c, dx_ppr_t *p)
{
    const char *v = tiku_xml_attr(c, "w:val");

    if (tiku_xml_is(c, "w:jc") && v != NULL) {
        p->align = strcmp(v, "center") == 0 ? OF_CENTER :
                   strcmp(v, "right") == 0 || strcmp(v, "end") == 0
                   ? OF_RIGHT : strcmp(v, "left") == 0 || strcmp(v,
                   "start") == 0 ? OF_LEFT : OF_JUSTIFY;
        p->set |= PP_ALIGN;
    } else if (tiku_xml_is(c, "w:keepNext")) {
        p->keep_next = (unsigned char)dx_on(c);
        p->set |= PP_KEEP_NEXT;
    } else if (tiku_xml_is(c, "w:keepLines")) {
        p->keep_lines = (unsigned char)dx_on(c);
        p->set |= PP_KEEP_LINES;
    } else if (tiku_xml_is(c, "w:pageBreakBefore")) {
        p->brk = (unsigned char)dx_on(c);
        p->set |= PP_BREAK;
    } else if (tiku_xml_is(c, "w:contextualSpacing")) {
        p->contextual = (unsigned char)dx_on(c);
        p->set |= PP_CONTEXT;
    } else if (tiku_xml_is(c, "w:widowControl")) {
        p->widows = (unsigned char)dx_on(c);
        p->set |= PP_WIDOWS;
    } else if (tiku_xml_is(c, "w:outlineLvl") && v != NULL) {
        p->outline = (signed char)(atoi(v) >= 0 && atoi(v) < 9 ? atoi(v)
                                                               : 9);
        p->set |= PP_OUTLINE;
    } else if (tiku_xml_is(c, "w:shd")) {
        p->fill = shading(c);
        p->set |= PP_FILL;
    }
}

void
dx_ppr_read(dx_t *x, const tiku_xml_node_t *e, dx_ppr_t *p)
{
    const tiku_xml_node_t *c, *k;

    (void)x;
    for (c = tiku_xml_child(e, NULL); c != NULL; c = tiku_xml_next(c, NULL)) {
        if (tiku_xml_is(c, "w:spacing")) {
            spacing(c, p);
        } else if (tiku_xml_is(c, "w:ind")) {
            indents(c, p);
        } else if (tiku_xml_is(c, "w:tabs")) {
            for (k = tiku_xml_child(c, "w:tab"); k; k = tiku_xml_next(k,
                                                                 "w:tab")) {
                tab(k, p);
            }
        } else if (tiku_xml_is(c, "w:numPr")) {
            p->num_id = (int)num(tiku_xml_child(c, "w:numId"), "w:val",
                                 p->num_id);
            p->num_lvl = (int)num(tiku_xml_child(c, "w:ilvl"), "w:val", 0);
            p->set |= PP_NUM;
        } else if (tiku_xml_is(c, "w:pBdr")) {
            dx_border(tiku_xml_child(c, "w:top"), &p->border[0]);
            dx_border(tiku_xml_child(c, "w:left") ? tiku_xml_child(c,
                      "w:left") : tiku_xml_child(c, "w:start"),
                      &p->border[1]);
            dx_border(tiku_xml_child(c, "w:bottom"), &p->border[2]);
            dx_border(tiku_xml_child(c, "w:right") ? tiku_xml_child(c,
                      "w:right") : tiku_xml_child(c, "w:end"),
                      &p->border[3]);
            p->set |= PP_BORDER;
        } else {
            ppr_child(c, p);
        }
    }
}

void
dx_tpr_read(const tiku_xml_node_t *e, dx_tpr_t *t)
{
    static const char *const side[6][2] = {
        {"w:top", "w:top"}, {"w:left", "w:start"}, {"w:bottom", "w:bottom"},
        {"w:right", "w:end"}, {"w:insideH", "w:insideH"},
        {"w:insideV", "w:insideV"}
    };
    const tiku_xml_node_t *b = tiku_xml_child(e, "w:tblBorders");
    const tiku_xml_node_t *m = tiku_xml_child(e, "w:tblCellMar"), *s;
    int k;

    b = b ? b : tiku_xml_child(e, "w:tcBorders");
    m = m ? m : tiku_xml_child(e, "w:tcMar");
    for (k = 0; k < 6 && b != NULL; k++) {
        if ((s = tiku_xml_child(b, side[k][0])) != NULL ||
            (s = tiku_xml_child(b, side[k][1])) != NULL) {
            dx_border(s, &t->border[k]);
            t->has_border[k] = 1;
        }
    }
    for (k = 0; k < 4 && m != NULL; k++) {
        if ((s = tiku_xml_child(m, side[k][0])) != NULL ||
            (s = tiku_xml_child(m, side[k][1])) != NULL) {
            t->margin[k] = dx_twips(tiku_xml_attr(s, "w:w"), 0);
            t->has_margin[k] = 1;
        }
    }
    if ((s = tiku_xml_child(e, "w:shd")) != NULL) {
        t->fill = shading(s);
    }
}

/* ------------------------------------------------------------ layering  */

void
dx_rpr_over(dx_rpr_t *dst, const dx_rpr_t *src)
{
    unsigned s = src->set;

    if (s & RP_FONT) memcpy(dst->font, src->font, sizeof dst->font);
    if (s & RP_FONT_EA) memcpy(dst->font_ea, src->font_ea,
                               sizeof dst->font_ea);
    if (s & RP_SIZE) dst->size = src->size;
    if (s & RP_COLOR) dst->color = src->color;
    if (s & RP_BACK) dst->back = src->back;
    if (s & RP_BOLD) dst->bold = src->bold;
    if (s & RP_ITALIC) dst->italic = src->italic;
    if (s & RP_CAPS) dst->caps = src->caps;
    if (s & RP_SMALLCAPS) dst->smallcaps = src->smallcaps;
    if (s & RP_HIDDEN) dst->hidden = src->hidden;
    if (s & RP_UNDERLINE) dst->underline = src->underline;
    if (s & RP_STRIKE) dst->strike = src->strike;
    if (s & RP_VERT) dst->vert = src->vert;
    if (s & RP_SPACING) dst->spacing = src->spacing;
    if (s & RP_RISE) dst->rise = src->rise;
    if (s & RP_SCALE) dst->scale = src->scale;
    dst->set |= s;
}

/** @brief @p src's stops laid over @p dst's: a clear takes a stop away,
 *         another at its place replaces it; kept in order. */
static void
tabs_over(dx_ppr_t *dst, const dx_ppr_t *src)
{
    int i, k, j;

    for (i = 0; i < src->ntab; i++) {
        const of_tab_t *t = &src->tab[i];

        for (k = 0; k < dst->ntab && (dst->tab[k].pos < t->pos - 0.5f ||
                                      dst->tab[k].pos > t->pos + 0.5f); k++) {}
        if (k < dst->ntab) {
            for (j = k; j + 1 < dst->ntab; j++) {
                dst->tab[j] = dst->tab[j + 1];
            }
            dst->ntab--;
        }
        if (t->align == 0xff || dst->ntab >= DX_TABS) {
            continue;
        }
        for (k = dst->ntab; k > 0 && dst->tab[k - 1].pos > t->pos; k--) {
            dst->tab[k] = dst->tab[k - 1];
        }
        dst->tab[k] = *t;
        dst->ntab++;
    }
}

void
dx_ppr_over(dx_ppr_t *dst, const dx_ppr_t *src)
{
    unsigned s = src->set;

    if (s & PP_ALIGN) dst->align = src->align;
    if (s & PP_LEFT) dst->left = src->left;
    if (s & PP_RIGHT) dst->right = src->right;
    if (s & PP_FIRST) dst->first = src->first;
    if (s & PP_BEFORE) dst->before = src->before;
    if (s & PP_AFTER) dst->after = src->after;
    if (s & PP_LINE) {
        dst->line = src->line;
        dst->line_rule = src->line_rule;
    }
    if (s & PP_KEEP_NEXT) dst->keep_next = src->keep_next;
    if (s & PP_KEEP_LINES) dst->keep_lines = src->keep_lines;
    if (s & PP_BREAK) dst->brk = src->brk;
    if (s & PP_CONTEXT) dst->contextual = src->contextual;
    if (s & PP_WIDOWS) dst->widows = src->widows;
    if (s & PP_OUTLINE) dst->outline = src->outline;
    if (s & PP_FILL) dst->fill = src->fill;
    if (s & PP_BORDER) memcpy(dst->border, src->border, sizeof dst->border);
    if (s & PP_NUM) {
        dst->num_id = src->num_id;
        dst->num_lvl = src->num_lvl;
    }
    if (s & PP_TABS) tabs_over(dst, src);
    dst->set |= s;
}

/* -------------------------------------------------------------- styles  */

/** @brief A table style's conditional part's kind from its w:type. */
static int
part_kind(const char *type)
{
    static const char *const kinds[TS_KINDS] = {"wholeTable", "firstRow",
        "lastRow", "firstCol", "lastCol", "band1Horz", "band2Horz",
        "band1Vert", "band2Vert"};
    int k;

    for (k = 0; type != NULL && k < TS_KINDS; k++) {
        if (strcmp(type, kinds[k]) == 0) {
            return k;
        }
    }
    return -1;
}

/** @brief A table style's own table and cell properties, whole and by
 *         the parts of a table it formats. */
static void
table_style(dx_t *x, const tiku_xml_node_t *e, dx_style_t *s)
{
    const tiku_xml_node_t *c;

    dx_tpr_read(tiku_xml_child(e, "w:tblPr"), &s->tpr[TS_WHOLE]);
    dx_tpr_read(tiku_xml_child(e, "w:tcPr"), &s->tpr[TS_WHOLE]);
    s->has[TS_WHOLE] = 1;
    for (c = tiku_xml_child(e, "w:tblStylePr"); c != NULL;
         c = tiku_xml_next(c, "w:tblStylePr")) {
        int k = part_kind(tiku_xml_attr(c, "w:type"));

        if (k <= 0) {
            continue;
        }
        dx_tpr_read(tiku_xml_child(c, "w:tblPr"), &s->tpr[k]);
        dx_tpr_read(tiku_xml_child(c, "w:tcPr"), &s->tpr[k]);
        dx_rpr_read(x, tiku_xml_child(c, "w:rPr"), &s->trpr[k]);
        s->has[k] = 1;
    }
}

/** @brief One w:style read into @p s. */
static void
style_read(dx_t *x, const tiku_xml_node_t *e, dx_style_t *s)
{
    const char *type = tiku_xml_attr(e, "w:type");
    const char *v;

    memset(s, 0, sizeof *s);
    s->type = type == NULL ? 'p' : type[0] == 'c' ? 'c' : type[0] == 't'
              ? 't' : type[0] == 'n' ? 'n' : 'p';
    snprintf(s->id, sizeof s->id, "%s", (v = tiku_xml_attr(e, "w:styleId"))
             ? v : "");
    s->dflt = dx_on(tiku_xml_child(e, "w:default")) ||
              ((v = tiku_xml_attr(e, "w:default")) && strcmp(v, "1") == 0);
    if ((v = tiku_xml_attr(tiku_xml_child(e, "w:basedOn"), "w:val")) != NULL)
        snprintf(s->based, sizeof s->based, "%s", v);
    if ((v = tiku_xml_attr(tiku_xml_child(e, "w:name"), "w:val")) != NULL)
        snprintf(s->name, sizeof s->name, "%s", v);
    dx_ppr_read(x, tiku_xml_child(e, "w:pPr"), &s->ppr);
    dx_rpr_read(x, tiku_xml_child(e, "w:rPr"), &s->rpr);
    if (s->type == 't') {
        table_style(x, e, s);
    }
}

/** @brief @p id's style of type @p type as read, the type's default for
 *         a NULL @p id, or NULL. */
static dx_style_t *
find_style(dx_t *x, const char *id, char type)
{
    int i;

    for (i = 0; i < x->nstyle; i++) {
        if (x->style[i].type == type && (id != NULL ? strcmp(x->style[i].id,
            id) == 0 : x->style[i].dflt)) {
            return &x->style[i];
        }
    }
    return NULL;
}

/** @brief A table style's parts laid over what it is based on. */
static void
table_chain(dx_style_t *s, const dx_style_t *base)
{
    int k, j;

    for (k = 0; base != NULL && k < TS_KINDS; k++) {
        dx_tpr_t own = s->tpr[k];
        dx_rpr_t rown = s->trpr[k];

        if (!base->has[k]) {
            continue;
        }
        s->tpr[k] = base->tpr[k];
        for (j = 0; j < 6; j++) {
            if (own.has_border[j]) {
                s->tpr[k].border[j] = own.border[j];
                s->tpr[k].has_border[j] = 1;
            }
            if (j < 4 && own.has_margin[j]) {
                s->tpr[k].margin[j] = own.margin[j];
                s->tpr[k].has_margin[j] = 1;
            }
        }
        s->tpr[k].fill = own.fill ? own.fill : base->tpr[k].fill;
        s->trpr[k] = base->trpr[k];
        dx_rpr_over(&s->trpr[k], &rown);
        s->has[k] = 1;
    }
}

const dx_style_t *
dx_style(dx_t *x, const char *id, char type)
{
    dx_style_t *s = find_style(x, id, type);
    const dx_style_t *base;

    if (s == NULL && id != NULL) {
        s = find_style(x, NULL, type);
    }
    if (s == NULL || s->done != 0) {
        return s;
    }
    s->done = 1;                    /* a style based on itself stops here */
    base = s->based[0] && strcmp(s->based, s->id) != 0
           ? find_style(x, s->based, type) : NULL;
    base = base != NULL && base->done != 1 ? dx_style(x, base->id, type)
                                           : NULL;
    memset(&s->all_ppr, 0, sizeof s->all_ppr);
    memset(&s->all_rpr, 0, sizeof s->all_rpr);
    if (base != NULL) {
        s->all_ppr = base->all_ppr;
        s->all_rpr = base->all_rpr;
        if (s->type == 't') {
            table_chain(s, base);
        }
    }
    dx_ppr_over(&s->all_ppr, &s->ppr);
    dx_rpr_over(&s->all_rpr, &s->rpr);
    if (!(s->all_ppr.set & PP_OUTLINE) && strncasecmp(s->name, "heading ",
                                                      8) == 0) {
        int n = atoi(s->name + 8);

        s->all_ppr.outline = (signed char)(n >= 1 && n <= 9 ? n - 1 : 9);
        s->all_ppr.set |= PP_OUTLINE;
    }
    s->done = 2;
    return s;
}

/* --------------------------------------------------------------- lists  */

/** @brief A list level's number format from w:numFmt. */
static int
num_format(const char *v)
{
    if (v == NULL) {
        return OF_NUM_DECIMAL;
    }
    return strcmp(v, "bullet") == 0 ? -1 : strcmp(v, "none") == 0 ? -2 :
           strcmp(v, "upperRoman") == 0 ? OF_NUM_UROMAN :
           strcmp(v, "lowerRoman") == 0 ? OF_NUM_LROMAN :
           strcmp(v, "upperLetter") == 0 ? OF_NUM_ULETTER :
           strcmp(v, "lowerLetter") == 0 ? OF_NUM_LLETTER : OF_NUM_DECIMAL;
}

/** @brief One w:lvl read into @p l. */
static void
level_read(dx_t *x, const tiku_xml_node_t *e, dx_level_t *l)
{
    const char *v;

    l->start = start_of(tiku_xml_child(e, "w:start"), "w:val");
    l->fmt = num_format(tiku_xml_attr(tiku_xml_child(e, "w:numFmt"),
                                      "w:val"));
    if ((v = tiku_xml_attr(tiku_xml_child(e, "w:lvlText"), "w:val")) != NULL)
        snprintf(l->text, sizeof l->text, "%s", v);
    v = tiku_xml_attr(tiku_xml_child(e, "w:suff"), "w:val");
    l->follow = v == NULL || strcmp(v, "tab") == 0 ? 0 :
                strcmp(v, "space") == 0 ? 1 : 2;
    l->legal = dx_on(tiku_xml_child(e, "w:isLgl"));
    l->restart = (int)num(tiku_xml_child(e, "w:lvlRestart"), "w:val", -1);
    dx_ppr_read(x, tiku_xml_child(e, "w:pPr"), &l->ppr);
    dx_rpr_read(x, tiku_xml_child(e, "w:rPr"), &l->rpr);
}

/** @brief The abstract numbering @p id's levels read into @p lvl. */
static void
abstract_read(dx_t *x, const tiku_xml_node_t *root, int id, dx_level_t *lvl)
{
    const tiku_xml_node_t *a, *l;

    for (a = tiku_xml_child(root, "w:abstractNum"); a != NULL;
         a = tiku_xml_next(a, "w:abstractNum")) {
        if (num(a, "w:abstractNumId", -1) != id) {
            continue;
        }
        for (l = tiku_xml_child(a, "w:lvl"); l; l = tiku_xml_next(l,
                                                                "w:lvl")) {
            long k = num(l, "w:ilvl", 0);

            if (k >= 0 && k < 9) {
                level_read(x, l, &lvl[k]);
            }
        }
        return;
    }
}

/** @brief numbering.xml's lists, each w:num with its overrides. */
static void
lists_load(dx_t *x, const tiku_xml_node_t *root)
{
    const tiku_xml_node_t *n, *o;
    int count = 0;

    for (n = tiku_xml_child(root, "w:num"); n; n = tiku_xml_next(n, "w:num"))
        count++;
    if (count == 0 || (x->list = calloc((size_t)count, sizeof *x->list)) ==
        NULL) {
        return;
    }
    for (n = tiku_xml_child(root, "w:num"); n; n = tiku_xml_next(n,
                                                                "w:num")) {
        dx_list_t *l = &x->list[x->nlist++];

        l->id = (int)num(n, "w:numId", -1);
        l->abstract = (int)num(tiku_xml_child(n, "w:abstractNumId"), "w:val",
                               -1);
        abstract_read(x, root, l->abstract, l->lvl);
        for (o = tiku_xml_child(n, "w:lvlOverride"); o;
             o = tiku_xml_next(o, "w:lvlOverride")) {
            long k = num(o, "w:ilvl", 0);
            const tiku_xml_node_t *s = tiku_xml_child(o, "w:startOverride");

            if (k < 0 || k > 8) {
                continue;
            }
            if (tiku_xml_child(o, "w:lvl") != NULL) {
                level_read(x, tiku_xml_child(o, "w:lvl"), &l->lvl[k]);
            }
            if (s != NULL) {
                l->lvl[k].start = start_of(s, "w:val");
            }
            l->own = 1;
        }
    }
}

/** @brief The list whose counts list @p l uses: its own when it
 *         restarts, else the first list of its abstract numbering. */
static dx_list_t *
counter(dx_t *x, dx_list_t *l)
{
    int i;

    for (i = 0; !l->own && i < x->nlist; i++) {
        if (x->list[i].abstract == l->abstract && !x->list[i].own) {
            return &x->list[i];
        }
    }
    return l;
}

/** @brief A bullet's text as Unicode: each of its characters mapped from
 *         the bullet's font, into @p out. */
static void
bullet(const char *text, const char *font, char *out, size_t max)
{
    const unsigned char *p = (const unsigned char *)text;
    size_t k = 0;

    while (*p && k + 5 < max) {
        uint32_t cp = *p++;

        if (cp >= 0xe0 && p[0] && p[1]) {
            cp = (cp & 0x0f) << 12 | (uint32_t)(p[0] & 0x3f) << 6 |
                 (p[1] & 0x3f);
            p += 2;
        } else if (cp >= 0xc0 && p[0]) {
            cp = (cp & 0x1f) << 6 | (p[0] & 0x3f);
            p++;
        }
        cp = of_symbol(font, cp);
        if (cp >= 0xe000 && cp < 0xf900) {
            cp = 0x2022;            /* a private character: a bullet */
        }
        if (cp < 0x80) {
            out[k++] = (char)cp;
        } else if (cp < 0x800) {
            out[k++] = (char)(0xc0 | cp >> 6);
            out[k++] = (char)(0x80 | (cp & 0x3f));
        } else {
            out[k++] = (char)(0xe0 | cp >> 12);
            out[k++] = (char)(0x80 | (cp >> 6 & 0x3f));
            out[k++] = (char)(0x80 | (cp & 0x3f));
        }
    }
    out[k] = '\0';
}

/**
 * @brief The label paragraph level @p lvl of list @p l gets, counted:
 *        this level on by one, the ones under it begun again; "%1.%2."
 *        filled with the levels' counts.  Into @p out.
 */
static void
label(dx_t *x, dx_list_t *l, int lvl, char *out, size_t max)
{
    dx_list_t *c = counter(x, l);
    const dx_level_t *lv = &l->lvl[lvl];
    const char *p;
    size_t k = 0;
    int j;

    c->count[lvl] = c->count[lvl] == 0 ? lv->start : c->count[lvl] + 1;
    for (j = lvl + 1; j < 9; j++) {
        if (l->lvl[j].restart != 0) {
            c->count[j] = 0;
        }
    }
    if (lv->fmt == -1) {
        bullet(lv->text, lv->rpr.font, out, max);
        return;
    }
    for (p = lv->text; *p && k + 12 < max; p++) {
        if (*p == '%' && p[1] >= '1' && p[1] <= '9') {
            int n = p[1] - '1', v = c->count[n] ? c->count[n]
                                                : l->lvl[n].start;
            int fmt = lv->legal || l->lvl[n].fmt < 0 ? OF_NUM_DECIMAL
                                                      : l->lvl[n].fmt;

            of_number(v, fmt, out + k, max - k);
            k += strlen(out + k);
            p++;
        } else {
            out[k++] = *p;
        }
    }
    out[k] = '\0';
}

/* ------------------------------------------------------------- results  */

/** @brief A theme font's name for "+minor" or "+major", else @p f. */
static const char *
theme_font(const dx_t *x, const char *f, int ea)
{
    if (strcmp(f, "+minor") == 0) {
        return ea ? (x->minor_ea[0] ? x->minor_ea : NULL)
                  : x->minor[0] ? x->minor : NULL;
    }
    if (strcmp(f, "+major") == 0) {
        return ea ? (x->major_ea[0] ? x->major_ea : NULL)
                  : x->major[0] ? x->major : NULL;
    }
    return f[0] ? f : NULL;
}

const of_style_t *
dx_look(dx_t *x, const char *pstyle, const char *rstyle, const dx_rpr_t *own)
{
    dx_rpr_t r = x->def_rpr;
    const dx_style_t *ps = dx_style(x, pstyle, 'p');
    const dx_style_t *cs = rstyle != NULL ? dx_style(x, rstyle, 'c') : NULL;
    of_style_t st;

    if (x->table != NULL) {
        dx_rpr_over(&r, &x->table->all_rpr);
        if (x->table->has[x->table_part]) {
            dx_rpr_over(&r, &x->table->trpr[x->table_part]);
        }
    }
    if (ps != NULL) {
        dx_rpr_over(&r, &ps->all_rpr);
    }
    if (cs != NULL && cs->type == 'c') {
        dx_rpr_over(&r, &cs->all_rpr);
    }
    if (own != NULL) {
        dx_rpr_over(&r, own);
    }
    of_style_default(&st);
    st.font = r.set & RP_FONT ? theme_font(x, r.font, 0) : NULL;
    st.font = st.font ? st.font : x->minor[0] ? x->minor : "Times New Roman";
    st.font_ea = r.set & RP_FONT_EA ? theme_font(x, r.font_ea, 1) : NULL;
    st.size = r.set & RP_SIZE && r.size >= 1 ? (r.size < 1638 ? r.size
                                                              : 1638) : 10;
    st.color = r.color;
    st.back = r.back;
    st.bold = r.bold;
    st.italic = r.italic;
    st.caps = r.caps;
    st.smallcaps = r.smallcaps;
    st.hidden = r.hidden;
    st.underline = r.underline;
    st.strike = r.strike;
    st.vert = r.vert;
    st.spacing = r.spacing;
    st.rise = r.rise;
    st.scale = r.set & RP_SCALE ? r.scale : 1;
    return of_style(x->d, &st);
}

/** @brief The list @p id, or NULL. */
static dx_list_t *
find_list(dx_t *x, int id)
{
    int i;

    for (i = 0; id > 0 && i < x->nlist; i++) {
        if (x->list[i].id == id) {
            return &x->list[i];
        }
    }
    return NULL;
}

/** @brief Resolved properties made the paragraph's layout. */
static void
to_layout(dx_t *x, const dx_ppr_t *pp, const char *pstyle, of_pstyle_t *o)
{
    of_tab_t *t;
    int i, n = 0;

    of_pstyle_default(o);
    o->align = pp->align;
    o->left = pp->left;
    o->right = pp->right;
    o->first = pp->first;
    o->before = pp->before;
    o->after = pp->after;
    o->line_rule = pp->set & PP_LINE ? pp->line_rule : OF_LINE_AUTO;
    o->line = pp->set & PP_LINE && pp->line > 0 ? pp->line : 1;
    o->keep_next = pp->keep_next;
    o->keep_lines = pp->keep_lines;
    o->break_before = pp->brk;
    o->contextual = pp->contextual;
    o->widows = pp->set & PP_WIDOWS ? pp->widows : 1;
    o->outline = pp->set & PP_OUTLINE && pp->outline < 9 ? pp->outline : -1;
    o->fill = pp->fill;
    memcpy(o->border, pp->border, sizeof o->border);
    o->style = pstyle ? of_strdup(&x->d->arena, pstyle) : NULL;
    if (pp->ntab > 0 && (t = of_alloc(&x->d->arena, (size_t)pp->ntab *
                                       sizeof *t)) != NULL) {
        for (i = 0; i < pp->ntab; i++) {
            if (pp->tab[i].align != 0xff) {
                t[n++] = pp->tab[i];
            }
        }
        o->tab = t;
        o->ntab = n;
    }
}

void
dx_layout(dx_t *x, const char *pstyle, const dx_ppr_t *own,
          const dx_rpr_t *mark, of_para_t *p)
{
    dx_ppr_t pp = x->def_ppr, style;
    const dx_style_t *ps = dx_style(x, pstyle, 'p');
    dx_list_t *l;
    int id, lvl;
    char text[96];

    if (x->table != NULL) {
        dx_ppr_over(&pp, &x->table->all_ppr);
    }
    memset(&style, 0, sizeof style);
    if (ps != NULL) {
        style = ps->all_ppr;
        dx_ppr_over(&pp, &style);
    }
    id = own != NULL && (own->set & PP_NUM) ? own->num_id : pp.num_id;
    lvl = own != NULL && (own->set & PP_NUM) ? own->num_lvl : pp.num_lvl;
    lvl = lvl >= 0 && lvl < 9 ? lvl : 0;
    if ((l = find_list(x, id)) != NULL) {
        dx_ppr_over(&pp, &l->lvl[lvl].ppr);
    }
    if (own != NULL) {
        dx_ppr_over(&pp, own);
    }
    to_layout(x, &pp, ps != NULL ? ps->id : pstyle, &p->ps);
    if (l != NULL && l->lvl[lvl].fmt != -2) {
        dx_rpr_t lr = mark != NULL ? *mark : x->def_rpr;

        if (mark == NULL) {
            lr.set = 0;
        }
        dx_rpr_over(&lr, &l->lvl[lvl].rpr);
        lr.set &= ~(unsigned)(RP_UNDERLINE | RP_BACK);
        label(x, l, lvl, text, sizeof text);
        p->label = text[0] ? of_strdup(&x->d->arena, text) : NULL;
        p->label_st = dx_look(x, ps ? ps->id : pstyle, NULL, &lr);
        p->label_follow = (unsigned char)l->lvl[lvl].follow;
        if (l->lvl[lvl].fmt == -1 && (lr.set & RP_FONT) &&
            (has(lr.font, "symbol") || has(lr.font, "wingdings"))) {
            lr.set &= ~(unsigned)RP_FONT;   /* mapped: any face draws it */
            p->label_st = dx_look(x, ps ? ps->id : pstyle, NULL, &lr);
        }
    }
}

/* --------------------------------------------------------------- loads  */

/** @brief The theme's major and minor fonts, Latin and East Asian. */
static void
theme_load(dx_t *x, const char *main)
{
    const of_rel_t *r = of_pkg_rel_type(x->pkg, main, "theme");
    tiku_xml_t *t = r ? of_pkg_xml(x->pkg, r->target) : NULL;
    const tiku_xml_node_t *fs = tiku_xml_path(tiku_xml_root(t),
                                              "a:themeElements/a:fontScheme");
    const char *v;

    snprintf(x->minor, sizeof x->minor, "Calibri");     /* Office's own, */
    snprintf(x->major, sizeof x->major, "Calibri Light");   /* no theme */
    if ((v = tiku_xml_attr(tiku_xml_path(fs, "a:minorFont/a:latin"),
                           "typeface")) != NULL)
        snprintf(x->minor, sizeof x->minor, "%s", v);
    if ((v = tiku_xml_attr(tiku_xml_path(fs, "a:majorFont/a:latin"),
                           "typeface")) != NULL)
        snprintf(x->major, sizeof x->major, "%s", v);
    if ((v = tiku_xml_attr(tiku_xml_path(fs, "a:minorFont/a:ea"),
                           "typeface")) != NULL)
        snprintf(x->minor_ea, sizeof x->minor_ea, "%s", v);
    if ((v = tiku_xml_attr(tiku_xml_path(fs, "a:majorFont/a:ea"),
                           "typeface")) != NULL)
        snprintf(x->major_ea, sizeof x->major_ea, "%s", v);
    tiku_xml_free(t);
}

void
dx_styles_load(dx_t *x, const char *main)
{
    const of_rel_t *r = of_pkg_rel_type(x->pkg, main, "styles");
    tiku_xml_t *st = r ? of_pkg_xml(x->pkg, r->target) : NULL;
    const tiku_xml_node_t *root = tiku_xml_root(st), *e;
    int n = 0;

    theme_load(x, main);
    dx_rpr_read(x, tiku_xml_path(root, "w:docDefaults/w:rPrDefault/w:rPr"),
                &x->def_rpr);
    dx_ppr_read(x, tiku_xml_path(root, "w:docDefaults/w:pPrDefault/w:pPr"),
                &x->def_ppr);
    for (e = tiku_xml_child(root, "w:style"); e; e = tiku_xml_next(e,
                                                                  "w:style"))
        n++;
    if (n > 0 && (x->style = calloc((size_t)n, sizeof *x->style)) != NULL) {
        for (e = tiku_xml_child(root, "w:style"); e;
             e = tiku_xml_next(e, "w:style")) {
            style_read(x, e, &x->style[x->nstyle++]);
        }
    }
    tiku_xml_free(st);
    r = of_pkg_rel_type(x->pkg, main, "numbering");
    st = r ? of_pkg_xml(x->pkg, r->target) : NULL;
    lists_load(x, tiku_xml_root(st));
    tiku_xml_free(st);
}
