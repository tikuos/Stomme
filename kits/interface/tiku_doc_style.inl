/*
 * TikuOS Interface Kit.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_doc_style.inl - inherited HTML text styles and element boundaries.
 * SPDX-License-Identifier: Apache-2.0
 */

/** @brief Restore the style preceding one open element and discard its children. */
static void css_restore(read_t *r, int index)
{
    for (int i = r->css_depth-1; i >= index; i--)
        if (r->css_stack[i].box_id >= 0) box_end(r,r->css_stack[i].box_id);
    unsigned flags = r->css_stack[index].flags;
    uint32_t color = r->css_stack[index].color;
    if ((r->flags & DOC_CSS_FLAGS) != flags || r->color != color ||
        memcmp(&r->font,&r->css_stack[index].font,sizeof r->font) ||
        memcmp(&r->flow,&r->css_stack[index].flow,sizeof r->flow) ||
        r->background != r->css_stack[index].background) run_close(r);
    r->flags = (int)((r->flags & ~DOC_CSS_FLAGS) | flags);
    r->color = color; r->css_depth = index;
    r->font = r->css_stack[index].font;
    r->flow = r->css_stack[index].flow;
    r->box = r->css_stack[index].box;
    r->background = r->css_stack[index].background;
    r->hidden = r->css_stack[index].hidden;
    r->display = r->css_stack[index].display;
    r->box_mask = r->css_stack[index].box_mask;
}

/** @brief Close an element's CSS scope within its list or definition container. */
static void css_implied(read_t *r, const char *name, int list)
{
    for (int i = r->css_depth-1; i >= 0; i--) {
        const char *open = r->css_stack[i].name;
        size_t n = r->css_stack[i].length;
        if (list == 1 && (html_name(open,n,"ul") || html_name(open,n,"ol"))) break;
        if (list == 2 && html_name(open,n,"dl")) break;
        if (list == 3 && (html_name(open,n,"tr") || html_name(open,n,"table"))) break;
        if (list == 4 && html_name(open,n,"table")) break;
        if (html_name(open,n,name) || (list == 2 &&
            (html_name(open,n,"dt") || html_name(open,n,"dd"))) ||
            (list == 3 && (html_name(open,n,"td") || html_name(open,n,"th")))) {
            css_restore(r,i); break;
        }
    }
}

/** @brief Close a heading's text style before a following heading opens. */
static void css_heading_end(read_t *r)
{
    for (int i = r->css_depth-1; i >= 0; i--) {
        const char *name = r->css_stack[i].name;
        if (r->css_stack[i].length == 2 && (name[0] == 'h' || name[0] == 'H') &&
            name[1] >= '1' && name[1] <= '6') {
            css_restore(r,i); break;
        }
    }
}

