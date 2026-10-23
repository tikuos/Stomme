/*
 * TikuOS Interface Kit.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_doc_length.inl - logical document length resolution.
 * SPDX-License-Identifier: Apache-2.0
 */

/** @brief Resolve a bounded length against its font, root and containing width. */
static int box_length(lay_t *L, const tiku_doc_length_t *length,
                        const tiku_doc_font_t *font, int width, int cap)
{
    int64_t unit = DOC_FONT_ONE;
    if (length->unit == TIKU_DOC_AUTO) return 0;
    if (length->unit == TIKU_DOC_EM)
        unit = (int64_t)font->scale*tiku_measure_font_size()+font->offset;
    else if (length->unit == TIKU_DOC_REM) unit *= tiku_measure_font_size();
    else if (length->unit == TIKU_DOC_PERCENT) unit = (int64_t)width*DOC_FONT_ONE/100;
    int64_t value = (unit*length->value/DOC_FONT_ONE+DOC_FONT_ONE/2)/DOC_FONT_ONE;
    if (value > cap) { L->d->notices |= TIKU_DOC_FLOW_FALLBACK; value = cap; }
    return value > 0 ? (int)value : 0;
}


/** @brief Fit one horizontal edge while reserving at least one content pixel. */
static int box_edge(lay_t *L, int wanted, int *room)
{
    if (wanted >= *room) { wanted = *room-1; L->d->notices |= TIKU_DOC_FLOW_FALLBACK; }
    if (wanted < 0) wanted = 0;
    *room -= wanted; return wanted;
}
