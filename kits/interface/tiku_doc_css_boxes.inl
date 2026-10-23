/*
 * TikuOS Interface Kit.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_doc_css_boxes.inl - bounded block box and display declarations.
 * SPDX-License-Identifier: Apache-2.0
 */

enum { DOC_DISPLAY_NATIVE, DOC_DISPLAY_INLINE, DOC_DISPLAY_BLOCK, DOC_DISPLAY_NONE };
#define DOC_LENGTH_INHERIT 255u
#define DOC_LENGTH_INITIAL 254u
#define DOC_CURRENT_COLOR 0x1000000u

/** @brief Parse a nonnegative length within the document box profile. */
static int css_box_length(const char *p, const char *end, tiku_doc_length_t *out, int automatic)
{
    size_t n = (size_t)(end-p);
    *out = (tiku_doc_length_t){0};
    if (html_name(p,n,"inherit")) { out->unit = DOC_LENGTH_INHERIT; return 1; }
    if (html_name(p,n,"initial") || html_name(p,n,"unset")) { out->unit = DOC_LENGTH_INITIAL; return 1; }
    if ((automatic && html_name(p,n,"auto")) || (automatic == 2 && html_name(p,n,"none"))) {
        out->unit = TIKU_DOC_AUTO; return 1;
    }
    if (p < end && *p == '+') p++;
    int whole = 0, part = 0, divisor = 1, digits = 0;
    while (p < end && *p >= '0' && *p <= '9') {
        if (whole > 10000) return 0;
        whole = whole*10+(*p++-'0'); digits++;
    }
    if (p < end && *p == '.') {
        p++;
        while (p < end && *p >= '0' && *p <= '9') {
            if (divisor < 10000) { part = part*10+(*p-'0'); divisor *= 10; }
            p++; digits++;
        }
    }
    if (!digits || whole > 10000) return 0;
    int64_t value = (int64_t)whole*DOC_FONT_ONE+(int64_t)part*DOC_FONT_ONE/divisor;
    n = (size_t)(end-p);
    if (html_name(p,n,"px") || (!n && !value)) {
        if (value > 4096LL*DOC_FONT_ONE) return 0;
        out->unit = TIKU_DOC_PX;
    } else if (html_name(p,n,"em") || html_name(p,n,"rem")) {
        if (value > 64LL*DOC_FONT_ONE) return 0;
        out->unit = html_name(p,n,"em") ? TIKU_DOC_EM : TIKU_DOC_REM;
    } else if (n == 1 && *p == '%') {
        if (value > 100LL*DOC_FONT_ONE) return 0;
        out->unit = TIKU_DOC_PERCENT;
    } else return 0;
    out->value = (int32_t)value; return 1;
}

/** @brief Parse a one-to-four-value margin or padding shorthand. */
static int css_box_sides(const char *p, const char *end, tiku_doc_length_t sides[4], int automatic)
{
    tiku_doc_length_t values[4]; int count = 0;
    while (p < end) {
        while (p < end && html_space(*p)) p++;
        if (p == end) break;
        const char *start = p;
        while (p < end && !html_space(*p)) p++;
        if (count == 4 || !css_box_length(start,p,&values[count],automatic)) return 0;
        count++;
    }
    if (!count) return 0;
    sides[0] = values[0]; sides[1] = values[count > 1 ? 1 : 0];
    sides[2] = values[count > 2 ? 2 : 0]; sides[3] = values[count > 3 ? 3 : count > 1 ? 1 : 0];
    return 1;
}

/** @brief Parse a uniform border width, colour and solid/none style. */
static int css_box_border(doc_css_decl_t *d, const char *p, const char *end)
{
    unsigned seen = 0;
    d->box.border = (tiku_doc_length_t){3*DOC_FONT_ONE,TIKU_DOC_PX};
    d->box.border_color = DOC_CURRENT_COLOR;
    while (p < end) {
        while (p < end && html_space(*p)) p++;
        if (p == end) break;
        const char *start = p;
        while (p < end && !html_space(*p)) p++;
        size_t n = (size_t)(p-start); unsigned bit;
        tiku_doc_length_t length; uint32_t rgb;
        if (html_name(start,n,"solid") || html_name(start,n,"none")) {
            bit = 1; d->box.border_style = html_name(start,n,"solid");
        } else if (css_box_length(start,p,&length,0) && length.unit <= TIKU_DOC_REM) {
            bit = 2; d->box.border = length;
        } else if (css_color(start,n,&rgb) || html_name(start,n,"currentColor")) {
            bit = 4; d->box.border_color = html_name(start,n,"currentColor") ? DOC_CURRENT_COLOR : rgb;
        } else return 0;
        if (seen & bit) return 0;
        seen |= bit;
    }
    return seen != 0;
}

