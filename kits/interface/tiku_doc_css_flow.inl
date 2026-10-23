/*
 * TikuOS Interface Kit.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_doc_css_flow.inl - bounded text-flow declaration values.
 * SPDX-License-Identifier: Apache-2.0
 */

enum { DOC_HEIGHT_NUMBER = DOC_SIZE_INITIAL+1, DOC_HEIGHT_NORMAL };

/** @brief Return the flow property bit for a CSS name, or zero. */
static unsigned css_flow_property(const char *key, size_t n)
{
    if (html_name(key,n,"text-align")) return DOC_CSS_ALIGN;
    if (html_name(key,n,"line-height")) return DOC_CSS_HEIGHT;
    if (html_name(key,n,"white-space")) return DOC_CSS_WHITE;
    return 0;
}

/** @brief Parse alignment, whitespace and bounded line-height declarations. */
static int css_flow_value(doc_css_decl_t *d, unsigned mask,
                            const char *p, const char *end)
{
    size_t n = (size_t)(end-p);
    int inherit = html_name(p,n,"inherit") || html_name(p,n,"unset");
    if (mask == DOC_CSS_HEIGHT) {
        if (inherit) { d->height_unit = DOC_SIZE_INHERIT; return 1; }
        if (html_name(p,n,"normal") || html_name(p,n,"initial")) {
            d->height_unit = DOC_HEIGHT_NORMAL; return 1;
        }
        doc_css_decl_t size = {0};
        if (n && ((end[-1] >= '0' && end[-1] <= '9') || end[-1] == '.')) {
            char value[160];
            if (n+3 > sizeof value) return 0;
            memcpy(value,p,n); memcpy(value+n,"em",3);
            if (!css_font_size(&size,value,value+n+2)) return 0;
            d->height_unit = DOC_HEIGHT_NUMBER;
        } else {
            if (!css_font_size(&size,p,end) || size.size_unit > DOC_SIZE_REM) return 0;
            d->height_unit = size.size_unit;
        }
        d->height = size.size; return 1;
    }
    unsigned char value;
    if (inherit) value = DOC_FONT_INHERIT;
    else if (html_name(p,n,"initial")) value = 0;
    else if (mask == DOC_CSS_ALIGN) {
        if (html_name(p,n,"left") || html_name(p,n,"start")) value = TIKU_DOC_ALIGN_LEFT;
        else if (html_name(p,n,"center")) value = TIKU_DOC_ALIGN_CENTER;
        else if (html_name(p,n,"right") || html_name(p,n,"end")) value = TIKU_DOC_ALIGN_RIGHT;
        else return 0;
    } else {
        if (html_name(p,n,"normal")) value = TIKU_DOC_WHITE_NORMAL;
        else if (html_name(p,n,"pre-wrap")) value = TIKU_DOC_WHITE_PRE_WRAP;
        else if (html_name(p,n,"pre")) value = TIKU_DOC_WHITE_PRE;
        else if (html_name(p,n,"nowrap")) value = TIKU_DOC_WHITE_NOWRAP;
        else return 0;
    }
    if (mask == DOC_CSS_ALIGN) d->align = value;
    else d->whitespace = value;
    return 1;
}