/** @brief Apply an element's cascade and keep its inherited parent state. */
static int css_element(read_t *r, int tag, int close, const char *name,
                         size_t length, const char *attrs)
{
    if (tag == T_SKIP) return 0;
    if (r->css_overflow || (!close && r->css_depth == TIKU_DOC_DEPTH_MAX)) {
        r->d->full |= TIKU_DOC_FULL_DEPTH;
        if (close) r->css_overflow--;
        else if (!css_transient(tag,name,length) && r->css_overflow < INT32_MAX) r->css_overflow++;
        return DOC_ELEMENT_IGNORED;
    }
    if (close) {
        for (int i = r->css_depth-1; i >= 0; i--)
            if (html_same(name,length,r->css_stack[i].name,r->css_stack[i].length)) {
                int result = r->css_stack[i].element_flags;
                css_restore(r,i); return result & ~DOC_ELEMENT_TRANSIENT;
            }
        return 0;
    }
    doc_css_decl_t d = css_resolve(&r->css,name,length,attrs);
    int hidden = r->hidden || ((d.mask & DOC_CSS_DISPLAY) && d.display == DOC_DISPLAY_NONE);
    if (!hidden) {
    if (tag == T_P || tag == T_CONTAINER || tag == T_UL || tag == T_OL ||
        tag == T_DL || tag == T_TABLE || tag == T_PRE || tag == T_QUOTE ||
        tag == T_FIGURE || tag == T_CAPTION ||
        (tag >= T_H1 && tag <= T_H3) || (tag >= T_H4 && tag <= T_H6))
        css_implied(r,"p",0);
    if (tag == T_LI) css_implied(r,"li",1);
    if (tag == T_DT || tag == T_DD) css_implied(r,"dt",2);
    if (tag == T_TD || tag == T_TH) css_implied(r,"td",3);
    if (tag == T_TR) css_implied(r,"tr",4);
    if ((tag >= T_H1 && tag <= T_H3) || (tag >= T_H4 && tag <= T_H6))
        css_heading_end(r);
    }
    if (r->css_depth == TIKU_DOC_DEPTH_MAX) {
        r->d->full |= TIKU_DOC_FULL_DEPTH; return 0;
    }
    int at = r->css_depth++;
    r->css_stack[at].name = name; r->css_stack[at].length = length;
    r->css_stack[at].color = r->color;
    r->css_stack[at].flags = r->flags & DOC_CSS_FLAGS;
    r->css_stack[at].font = r->font;
    r->css_stack[at].flow = r->flow;
    r->css_stack[at].box = r->box;
    r->css_stack[at].background = r->background;
    r->css_stack[at].hidden = r->hidden;
    r->css_stack[at].display = r->display;
    r->css_stack[at].box_mask = r->box_mask;
    r->css_stack[at].box_id = TIKU_DOC_NONE;
    r->hidden = hidden;
    if (tag == T_PRE) r->flow.whitespace = TIKU_DOC_WHITE_PRE_WRAP;
    css_font_defaults(r,tag);
    unsigned flags = r->flags & DOC_CSS_FLAGS;
    if (tag == T_A) flags &= ~TIKU_DOC_RUN_LINK_PLAIN;
    uint32_t color = r->color;
    unsigned parent_decoration = flags & (TIKU_DOC_RUN_UNDERLINE | TIKU_DOC_RUN_STRIKE);
    if (tag == T_U) flags |= TIKU_DOC_RUN_UNDERLINE;
    if (tag == T_S) flags |= TIKU_DOC_RUN_STRIKE;
    css_font_apply(r,&d);
    css_flow_apply(r,&d);
    flags &= ~TIKU_DOC_RUN_PRE;
    if (r->flow.whitespace == TIKU_DOC_WHITE_PRE ||
        r->flow.whitespace == TIKU_DOC_WHITE_PRE_WRAP) flags |= TIKU_DOC_RUN_PRE;
    if (d.mask & DOC_CSS_COLOR) {
        if (d.color_mode == 0) { flags |= TIKU_DOC_RUN_COLOR; color = d.color; }
        else if (d.color_mode == 2) flags &= ~TIKU_DOC_RUN_COLOR;
    }
    if (d.mask & DOC_CSS_DECORATION) {
        flags &= ~(TIKU_DOC_RUN_UNDERLINE | TIKU_DOC_RUN_STRIKE);
        flags |= parent_decoration | d.decoration;
        if (tag == T_A) flags |= TIKU_DOC_RUN_LINK_PLAIN;
    }
    uint32_t background = r->background;
    if ((d.mask & DOC_CSS_BACKGROUND) && d.box.has_background == 1) {
        flags |= TIKU_DOC_RUN_BACKGROUND; background = d.box.background;
    }
    if (flags != (r->flags & DOC_CSS_FLAGS) || color != r->color ||
        memcmp(&r->font,&r->css_stack[at].font,sizeof r->font) ||
        memcmp(&r->flow,&r->css_stack[at].flow,sizeof r->flow) ||
        background != r->background) run_close(r);
    r->flags = (int)((r->flags & ~DOC_CSS_FLAGS) | flags); r->color = color;
    r->background = background;
    css_box_apply(r,&d);
    if ((tag == T_TD || tag == T_TH) && !r->box.has_background &&
        (r->flags & TIKU_DOC_RUN_BACKGROUND)) {
        r->box.has_background = 1; r->box.background = r->background;
    }
    if ((d.mask & DOC_CSS_IMAGE_HEIGHT) && tag != T_IMG)
        r->css.notices |= TIKU_DOC_CSS_UNSUPPORTED;
    if (tag == T_IMG && (d.mask & (DOC_CSS_MARGIN*15u | DOC_CSS_PADDING*15u |
        DOC_CSS_BORDER_WIDTH | DOC_CSS_BORDER_COLOR | DOC_CSS_BORDER_STYLE)))
        r->css.notices |= TIKU_DOC_CSS_UNSUPPORTED;
    r->display = css_native_block(tag) ? DOC_DISPLAY_BLOCK : DOC_DISPLAY_INLINE;
    int result = 0;
    if (d.mask & DOC_CSS_DISPLAY) {
        r->display = d.display == DOC_LENGTH_INHERIT ? r->css_stack[at].display : d.display;
        r->font.specified |= DOC_CSS_SIZE | DOC_CSS_FAMILY | DOC_CSS_WEIGHT | DOC_CSS_ITALIC;
        if (r->display == DOC_DISPLAY_INLINE &&
            ((tag >= T_H1 && tag <= T_H3) || (tag >= T_H4 && tag <= T_H6) ||
             tag == T_TABLE || tag == T_TR || tag == T_TH || tag == T_TD ||
             tag == T_IMG || tag == T_HR)) {
            r->display = DOC_DISPLAY_BLOCK; r->css.notices |= TIKU_DOC_CSS_UNSUPPORTED;
        }
        if (r->display == DOC_DISPLAY_INLINE) result |= DOC_ELEMENT_INLINE;
        if (r->display == DOC_DISPLAY_BLOCK) result |= DOC_ELEMENT_BLOCK;
    }
    if (hidden) result |= DOC_ELEMENT_HIDDEN;
    if (!css_native_block(tag) && !(result & DOC_ELEMENT_BLOCK) &&
        (r->box_mask & ~DOC_CSS_BACKGROUND)) r->css.notices |= TIKU_DOC_CSS_UNSUPPORTED;
    if (css_transient(tag,name,length)) result |= DOC_ELEMENT_TRANSIENT;
    r->css_stack[at].element_flags = result;
    return result;
}
