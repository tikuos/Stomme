/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_office_page.c - paragraphs placed on pages.
 *
 * Lines are placed one at a time where the page is: beside the floats on
 * it, above its footnotes.  A line that does not fit begins the next
 * column or page.  A paragraph with widow control leaves no line alone at
 * either end of a page, one kept together moves whole, and a heading kept
 * with what follows goes over with it.  A page's footnotes are placed when
 * it closes; headers and footers are placed last, once the pages are
 * counted.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_office_pager.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NOTE_RULE  12               /* room for the rule above the notes */
#define MIN_SPAN   36               /* a line beside a float, at least   */
#define KEEP_CHAIN 6                /* paragraphs one keep chain holds   */

/** @brief A line placed on this page, remembered so it can be taken back
 *         when the page breaks badly. */
typedef struct {
    int   frag, from, places, notes, clear;   /* counts before it        */
    float y, notes_h;
} pg_back_t;

/* ----------------------------------------------------------- fragments  */

void
pg_move(of_vec_t *frags, int from, float dx, float dy)
{
    of_frag_t *f = frags->p;
    int i;

    for (i = from; i < frags->n; i++) {
        f[i].x += dx;
        f[i].y += dy;
    }
}

void
pg_under(of_vec_t *frags, int at, int from)
{
    of_frag_t *f = frags->p, *tmp;
    int n = frags->n - from;

    if (n <= 0 || from <= at || (tmp = malloc((size_t)n * sizeof *tmp))
        == NULL) {
        return;
    }
    memcpy(tmp, f + from, (size_t)n * sizeof *tmp);
    memmove(f + at + n, f + at, (size_t)(from - at) * sizeof *tmp);
    memcpy(f + at, tmp, (size_t)n * sizeof *tmp);
    free(tmp);
}

int
pg_rule(of_vec_t *frags, float x, float y, float w, float h,
        const of_border_t *b)
{
    of_frag_t *f;
    int k, n = b->style == OF_LS_DOUBLE ? 2 : 1;

    for (k = 0; b->width > 0 && k < n; k++) {
        float off = n == 2 ? (k ? 1 : -1) * b->width : 0;

        if ((f = of_vec_push(frags)) == NULL) {
            return -1;
        }
        f->kind = OF_FRAG_RULE;
        f->x = x + (h != 0 ? off : 0);
        f->y = y + (w != 0 ? off : 0);
        f->w = w;
        f->h = h;
        f->size = b->width / (float)n;
        f->color = b->color ? b->color : 0xff000000u;
        f->dash = b->style == OF_LS_DOUBLE ? OF_LS_SINGLE : b->style;
    }
    return 0;
}

int
pg_box(of_vec_t *frags, float x, float y, float w, float h, uint32_t color)
{
    of_frag_t *f;

    if (color == 0 || w <= 0 || h <= 0) {
        return 0;
    }
    if ((f = of_vec_push(frags)) == NULL) {
        return -1;
    }
    f->kind = OF_FRAG_RECT;
    f->x = x;
    f->y = y;
    f->w = w;
    f->h = h;
    f->color = color;
    return 0;
}

float
pg_limit(const of_pager_t *pg)
{
    return pg->bottom - pg->notes_h;
}

/* --------------------------------------------------------------- pages  */

/** @brief The section's column geometry, from @p top down. */
static void
columns(of_pager_t *pg, float top)
{
    const of_section_t *s = pg->sec;
    float usable = s->w - s->left - s->right;

    pg->ncol = s->cols > 1 && s->cols < 16 ? s->cols : 1;
    pg->gap = pg->ncol > 1 ? s->gap : 0;
    pg->width = (usable - pg->gap * (float)(pg->ncol - 1)) /
                (float)pg->ncol;
    if (pg->width < 36) {
        pg->width = usable > 36 ? usable : 36;
        pg->ncol = 1;
    }
    pg->col = 0;
    pg->x = s->left;
    pg->top = pg->y = top;
}

/** @brief A note's height, laid out once at the body's width. */
static float
note_height(of_pager_t *pg, int n)
{
    of_vec_t tmp;
    of_arena_t scratch = { NULL, 0 };
    of_pageno_t no = { pg->num, 0, pg->sec->num };
    float w = pg->sec->w - pg->sec->left - pg->sec->right;

    if (pg->note_h == NULL || n < 0 || n >= pg->d->nnote ||
        pg->d->note[n] == NULL) {
        return 0;
    }
    if (pg->note_h[n] < 0) {
        of_vec_init(&tmp, sizeof(of_frag_t));
        pg->note_h[n] = of_layout_story(pg->d, pg->fs, pg->d->note[n], w,
                                        &no, &tmp, &scratch);
        of_vec_free(&tmp);
        of_arena_free(&scratch);
        if (pg->note_h[n] < 0) {
            pg->note_h[n] = 0;
        }
    }
    return pg->note_h[n];
}

