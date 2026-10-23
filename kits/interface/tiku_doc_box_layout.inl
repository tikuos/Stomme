/*
 * TikuOS Interface Kit.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_doc_box_layout.inl - nested document box geometry.
 * SPDX-License-Identifier: Apache-2.0
 */

static void lay_range(lay_t *L, int first, int last);

/** @brief Lay a container's border, padding and descendant block range. */
static void lay_box(lay_t *L, int index, int end)
{
    tiku_doc_block_t *b = &L->d->block[index];
    int old_left = L->left, old_width = L->width;
    int available = old_width-old_left, room = available;
    if (room < 1) room = available = 1;
    int margin[4], padding[4];
    for (int i = 0; i < 4; i++) {
        margin[i] = box_length(L,&b->box.margin[i],&b->font,available,4096);
        padding[i] = box_length(L,&b->box.padding[i],&b->font,available,4096);
    }
    margin[3] = box_edge(L,margin[3],&room); margin[1] = box_edge(L,margin[1],&room);
    int border = b->box.border_style ? box_length(L,&b->box.border,&b->font,available,4096) : 0;
    if (border > (room-1)/2) { border = (room-1)/2; L->d->notices |= TIKU_DOC_FLOW_FALLBACK; }
    room -= border*2;
    padding[3] = box_edge(L,padding[3],&room); padding[1] = box_edge(L,padding[1],&room);
    int content = b->box.width.unit == TIKU_DOC_AUTO ? room :
        box_length(L,&b->box.width,&b->font,available,TIKU_DOC_HEIGHT_MAX);
    if (b->box.max_width.unit != TIKU_DOC_AUTO) {
        int maximum = box_length(L,&b->box.max_width,&b->font,available,TIKU_DOC_HEIGHT_MAX);
        if (content > maximum) content = maximum;
    }
    if (content > room) { content = room; L->d->notices |= TIKU_DOC_FLOW_FALLBACK; }
    if (content < 1) content = 1;
    int spare = room-content;
    if (b->box.margin[3].unit == TIKU_DOC_AUTO)
        margin[3] += b->box.margin[1].unit == TIKU_DOC_AUTO ? spare/2 : spare;
    b->x = old_left+margin[3]; b->y = L->y+margin[0];
    b->w = content+padding[3]+padding[1]+border*2;
    b->border_px = border; b->line0 = L->d->nline; b->nline = 0;
    L->left = b->x+border+padding[3]; L->width = L->left+content;
    b->content_x = L->left; b->content_w = content;
    L->y = b->y+border+padding[0];
    lay_range(L,index+1,end);
    L->y += padding[2]+border;
    if (L->y > TIKU_DOC_HEIGHT_MAX-8192) {
        L->y = TIKU_DOC_HEIGHT_MAX-8192; L->d->full |= TIKU_DOC_FULL_HEIGHT;
    }
    b->h = L->y-b->y;
    if (b->h < 0) b->h = 0;
    L->y += margin[2]; L->left = old_left; L->width = old_width;
}

/** @brief Lay sibling blocks and recursively enter bounded container ranges. */
static void lay_range(lay_t *L, int first, int last)
{
    for (int i = first; i < last; ) {
        tiku_doc_block_t *b = &L->d->block[i];
        if (L->y > TIKU_DOC_HEIGHT_MAX-16384) L->d->full |= TIKU_DOC_FULL_HEIGHT;
        if (L->d->full & TIKU_DOC_FULL_HEIGHT) { block_unlaid(L,i++); continue; }
        if (b->kind == TIKU_DOC_BLOCK_BOX) {
            int end = b->end > i && b->end <= last ? b->end : i+1;
            lay_box(L,i,end); i = end;
        } else if (b->kind == TIKU_DOC_BLOCK_CELL) i = lay_table(L,i,last);
        else lay_block(L,i++);
    }
}
