/*
 * KintsugiOS v0.01 kits -- the foundation KintsugiDesktop and KintsugiTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 *
 * tiku_pdf_text.c - the words on a page, from its content stream.
 *
 * The text operators (ISO 32000 9.4) are followed with the transforms
 * that place them, and nothing is drawn: each glyph's position decides
 * whether it continues the line, starts a word, or starts a line.  Form
 * XObjects are entered and their text read with the page's.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_pdf_private.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define TEXT_MAX (1L << 20) /* bytes of text one page may give  */
#define STACK_MAX 32        /* q nesting                        */
#define OPERANDS 32
#define FORM_DEPTH 8

typedef struct {
    double a, b, c, d, e, f;
} mat_t;

/** @brief The graphics and text state the operators move. */
typedef struct {
    mat_t ctm;
    double size, tc, tw, th, tl, rise;
    pdf_tfont_t *font;
} gstate_t;

/** @brief A page's text being read: the states, the text so far and,
 *         when asked for, each character's box. */
typedef struct {
    tiku_pdf_t *doc;
    gstate_t gs[STACK_MAX];
    int sp, qfloor; /* the top; where the stream began  */
    char *actual;   /**< owned outermost ActualText source */
    int actual_depth, actual_seen;
    double actual_x, actual_y, actual_size;
    double actual_start, actual_weight;
    double actual_prev_x, actual_prev_y, actual_prev_size;
    int actual_prev_seen, actual_down;
    float actual_box[4]; /**< union of replaced glyph boxes */
    int marked, hide;    /* marked content deep; hidden from */
    mat_t tm, tlm;
    char *out;
    long n, cap;
    int have_last; /* a glyph has been placed           */
    double last_x, last_y, last_size;
    int last_space;
    int failed;
    /* Characters with their boxes, when asked for: each code point the
     * text gets, placed on the page as drawn (tiku_pdf_page_chars). */
    int want_chars;
    mat_t base; /* user space to the page drawn     */
    tiku_pdf_char_t *chars;
    long nchars, charcap;
    float box[4]; /* the box the next character takes */
} tx_t;

static void run(tx_t *t, const unsigned char *data, long n, pdf_obj_t *res,
                int depth);

/** @brief @p m then @p n, as PDF composes them: row vectors, m first. */
static mat_t
mat_mul(mat_t m, mat_t n)
{
    mat_t r;

    r.a = m.a * n.a + m.b * n.c;
    r.b = m.a * n.b + m.b * n.d;
    r.c = m.c * n.a + m.d * n.c;
    r.d = m.c * n.b + m.d * n.d;
    r.e = m.e * n.a + m.f * n.c + n.e;
    r.f = m.e * n.b + m.f * n.d + n.f;
    return r;
}

/** @brief A matrix from its six numbers. */
static mat_t
mat_of(double a, double b, double c, double d, double e, double f)
{
    mat_t m = {a, b, c, d, e, f};

    return m;
}

/** @brief Record @p cp with the box the extractor holds for it. */
static void
put_box(tx_t *t, uint32_t cp)
{
    tiku_pdf_char_t *c;

    if (t->nchars == t->charcap) {
        long cap = t->charcap ? t->charcap * 2 : 1024;

        c = realloc(t->chars, (size_t)cap * sizeof *c);
        if (c == NULL) {
            t->failed = 1;
            return;
        }
        t->chars = c;
        t->charcap = cap;
    }
    c = &t->chars[t->nchars++];
    c->cp = cp;
    c->x0 = t->box[0];
    c->y0 = t->box[1];
    c->x1 = t->box[2];
    c->y1 = t->box[3];
}

/** @brief Where a glyph's box, @p x0 to @p x1 and @p y0 to @p y1 in its
 *        own ems, lands through @p trm on the page drawn. */
static void
glyph_box(tx_t *t, const mat_t *trm, double x0, double x1, double y0, double y1,
          float *box)
{
    mat_t m = mat_mul(*trm, t->base);
    double gx[4] = {x0, x1, x1, x0}, gy[4] = {y0, y0, y1, y1};
    int k;

    for (k = 0; k < 4; k++) {
        float x = (float)(m.a * gx[k] + m.c * gy[k] + m.e);
        float y = (float)(m.b * gx[k] + m.d * gy[k] + m.f);

        if (k == 0 || x < box[0])
            box[0] = x;
        if (k == 0 || y < box[1])
            box[1] = y;
        if (k == 0 || x > box[2])
            box[2] = x;
        if (k == 0 || y > box[3])
            box[3] = y;
    }
}