/** @brief The page's notes, under a short rule, above its bottom. */
static int
place_notes(of_pager_t *pg)
{
    const of_section_t *s = pg->sec;
    of_pageno_t no = { pg->num, 0, s->num };
    of_border_t rule = { 0.5f, 0, 0, OF_LS_SINGLE };
    float y = pg->bottom - pg->notes_h, w = s->w - s->left - s->right;
    int i, *n = pg->notes.p;

    if (pg->notes.n == 0) {
        return 0;
    }
    if (pg_rule(pg->frags, s->left, y + NOTE_RULE / 2, 144, 0, &rule)) {
        return -1;
    }
    y += NOTE_RULE;
    for (i = 0; i < pg->notes.n; i++) {
        int from = pg->frags->n;
        float h = of_layout_story(pg->d, pg->fs, pg->d->note[n[i]], w, &no,
                                  pg->frags, pg->a);

        if (h < 0) {
            return -1;
        }
        pg_move(pg->frags, from, s->left, y);
        y += h;
    }
    return 0;
}

/** @brief The open page finished: its notes placed, its fragments kept. */
static int
page_close(of_pager_t *pg)
{
    of_page_t *p;

    if (pg->lay == NULL || pg->page < 0) {
        return 0;
    }
    if (place_notes(pg) != 0) {
        return -1;
    }
    p = &pg->lay->page[pg->page];
    p->frag = of_vec_freeze(pg->frags, pg->a, &p->nfrag);
    pg->notes.n = 0;
    pg->notes_h = 0;
    pg->clear.n = 0;
    return pg->a->bad ? -1 : 0;
}

int
pg_new_page(of_pager_t *pg)
{
    of_page_t *p;
    pg_info_t *info;

    if (pg->lay == NULL) {
        return 0;
    }
    if (page_close(pg) != 0 || pg->lay->npage >= OF_PAGES_MAX) {
        pg->failed = 1;
        return -1;
    }
    if (pg->lay->npage == pg->pagecap) {
        pg->pagecap = pg->pagecap ? pg->pagecap * 2 : 16;
        p = realloc(pg->lay->page, (size_t)pg->pagecap * sizeof *p);
        if (p == NULL) {
            pg->failed = 1;
            return -1;
        }
        pg->lay->page = p;
    }
    if ((info = of_vec_push(&pg->info)) == NULL) {
        pg->failed = 1;
        return -1;
    }
    p = &pg->lay->page[pg->lay->npage];
    memset(p, 0, sizeof *p);
    p->w = pg->sec->w;
    p->h = pg->sec->h;
    pg->page = pg->lay->npage++;
    pg->num++;
    info->sect = pg->nsect;
    info->num = pg->num;
    info->first = pg->content < 0;  /* -1 from section_begin()         */
    columns(pg, pg->page_top);
    pg->content = 0;
    pg->after = 0;
    return 0;
}

int
pg_next_column(of_pager_t *pg)
{
    if (pg->lay == NULL) {
        return 0;
    }
    if (pg->col + 1 < pg->ncol) {
        pg->col++;
        pg->x = pg->sec->left + (float)pg->col * (pg->width + pg->gap);
        pg->y = pg->top;
        pg->after = 0;
        return 0;
    }
    return pg_new_page(pg);
}

/* -------------------------------------------------------------- floats  */

/** @brief The part of the column a line from @p y, @p h tall, may take
 *         beside the page's floats, into @p x0 and @p x1 (the column's
 *         own measure).  @return 0, or the y below a float the line must
 *         go down to. */
static float
span(const of_pager_t *pg, float y, float h, float *x0, float *x1)
{
    const pg_clear_t *c = pg->clear.p;
    float below = 0;
    int i, hit = 0;

    *x0 = 0;
    *x1 = pg->width;
    for (i = 0; i < pg->clear.n; i++) {
        float a = c[i].x0 - pg->x, b = c[i].x1 - pg->x;

        if (c[i].y1 <= y || c[i].y0 >= y + h || b <= *x0 || a >= *x1) {
            continue;
        }
        if (c[i].wrap == OF_WRAP_BLOCK) {
            return c[i].y1;
        }
        if (*x1 - b >= a - *x0) {
            *x0 = b;
        } else {
            *x1 = a;
        }
        below = below == 0 || c[i].y1 < below ? c[i].y1 : below;
        hit = 1;
    }
    if (hit && *x1 - *x0 < MIN_SPAN) {
        *x0 = 0;
        *x1 = pg->width;
        return below > y ? below : y + h;
    }
    return 0;
}

