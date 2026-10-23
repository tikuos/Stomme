/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_office_flow.c - a paragraph set in lines.
 *
 * First every character is measured in the face that draws it, as an
 * atom that knows where a line may break after it.  Then lines are taken
 * one at a time, as word processors take them: as many atoms as fit,
 * broken at the last place allowed; tabs to their stops, aligned there;
 * the line aligned or justified, its height from the tallest atom and
 * the paragraph's spacing.  A line is a pure function of where it starts
 * and how wide it may be, so a page can ask for one again, narrower.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_office_private.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SMALL   0.65f               /* superscripts, subscripts          */
#define CAPS    0.8f                /* small capitals                    */

/* ---------------------------------------------------------- characters  */

/** @brief The next character of UTF-8 at @p *p; a broken byte is itself,
 *         read as Latin-1, so no text is lost. */
static uint32_t
next_cp(const unsigned char **p)
{
    const unsigned char *s = *p;
    uint32_t cp = *s++;
    int more = cp >= 0xf0 && cp < 0xf8 ? 3 : cp >= 0xe0 ? 2 :
               cp >= 0xc0 ? 1 : 0, k;

    for (k = 0; k < more; k++) {
        if ((s[k] & 0xc0) != 0x80) {
            more = 0;               /* no continuation: not UTF-8 */
            break;
        }
    }
    if (more == 0) {
        *p = s;                     /* ASCII, or a stray byte as itself */
        return cp;
    }
    cp &= more == 3 ? 0x07 : more == 2 ? 0x0f : 0x1f;
    for (k = 0; k < more; k++) {
        cp = cp << 6 | (*s++ & 0x3f);
    }
    *p = s;
    return cp;
}

/** @brief Whether @p cp is written without spaces between words: CJK
 *         ideographs, kana, Hangul and fullwidth forms. */
static int
cjk(uint32_t cp)
{
    return (cp >= 0x2e80 && cp <= 0x9fff) || (cp >= 0xac00 && cp <= 0xd7af)
           || (cp >= 0xf900 && cp <= 0xfaff) || (cp >= 0xfe30 &&
           cp <= 0xfe4f) || (cp >= 0xff00 && cp <= 0xffef) ||
           (cp >= 0x20000 && cp <= 0x2ffff);
}

/** @brief Whether no line may start with @p cp: closing marks. */
static int
closing(uint32_t cp)
{
    static const uint32_t marks[] = {',', '.', ';', ':', '!', '?', ')',
        ']', '}', 0x3001, 0x3002, 0xff0c, 0xff0e, 0xff1a, 0xff1b, 0xff01,
        0xff1f, 0x300d, 0x300f, 0xff09, 0x3011, 0x3015, 0x300b, 0x3009,
        0x30fc, 0x2019, 0x201d};
    size_t i;

    for (i = 0; i < sizeof marks / sizeof marks[0]; i++) {
        if (marks[i] == cp) {
            return 1;
        }
    }
    return 0;
}

/** @brief Whether no line may end with @p cp: opening marks. */
static int
opening(uint32_t cp)
{
    return cp == '(' || cp == '[' || cp == '{' || cp == 0x300c ||
           cp == 0x300e || cp == 0xff08 || cp == 0x3010 || cp == 0x3014 ||
           cp == 0x300a || cp == 0x3008 || cp == 0x2018 || cp == 0x201c;
}

/** @brief @p cp in capitals, for the letters documents set so. */
static uint32_t
upper(uint32_t cp)
{
    if ((cp >= 'a' && cp <= 'z') || (cp >= 0xe0 && cp <= 0xfe &&
                                      cp != 0xf7) ||
        (cp >= 0x3b1 && cp <= 0x3c9 && cp != 0x3c2) ||
        (cp >= 0x430 && cp <= 0x44f)) {
        return cp - 0x20;
    }
    if (cp >= 0x450 && cp <= 0x45f) {
        return cp - 0x50;
    }
    if (((cp >= 0x100 && cp <= 0x12f) ||
         (cp >= 0x132 && cp <= 0x137) ||
         (cp >= 0x14a && cp <= 0x177)) && (cp & 1)) {
        return cp - 1;              /* Latin Extended-A, pairs */
    }
    if (((cp >= 0x13a && cp <= 0x148) ||
         (cp >= 0x17a && cp <= 0x17e)) && !(cp & 1)) {
        return cp - 1;
    }
    if (cp == 0x131) return 'I';
    if (cp == 0x17f) return 'S';
    return cp == 0xff ? 0x178 : cp;
}