/** @brief Return a parsed box property mask, zero for another name, or UINT32_MAX. */
static unsigned css_box_property(doc_css_decl_t *d, const char *key, size_t n,
                                   const char *p, const char *end)
{
    size_t length = (size_t)(end-p);
    if (html_name(key,n,"background") || html_name(key,n,"background-color")) {
        d->box.has_background = 1;
        if (html_name(p,length,"inherit")) d->box.has_background = DOC_LENGTH_INHERIT;
        else if (html_name(p,length,"transparent") || html_name(p,length,"initial") || html_name(p,length,"unset")) d->box.has_background = 0;
        else if (!css_color(p,length,&d->box.background)) return UINT32_MAX;
        return DOC_CSS_BACKGROUND;
    }
    if (html_name(key,n,"display")) {
        if (html_name(p,length,"inline") || html_name(p,length,"initial") || html_name(p,length,"unset")) d->display = DOC_DISPLAY_INLINE;
        else if (html_name(p,length,"block")) d->display = DOC_DISPLAY_BLOCK;
        else if (html_name(p,length,"none")) d->display = DOC_DISPLAY_NONE;
        else if (html_name(p,length,"inherit")) d->display = DOC_LENGTH_INHERIT;
        else return UINT32_MAX;
        return DOC_CSS_DISPLAY;
    }
    if (html_name(key,n,"height"))
        return css_box_length(p,end,&d->box.height,1) && d->box.height.unit != TIKU_DOC_PERCENT ?
            DOC_CSS_IMAGE_HEIGHT : UINT32_MAX;
    static const char *side_names[] = {"top","right","bottom","left"};
    for (int padding = 0; padding < 2; padding++) {
        const char *base = padding ? "padding" : "margin";
        size_t base_n = strlen(base); unsigned mask = padding ? DOC_CSS_PADDING : DOC_CSS_MARGIN;
        tiku_doc_length_t *sides = padding ? d->box.padding : d->box.margin;
        if (html_name(key,n,base))
            return css_box_sides(p,end,sides,!padding) ? mask*15u : UINT32_MAX;
        for (int i = 0; i < 4; i++)
            if (n > base_n && html_same(key,base_n,base,base_n) && key[base_n] == '-' &&
                html_name(key+base_n+1,n-base_n-1,side_names[i]))
                return css_box_length(p,end,&sides[i],!padding) ? mask<<i : UINT32_MAX;
    }
    if (html_name(key,n,"width") || html_name(key,n,"max-width")) {
        int maximum = html_name(key,n,"max-width");
        tiku_doc_length_t *out = maximum ? &d->box.max_width : &d->box.width;
        return css_box_length(p,end,out,maximum ? 2 : 1) ? maximum ? DOC_CSS_MAX_WIDTH : DOC_CSS_WIDTH : UINT32_MAX;
    }
    if (html_name(key,n,"border"))
        return css_box_border(d,p,end) ? DOC_CSS_BORDER_WIDTH|DOC_CSS_BORDER_COLOR|DOC_CSS_BORDER_STYLE : UINT32_MAX;
    if (html_name(key,n,"border-width"))
        return css_box_length(p,end,&d->box.border,0) && d->box.border.unit != TIKU_DOC_PERCENT ? DOC_CSS_BORDER_WIDTH : UINT32_MAX;
    if (html_name(key,n,"border-color")) {
        if (html_name(p,length,"inherit")) d->box.border_color = UINT32_MAX;
        else if (html_name(p,length,"currentColor") || html_name(p,length,"initial") || html_name(p,length,"unset")) d->box.border_color = DOC_CURRENT_COLOR;
        else if (!css_color(p,length,&d->box.border_color)) return UINT32_MAX;
        return DOC_CSS_BORDER_COLOR;
    }
    if (html_name(key,n,"border-style")) {
        if (html_name(p,length,"solid")) d->box.border_style = 1;
        else if (html_name(p,length,"none") || html_name(p,length,"initial") || html_name(p,length,"unset")) d->box.border_style = 0;
        else if (html_name(p,length,"inherit")) d->box.border_style = DOC_LENGTH_INHERIT;
        else return UINT32_MAX;
        return DOC_CSS_BORDER_STYLE;
    }
    return 0;
}