/** @brief What a float's place counts from, along one side: @p rel of
 *         a page @p size long with margins @p m0 and @p m1, the column or
 *         paragraph at @p at, @p len long; into @p r0 and @p r1. */
static void
reference(int rel, float size, float m0, float m1, float at, float len,
          float *r0, float *r1)
{
    *r0 = rel == OF_REL_PAGE || rel == OF_REL_BEFORE ? 0 :
          rel == OF_REL_MARGIN ? m0 : rel == OF_REL_AFTER ? size - m1 : at;
    *r1 = rel == OF_REL_PAGE || rel == OF_REL_AFTER ? size :
          rel == OF_REL_MARGIN ? size - m1 : rel == OF_REL_BEFORE ? m0
                                                             : at + len;
}

/** @brief Where float @p fl goes: as its offsets or alignment say, from
 *         the page, the margins, or the column and paragraph at @p py. */
static void
float_box(const of_pager_t *pg, const of_float_t *fl, float py, float *x,
          float *y)
{
    const of_section_t *s = pg->sec;
    float rx0 = 0, rx1 = pg->width, ry0 = py, ry1 = py + fl->h;
    float ox, oy;

    if (pg->lay != NULL) {          /* on a page, not in a box */
        reference(fl->rel_x, s->w, s->left, s->right, pg->x, pg->width,
                  &rx0, &rx1);
        if (fl->rel_y != OF_REL_PARA && fl->rel_y != OF_REL_COLUMN) {
            reference(fl->rel_y, s->h, s->top, s->bottom, 0, 0, &ry0, &ry1);
        }
    }
    ox = fl->pct_x ? fl->x * (rx1 - rx0) : fl->x;
    oy = fl->pct_y ? fl->y * (ry1 - ry0) : fl->y;
    *x = fl->align_x == 1 ? rx0 : fl->align_x == 2 ? (rx0 + rx1 - fl->w) / 2
         : fl->align_x == 3 ? rx1 - fl->w : rx0 + ox;
    *y = fl->align_y == 1 ? ry0 : fl->align_y == 2 ? (ry0 + ry1 - fl->h) / 2
         : fl->align_y == 3 ? ry1 - fl->h : ry0 + oy;
}

/** @brief Story @p s set @p w wide with its top left at (@p x, @p y),
 *         down @p h as @p anchor says: 0 at the top, 1 in the middle, 2
 *         at the bottom.  @return 0, or -1 out of memory. */
static int
box_text(of_pager_t *pg, const of_story_t *s, float x, float y, float w,
         float h, int anchor)
{
    of_pageno_t no = { pg->num, 0, pg->sec ? pg->sec->num : 0 };
    int from = pg->frags->n;
    float used;

    if (s == NULL || pg->depth >= OF_NEST_MAX) {
        return 0;
    }
    used = of_layout_story(pg->d, pg->fs, s, w > 1 ? w : 1,
                           pg->no ? pg->no : &no, pg->frags, pg->a);
    if (used < 0) {
        return -1;
    }
    y += anchor == 1 && used < h ? (h - used) / 2 :
         anchor == 2 && used < h ? h - used : 0;
    pg_move(pg->frags, from, x, y);
    return 0;
}

/** @brief Float @p fl drawn, its paragraph at @p py: a picture, a text
 *         box's ground, outline and text, or a drawing and the text in
 *         its boxes; under the page's text when it lies behind.  Text is
 *         kept clear of it as its wrap says. */
static int
place_float(of_pager_t *pg, const of_float_t *fl, float py)
{
    int at = pg->frags->n, i;
    of_frag_t *f;
    float x, y;
    pg_clear_t *c;

    float_box(pg, fl, py, &x, &y);
    if ((f = of_vec_push(pg->frags)) == NULL) {
        return -1;
    }
    f->kind = fl->art ? OF_FRAG_ART : fl->image ? OF_FRAG_IMAGE
                                                : OF_FRAG_BOX;
    f->x = x;
    f->y = y;
    f->w = fl->w;
    f->h = fl->h;
    f->image = fl->image;
    f->flt = (of_float_t *)fl;
    if (box_text(pg, fl->text, x + fl->inset[1], y + fl->inset[0],
                 fl->w - fl->inset[1] - fl->inset[3], 0, 0) != 0) {
        return -1;
    }
    for (i = 0; i < fl->nbox; i++) {
        const of_textbox_t *b = &fl->box[i];

        if (box_text(pg, b->text, x + b->x, y + b->y, b->w, b->h,
                     b->anchor) != 0) {
            return -1;
        }
    }
    if (fl->behind) {
        pg_under(pg->frags, 0, at);
    }
    if (fl->wrap != OF_WRAP_NONE && (c = of_vec_push(&pg->clear))) {
        c->x0 = x - fl->gap;
        c->y0 = y - fl->gap;
        c->x1 = x + fl->w + fl->gap;
        c->y1 = y + fl->h + fl->gap;
        c->wrap = fl->wrap;
    }
    return 0;
}