/* -------------------------------------------------------------- atoms  */

/** @brief Room for one atom more.  @return it, cleared, or NULL. */
static of_atom_t *
atom_room(of_flow_t *f)
{
    if (f->natom == f->cap) {
        int cap = f->cap ? f->cap * 2 : 64;
        of_atom_t *g = realloc(f->atom, (size_t)cap * sizeof *g);

        if (g == NULL) {
            return NULL;
        }
        f->atom = g;
        f->cap = cap;
    }
    memset(&f->atom[f->natom], 0, sizeof f->atom[0]);
    return &f->atom[f->natom++];
}

/** @brief The faces for @p st: its Latin one and its East Asian one,
 *         any face at all standing in for one that cannot be had. */
static void
faces_for(of_faces_t *fs, const of_style_t *st, int *latin, int *ea)
{
    *latin = of_face(fs, st->font, st->bold, st->italic);
    if (*latin < 0) {
        *latin = of_face(fs, "", st->bold, st->italic);
    }
    *ea = st->font_ea ? of_face(fs, st->font_ea, st->bold, st->italic)
                      : *latin;
    if (*ea < 0) {
        *ea = *latin;
    }
}

/** @brief The extent atom @p a takes above and below its baseline, in
 *         its face at @p size. */
static void
extent(const of_faces_t *fs, of_atom_t *a, float size)
{
    const of_face_t *f = &fs->face[a->face];

    a->asc = (float)f->ascent * size / (float)f->upem;
    a->desc = (float)(f->height - f->ascent) * size / (float)f->upem;
}

/** @brief Breaks around a character: after spaces and hyphens, and on
 *         either side of a CJK character but at closing and opening
 *         marks. */
static void
breaks(of_flow_t *f, of_atom_t *a, uint32_t cp)
{
    of_atom_t *prev = f->natom > 1 ? a - 1 : NULL;

    a->brk = cp == ' ' || cp == '\t' || cp == 0x3000 || cp == 0x2010 ||
             (cp == '-' && prev != NULL && prev->cp > '9') ||
             (cjk(cp) && !opening(cp));
    if (prev != NULL && cjk(cp) && !closing(cp) && !opening(prev->cp) &&
        prev->kind != OA_SPACE) {
        prev->brk = 1;
    }
    if (prev != NULL && closing(cp) && prev->kind != OA_SPACE) {
        prev->brk = 0;              /* a closing mark keeps to its word */
    }
}

/** @brief @p text in look @p st as atoms of @p kind, from run @p run.
 *  @return 0, or -1 out of memory. */
static int
add_text(of_faces_t *fs, of_flow_t *f, const char *text,
         const of_style_t *st, const of_run_t *run, int kind)
{
    const unsigned char *p = (const unsigned char *)text;
    float size = st->size * (st->vert ? SMALL : 1.0f);
    float rise = st->rise + (st->vert > 0 ? st->size * 0.33f :
                             st->vert < 0 ? -st->size * 0.14f : 0);
    int latin, ea;

    faces_for(fs, st, &latin, &ea);
    while (latin >= 0 && p != NULL && *p) {
        uint32_t cp = next_cp(&p), c = cp;
        float sz = size;
        of_atom_t *a;

        if (cp == '\r' || cp == 0xad || cp == 0x200b || cp == 0xfeff) {
            if (cp != '\r' && f->natom > 0) {
                f->atom[f->natom - 1].brk = 1;  /* a place to break */
            }
            continue;
        }
        if ((a = atom_room(f)) == NULL) {
            return -1;
        }
        a->run = run;
        a->kind = cp == '\t' ? OA_TAB : cp == '\n' ? OA_BREAK :
                  cp == ' ' ? OA_SPACE : (unsigned char)kind;
        if (st->caps || (st->smallcaps && upper(cp) != cp)) {
            c = upper(cp);
            sz = st->caps ? size : size * CAPS;
        }
        a->cp = c;                  /* what is drawn: what its glyph says */
        a->size = sz;
        a->rise = rise;
        a->face = of_face_for(fs, cjk(cp) ? ea : latin, c, &a->gid);
        extent(fs, a, sz);
        if (a->kind == OA_GLYPH || a->kind == OA_SPACE || a->kind == OA_NOTE
            || a->kind == OA_LABEL) {
            a->w = of_face_advance(fs, a->face, a->gid, sz) * st->scale +
                   st->spacing;
        }
        breaks(f, a, cp);
    }
    return 0;
}

