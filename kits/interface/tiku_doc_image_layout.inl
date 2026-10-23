/*
 * TikuOS Interface Kit.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_doc_image_layout.inl - image bounds and intrinsic aspect ratios.
 * SPDX-License-Identifier: Apache-2.0
 */

/** @brief Fit valid intrinsic dimensions to hints and available content width. */
static int image_dimensions(lay_t *L, const tiku_doc_block_t *b, int *w, int *h)
{
    if (*w < 1 || *h < 1 || *w > 32768 || *h > 32768) {
        L->d->notices |= TIKU_DOC_FLOW_FALLBACK; return 0;
    }
    const tiku_doc_image_t *im = &L->d->image[b->image];
    int available = L->width-L->left;
    if (available < 1) available = 1;
    int width = *w, height = *h;
    int wanted_w = im->width.unit == TIKU_DOC_AUTO ? 0 : box_length(L,&im->width,&b->font,available,32768);
    int wanted_h = im->height.unit == TIKU_DOC_AUTO ? 0 : box_length(L,&im->height,&b->font,available,32768);
    if (wanted_w > 0) { width = wanted_w; height = (int)((int64_t)*h*width / *w); }
    if (wanted_h > 0 && (!wanted_w || (int64_t)wanted_h* *w < (int64_t)wanted_w* *h)) {
        height = wanted_h; width = (int)((int64_t)*w*height / *h);
    }
    int limit = available;
    if (im->max_width.unit != TIKU_DOC_AUTO) {
        int maximum = box_length(L,&im->max_width,&b->font,available,32768);
        if (maximum < limit) limit = maximum;
    }
    if (limit < 1) limit = 1;
    if (width > limit) { height = (int)((int64_t)*h*limit / *w); width = limit; }
    if (height > 4096) {
        height = 4096; width = (int)((int64_t)*w*height / *h);
        L->d->notices |= TIKU_DOC_FLOW_FALLBACK;
    }
    *w = width > 0 ? width : 1; *h = height > 0 ? height : 1;
    return 1;
}
