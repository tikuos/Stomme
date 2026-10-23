/*
 * TikuOS Interface Kit.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_doc_table_layout.inl - styled cells and bounded spanning rows.
 * SPDX-License-Identifier: Apache-2.0
 */

/** @brief Lay one cell's text inside its resolved padding and solid border. */
static int lay_cell(lay_t *L, int index, int x, int width, int y)
{
    tiku_doc_block_t *b = &L->d->block[index];
    int room = width > 0 ? width : 1;
    int border = b->box.border_style ? box_length(L,&b->box.border,&b->font,width,4096) : 0;
    if (border > (room-1)/2) border = (room-1)/2;
    room -= border*2;
    int left = box_edge(L,box_length(L,&b->box.padding[3],&b->font,width,4096),&room);
    int right = box_edge(L,box_length(L,&b->box.padding[1],&b->font,width,4096),&room);
    int top = box_length(L,&b->box.padding[0],&b->font,width,4096);
    int bottom = box_length(L,&b->box.padding[2],&b->font,width,4096);
    b->x = x; b->y = y; b->w = width; b->border_px = border;
    b->content_x = x+border+left; b->content_w = room;
    b->line0 = L->d->nline; b->nline = 0;
    L->block = index; L->y = y+border+top;
    L->width = x+width-border-right; L->xc = b->content_x;
    if (line_open(L,L->xc)) {
        for (int k = 0; k < b->nrun; k++) {
            place_run(L,b->run0+k);
            if (L->line == TIKU_DOC_NONE && k+1 < b->nrun && !line_open(L,L->xc)) break;
        }
        line_close(L);
    }
    L->y += bottom+border;
    return L->y-y;
}

/** @brief Finish active row-spanning cell rectangles at a row's bottom. */
static void table_span_end(tiku_doc_t *d, int *active, int *ends, int row, int bottom)
{
    for (int c = 0; c < TIKU_DOC_COLS_MAX; c++) if (active[c] >= 0 && ends[c] <= row) {
        tiku_doc_block_t *b = &d->block[active[c]];
        b->h = bottom-b->y; active[c] = -1;
    }
}

/** @brief Lay a contiguous cell range with shared columns and bounded row spans. */
static int lay_table(lay_t *L, int first, int limit)
{
    tiku_doc_t *d = L->d;
    int line = face_h(TIKU_MEASURE_PLAIN), gap = line/2, pad = line/4;
    int page = L->width, last = first;
    while (last < limit && d->block[last].kind == TIKU_DOC_BLOCK_CELL) last++;
    int measure_first = first, measure_last = last, row_count = d->block[last-1].index+1;
    int parent = d->block[first].parent;
    if (parent >= 0 && (d->block[parent].flags & TIKU_DOC_BLOCK_TABLE)) {
        measure_first = parent+1; measure_last = d->block[parent].end;
        if (d->block[parent].index > row_count) row_count = d->block[parent].index;
    }
    cols_t C; cols_measure(L,measure_first,measure_last,page-L->left,gap,&C);
    for (int c = 0; c < C.n; c++) C.x[c] += L->left;
    if (first > 0) L->y += line/2;
    if (page-L->left < C.n*(line/2+1)+gap*(C.n-1)) {
        d->notices |= TIKU_DOC_TABLE_FALLBACK;
        for (int i = first; i < last; i++) {
            int height = lay_cell(L,i,L->left,page-L->left,L->y);
            d->block[i].h = height+pad; L->y += pad;
            if (d->full & TIKU_DOC_FULL_HEIGHT) {
                for (int j = i+1; j < last; j++) block_unlaid(L,j);
                break;
            }
        }
        L->width = page; L->y += line/2; return last;
    }
    int pending[TIKU_DOC_ROWSPAN_MAX] = {0};
    int active[TIKU_DOC_COLS_MAX], ends[TIKU_DOC_COLS_MAX] = {0};
    for (int c = 0; c < TIKU_DOC_COLS_MAX; c++) active[c] = -1;
    int i = first, row = d->block[first].index;
    while (i < last) {
        int top = L->y, bottom = top, j = i;
        int bot[TIKU_DOC_COLS_MAX];
        for (int c = 0; c < C.n; c++) bot[c] = top;
        while (j < last && d->block[j].index == row) {
            tiku_doc_block_t *b = &d->block[j];
            int c = b->col < C.n ? b->col : C.n-1;
            int last_col = c+(b->span ? b->span : 1)-1;
            if (last_col >= C.n) last_col = C.n-1;
            int rows = b->rowspan ? b->rowspan : 1;
            if (rows > row_count-row) rows = row_count-row;
            if (rows < 1) rows = 1;
            if (rows > TIKU_DOC_ROWSPAN_MAX) rows = TIKU_DOC_ROWSPAN_MAX;
            int height = lay_cell(L,j,C.x[c],C.x[last_col]+C.w[last_col]-C.x[c],bot[c]);
            int need = (height+pad+rows-1)/rows;
            for (int n = 0; n < rows; n++) {
                int slot = (row+n)%TIKU_DOC_ROWSPAN_MAX;
                if (pending[slot] < need) pending[slot] = need;
            }
            if (rows == 1) {
                for (int n = c; n <= last_col; n++) bot[n] = L->y;
                if (L->y > bottom) bottom = L->y;
                b->h = height+pad;
            } else {
                active[c] = j; ends[c] = row+rows-1;
                b->h = height+pad;
            }
            j++;
        }
        int slot = row%TIKU_DOC_ROWSPAN_MAX;
        int row_height = pending[slot]; pending[slot] = 0;
        if (bottom+pad-top > row_height) row_height = bottom+pad-top;
        if (row_height < 1) row_height = line+pad;
        L->y = top+row_height;
        for (int k = i; k < j; k++) if (d->block[k].rowspan <= 1)
            d->block[k].h = L->y-d->block[k].y;
        table_span_end(d,active,ends,row,L->y);
        i = j; row++;
        if (d->full & TIKU_DOC_FULL_HEIGHT) {
            while (i < last) block_unlaid(L,i++);
            break;
        }
        if (i < last && d->block[i].index > row) {
            int target = d->block[i].index;
            int remaining = TIKU_DOC_ROWSPAN_MAX;
            while (row < target && remaining-- > 0) {
                slot = row%TIKU_DOC_ROWSPAN_MAX;
                L->y += pending[slot]; pending[slot] = 0;
                table_span_end(d,active,ends,row,L->y); row++;
            }
            row = target;
        }
    }
    for (int n = 0; n < TIKU_DOC_ROWSPAN_MAX && row < row_count; n++, row++) {
        int slot = row%TIKU_DOC_ROWSPAN_MAX;
        L->y += pending[slot]; pending[slot] = 0;
        table_span_end(d,active,ends,row,L->y);
    }
    table_span_end(d,active,ends,INT32_MAX,L->y);
    L->width = page; L->y += line/2; return last;
}