/** @brief One object of the paragraph as an atom: a tab, a break, a
 *         picture, a float, a bookmark.  @return 0, or -1. */
static int
add_object(of_faces_t *fs, of_flow_t *f, const of_run_t *run, int kind)
{
    of_atom_t *a = atom_room(f);
    int latin, ea;

    if (a == NULL) {
        return -1;
    }
    a->run = run;
    a->kind = (unsigned char)kind;
    faces_for(fs, run->st, &latin, &ea);
    a->face = latin >= 0 ? latin : 0;
    a->size = run->st->size;
    if (latin >= 0) {
        extent(fs, a, a->size);
    }
    if (kind == OA_IMAGE && run->image != NULL) {
        a->w = run->image->w;
        a->asc = run->image->h;
        a->desc = 0;
        a->brk = 1;
        if (f->natom > 1) {
            f->atom[f->natom - 2].brk = 1;
        }
    }
    if (kind == OA_MATH && run->math != NULL) {   /* as set, a line's
                                                    height at least */
        a->w = run->math->w;
        a->asc = fmaxf(a->asc, run->math->asc);
        a->desc = fmaxf(a->desc, run->math->desc);
        a->brk = 1;
        if (f->natom > 1) {
            f->atom[f->natom - 2].brk = 1;
        }
    }
    if (kind == OA_FLOAT || kind == OA_MARK) {
        a->asc = a->desc = 0;
    }
    return 0;
}

/** @brief The label a list gives the paragraph, and what follows it: a
 *         tab to the hanging indent, a space, or nothing. */
static int
add_label(of_faces_t *fs, of_flow_t *f, const of_para_t *p)
{
    const of_style_t *st = p->label_st ? p->label_st : p->mark;

    if (p->label == NULL || st == NULL) {
        return 0;
    }
    if (add_text(fs, f, p->label, st, NULL, OA_LABEL) != 0) {
        return -1;
    }
    if (p->label_follow == 2) {
        return 0;
    }
    return add_text(fs, f, p->label_follow ? " " : "\t", st, NULL,
                    OA_LABEL);
}

/** @brief The number field @p run stands for, as @p no says, in text. */
static int
add_field(of_faces_t *fs, of_flow_t *f, const of_run_t *run,
          const of_pageno_t *no)
{
    char num[32];

    of_number(run->n == OF_FIELD_PAGES ? no->pages : no->page,
              run->n == OF_FIELD_PAGES ? OF_NUM_DECIMAL : no->num, num,
              sizeof num);
    return add_text(fs, f, num, run->st, run, OA_GLYPH);
}

/** @brief Each run of @p p as atoms.  @return 0, or -1. */
static int
add_runs(of_faces_t *fs, of_flow_t *f, const of_para_t *p,
         const of_pageno_t *no)
{
    static const signed char kinds[] = {
        [OR_TAB] = OA_TAB, [OR_BREAK] = OA_BREAK, [OR_PAGE] = OA_PAGE,
        [OR_COLUMN] = OA_COLUMN, [OR_IMAGE] = OA_IMAGE,
        [OR_FLOAT] = OA_FLOAT, [OR_MARK] = OA_MARK, [OR_MATH] = OA_MATH };
    int i, err = 0;

    for (i = 0; i < p->nrun && err == 0; i++) {
        const of_run_t *r = &p->run[i];

        if (r->st == NULL || (r->st->hidden && r->kind != OR_MARK)) {
            continue;
        }
        if (r->kind == OR_TEXT) {
            err = add_text(fs, f, r->text, r->st, r, OA_GLYPH);
        } else if (r->kind == OR_NOTE) {
            err = add_text(fs, f, r->text ? r->text : "*", r->st, r,
                           OA_NOTE);
        } else if (r->kind == OR_FIELD) {
            err = add_field(fs, f, r, no);
        } else if (r->kind < (int)sizeof kinds && kinds[r->kind]) {
            err = add_object(fs, f, r, kinds[r->kind]);
        }
    }
    return err;
}