/* --------------------------------------------------------------- lines  */

/** @brief The notes line atoms @p from to @p to refer to, into @p list,
 *         @p max at most, their count in @p n.  @return the height they
 *         need, the rule above them too when they are the page's first. */
static float
line_notes(of_pager_t *pg, const of_flow_t *f, int from, int to, int *list,
           int *n, int max)
{
    const of_run_t *last = NULL;
    float h = 0;
    int i;

    *n = 0;
    for (i = from; i < to && pg->lay != NULL; i++) {
        const of_atom_t *a = &f->atom[i];

        if (a->kind != OA_NOTE || a->run == last || *n >= max) {
            continue;
        }
        last = a->run;
        list[(*n)++] = a->run->n;
        h += note_height(pg, a->run->n);
    }
    return h > 0 && pg->notes.n == 0 ? h + NOTE_RULE : h;
}

/** @brief The plain text of @p p's runs, for an outline entry. */
static const char *
para_text(of_arena_t *a, const of_para_t *p)
{
    char buf[256];
    size_t k = 0;
    int i;

    buf[0] = '\0';
    for (i = 0; i < p->nrun && k + 1 < sizeof buf; i++) {
        if (p->run[i].kind == OR_TEXT && p->run[i].text != NULL) {
            k += (size_t)snprintf(buf + k, sizeof buf - k, "%s",
                                  p->run[i].text);
        }
    }
    if (k >= sizeof buf) {
        k = sizeof buf - 1;
        while (k > 0 && ((unsigned char)buf[k] & 0xc0) == 0x80) {
            k--;                    /* not half a character */
        }
        buf[k] = '\0';
    }
    return k > 0 ? of_strdup(a, buf) : NULL;
}

/** @brief A place noted where a line lands: a bookmark or a heading. */
static void
note_place(of_pager_t *pg, const char *name, int level, float y)
{
    of_place_t *pl;

    if (pg->lay != NULL && name != NULL &&
        (pl = of_vec_push(&pg->places)) != NULL) {
        pl->name = name;
        pl->level = level;
        pl->page = pg->page;
        pl->y = y;
    }
}

/** @brief The floats of flow @p f placed with its top at the page's y,
 *         before its first line is: the text is set around them.
 *  @return 0, or -1 out of memory. */
static int
place_floats(of_pager_t *pg, const of_flow_t *f)
{
    int i;

    for (i = 0; i < f->natom; i++) {
        const of_atom_t *a = &f->atom[i];

        if (a->kind == OA_FLOAT && a->run->flt != NULL &&
            place_float(pg, a->run->flt, pg->y) != 0) {
            return -1;
        }
    }
    return 0;
}

/** @brief A line laid out by of_flow_line() kept at the column's x and
 *         the pager's y: its fragments moved there, its heading, bookmarks
 *         and notes noted.  @return 0, or -1 out of memory. */
static int
commit(of_pager_t *pg, const of_flow_t *f, int from, const of_line_t *l,
       int first)
{
    int i, list[32], n = 0, *slot;

    pg_move(pg->frags, l->frag, pg->x, pg->y);
    if (first && f->para->ps.outline >= 0 && f->para->ps.outline < 9) {
        note_place(pg, para_text(pg->a, f->para), f->para->ps.outline,
                   pg->y);
    }
    pg->notes_h += line_notes(pg, f, from, l->next, list, &n, 32);
    for (i = 0; i < n; i++) {
        if ((slot = of_vec_push(&pg->notes)) == NULL) {
            return -1;
        }
        *slot = list[i];
    }
    for (i = from; i < l->next; i++) {
        const of_atom_t *a = &f->atom[i];

        if (a->kind == OA_MARK && a->run->text != NULL) {
            note_place(pg, a->run->text, -1, pg->y);
        }
    }
    pg->y += l->h;
    pg->content = 1;
    return 0;
}

/** @brief How many lines, up to four, the paragraph has from atom
 *         @p from at this column's width.  Laid out and thrown away. */
static int
lines_left(of_pager_t *pg, of_flow_t *f, int from, int first)
{
    of_vec_t tmp;
    of_arena_t scratch = { NULL, 0 };
    of_line_t l;
    int n = 0;

    of_vec_init(&tmp, sizeof(of_frag_t));
    while (from < f->natom && n < 4) {
        if (of_flow_line(pg->d, pg->fs, f, from, first, pg->width, 0,
                         pg->width, &tmp, &scratch, &l) != 0 ||
            l.next <= from) {
            break;
        }
        from = l.next;
        first = 0;
        n++;
        tmp.n = 0;
    }
    of_vec_free(&tmp);
    of_arena_free(&scratch);
    return n;
}

