/*
 * TikuOS Interface Kit.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_doc_box_style.inl - computed box styles and native display categories.
 * SPDX-License-Identifier: Apache-2.0
 */

#define DOC_ELEMENT_TRANSIENT 1
#define DOC_ELEMENT_HIDDEN 2
#define DOC_ELEMENT_INLINE 4
#define DOC_ELEMENT_BLOCK 8
#define DOC_ELEMENT_IGNORED 16

/** @brief Whether an element has no closing scope in the document profile. */
static int css_transient(int tag, const char *name, size_t length)
{
    return tag == T_IMG || tag == T_BR || tag == T_HR ||
        html_name(name,length,"meta") || html_name(name,length,"link") ||
        html_name(name,length,"input") || html_name(name,length,"wbr") ||
        html_name(name,length,"area") || html_name(name,length,"base") ||
        html_name(name,length,"col") || html_name(name,length,"embed") ||
        html_name(name,length,"param") || html_name(name,length,"source") ||
        html_name(name,length,"track");
}

/** @brief Whether the native element separates blocks in the document profile. */
static int css_native_block(int tag)
{
    return tag == T_P || tag == T_CONTAINER || tag == T_UL || tag == T_OL ||
        tag == T_LI || tag == T_DL || tag == T_DT || tag == T_DD ||
        tag == T_TABLE || tag == T_TR || tag == T_TD || tag == T_TH ||
        tag == T_PRE || tag == T_QUOTE || tag == T_IMG || tag == T_HR ||
        tag == T_FIGURE || tag == T_CAPTION ||
        (tag >= T_H1 && tag <= T_H3) || (tag >= T_H4 && tag <= T_H6);
}

/** @brief Default noninherited box values. */
static tiku_doc_box_t css_box_default(void)
{
    tiku_doc_box_t box = {0};
    box.width.unit = box.max_width.unit = box.height.unit = TIKU_DOC_AUTO;
    box.border.value = 3*DOC_FONT_ONE;
    box.border_color = DOC_CURRENT_COLOR;
    return box;
}

/** @brief Apply noninherited box declarations, including explicit inheritance. */
static void css_box_apply(read_t *r, const doc_css_decl_t *d)
{
    const tiku_doc_box_t *parent = &r->css_stack[r->css_depth-1].box;
    r->box = css_box_default();
    r->box.border_declared = (d->mask & (DOC_CSS_BORDER_WIDTH |
        DOC_CSS_BORDER_COLOR | DOC_CSS_BORDER_STYLE)) != 0;
    r->box_mask = d->mask & (DOC_CSS_BOX_MASK & ~DOC_CSS_DISPLAY);
    for (int i = 0; i < 4; i++) {
        if (d->mask & (DOC_CSS_MARGIN<<i)) {
            tiku_doc_length_t v = d->box.margin[i];
            r->box.margin[i] = v.unit == DOC_LENGTH_INHERIT ? parent->margin[i] :
                v.unit == DOC_LENGTH_INITIAL ? (tiku_doc_length_t){0} : v;
        }
        if (d->mask & (DOC_CSS_PADDING<<i)) {
            tiku_doc_length_t v = d->box.padding[i];
            r->box.padding[i] = v.unit == DOC_LENGTH_INHERIT ? parent->padding[i] :
                v.unit == DOC_LENGTH_INITIAL ? (tiku_doc_length_t){0} : v;
        }
    }
    if (d->mask & DOC_CSS_WIDTH) {
        tiku_doc_length_t v = d->box.width;
        r->box.width = v.unit == DOC_LENGTH_INHERIT ? parent->width :
            v.unit == DOC_LENGTH_INITIAL ? r->box.width : v;
    }
    if (d->mask & DOC_CSS_MAX_WIDTH) {
        tiku_doc_length_t v = d->box.max_width;
        r->box.max_width = v.unit == DOC_LENGTH_INHERIT ? parent->max_width :
            v.unit == DOC_LENGTH_INITIAL ? r->box.max_width : v;
    }
    if (d->mask & DOC_CSS_IMAGE_HEIGHT) {
        tiku_doc_length_t v = d->box.height;
        r->box.height = v.unit == DOC_LENGTH_INHERIT ? parent->height :
            v.unit == DOC_LENGTH_INITIAL ? r->box.height : v;
    }
    if (d->mask & DOC_CSS_BACKGROUND) {
        r->box.has_background = d->box.has_background == DOC_LENGTH_INHERIT ?
            parent->has_background : d->box.has_background;
        r->box.background = d->box.has_background == DOC_LENGTH_INHERIT ?
            parent->background : d->box.background;
    }
    if (d->mask & DOC_CSS_BORDER_WIDTH) {
        tiku_doc_length_t v = d->box.border;
        r->box.border = v.unit == DOC_LENGTH_INHERIT ? parent->border :
            v.unit == DOC_LENGTH_INITIAL ? r->box.border : v;
    }
    if (d->mask & DOC_CSS_BORDER_STYLE)
        r->box.border_style = d->box.border_style == DOC_LENGTH_INHERIT ? parent->border_style : d->box.border_style;
    if (d->mask & DOC_CSS_BORDER_COLOR)
        r->box.border_color = d->box.border_color == UINT32_MAX ? parent->border_color : d->box.border_color;
    if (r->box.border_color == DOC_CURRENT_COLOR && (r->flags & TIKU_DOC_RUN_COLOR))
        r->box.border_color = r->color;
}