int
of_flow_build(of_faces_t *fs, const of_para_t *p, const of_pageno_t *no,
              of_flow_t *out)
{
    of_style_t def;
    const of_style_t *mark = p->mark;
    of_atom_t probe;
    int latin, ea;

    memset(out, 0, sizeof *out);
    out->para = p;
    if (mark == NULL) {
        of_style_default(&def);
        mark = &def;
    }
    faces_for(fs, mark, &latin, &ea);
    memset(&probe, 0, sizeof probe);
    if (latin >= 0) {
        probe.face = latin;
        extent(fs, &probe, mark->size);
    }
    out->mark_asc = probe.asc;
    out->mark_desc = probe.desc;
    if (add_label(fs, out, p) != 0 || add_runs(fs, out, p, no) != 0) {
        return -1;
    }
    out->x = malloc(((size_t)out->natom + 1) * sizeof *out->x);
    out->lead = calloc((size_t)out->natom + 1, 1);
    return out->x != NULL && out->lead != NULL ? 0 : -1;
}

void
of_flow_free(of_flow_t *f)
{
    free(f->atom);
    free(f->x);
    free(f->lead);
    memset(f, 0, sizeof *f);
}

/* --------------------------------------------------------------- tabs  */

/** @brief The stop tab atom @p a at @p x goes to: the paragraph's next,
 *         or the hanging indent's on its first line, whichever is nearer,
 *         else the next default one; an absolute tab's own place in a
 *         column @p width wide.  Its alignment and leader too. */
static float
tab_stop(const of_doc_t *d, const of_flow_t *f, const of_atom_t *a, float x,
         int first, float width, unsigned char *align, unsigned char *leader)
{
    const of_pstyle_t *ps = &f->para->ps;
    float origin = d->tab_indent ? ps->left : 0, step = d->tab > 1 ? d->tab
                                                                   : 36;
    float hang = first && ps->first < 0 && x < ps->left - 0.01f ? ps->left
                                                               : -1;
    float last = origin, p;
    int i, n = a->run != NULL && a->run->kind == OR_TAB ? a->run->n : 0;

    *align = OF_TAB_LEFT;
    *leader = (unsigned char)(n >> 8);
    if (n != 0) {
        float l = (n >> 2 & 1) ? ps->left : 0;
        float r = (n >> 2 & 1) ? width - ps->right : width;

        *align = (n & 3) == OF_PTAB_CENTER ? OF_TAB_CENTER :
                 (n & 3) == OF_PTAB_RIGHT ? OF_TAB_RIGHT : OF_TAB_LEFT;
        p = (n & 3) == OF_PTAB_CENTER ? (l + r) / 2 :
            (n & 3) == OF_PTAB_RIGHT ? r : l;
        return p > x || *align != OF_TAB_LEFT ? p : x;
    }
    for (i = 0; i < ps->ntab; i++) {
        p = ps->tab[i].pos + origin;
        last = p;
        if (p > x + 0.01f && (hang < 0 || p <= hang)) {
            *align = ps->tab[i].align;
            *leader = ps->tab[i].leader;
            return p;
        }
    }
    if (hang >= 0) {
        return hang;
    }
    p = (float)(floor(((x > last ? x : last) - origin) / step + 1e-3) + 1)
        * step + origin;
    return p;
}

/**
 * @brief Where atoms @p from to @p end sit, from @p left: into f->x, tabs
 *        taken to their stops and the text after a right, centred or
 *        decimal one lined up there.  @return the end of the last atom
 *        that is not a space; how many tabs there were in @p tabs.
 */