/** @brief Lines placed on this page taken back to @p b: fragments,
 *         places, notes and floats. */
static void
take_back(of_pager_t *pg, const pg_back_t *b)
{
    pg->frags->n = b->frag;
    pg->places.n = b->places;
    pg->notes.n = b->notes;
    pg->notes_h = b->notes_h;
    pg->clear.n = b->clear;
    pg->y = b->y;
}

/** @brief The paragraph's shading and borders around its lines on this
 *         page, from @p y0 to @p y1, under the lines from @p at. */
static int
para_box(of_pager_t *pg, const of_pstyle_t *ps, int at, float y0, float y1)
{
    const of_border_t *b = ps->border;
    float x0 = pg->x + ps->left - b[1].space - b[1].width;
    float x1 = pg->x + pg->width - ps->right + b[3].space + b[3].width;
    int from = pg->frags->n, err = 0;

    if (y1 <= y0 + 0.5f || (ps->fill == 0 && b[0].width <= 0 &&
        b[1].width <= 0 && b[2].width <= 0 && b[3].width <= 0)) {
        return 0;
    }
    y0 -= b[0].space + b[0].width;
    y1 += b[2].space + b[2].width;
    err |= pg_box(pg->frags, x0, y0, x1 - x0, y1 - y0, ps->fill);
    err |= pg_rule(pg->frags, x0, y0, x1 - x0, 0, &b[0]);
    err |= pg_rule(pg->frags, x0, y1, x1 - x0, 0, &b[2]);
    err |= pg_rule(pg->frags, x0, y0, 0, y1 - y0, &b[1]);
    err |= pg_rule(pg->frags, x1, y0, 0, y1 - y0, &b[3]);
    pg_under(pg->frags, at, from);
    return err;
}

/** @brief What to do with a line that does not fit, @p back holding the
 *         lines of the paragraph already on this page: go on at the next
 *         page, or take back lines so none is left alone.  @return the
 *         atom to go on from. */
static int
bad_break(of_pager_t *pg, of_flow_t *f, int from, int first,
          of_vec_t *back, int began_here)
{
    const of_pstyle_t *ps = &f->para->ps;
    pg_back_t *b = back->p;
    int placed = back->n;

    if (placed > 0 && began_here && (ps->keep_lines ||
        (ps->widows && placed == 1))) {
        take_back(pg, &b[0]);           /* all of it goes over */
        return b[0].from;
    }
    if (ps->widows && placed >= 2 && lines_left(pg, f, from, first) == 1) {
        take_back(pg, &b[placed - 1]);  /* the last line goes with it */
        return b[placed - 1].from;
    }
    return from;
}

/** @brief Every line of flow @p f placed, from where the page is,
 *         breaking the page as a line needs; its floats first, where its
 *         first line on a page goes.  @return 0, or -1. */
static int
place_lines(of_pager_t *pg, of_flow_t *f, int *box_at, float *box_y)
{
    of_vec_t back;
    of_line_t l;
    int from = 0, first = 1, began_here = pg->content, err = 0, guard = 0;
    float x0, x1, skip;

    of_vec_init(&back, sizeof(pg_back_t));
    memset(&l, 0, sizeof l);
    while (err == 0 && !pg->failed && (from < f->natom || first) &&
           guard++ < 200000) {
        int mark = pg->frags->n, clear = pg->clear.n, n = 0, list[32];
        float need, h = l.h > 0 && !first ? l.h : f->mark_asc + f->mark_desc;
        pg_back_t *b;

        if (first && from == 0 && place_floats(pg, f) != 0) {
            err = -1;
            break;
        }
        while ((skip = span(pg, pg->y, h, &x0, &x1)) > pg->y &&
               (pg->lay == NULL || skip < pg_limit(pg))) {
            pg->y = skip;
        }
        if (of_flow_line(pg->d, pg->fs, f, from, first, pg->width, x0, x1,
                         pg->frags, pg->a, &l) != 0) {
            err = -1;
            break;
        }
        need = line_notes(pg, f, from, l.next, list, &n, 32);
        if (pg->lay != NULL && pg->content && pg->y + l.h + need >
            pg_limit(pg) + 0.01f) {
            pg->frags->n = mark;
            pg->clear.n = clear;
            from = bad_break(pg, f, from, first, &back, began_here);
            first = from == 0;
            err = para_box(pg, &f->para->ps, *box_at, *box_y, pg->y);
            back.n = 0;
            began_here = 0;
            err |= pg_next_column(pg);
            *box_at = pg->frags->n;
            *box_y = pg->y;
            continue;
        }
        if ((b = of_vec_push(&back)) == NULL) {
            err = -1;
            break;
        }
        b->frag = mark;
        b->from = from;
        b->places = pg->places.n;
        b->notes = pg->notes.n;
        b->notes_h = pg->notes_h;
        b->clear = clear;
        b->y = pg->y;
        err = commit(pg, f, from, &l, first);
        if (l.next <= from && !first) {
            break;                  /* no progress: nothing more to lay */
        }
        from = l.next;
        first = 0;
        if (l.ends == OA_PAGE || l.ends == OA_COLUMN) {
            err |= para_box(pg, &f->para->ps, *box_at, *box_y, pg->y);
            err |= l.ends == OA_PAGE ? pg_new_page(pg) : pg_next_column(pg);
            back.n = 0;
            *box_at = pg->frags->n;
            *box_y = pg->y;
        }
    }
    of_vec_free(&back);
    return err;
}

