/*
 * TikuOS Interface Kit.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_doc_flow.inl - inherited logical text flow.
 * SPDX-License-Identifier: Apache-2.0
 */

/** @brief Apply flow declarations after the element's font is computed. */
static void css_flow_apply(read_t *r, const doc_css_decl_t *d)
{
    const tiku_doc_flow_t *parent = &r->css_stack[r->css_depth-1].flow;
    if (d->mask & DOC_CSS_ALIGN)
        r->flow.align = d->align == DOC_FONT_INHERIT ? parent->align : d->align;
    if (d->mask & DOC_CSS_WHITE)
        r->flow.whitespace = d->whitespace == DOC_FONT_INHERIT ? parent->whitespace : d->whitespace;
    if ((d->mask & DOC_CSS_HEIGHT) && d->height_unit != DOC_SIZE_INHERIT) {
        r->flow.scale = r->flow.offset = 0;
        r->flow.height = TIKU_DOC_HEIGHT_LENGTH;
        if (d->height_unit == DOC_HEIGHT_NORMAL) r->flow.height = TIKU_DOC_HEIGHT_NORMAL;
        else if (d->height_unit == DOC_HEIGHT_NUMBER) {
            r->flow.height = TIKU_DOC_HEIGHT_FACTOR; r->flow.scale = d->height;
        } else if (d->height_unit == DOC_SIZE_PX) r->flow.offset = d->height;
        else if (d->height_unit == DOC_SIZE_REM) r->flow.scale = d->height;
        else {
            r->flow.scale = css_font_bound(r,(int64_t)r->font.scale*d->height/DOC_FONT_ONE,
                                           64*DOC_FONT_ONE);
            r->flow.offset = css_font_bound(r,(int64_t)r->font.offset*d->height/DOC_FONT_ONE,
                                            512*DOC_FONT_ONE);
        }
    }
}