static float
place(const of_doc_t *d, of_flow_t *f, int from, int end, float left,
      int first, float width, int *tabs)
{
    float x = left, stop = 0, seg_x = 0, ink = left;
    unsigned char align = OF_TAB_LEFT, lead;
    int i, seg = -1, k;

    *tabs = 0;
    for (i = from; i <= end; i++) {
        const of_atom_t *a = i < end ? &f->atom[i] : NULL;

        if (seg >= 0 && (a == NULL || a->kind == OA_TAB)) {
            float w = x - seg_x, pre = w, shift;

            for (k = seg; align == OF_TAB_DECIMAL && k < i; k++) {
                if (f->atom[k].cp == '.' || f->atom[k].cp == ',') {
                    pre = f->x[k] - seg_x;
                    break;
                }
            }
            shift = align == OF_TAB_RIGHT ? stop - w - seg_x :
                    align == OF_TAB_CENTER ? stop - w / 2 - seg_x :
                    stop - pre - seg_x;
            shift = shift > 0 ? shift : 0;
            for (k = seg; k < i; k++) {
                f->x[k] += shift;
            }
            x += shift;
            ink = ink > seg_x ? ink + shift : ink;
            seg = -1;
        }
        if (a == NULL) {
            break;
        }
        f->x[i] = x;
        f->lead[i] = 0;
        if (a->kind == OA_TAB) {
            stop = tab_stop(d, f, a, x, first, width, &align, &lead);
            f->lead[i] = lead;
            (*tabs)++;
            if (align == OF_TAB_LEFT) {
                x = stop;
            } else {
                seg = i + 1;
                seg_x = x;
            }
            continue;
        }
        x += a->w;
        if (a->kind != OA_SPACE && a->w > 0) {
            ink = x;
        }
    }
    return ink;
}

/* ------------------------------------------------------------ breaking  */

/** @brief Where the line from @p from must end, fitting between @p left
 *         and @p right: after the last atom a line may break at, or
 *         before the first that does not fit when there is none.  A tab
 *         lined up right or centred holds its text at its stop.
 *  @return one past its last atom; @p ends says what ended it. */
static int
fit(const of_doc_t *d, of_flow_t *f, int from, float left, float right,
    int first, float width, int *ends)
{
    float x = left, stop = 0;
    unsigned char align = OF_TAB_LEFT, lead;
    int i, last = -1;

    *ends = 0;
    for (i = from; i < f->natom; i++) {
        const of_atom_t *a = &f->atom[i];
        float end;

        if (a->kind == OA_BREAK || a->kind == OA_PAGE ||
            a->kind == OA_COLUMN) {
            *ends = a->kind;
            return i + 1;
        }
        if (a->kind == OA_TAB) {
            stop = tab_stop(d, f, a, x, first, width, &align, &lead);
            if (align == OF_TAB_LEFT) {
                x = stop < right ? stop : x;
            }
            last = i;
            continue;
        }
        end = x + a->w;
        if (align != OF_TAB_LEFT && stop > end) {
            end = stop;             /* lined up at the stop */
        }
        if (end > right + 0.01f && a->kind != OA_SPACE && i > from &&
            a->w > 0) {
            return last >= from ? last + 1 : i;
        }
        x += a->w;
        if (a->brk) {
            last = i;
        }
    }
    return f->natom;
}

/* --------------------------------------------------------------- lines  */

/** @brief The line's extent: the tallest atom above and below the
 *         baseline, or an empty line's; then the paragraph's spacing.
 *  @return the height, with the baseline in @p base. */
static float
line_height(const of_flow_t *f, int from, int end, float *base)
{
    const of_pstyle_t *ps = &f->para->ps;
    float asc = 0, desc = 0, h, natural;
    int i, any = 0;

    for (i = from; i < end; i++) {
        const of_atom_t *a = &f->atom[i];

        if (a->kind == OA_FLOAT || a->kind == OA_MARK || (a->kind ==
            OA_BREAK && i > from) || a->kind == OA_PAGE ||
            a->kind == OA_COLUMN) {
            continue;
        }
        asc = a->asc + a->rise > asc ? a->asc + a->rise : asc;
        desc = a->desc - a->rise > desc ? a->desc - a->rise : desc;
        any = 1;
    }
    if (!any) {
        asc = f->mark_asc;
        desc = f->mark_desc;
    }
    natural = asc + desc > 0.1f ? asc + desc : 12;
    if (ps->line_rule == OF_LINE_EXACT && ps->line > 0) {
        h = ps->line;
        *base = h * asc / natural;
    } else if (ps->line_rule == OF_LINE_LEAST) {
        h = ps->line > natural ? ps->line : natural;
        *base = h - desc;
    } else {
        h = natural * (ps->line > 0 ? ps->line : 1);
        *base = h - desc;
    }
    return h;
}

/** @brief The line's atoms moved as its alignment says: centred, to the
 *         right, or justified by widening its spaces.  Only a line with
 *         no tabs is moved; a paragraph's last line is not justified. */