/** @brief Space above a paragraph: the last one's after and this one's
 *         before, each dropped between two of a style that says so; the
 *         last one's after is dropped at the top of a page. */
static void
space_before(of_pager_t *pg, const of_pstyle_t *ps)
{
    float before = ps->before;

    if (pg->style != NULL && ps->style != NULL &&
        strcmp(pg->style, ps->style) == 0) {
        if (ps->contextual) {
            before = 0;
        }
        if (pg->contextual) {
            pg->after = 0;
        }
    }
    if (pg->content || pg->lay == NULL || pg->y > pg->top) {
        pg->y += pg->after + before;
    } else {
        pg->y += before;
    }
}

int
pg_para(of_pager_t *pg, const of_para_t *p)
{
    of_flow_t f;
    of_pageno_t no = { pg->num > 0 ? pg->num : 1, 0,
                       pg->sec ? pg->sec->num : 0 };
    int box_at, err;
    float box_y;

    if (pg->lay != NULL && p->ps.break_before && pg->content &&
        pg_new_page(pg) != 0) {
        return -1;
    }
    space_before(pg, &p->ps);
    if (of_flow_build(pg->fs, p, pg->no ? pg->no : &no, &f) != 0) {
        of_flow_free(&f);
        return -1;
    }
    box_at = pg->frags->n;
    box_y = pg->y;
    err = place_lines(pg, &f, &box_at, &box_y);
    if (err == 0) {
        err = para_box(pg, &p->ps, box_at, box_y, pg->y);
    }
    of_flow_free(&f);
    pg->after = p->ps.after;
    pg->style = p->ps.style;
    pg->contextual = p->ps.contextual;
    return err;
}

/* -------------------------------------------------------------- stories  */

/** @brief How tall paragraph @p p is, all its lines or its first @p max,
 *         in this column: the space above it included, and the space below
 *         when all its lines are counted. */
static float
para_height(of_pager_t *pg, const of_para_t *p, int max)
{
    of_vec_t tmp;
    of_arena_t scratch = { NULL, 0 };
    of_pageno_t no = { 1, 1, 0 };
    of_flow_t f;
    of_line_t l;
    float h = p->ps.before;
    int from = 0, first = 1, n = 0;

    of_vec_init(&tmp, sizeof(of_frag_t));
    if (of_flow_build(pg->fs, p, &no, &f) == 0) {
        while ((from < f.natom || first) && (max == 0 || n < max) &&
               of_flow_line(pg->d, pg->fs, &f, from, first, pg->width, 0,
                            pg->width, &tmp, &scratch, &l) == 0) {
            h += l.h;
            n++;
            if (l.next <= from && !first) {
                break;
            }
            from = l.next;
            first = 0;
            tmp.n = 0;
        }
    }
    of_flow_free(&f);
    of_vec_free(&tmp);
    of_arena_free(&scratch);
    return h + (max == 0 ? p->ps.after : 0);
}

/** @brief Headings kept with what follows them, from block @p i: when
 *         they and the first two lines after them (24 points of a table)
 *         do not fit what is left of the page but would fit a page, they
 *         begin the next column or page. */
static void
keep_chain(of_pager_t *pg, const of_story_t *s, int i)
{
    float h = 0;
    int k;

    for (k = i; k < s->nblock && k < i + KEEP_CHAIN &&
         s->block[k].kind == OB_PARA && s->block[k].para->ps.keep_next;
         k++) {
        h += para_height(pg, s->block[k].para, 0);
    }
    if (k < s->nblock) {
        h += s->block[k].kind == OB_PARA
             ? para_height(pg, s->block[k].para, 2) : 24;
    }
    if (pg->content && pg->y + pg->after + h > pg_limit(pg) &&
        h < pg->bottom - pg->page_top) {
        (void)pg_next_column(pg);
    }
}

