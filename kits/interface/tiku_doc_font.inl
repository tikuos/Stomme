/*
 * TikuOS Interface Kit.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_doc_font.inl - inherited logical fonts and available layout faces.
 * SPDX-License-Identifier: Apache-2.0
 */

/** @brief Bound logical size expressions during nested relative sizing. */
static int32_t css_font_bound(read_t *r, int64_t value, int32_t limit)
{
    if (value <= limit) return (int32_t)value;
    r->css.notices |= TIKU_DOC_CSS_LIMIT;
    return limit;
}

/** @brief Apply semantic font defaults without accessing desktop font state. */
static void css_font_defaults(read_t *r, int tag)
{
    if (tag == T_B || tag == T_TH || tag == T_DT ||
        (tag >= T_H1 && tag <= T_H3) || (tag >= T_H4 && tag <= T_H6))
        r->font.weight = 1;
    if (tag == T_I) r->font.italic = 1;
    if (tag == T_CODE || tag == T_PRE) r->font.family = TIKU_MEASURE_FAMILY_MONO;
    if (tag == T_H1 || tag == T_H2)
        r->font.offset = css_font_bound(r,(int64_t)r->font.offset+
                                (tag == T_H1 ? 4 : 2)*DOC_FONT_ONE,
                                512*DOC_FONT_ONE);
}

/** @brief Apply resolved font properties to their inherited logical values. */
static void css_font_apply(read_t *r, const doc_css_decl_t *d)
{
    const tiku_doc_font_t *parent = &r->css_stack[r->css_depth-1].font;
    if (d->mask & DOC_CSS_SIZE) {
        if (d->size_unit == DOC_SIZE_PX) { r->font.scale = 0; r->font.offset = d->size; }
        else if (d->size_unit == DOC_SIZE_REM || d->size_unit == DOC_SIZE_INITIAL) {
            r->font.scale = d->size_unit == DOC_SIZE_REM ? d->size : DOC_FONT_ONE;
            r->font.offset = 0;
        } else if (d->size_unit == DOC_SIZE_EM) {
            r->font.scale = css_font_bound(r,(int64_t)parent->scale*d->size/DOC_FONT_ONE,
                                          64*DOC_FONT_ONE);
            r->font.offset = css_font_bound(r,(int64_t)parent->offset*d->size/DOC_FONT_ONE,
                                           512*DOC_FONT_ONE);
        } else {
            r->font.scale = parent->scale; r->font.offset = parent->offset;
        }
    }
    if (d->mask & DOC_CSS_FAMILY)
        r->font.family = d->family == DOC_FONT_INHERIT ? parent->family : d->family;
    if (d->mask & DOC_CSS_WEIGHT)
        r->font.weight = d->weight == DOC_FONT_INHERIT ? parent->weight : d->weight;
    if (d->mask & DOC_CSS_ITALIC)
        r->font.italic = d->italic == DOC_FONT_INHERIT ? parent->italic : d->italic;
    r->font.specified |= d->mask & (DOC_CSS_SIZE | DOC_CSS_FAMILY | DOC_CSS_WEIGHT | DOC_CSS_ITALIC);
}

/** @brief Select an available face at layout time and report font approximation. */
static int doc_font_face(tiku_doc_t *d, const tiku_doc_font_t *font)
{
    int64_t size = (int64_t)font->scale*tiku_measure_font_size()+font->offset;
    int pixels = (int)((size+DOC_FONT_ONE/2)/DOC_FONT_ONE);
    unsigned cut = (font->weight ? TIKU_MEASURE_FONT_BOLD : 0) |
                    (font->italic ? TIKU_MEASURE_FONT_ITALIC : 0);
    int face = tiku_measure_font_face(font->family,pixels,cut);
    if (face < TIKU_MEASURE_FONT_BASE || (face & 255) != pixels ||
        (((unsigned)face >> 8) & 3u) != cut) d->notices |= TIKU_DOC_FONT_FALLBACK;
    return face;
}