/** @brief Append one code point as UTF-8. */
static void
put_char(tx_t *t, uint32_t cp)
{
    char b[4];
    int n;

    if (cp == 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp < 0xE000)) {
        return;
    }
    if (cp < 0x80) {
        b[0] = (char)cp;
        n = 1;
    } else if (cp < 0x800) {
        b[0] = (char)(0xC0 | (cp >> 6));
        b[1] = (char)(0x80 | (cp & 0x3F));
        n = 2;
    } else if (cp < 0x10000) {
        b[0] = (char)(0xE0 | (cp >> 12));
        b[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        b[2] = (char)(0x80 | (cp & 0x3F));
        n = 3;
    } else {
        b[0] = (char)(0xF0 | (cp >> 18));
        b[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        b[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        b[3] = (char)(0x80 | (cp & 0x3F));
        n = 4;
    }
    if (t->n + n + 1 > t->cap) {
        long cap = t->cap ? t->cap * 2 : 4096;
        char *p;

        if (t->cap >= TEXT_MAX) {
            t->failed = 1;
            return;
        }
        p = realloc(t->out, (size_t)cap);
        if (p == NULL) {
            t->failed = 1;
            return;
        }
        t->out = p;
        t->cap = cap;
    }
    memcpy(t->out + t->n, b, (size_t)n);
    t->n += n;
    t->out[t->n] = '\0';
    t->last_space = cp == ' ' || cp == '\n';
    if (t->want_chars) {
        put_box(t, cp);
    }
}

/** @brief The font named @p name in @p res, as the document keeps it. */
static pdf_tfont_t *
font_named(tx_t *t, pdf_obj_t *res, const char *name)
{
    return pdf_font_text(t->doc,
                         pdf_get(t->doc, pdf_get(t->doc, res, "Font"), name));
}

/** @brief The text matrix moved past glyph @p gl: across by its width,
 *         or down by its advance when the font writes downward. */
static void
advance(tx_t *t, const gstate_t *g, const pdf_shown_t *gl)
{
    double extra = g->tc + (gl->word ? g->tw : 0);

    if (g->font != NULL && g->font->vertical) {
        t->tm = mat_mul(mat_of(1, 0, 0, 1, 0, gl->w1 * g->size + extra), t->tm);
    } else {
        t->tm = mat_mul(
            mat_of(1, 0, 0, 1, (gl->w * g->size + extra) * g->th, 0), t->tm);
    }
}

/** @brief The text render matrix for glyph @p gl, hung from its
 *         vertical origin when the font writes downward (9.7.4.3). */
static mat_t
glyph_matrix(const tx_t *t, const gstate_t *g, const pdf_shown_t *gl)
{
    mat_t m = mat_of(g->size * g->th, 0, 0, g->size, 0, g->rise);

    if (g->font != NULL && g->font->vertical) {
        m = mat_mul(mat_of(1, 0, 0, 1, -gl->vx, -gl->vy), m);
    }
    return mat_mul(mat_mul(m, t->tm), g->ctm);
}

/**
 * @brief A space or a line break before a glyph at @p x, @p y, as where
 *        it lands against where the last one ended says: along a line
 *        written across, or down a column written @p down.
 */
static void
gap(tx_t *t, double x, double y, double size, int down)
{
    double ref = size > t->last_size ? t->last_size : size;
    double along = down ? t->last_y - y : x - t->last_x;
    double across = down ? x - t->last_x : y - t->last_y;

    /* A break made up between glyphs is an empty box where the last one
     * ended: it selects and matches, and draws nothing. */
    if (down) {
        t->box[1] = t->box[3];
    } else {
        t->box[0] = t->box[2];
    }
    if (fabs(across) > 0.5 * ref || along < -1.5 * ref) {
        if (!t->last_space) {
            put_char(t, '\n');
        }
    } else if (along > 0.18 * ref && !t->last_space) {
        put_char(t, ' ');
    }
}

/** @brief Glyph @p gl's characters through @p trm, each with its share
 *         of the glyph's box. */
static void
emit(tx_t *t, const mat_t *trm, const pdf_shown_t *gl, int down)
{
    if (t->want_chars) {
        if (down) {
            glyph_box(t, trm, 0, gl->w, gl->vy + gl->w1, gl->vy, t->box);
        } else {
            glyph_box(t, trm, 0, gl->w, -0.2, 0.8, t->box);
        }
    }
    if (gl->uni2) {
        /* A ligature: its two characters share the glyph. */
        float right = t->box[2];

        t->box[2] = (t->box[0] + right) / 2;
        put_char(t, gl->uni);
        t->box[0] = t->box[2];
        t->box[2] = right;
        put_char(t, gl->uni2);
    } else {
        put_char(t, gl->uni);
    }
}

/** @brief Accumulate the geometry replaced by the outermost ActualText span. */
static void
actual_glyph(tx_t *t, const mat_t *trm, const pdf_shown_t *gl, int down)
{
    float box[4] = {0};

    if (t->want_chars) {
        if (down)
            glyph_box(t, trm, 0, gl->w, gl->vy + gl->w1, gl->vy, box);
        else
            glyph_box(t, trm, 0, gl->w, -0.2, 0.8, box);
        for (int i = 0; i < 4; i++)
            if (!t->actual_seen ||
                (i < 2 ? box[i] < t->actual_box[i] : box[i] > t->actual_box[i]))
                t->actual_box[i] = box[i];
    }
    t->actual_seen = 1;
}

/** @brief Emit source scalars once, sharing the replaced glyphs' union box. */
static void
actual_end(tx_t *t)
{
    const unsigned char *p = (const unsigned char *)t->actual;
    size_t count = 0, index = 0;

    if (!p)
        return;
    if (t->actual_seen && t->actual_prev_seen) {
        t->last_x = t->actual_prev_x;
        t->last_y = t->actual_prev_y;
        t->last_size = t->actual_prev_size;
        gap(t, t->actual_down ? t->actual_y : t->actual_start,
            t->actual_down ? t->actual_start : t->actual_y, t->actual_size,
            t->actual_down);
    }
    for (size_t i = 0; p[i]; i++)
        count += (p[i] & 0xc0) != 0x80;
    while (*p && t->actual_seen && !t->failed) {
        uint32_t cp = *p++;
        unsigned more = cp < 0x80 ? 0 : cp < 0xe0 ? 1 : cp < 0xf0 ? 2 : 3;
        if (more)
            cp &= (1u << (6 - more)) - 1;
        while (more-- && (*p & 0xc0) == 0x80)
            cp = cp << 6 | (*p++ & 63);
        memcpy(t->box, t->actual_box, sizeof t->box);
        if (count) {
            double width = t->box[2] - t->box[0];
            t->box[2] = t->box[0] + (float)(width * (index + 1) / count);
            t->box[0] += (float)(width * index / count);
        }
        put_char(t, cp);
        index++;
    }
    if (t->actual_seen) {
        t->last_x = t->actual_down ? t->actual_y : t->actual_x;
        t->last_y = t->actual_down ? t->actual_x : t->actual_y;
        t->last_size = t->actual_size;
    }
    free(t->actual);
    t->actual = NULL;
    t->actual_depth = t->actual_seen = 0;
}

/**
 * @brief Place the glyphs of one string.
 *
 * Where each lands against where the last one ended says whether a
 * space or a line break comes first; the text is in the stream's order,
 * wherever the glyphs land.  The font's CMap says how many bytes each
 * code takes.
 */
static void
show(tx_t *t, const unsigned char *s, size_t n)
{
    gstate_t *g = &t->gs[t->sp];
    int down = g->font != NULL && g->font->vertical, k;
    pdf_shown_t gl;
    size_t i = 0;

    while ((k = pdf_tfont_next(g->font, s + i, n - i, &gl)) > 0) {
        mat_t trm = glyph_matrix(t, g, &gl);
        mat_t pen = mat_mul(t->tm, g->ctm);
        double size = sqrt(fabs(trm.a * trm.d - trm.b * trm.c));

        i += (size_t)k;
        if (size <= 0) {
            size = g->size;
        }
        if (t->hide) {
            advance(t, g, &gl); /* placed, but not read */
            continue;
        }
        if (t->have_last && !t->actual) {
            gap(t, down ? pen.e : trm.e, down ? pen.f : trm.f, size, down);
        }
        if (t->actual) {
            if (!t->actual_seen) {
                t->actual_prev_x = t->last_x;
                t->actual_prev_y = t->last_y;
                t->actual_prev_size = t->last_size;
                t->actual_prev_seen = t->have_last;
                t->actual_x = down ? pen.f : pen.e;
                t->actual_weight = -1;
            }
            double weight = fabs(down ? gl.w1 : gl.w);
            if (weight > t->actual_weight) {
                t->actual_weight = weight;
                t->actual_down = down;
                t->actual_start = down ? pen.f : trm.e;
                t->actual_y = down ? pen.e : trm.f;
                t->actual_size = size;
            }
            actual_glyph(t, &trm, &gl, down);
        } else if (g->font != NULL) {
            emit(t, &trm, &gl, down);
        }
        advance(t, g, &gl);
        pen = mat_mul(t->tm, g->ctm);
        t->last_x = pen.e;
        t->last_y = down ? pen.f : trm.f;
        t->last_size = size;
        if (t->actual) {
            if (down && pen.f < t->actual_x)
                t->actual_x = pen.f;
            else if (!down && pen.e > t->actual_x)
                t->actual_x = pen.e;
        }
        t->have_last = 1;
    }
}

/** @brief Operand @p i of @p n as a number, or @p dflt. */
static double
num(pdf_obj_t **ops, int n, int i, double dflt)
{
    return i < n ? pdf_number(ops[i], dflt) : dflt;
}

/** @brief A form XObject: its own resources and matrix, then its bytes. */
static void
form(tx_t *t, pdf_obj_t *res, const char *name, int depth)
{
    pdf_obj_t *xo = pdf_get(t->doc, pdf_get(t->doc, res, "XObject"), name);
    pdf_obj_t *m, *fres;
    unsigned char *data = NULL;
    long n = 0;
    int sp = t->sp;

    if (xo == NULL || xo->kind != PDF_STREAM || depth >= FORM_DEPTH ||
        !pdf_is_name(pdf_get(t->doc, xo, "Subtype"), "Form") || t->hide ||
        !pdf_oc_visible(t->doc, pdf_get(t->doc, xo, "OC"))) {
        return;
    }
    if (t->sp + 1 >= STACK_MAX) {
        return;
    }
    t->gs[t->sp + 1] = t->gs[t->sp];
    t->sp++;
    m = pdf_get(t->doc, xo, "Matrix");
    if (m != NULL && m->kind == PDF_ARRAY && m->u.a.n == 6) {
        mat_t fm = mat_of(pdf_number(pdf_resolve(t->doc, m->u.a.item[0]), 1),
                          pdf_number(pdf_resolve(t->doc, m->u.a.item[1]), 0),
                          pdf_number(pdf_resolve(t->doc, m->u.a.item[2]), 0),
                          pdf_number(pdf_resolve(t->doc, m->u.a.item[3]), 1),
                          pdf_number(pdf_resolve(t->doc, m->u.a.item[4]), 0),
                          pdf_number(pdf_resolve(t->doc, m->u.a.item[5]), 0));

        t->gs[t->sp].ctm = mat_mul(fm, t->gs[t->sp].ctm);
    }
    fres = pdf_get(t->doc, xo, "Resources");
    if (pdf_stream_data(t->doc, xo, &data, &n, NULL) == 0) {
        run(t, data, n, fres != NULL ? fres : res, depth + 1);
        free(data);
    }
    t->sp = sp; /* whatever q it left unmatched */
}

/** @brief Past an inline image: BI ... ID <bytes> EI. */
static void
skip_inline_image(pdf_cur_t *c)
{
    long id = pdf_find(c->p, c->n, c->pos, "ID");

    if (id < 0) {
        c->pos = c->n;
        return;
    }
    c->pos = id + 3;
    while (c->pos < c->n) {
        long ei = pdf_find(c->p, c->n, c->pos, "EI");

        if (ei < 0) {
            c->pos = c->n;
            return;
        }
        c->pos = ei + 2;
        if ((ei == 0 || c->p[ei - 1] <= ' ') &&
            (ei + 2 >= c->n || c->p[ei + 2] <= ' ')) {
            return;
        }
    }
}

/** @brief Apply optional-content visibility and outermost ActualText spans. */
static void
marked(tx_t *t, const char *o, pdf_obj_t **ops, int n, pdf_obj_t *res)
{
    if (strcmp(o, "EMC") == 0) {
        if (t->marked > 0) {
            if (t->actual && t->actual_depth == t->marked)
                actual_end(t);
            t->hide = t->hide == t->marked ? 0 : t->hide;
            t->marked--;
        }
        return;
    }
    t->marked++;
    if (!t->hide && strcmp(o, "BDC") == 0 && n >= 2 &&
        pdf_is_name(ops[0], "OC") &&
        !pdf_oc_visible(t->doc, pdf_oc_props(t->doc, ops[1], res))) {
        t->hide = t->marked;
    }
    if (!t->hide && !t->actual && !strcmp(o, "BDC") && n >= 2) {
        pdf_obj_t *value =
            pdf_get(t->doc, pdf_oc_props(t->doc, ops[1], res), "ActualText");
        if (value && value->kind == PDF_STRING) {
            if (value->u.s.n > TEXT_MAX / 3) {
                t->failed = 1;
                return;
            }
            t->actual = pdf_text_string(value);
            if (!t->actual) {
                t->failed = 1;
                return;
            }
            t->actual_depth = t->marked;
            t->actual_seen = 0;
        }
    }
}

/** @brief One operator with its operands. */
static void
op(tx_t *t, const char *o, pdf_obj_t **ops, int n, pdf_obj_t *res, pdf_cur_t *c,
   int depth)
{
    gstate_t *g = &t->gs[t->sp];

    if (strcmp(o, "q") == 0) {
        if (t->sp + 1 < STACK_MAX) {
            t->gs[t->sp + 1] = *g;
            t->sp++;
        }
    } else if (strcmp(o, "Q") == 0) {
        if (t->sp > t->qfloor) {
            t->sp--;
        }
    } else if (strcmp(o, "cm") == 0 && n >= 6) {
        g->ctm = mat_mul(mat_of(num(ops, n, 0, 1), num(ops, n, 1, 0),
                                num(ops, n, 2, 0), num(ops, n, 3, 1),
                                num(ops, n, 4, 0), num(ops, n, 5, 0)),
                         g->ctm);
    } else if (strcmp(o, "BT") == 0) {
        t->tm = t->tlm = mat_of(1, 0, 0, 1, 0, 0);
    } else if (strcmp(o, "Tf") == 0 && n >= 2) {
        if (ops[0]->kind == PDF_NAME) {
            g->font = font_named(t, res, (const char *)ops[0]->u.s.p);
        }
        g->size = num(ops, n, 1, 1);
    } else if (strcmp(o, "Td") == 0 && n >= 2) {
        t->tlm = mat_mul(
            mat_of(1, 0, 0, 1, num(ops, n, 0, 0), num(ops, n, 1, 0)), t->tlm);
        t->tm = t->tlm;
    } else if (strcmp(o, "TD") == 0 && n >= 2) {
        g->tl = -num(ops, n, 1, 0);
        t->tlm = mat_mul(
            mat_of(1, 0, 0, 1, num(ops, n, 0, 0), num(ops, n, 1, 0)), t->tlm);
        t->tm = t->tlm;
    } else if (strcmp(o, "Tm") == 0 && n >= 6) {
        t->tm = t->tlm =
            mat_of(num(ops, n, 0, 1), num(ops, n, 1, 0), num(ops, n, 2, 0),
                   num(ops, n, 3, 1), num(ops, n, 4, 0), num(ops, n, 5, 0));
    } else if (strcmp(o, "T*") == 0) {
        t->tlm = mat_mul(mat_of(1, 0, 0, 1, 0, -g->tl), t->tlm);
        t->tm = t->tlm;
    } else if (strcmp(o, "TL") == 0) {
        g->tl = num(ops, n, 0, 0);
    } else if (strcmp(o, "Tc") == 0) {
        g->tc = num(ops, n, 0, 0);
    } else if (strcmp(o, "Tw") == 0) {
        g->tw = num(ops, n, 0, 0);
    } else if (strcmp(o, "Tz") == 0) {
        g->th = num(ops, n, 0, 100) / 100.0;
    } else if (strcmp(o, "Ts") == 0) {
        g->rise = num(ops, n, 0, 0);
    } else if (strcmp(o, "Tj") == 0 && n >= 1 && ops[0]->kind == PDF_STRING) {
        show(t, ops[0]->u.s.p, ops[0]->u.s.n);
    } else if ((strcmp(o, "'") == 0 || strcmp(o, "\"") == 0) && n >= 1) {
        pdf_obj_t *s = ops[n - 1];

        if (o[0] == '"' && n >= 3) {
            g->tw = num(ops, n, 0, g->tw);
            g->tc = num(ops, n, 1, g->tc);
        }
        t->tlm = mat_mul(mat_of(1, 0, 0, 1, 0, -g->tl), t->tlm);
        t->tm = t->tlm;
        if (s->kind == PDF_STRING) {
            show(t, s->u.s.p, s->u.s.n);
        }
    } else if (strcmp(o, "TJ") == 0 && n >= 1 && ops[0]->kind == PDF_ARRAY) {
        int i;

        for (i = 0; i < ops[0]->u.a.n; i++) {
            pdf_obj_t *e = ops[0]->u.a.item[i];

            if (e->kind == PDF_STRING) {
                show(t, e->u.s.p, e->u.s.n);
            } else if (e->kind == PDF_INT || e->kind == PDF_REAL) {
                double d = -pdf_number(e, 0) / 1000.0 * g->size;

                t->tm = mat_mul(g->font != NULL && g->font->vertical
                                    ? mat_of(1, 0, 0, 1, 0, d)
                                    : mat_of(1, 0, 0, 1, d * g->th, 0),
                                t->tm);
            }
        }
    } else if (strcmp(o, "Do") == 0 && n >= 1 && ops[0]->kind == PDF_NAME) {
        form(t, res, (const char *)ops[0]->u.s.p, depth);
    } else if (strcmp(o, "BMC") == 0 || strcmp(o, "BDC") == 0 ||
               strcmp(o, "EMC") == 0) {
        marked(t, o, ops, n, res);
    } else if (strcmp(o, "BI") == 0) {
        skip_inline_image(c);
    }
}

/** @brief Every operator of @p data, in order. */
static void
run(tx_t *t, const unsigned char *data, long n, pdf_obj_t *res, int depth)
{
    pdf_cur_t c = {data, n, 0};
    pdf_obj_t *ops[OPERANDS];
    int nops = 0, qfloor = t->qfloor, mk = t->marked, hide = t->hide;

    t->qfloor = t->sp; /* its Q undoes its own q, no more */
    for (;;) {
        pdf_obj_t *o = pdf_parse(&c, 0);
        int i;

        if (o == NULL || t->failed) {
            pdf_free(o);
            break;
        }
        if (o->kind != PDF_OP) {
            if (nops == OPERANDS) {
                pdf_free(ops[0]);
                memmove(ops, ops + 1, (size_t)(OPERANDS - 1) * sizeof ops[0]);
                nops--;
            }
            ops[nops++] = o;
            continue;
        }
        op(t, (const char *)o->u.s.p, ops, nops, res, &c, depth);
        pdf_free(o);
        for (i = 0; i < nops; i++) {
            pdf_free(ops[i]);
        }
        nops = 0;
    }
    while (nops > 0) {
        pdf_free(ops[--nops]);
    }
    if (t->actual && t->actual_depth > mk) {
        free(t->actual);
        t->actual = NULL;
        t->actual_depth = t->actual_seen = 0;
    }
    t->qfloor = qfloor;
    t->marked = mk; /* a stream's marked content is its own */
    t->hide = hide;
}

/** @brief Run @p page's content for its text, and its characters' boxes
 *        with @p want_chars.  @return the extractor to free, or NULL. */
static tx_t *
extract(tiku_pdf_t *doc, int page, int want_chars)
{
    pdf_obj_t *p = pdf_page_dict(doc, page);
    unsigned char *data = NULL;
    long n = 0;
    tx_t *t;

    if (p == NULL || pdf_page_content(doc, p, &data, &n) != 0) {
        return NULL;
    }
    pdf_fonts_page(doc);
    t = calloc(1, sizeof *t);
    if (t == NULL) {
        free(data);
        return NULL;
    }
    t->doc = doc;
    t->gs[0].ctm = mat_of(1, 0, 0, 1, 0, 0);
    t->gs[0].th = 1;
    t->tm = t->tlm = t->gs[0].ctm;
    t->want_chars = want_chars;
    if (want_chars) {
        pdf_mat_t m;
        int w, h;

        if (pdf_page_matrix(doc, page, 1.0, 0, 0, 0, &m, &w, &h) != 0) {
            free(data);
            free(t);
            return NULL;
        }
        t->base = mat_of(m.a, m.b, m.c, m.d, m.e, m.f);
    }
    run(t, data, n, pdf_page_inherited(doc, p, "Resources"), 0);
    free(data);
    return t;
}

char *
tiku_pdf_page_text(tiku_pdf_t *doc, int page)
{
    tx_t *t = extract(doc, page, 0);
    char *out;

    if (t == NULL) {
        return NULL;
    }
    out = t->out != NULL ? t->out : calloc(1, 1);
    free(t);
    return out;
}

long
tiku_pdf_page_chars(tiku_pdf_t *doc, int page, tiku_pdf_char_t **out)
{
    tx_t *t = extract(doc, page, 1);
    long n;

    *out = NULL;
    if (t == NULL) {
        return -1;
    }
    n = t->nchars;
    *out = t->chars;
    free(t->out);
    free(t);
    return n;
}