static void
align_line(of_flow_t *f, int from, int end, float left, float right,
           float ink, int last)
{
    const of_pstyle_t *ps = &f->para->ps;
    float room = right - ink, step = 0, shift;
    int i, spaces = 0, trail = end;

    while (trail > from && (f->atom[trail - 1].kind == OA_SPACE ||
                            f->atom[trail - 1].kind == OA_BREAK)) {
        trail--;
    }
    if (room <= 0) {
        return;
    }
    if (ps->align == OF_JUSTIFY && !last) {
        for (i = from; i < trail; i++) {
            spaces += f->atom[i].kind == OA_SPACE;
        }
        step = spaces > 0 ? room / (float)spaces : 0;
        for (i = from, shift = 0; i < end && step > 0; i++) {
            f->x[i] += shift;
            if (f->atom[i].kind == OA_SPACE && i < trail) {
                shift += step;
            }
        }
        return;
    }
    shift = ps->align == OF_CENTER ? room / 2 :
            ps->align == OF_RIGHT ? room : 0;
    for (i = from; i < end && shift > 0; i++) {
        f->x[i] += shift;
    }
    (void)left;
}

/** @brief A fragment more on @p frags.  NULL out of memory. */
static of_frag_t *
frag(of_vec_t *frags, int kind)
{
    of_frag_t *g = of_vec_push(frags);

    if (g != NULL) {
        g->kind = (unsigned char)kind;
    }
    return g;
}

/** @brief Whether atoms @p a and @p b can share one run of glyphs. */
static int
same_run(const of_faces_t *fs, const of_atom_t *a, const of_atom_t *b)
{
    const of_style_t *sa = a->run ? a->run->st : NULL;
    const of_style_t *sb = b->run ? b->run->st : NULL;
    const char *la = a->run ? a->run->link : NULL;
    const char *lb = b->run ? b->run->link : NULL;

    (void)fs;
    return a->face == b->face && a->size == b->size && a->rise == b->rise &&
           la == lb && (sa == sb || (sa && sb && sa->color == sb->color &&
                                     sa->bold == sb->bold &&
                                     sa->italic == sb->italic));
}

/** @brief The colour atom @p a is drawn in: its run's, or @p label's when
 *         it has no run; black when that look sets none. */
static uint32_t
ink_of(const of_atom_t *a, const of_style_t *label)
{
    const of_style_t *st = a->run ? a->run->st : label;
    uint32_t c = st ? st->color : 0;

    return c ? c : 0xff000000u;
}

/** @brief The look atom @p a has: its run's, or the label's. */
static const of_style_t *
look(const of_atom_t *a, const of_flow_t *f)
{
    return a->run ? a->run->st : f->para->label_st ? f->para->label_st
                                                   : f->para->mark;
}

/** @brief Glyph atoms @p i to @p j as one fragment on @p base.  @return 0,
 *         or -1 out of memory. */
static int
glyph_frag(of_faces_t *fs, const of_flow_t *f, int i, int j, float base,
           of_vec_t *frags, of_arena_t *a)
{
    const of_atom_t *at = &f->atom[i];
    const of_face_t *face = &fs->face[at->face];
    const of_style_t *st = look(at, f);
    of_frag_t *g = frag(frags, OF_FRAG_GLYPHS);
    int k;

    if (g == NULL || (g->g = of_alloc(a, (size_t)(j - i) * sizeof *g->g))
        == NULL) {
        return -1;
    }
    g->x = f->x[i];
    g->y = base - at->rise;
    g->face = at->face;
    g->size = at->size;
    g->color = ink_of(at, st);
    g->bold = (unsigned char)face->f.bold_synth;
    g->italic = (unsigned char)face->f.italic_synth;
    g->link = at->run ? at->run->link : NULL;
    for (k = i; k < j; k++) {
        g->g[g->ng].gid = f->atom[k].gid;
        g->g[g->ng].cp = f->atom[k].cp;
        g->g[g->ng++].x = f->x[k] - f->x[i];
    }
    g->w = f->x[j - 1] + f->atom[j - 1].w - f->x[i];
    g->h = at->size;
    return 0;
}

/** @brief A rule under or through atoms @p i to @p j: at @p y, @p thick
 *         thick, in @p color, dotted or dashed as @p style says. */
