/*
 * TikuOS Interface Kit.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_doc_css_fonts.inl - bounded CSS font values and logical size units.
 * SPDX-License-Identifier: Apache-2.0
 */

enum { DOC_SIZE_PX, DOC_SIZE_EM, DOC_SIZE_REM, DOC_SIZE_INHERIT, DOC_SIZE_INITIAL };
#define DOC_FONT_INHERIT 255u
#define DOC_FONT_INITIAL 254u
#define DOC_FONT_ONE 65536

/** @brief The font property bit for a CSS name, or zero. */
static unsigned css_font_property(const char *key, size_t n)
{
    if (html_name(key,n,"font-size")) return DOC_CSS_SIZE;
    if (html_name(key,n,"font-family")) return DOC_CSS_FAMILY;
    if (html_name(key,n,"font-weight")) return DOC_CSS_WEIGHT;
    if (html_name(key,n,"font-style")) return DOC_CSS_ITALIC;
    return 0;
}

/** @brief Parse a positive fixed-point CSS size with px, em, rem or percent. */
static int css_font_size(doc_css_decl_t *d, const char *p, const char *end)
{
    if (html_name(p,(size_t)(end-p),"inherit") || html_name(p,(size_t)(end-p),"unset")) {
        d->size_unit = DOC_SIZE_INHERIT; return 1;
    }
    if (html_name(p,(size_t)(end-p),"initial") || html_name(p,(size_t)(end-p),"medium")) {
        d->size_unit = DOC_SIZE_INITIAL; return 1;
    }
    if (p < end && *p == '+') p++;
    int digits = 0, whole = 0, fraction = 0, denominator = 1;
    while (p < end && *p >= '0' && *p <= '9') {
        if (whole > 6400) return 0;
        whole = whole*10 + (*p++-'0'); digits++;
    }
    if (p < end && *p == '.') {
        p++;
        while (p < end && *p >= '0' && *p <= '9') {
            if (denominator < 10000) { fraction = fraction*10+(*p-'0'); denominator *= 10; }
            p++; digits++;
        }
    }
    if (!digits || whole > 6400) return 0;
    int64_t value = (int64_t)whole*DOC_FONT_ONE +
                    (int64_t)fraction*DOC_FONT_ONE/denominator;
    size_t unit = (size_t)(end-p);
    if (html_name(p,unit,"px")) {
        if (value < DOC_FONT_ONE || value > 512LL*DOC_FONT_ONE) return 0;
        d->size_unit = DOC_SIZE_PX;
    } else if (html_name(p,unit,"em") || html_name(p,unit,"rem") ||
               (unit == 1 && *p == '%')) {
        d->size_unit = html_name(p,unit,"rem") ? DOC_SIZE_REM : DOC_SIZE_EM;
        if (unit == 1 && *p == '%') value /= 100;
        if (value < DOC_FONT_ONE/64 || value > 64LL*DOC_FONT_ONE) return 0;
    } else return 0;
    d->size = (int32_t)value;
    return 1;
}

/** @brief Select the first available family from a CSS fallback list. */
static int css_font_family(doc_css_decl_t *d, const char *p, const char *end)
{
    if (html_name(p,(size_t)(end-p),"inherit") || html_name(p,(size_t)(end-p),"unset")) {
        d->family = DOC_FONT_INHERIT; return 1;
    }
    if (html_name(p,(size_t)(end-p),"initial")) {
        d->family = TIKU_MEASURE_FAMILY_CURRENT; return 1;
    }
    while (p < end) {
        const char *start = p;
        char quote = 0;
        while (p < end) {
            if (quote) { if (*p == quote) quote = 0; }
            else if (*p == '\'' || *p == '"') quote = *p;
            else if (*p == ',') break;
            p++;
        }
        const char *last = p; css_trim(&start,&last);
        if (last-start >= 2 && ((*start == '\'' && last[-1] == '\'') ||
                                (*start == '"' && last[-1] == '"'))) { start++; last--; }
        size_t n = (size_t)(last-start);
        if (html_name(start,n,"system-ui")) { d->family = TIKU_MEASURE_FAMILY_CURRENT; return 1; }
        if (html_name(start,n,"sans") || html_name(start,n,"sans-serif")) {
            d->family = TIKU_MEASURE_FAMILY_SANS; return 1;
        }
        if (html_name(start,n,"serif")) { d->family = TIKU_MEASURE_FAMILY_SERIF; return 1; }
        if (html_name(start,n,"mono") || html_name(start,n,"monospace")) {
            d->family = TIKU_MEASURE_FAMILY_MONO; return 1;
        }
        if (p < end) p++;
    }
    return 0;
}

/** @brief Parse a supported font declaration without reading font resources. */
static int css_font_value(doc_css_decl_t *d, unsigned mask,
                           const char *p, const char *end)
{
    if (mask == DOC_CSS_SIZE) return css_font_size(d,p,end);
    if (mask == DOC_CSS_FAMILY) return css_font_family(d,p,end);
    unsigned char value;
    size_t n = (size_t)(end-p);
    if (html_name(p,n,"inherit") || html_name(p,n,"unset")) value = DOC_FONT_INHERIT;
    else if (html_name(p,n,"initial") || html_name(p,n,"normal")) value = 0;
    else if (mask == DOC_CSS_WEIGHT) {
        if (html_name(p,n,"bold") || html_name(p,n,"bolder")) value = 1;
        else if (html_name(p,n,"lighter")) value = 0;
        else if (n == 3 && p[0] >= '1' && p[0] <= '9' && p[1] == '0' && p[2] == '0')
            value = p[0] >= '6';
        else return 0;
    } else if (html_name(p,n,"italic") || html_name(p,n,"oblique")) value = 1;
    else return 0;
    if (mask == DOC_CSS_WEIGHT) d->weight = value;
    else d->italic = value;
    return 1;
}