int
pg_story(of_pager_t *pg, const of_story_t *s)
{
    int i, err = 0;

    for (i = 0; s != NULL && i < s->nblock && err == 0 && !pg->failed;
         i++) {
        const of_block_t *b = &s->block[i];

        if (b->kind == OB_PARA && b->para != NULL) {
            if (b->para->ps.keep_next && pg->lay != NULL) {
                keep_chain(pg, s, i);
            }
            err = pg_para(pg, b->para);
        } else if (b->kind == OB_TABLE && b->table != NULL) {
            err = pg_table(pg, b->table);
        }
    }
    return err != 0 || pg->failed ? -1 : 0;
}

/* -------------------------------------------------------------- columns  */

void
pg_column(of_pager_t *pg, of_doc_t *d, of_faces_t *fs, float width,
          const of_pageno_t *no, of_vec_t *frags, of_arena_t *a)
{
    memset(pg, 0, sizeof *pg);
    pg->d = d;
    pg->fs = fs;
    pg->a = a;
    pg->frags = frags;
    pg->no = no;
    pg->width = width > 1 ? width : 1;
    pg->ncol = 1;
    pg->page = -1;
    of_vec_init(&pg->info, sizeof(pg_info_t));
    of_vec_init(&pg->notes, sizeof(int));
    of_vec_init(&pg->clear, sizeof(pg_clear_t));
    of_vec_init(&pg->places, sizeof(of_place_t));
}

void
pg_done(of_pager_t *pg)
{
    of_vec_free(&pg->info);
    of_vec_free(&pg->notes);
    of_vec_free(&pg->clear);
    of_vec_free(&pg->places);
    free(pg->note_h);
    pg->note_h = NULL;
}

float
of_layout_story(of_doc_t *d, of_faces_t *fs, const of_story_t *s,
                float width, const of_pageno_t *no, of_vec_t *frags,
                of_arena_t *a)
{
    static _Thread_local int depth;
    of_pager_t pg;
    float h;

    if (s == NULL) {
        return 0;
    }
    if (depth >= OF_NEST_MAX) {
        return 0;                   /* boxes in boxes: no deeper */
    }
    depth++;
    pg_column(&pg, d, fs, width, no, frags, a);
    pg.depth = depth;
    h = pg_story(&pg, s) == 0 ? pg.y + pg.after : -1;
    pg_done(&pg);
    depth--;
    return h;
}

/* ------------------------------------------------------- the document  */

/** @brief The height story @p s takes laid out @p w wide, or 0. */
static float
story_height(of_doc_t *d, of_faces_t *fs, const of_story_t *s, float w)
{
    of_vec_t tmp;
    of_arena_t scratch = { NULL, 0 };
    of_pageno_t no = { 1, 1, 0 };
    float h;

    if (s == NULL) {
        return 0;
    }
    of_vec_init(&tmp, sizeof(of_frag_t));
    h = of_layout_story(d, fs, s, w, &no, &tmp, &scratch);
    of_vec_free(&tmp);
    of_arena_free(&scratch);
    return h > 0 ? h : 0;
}

/** @brief @p v held between @p lo and @p hi; @p lo when it is no number. */
static float
within(float v, float lo, float hi)
{
    return v >= lo && v <= hi ? v : v > hi ? hi : lo;
}

/** @brief A section's page made one that can be laid out, whatever its
 *         file said: a size from an inch to 200, margins that leave an
 *         inch between them, columns from 1 to 16 with gaps between. */
static void
sane_section(of_section_t *s)
{
    s->w = within(s->w, 72, 14400);
    s->h = within(s->h, 72, 14400);
    s->left = within(s->left, 0, s->w / 2 - 36);
    s->right = within(s->right, 0, s->w - s->left - 72);
    s->top = within(s->top, 0, s->h / 2 - 36);
    s->bottom = within(s->bottom, 0, s->h - s->top - 72);
    s->head = within(s->head, 0, s->top);
    s->foot = within(s->foot, 0, s->bottom);
    s->cols = s->cols >= 1 && s->cols <= 16 ? s->cols : 1;
    s->gap = within(s->gap, 0, (s->w - s->left - s->right) / 2);
    if ((s->w - s->left - s->right - s->gap * (float)(s->cols - 1)) /
        (float)s->cols < 18) {
        s->cols = 1;                /* columns too thin for a word */
    }
}

/** @brief Section @p i begun: its body's top and bottom made room for
 *         its header and footer, and a new page unless it runs on. */