static int
rule(of_vec_t *frags, const of_flow_t *f, int i, int j, float y,
     float thick, uint32_t color, int style)
{
    of_frag_t *g = frag(frags, OF_FRAG_RULE);

    if (g == NULL) {
        return -1;
    }
    g->x = f->x[i];
    g->y = y;
    g->w = f->x[j - 1] + f->atom[j - 1].w - f->x[i];
    g->h = 0;
    g->size = thick > 0.3f ? thick : 0.3f;
    g->color = color;
    g->dash = (unsigned char)style;
    return 0;
}

/** @brief The underline and strikeout atoms @p i to @p j have, drawn as
 *         their face says. */
static int
decorate(of_faces_t *fs, const of_flow_t *f, int i, int j, float base,
         of_vec_t *frags)
{
    const of_atom_t *at = &f->atom[i];
    const of_style_t *st = look(at, f);
    const of_face_t *face = &fs->face[at->face];
    float s = at->size / (float)face->upem, y, t;
    int err = 0;

    if (st == NULL || j <= i) {
        return 0;
    }
    t = (face->under_size > 0 ? (float)face->under_size : face->upem / 20.0f)
        * s;
    y = base - at->rise - (face->under != 0 ? (float)face->under
                           : -face->upem / 10.0f) * s + t / 2;
    if (st->underline) {
        err |= rule(frags, f, i, j, y, st->underline == OF_UL_THICK ? t * 2
                    : t, ink_of(at, st), st->underline == OF_UL_DOTTED
                    ? OF_LS_DOTTED : st->underline == OF_UL_DASHED
                    ? OF_LS_DASHED : OF_LS_SINGLE);
        if (st->underline == OF_UL_DOUBLE) {
            err |= rule(frags, f, i, j, y + t * 2, t, ink_of(at, st),
                        OF_LS_SINGLE);
        }
    }
    if (st->strike) {
        y = base - at->rise - (face->strike ? (float)face->strike
                               : face->upem * 0.28f) * s;
        err |= rule(frags, f, i, j, y, t, ink_of(at, st), OF_LS_SINGLE);
        if (st->strike == 2) {
            err |= rule(frags, f, i, j, y - t * 2, t, ink_of(at, st),
                        OF_LS_SINGLE);
        }
    }
    return err;
}

/** @brief A tab's leader: its character repeated over the tab's gap, in
 *         the tab's look. */
static int
leader(of_faces_t *fs, const of_flow_t *f, int i, float to, float base,
       of_vec_t *frags, of_arena_t *a)
{
    const of_atom_t *at = &f->atom[i];
    uint32_t cp = f->lead[i] == 0xb7 ? 0xb7 : f->lead[i];
    unsigned gid;
    int face = of_face_for(fs, at->face, cp, &gid), n, k;
    float w = of_face_advance(fs, face, gid, at->size), from = f->x[i];
    of_frag_t *g;

    if (w <= 0.5f || to - from < w * 2 || (to - from) / w > 4000) {
        return 0;                   /* no room, or more than is sensible */
    }
    n = (int)((to - from - w * 0.5f) / w);
    if (n <= 0 || n > 2000 || (g = frag(frags, OF_FRAG_GLYPHS)) == NULL ||
        (g->g = of_alloc(a, (size_t)n * sizeof *g->g)) == NULL) {
        return n > 0 && n <= 2000 ? -1 : 0;
    }
    g->x = to - (float)n * w - w * 0.25f;
    g->y = base;
    g->face = face;
    g->size = at->size;
    g->color = ink_of(at, look(at, f));
    for (k = 0; k < n; k++) {
        g->g[k].gid = gid;
        g->g[k].cp = cp;
        g->g[k].x = (float)k * w;
    }
    g->ng = n;
    g->w = (float)n * w;
    return 0;
}

/** @brief The backgrounds of atoms @p from to @p end: each run's
 *         highlight, as tall as its text. */
static int
backgrounds(const of_flow_t *f, int from, int end, float base,
            of_vec_t *frags)
{
    int i = from, j;

    while (i < end) {
        const of_style_t *st = look(&f->atom[i], f);
        of_frag_t *g;

        for (j = i + 1; j < end && look(&f->atom[j], f) == st; j++) {}
        if (st != NULL && st->back != 0 && (g = frag(frags,
                                                     OF_FRAG_RECT))) {
            g->x = f->x[i];
            g->y = base - f->atom[i].asc;
            g->w = f->x[j - 1] + f->atom[j - 1].w - f->x[i];
            g->h = f->atom[i].asc + f->atom[i].desc;
            g->color = st->back;
        } else if (st != NULL && st->back != 0) {
            return -1;
        }
        i = j;
    }
    return 0;
}

/** @brief Atom @p i, a picture or an equation, as its fragment on the
 *         line whose baseline is @p base.  @return 0, or -1. */
static int
object_frag(const of_flow_t *f, int i, float base, of_vec_t *frags)
{
    const of_atom_t *at = &f->atom[i];
    const of_math_t *m = at->kind == OA_MATH ? at->run->math : NULL;
    of_frag_t *g = frag(frags, m != NULL ? OF_FRAG_MATH : OF_FRAG_IMAGE);

    if (g == NULL) {
        return -1;
    }
    g->x = f->x[i];
    g->link = at->run->link;
    if (m != NULL) {                /* its own baseline on the line's */
        g->y = base - at->rise;
        g->w = m->w;
        g->h = m->asc + m->desc;
        g->math = m;
        return 0;
    }
    g->y = base - at->run->image->h - at->rise;
    g->w = at->run->image->w;
    g->h = at->run->image->h;
    g->image = at->run->image;
    return 0;
}

/** @brief Every fragment of the line: backgrounds, glyphs a run at a
 *         time with their rules, pictures, equations, and tab leaders. */
static int
fragments(of_faces_t *fs, const of_flow_t *f, int from, int end, float base,
          of_vec_t *frags, of_arena_t *a)
{
    int i = from, j, err = backgrounds(f, from, end, base, frags);

    while (i < end && err == 0) {
        const of_atom_t *at = &f->atom[i];

        if (at->run != NULL && ((at->kind == OA_IMAGE && at->run->image) ||
                                (at->kind == OA_MATH && at->run->math))) {
            err = object_frag(f, i++, base, frags);
            continue;
        }
        if (at->kind == OA_TAB && f->lead[i] && i + 1 <= end) {
            err = leader(fs, f, i, i + 1 < end ? f->x[i + 1] : f->x[i],
                         base, frags, a);
        }
        if (at->kind != OA_GLYPH && at->kind != OA_SPACE &&
            at->kind != OA_NOTE && at->kind != OA_LABEL) {
            i++;
            continue;
        }
        for (j = i + 1; j < end && (f->atom[j].kind == OA_GLYPH ||
             f->atom[j].kind == OA_SPACE || f->atom[j].kind == OA_NOTE ||
             f->atom[j].kind == OA_LABEL) && same_run(fs, at, &f->atom[j]) &&
             f->atom[j].run == at->run; j++) {}
        err = glyph_frag(fs, f, i, j, base, frags, a);
        if (err == 0) {
            int k = j;

            while (k > i && f->atom[k - 1].kind == OA_SPACE && k == end) {
                k--;                /* no rule under a line's last spaces */
            }
            err = decorate(fs, f, i, k, base, frags);
        }
        i = j;
    }
    return err;
}

int
of_flow_line(of_doc_t *d, of_faces_t *fs, of_flow_t *f, int from,
             int first, float width, float x0, float x1, of_vec_t *frags,
             of_arena_t *a, of_line_t *out)
{
    const of_pstyle_t *ps = &f->para->ps;
    float left = ps->left + (first ? ps->first : 0), right = width -
                 ps->right, ink;
    int end, tabs, ends;

    if (left < x0) {
        left = x0;
    }
    if (right > x1) {
        right = x1;
    }
    if (right < left + 1) {
        right = left + 1;
    }
    memset(out, 0, sizeof *out);
    end = fit(d, f, from, left, right, first, width, &ends);
    ink = place(d, f, from, end, left, first, width, &tabs);
    if (tabs == 0) {
        align_line(f, from, end, left, right, ink,
                   end >= f->natom || ends != 0);
    }
    out->h = line_height(f, from, end, &out->base);
    out->frag = frags->n;
    if (fragments(fs, f, from, end, out->base, frags, a) != 0) {
        return -1;
    }
    out->nfrag = frags->n - out->frag;
    out->next = end;
    out->ends = ends == OA_BREAK ? 0 : ends;
    return 0;
}