static int
section_begin(of_pager_t *pg, int i)
{
    const of_section_t *s = &pg->d->sect[i];
    float w = s->w - s->left - s->right;
    float hh = story_height(pg->d, pg->fs, s->header[0], w);
    float fh = story_height(pg->d, pg->fs, s->footer[0], w);

    if (pg->page >= 0 && s->kind != 1) {
        if (page_close(pg) != 0) {
            return -1;
        }
        pg->page = -1;              /* closed under its own section */
    }
    pg->sec = s;
    pg->nsect = i;
    pg->page_top = s->head + hh > s->top ? s->head + hh : s->top;
    pg->bottom = s->h - s->foot - fh < s->h - s->bottom
                 ? s->h - s->foot - fh : s->h - s->bottom;
    if (pg->bottom < pg->page_top + 72) {
        pg->bottom = pg->page_top + 72;
    }
    if (s->start >= 0) {
        pg->num = s->start - 1;
    }
    if (pg->page >= 0 && s->kind == 1) {
        columns(pg, pg->y);         /* continuous: on the same page */
        return 0;
    }
    pg->content = -1;               /* the page to come is its first */
    return pg_new_page(pg);
}

/** @brief One of a page's headers or footers laid out and placed: its
 *         top at the header distance, or, when @p bottom, its foot at the
 *         footer distance from the page's bottom. */
static int
header(of_pager_t *pg, const of_story_t *s, const of_section_t *sec,
       const of_pageno_t *no, of_vec_t *frags, int bottom)
{
    int from = frags->n;
    float h = of_layout_story(pg->d, pg->fs, s, sec->w - sec->left -
                              sec->right, no, frags, pg->a);

    if (h < 0) {
        return -1;
    }
    pg_move(frags, from, sec->left, bottom ? sec->h - sec->foot - h
                                           : sec->head);
    return 0;
}

/** @brief Each page's header and footer, drawn under its body, now the
 *         pages are counted. */
static int
headers(of_pager_t *pg)
{
    const pg_info_t *info = pg->info.p;
    of_vec_t frags;
    int i, k;

    of_vec_init(&frags, sizeof(of_frag_t));
    for (i = 0; i < pg->lay->npage; i++) {
        const of_section_t *s = &pg->d->sect[info[i].sect];
        of_page_t *p = &pg->lay->page[i];
        of_pageno_t no = { info[i].num, pg->lay->npage, s->num };
        of_frag_t *all;

        k = s->title_page && info[i].first ? 1 :
            pg->d->even_odd && info[i].num % 2 == 0 ? 2 : 0;
        frags.n = 0;
        if (header(pg, s->header[k], s, &no, &frags, 0) != 0 ||
            header(pg, s->footer[k], s, &no, &frags, 1) != 0) {
            of_vec_free(&frags);
            return -1;
        }
        if (frags.n == 0) {
            continue;
        }
        all = of_alloc(pg->a, (size_t)(frags.n + p->nfrag) * sizeof *all);
        if (all == NULL) {
            of_vec_free(&frags);
            return -1;
        }
        memcpy(all, frags.p, (size_t)frags.n * sizeof *all);
        memcpy(all + frags.n, p->frag, (size_t)p->nfrag * sizeof *all);
        p->frag = all;
        p->nfrag += frags.n;
    }
    of_vec_free(&frags);
    return 0;
}

int
of_layout(of_doc_t *d, of_faces_t *fs, of_layout_t *out)
{
    of_pager_t pg;
    of_vec_t frags;
    int i, err = 0;

    memset(out, 0, sizeof *out);
    of_vec_init(&frags, sizeof(of_frag_t));
    pg_column(&pg, d, fs, 1, NULL, &frags, &out->arena);
    pg.no = NULL;
    pg.lay = out;
    pg.note_h = d->nnote > 0 ? malloc((size_t)d->nnote * sizeof(float))
                             : NULL;
    for (i = 0; pg.note_h != NULL && i < d->nnote; i++) {
        pg.note_h[i] = -1;
    }
    for (i = 0; i < d->nsect && err == 0; i++) {
        sane_section(&d->sect[i]);
        err = section_begin(&pg, i);
        err |= pg_story(&pg, &d->sect[i].body);
    }
    if (err == 0 && pg.page >= 0) {
        err = page_close(&pg);
        pg.page = -1;
    }
    if (err == 0 && out->npage > 0) {
        err = headers(&pg);
        out->place = of_vec_freeze(&pg.places, &out->arena, &out->nplace);
    }
    pg_done(&pg);
    of_vec_free(&frags);
    return err != 0 || out->arena.bad || out->npage == 0 ? -1 : 0;
}

void
of_layout_free(of_layout_t *l)
{
    free(l->page);
    of_arena_free(&l->arena);
    memset(l, 0, sizeof *l);
}
