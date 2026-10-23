/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_pdf_color.c - colour spaces and the functions some of them run.
 *
 * Gray, RGB and CMYK are turned to screen colour directly; an ICC
 * profile is taken for its Alternate, else the device space with as many
 * parts (ISO 32000 8.6); Indexed looks its number up; a Separation runs
 * its tint through a function.  The functions (7.10) are sampled,
 * exponential, stitched, or a small PostScript calculator.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_pdf_private.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define PS_MAX_CODE  4096           /* words of a calculator program    */
#define PS_STACK     100
#define FUNC_DEPTH   8              /* stitched functions in functions  */
#define SAMPLES_MAX  (1L << 22)

/* ----------------------------------------------------------- functions  */

/** @brief A calculator word: a number, an operator, or a jump. */
typedef struct {
    int    op;                      /* 0: push value; else an operator  */
    double v;
    int    to;                      /* if/ifelse: where the block ends  */
} ps_word_t;

struct pdf_func {
    int         type, nin, nout;
    double      domain[PDF_FUNC_MAX_IN * 2], range[PDF_FUNC_MAX_OUT * 2];
    int         has_range;
    /* type 2 */
    double      c0[PDF_FUNC_MAX_OUT], c1[PDF_FUNC_MAX_OUT], expo;
    /* type 3 */
    pdf_func_t *part[16];
    double      bounds[16], encode3[32];
    int         nparts;
    /* type 0 */
    unsigned char *samples;
    long        nsamples;
    int         size[PDF_FUNC_MAX_IN], bps;
    double      encode0[PDF_FUNC_MAX_IN * 2], decode0[PDF_FUNC_MAX_OUT * 2];
    /* type 4 */
    ps_word_t  *code;
    int         ncode;
};

/** @brief The numbers of array @p a into @p out, @p max at most.
 *  @return how many. */
static int
numbers(tiku_pdf_t *doc, pdf_obj_t *a, double *out, int max)
{
    int i, n = 0;

    if (a == NULL || a->kind != PDF_ARRAY) {
        return 0;
    }
    for (i = 0; i < a->u.a.n && n < max; i++) {
        out[n++] = pdf_number(pdf_resolve(doc, a->u.a.item[i]), 0);
    }
    return n;
}

/* The calculator's operators, by the word that names them. */
enum { PS_ADD = 1, PS_SUB, PS_MUL, PS_DIV, PS_IDIV, PS_MOD, PS_NEG, PS_ABS,
       PS_SQRT, PS_SIN, PS_COS, PS_ATAN, PS_EXP, PS_LN, PS_LOG, PS_CVI,
       PS_CVR, PS_FLOOR, PS_CEILING, PS_ROUND, PS_TRUNCATE, PS_DUP, PS_POP,
       PS_EXCH, PS_COPY, PS_INDEX, PS_ROLL, PS_EQ, PS_NE, PS_GT, PS_GE,
       PS_LT, PS_LE, PS_AND, PS_OR, PS_XOR, PS_NOT, PS_BITSHIFT, PS_TRUE,
       PS_FALSE, PS_IF, PS_IFELSE, PS_BLOCK };

static const struct { const char *name; int op; } ps_ops[] = {
    {"add", PS_ADD}, {"sub", PS_SUB}, {"mul", PS_MUL}, {"div", PS_DIV},
    {"idiv", PS_IDIV}, {"mod", PS_MOD}, {"neg", PS_NEG}, {"abs", PS_ABS},
    {"sqrt", PS_SQRT}, {"sin", PS_SIN}, {"cos", PS_COS}, {"atan", PS_ATAN},
    {"exp", PS_EXP}, {"ln", PS_LN}, {"log", PS_LOG}, {"cvi", PS_CVI},
    {"cvr", PS_CVR}, {"floor", PS_FLOOR}, {"ceiling", PS_CEILING},
    {"round", PS_ROUND}, {"truncate", PS_TRUNCATE}, {"dup", PS_DUP},
    {"pop", PS_POP}, {"exch", PS_EXCH}, {"copy", PS_COPY},
    {"index", PS_INDEX}, {"roll", PS_ROLL}, {"eq", PS_EQ}, {"ne", PS_NE},
    {"gt", PS_GT}, {"ge", PS_GE}, {"lt", PS_LT}, {"le", PS_LE},
    {"and", PS_AND}, {"or", PS_OR}, {"xor", PS_XOR}, {"not", PS_NOT},
    {"bitshift", PS_BITSHIFT}, {"true", PS_TRUE}, {"false", PS_FALSE},
    {"if", PS_IF}, {"ifelse", PS_IFELSE}
};

/**
 * @brief Compile a calculator program: { ... } with nested blocks.
 *
 * A block becomes a PS_BLOCK word whose @c to says where it ends, so
 * that if and ifelse can skip or take it without a parser at run time.
 */
static int
ps_compile(pdf_func_t *f, pdf_cur_t *c, int depth)
{
    for (;;) {
        pdf_obj_t *o = pdf_parse(c, 0);
        size_t i;

        if (o == NULL) {
            return depth == 0 ? 0 : -1;
        }
        if (f->ncode >= PS_MAX_CODE) {
            pdf_free(o);
            return -1;
        }
        if (o->kind == PDF_INT || o->kind == PDF_REAL) {
            f->code[f->ncode].op = 0;
            f->code[f->ncode].v = pdf_number(o, 0);
            f->ncode++;
            pdf_free(o);
            continue;
        }
        if (o->kind != PDF_OP) {
            pdf_free(o);
            continue;
        }
        if (strcmp((const char *)o->u.s.p, "{") == 0) {
            int at = f->ncode++;

            pdf_free(o);
            f->code[at].op = PS_BLOCK;
            if (depth > 16 || ps_compile(f, c, depth + 1) != 0) {
                return -1;
            }
            f->code[at].to = f->ncode;
            continue;
        }
        if (strcmp((const char *)o->u.s.p, "}") == 0) {
            pdf_free(o);
            return depth > 0 ? 0 : -1;
        }
        for (i = 0; i < sizeof ps_ops / sizeof ps_ops[0]; i++) {
            if (strcmp((const char *)o->u.s.p, ps_ops[i].name) == 0) {
                f->code[f->ncode++].op = ps_ops[i].op;
                break;
            }
        }
        pdf_free(o);
    }
}

/** @brief @p a as the calculator's integers are: held to 32 bits, and
 *         clear of the one quotient that overflows; a NaN is 0. */
static long
ps_int(double a)
{
    return a > -2147483647.0 ? (a < 2147483647.0 ? (long)a : 2147483647L)
                             : a == a ? -2147483647L : 0;
}

/** @brief bitshift: @p a's 32 bits moved left @p by, or right when it is
 *         negative, zeros coming in. */
static double
ps_shift(double a, double by)
{
    uint32_t u = (uint32_t)ps_int(a);
    long s = ps_int(by);

    u = s >= 32 || s <= -32 ? 0 : s >= 0 ? u << s : u >> -s;
    return (double)(int32_t)u;
}

/** @brief Run the calculator over @p st with @p sp values on it. */
static int
ps_run(const pdf_func_t *f, int from, int to, double *st, int *sp, int depth)
{
    int pc = from;

    if (depth > 64) {
        return -1;
    }
    while (pc < to) {
        const ps_word_t *w = &f->code[pc++];
        double a, b;
        int n = *sp;

        if (w->op == 0) {
            if (n >= PS_STACK) return -1;
            st[(*sp)++] = w->v;
            continue;
        }
        if (w->op == PS_BLOCK) {
            /* Blocks are taken by the if or ifelse after them. */
            int b1 = pc - 1, e1 = w->to, b2 = -1, e2 = -1, next = e1;

            if (next < to && f->code[next].op == PS_BLOCK) {
                b2 = next;
                e2 = f->code[next].to;
                next = e2;
            }
            if (next >= to) return -1;
            if (f->code[next].op == PS_IF && b2 < 0) {
                if (n < 1) return -1;
                (*sp)--;
                if (st[*sp] != 0 && ps_run(f, b1 + 1, e1, st, sp, depth + 1))
                    return -1;
            } else if (f->code[next].op == PS_IFELSE && b2 >= 0) {
                if (n < 1) return -1;
                (*sp)--;
                if (ps_run(f, st[*sp] != 0 ? b1 + 1 : b2 + 1,
                           st[*sp] != 0 ? e1 : e2, st, sp, depth + 1))
                    return -1;
            } else {
                return -1;
            }
            pc = next + 1;
            continue;
        }
        switch (w->op) {
        case PS_ADD: case PS_SUB: case PS_MUL: case PS_DIV: case PS_IDIV:
        case PS_MOD: case PS_ATAN: case PS_EXP: case PS_EQ: case PS_NE:
        case PS_GT: case PS_GE: case PS_LT: case PS_LE: case PS_AND:
        case PS_OR: case PS_XOR: case PS_BITSHIFT:
            if (n < 2) return -1;
            b = st[--(*sp)];
            a = st[--(*sp)];
            switch (w->op) {
            case PS_ADD: a += b; break;
            case PS_SUB: a -= b; break;
            case PS_MUL: a *= b; break;
            case PS_DIV: a = b != 0 ? a / b : 0; break;
            case PS_IDIV: a = ps_int(b) != 0
                              ? (double)(ps_int(a) / ps_int(b)) : 0; break;
            case PS_MOD: a = ps_int(b) != 0
                             ? (double)(ps_int(a) % ps_int(b)) : 0; break;
            case PS_ATAN: a = atan2(a, b) * 180 / M_PI;
                          if (a < 0) a += 360; break;
            case PS_EXP: a = pow(a, b); break;
            case PS_EQ: a = a == b; break;
            case PS_NE: a = a != b; break;
            case PS_GT: a = a > b; break;
            case PS_GE: a = a >= b; break;
            case PS_LT: a = a < b; break;
            case PS_LE: a = a <= b; break;
            case PS_AND: a = (double)(ps_int(a) & ps_int(b)); break;
            case PS_OR: a = (double)(ps_int(a) | ps_int(b)); break;
            case PS_XOR: a = (double)(ps_int(a) ^ ps_int(b)); break;
            default: a = ps_shift(a, b);
            }
            st[(*sp)++] = a;
            break;
        case PS_NEG: case PS_ABS: case PS_SQRT: case PS_SIN: case PS_COS:
        case PS_LN: case PS_LOG: case PS_CVI: case PS_CVR: case PS_FLOOR:
        case PS_CEILING: case PS_ROUND: case PS_TRUNCATE: case PS_NOT:
            if (n < 1) return -1;
            a = st[*sp - 1];
            switch (w->op) {
            case PS_NEG: a = -a; break;
            case PS_ABS: a = fabs(a); break;
            case PS_SQRT: a = a > 0 ? sqrt(a) : 0; break;
            case PS_SIN: a = sin(a * M_PI / 180); break;
            case PS_COS: a = cos(a * M_PI / 180); break;
            case PS_LN: a = a > 0 ? log(a) : 0; break;
            case PS_LOG: a = a > 0 ? log10(a) : 0; break;
            case PS_CVI: a = (double)ps_int(a); break;
            case PS_TRUNCATE: a = trunc(a); break;
            case PS_FLOOR: a = floor(a); break;
            case PS_CEILING: a = ceil(a); break;
            case PS_ROUND: a = floor(a + 0.5); break;
            case PS_NOT: a = (a == 0 || a == 1) ? !(int)a
                                                : (double)~ps_int(a); break;
            default: break;
            }
            st[*sp - 1] = a;
            break;
        case PS_DUP:
            if (n < 1 || n >= PS_STACK) return -1;
            st[*sp] = st[*sp - 1];
            (*sp)++;
            break;
        case PS_POP:
            if (n < 1) return -1;
            (*sp)--;
            break;
        case PS_EXCH:
            if (n < 2) return -1;
            a = st[n - 1]; st[n - 1] = st[n - 2]; st[n - 2] = a;
            break;
        case PS_COPY: {
            int k, i;

            if (n < 1) return -1;
            k = (int)ps_int(st[--(*sp)]);
            if (k < 0 || k > *sp || *sp + k > PS_STACK) return -1;
            for (i = 0; i < k; i++) st[*sp + i] = st[*sp - k + i];
            *sp += k;
            break;
        }
        case PS_INDEX: {
            int k;

            if (n < 1) return -1;
            k = (int)ps_int(st[--(*sp)]);
            if (k < 0 || k >= *sp) return -1;
            st[*sp] = st[*sp - 1 - k];
            (*sp)++;
            break;
        }
        case PS_ROLL: {
            int k, j, i;
            double tmp[PS_STACK];

            if (n < 2) return -1;
            j = (int)ps_int(st[--(*sp)]);
            k = (int)ps_int(st[--(*sp)]);
            if (k <= 0 || k > *sp) return -1;
            j = ((j % k) + k) % k;
            for (i = 0; i < k; i++) tmp[(i + j) % k] = st[*sp - k + i];
            for (i = 0; i < k; i++) st[*sp - k + i] = tmp[i];
            break;
        }
        case PS_TRUE: case PS_FALSE:
            if (n >= PS_STACK) return -1;
            st[(*sp)++] = w->op == PS_TRUE;
            break;
        case PS_IF: case PS_IFELSE:
            return -1;              /* without a block before it */
        default:
            return -1;
        }
    }
    return 0;
}

/** @brief Load a function or component array with bounded recursion. */
static pdf_func_t *
func_load(tiku_pdf_t *doc, pdf_obj_t *spec, int depth)
{
    pdf_func_t *f;
    int i;

    spec = pdf_resolve(doc, spec);
    if (depth > FUNC_DEPTH || spec == NULL ||
        (spec->kind != PDF_DICT && spec->kind != PDF_STREAM &&
         spec->kind != PDF_ARRAY)) {
        return NULL;
    }
    f = calloc(1, sizeof *f);
    if (f == NULL) {
        return NULL;
    }
    if (spec->kind == PDF_ARRAY) {
        if (spec->u.a.n < 1 || spec->u.a.n > PDF_FUNC_MAX_OUT) {
            pdf_func_free(f);
            return NULL;
        }
        f->type = -2;
        for (i = 0; i < spec->u.a.n; i++) {
            f->part[i] = func_load(doc, spec->u.a.item[i], depth + 1);
            f->nparts++;
            if (f->part[i] == NULL || f->part[i]->nout != 1 ||
                (i && f->part[i]->nin != f->nin)) {
                pdf_func_free(f);
                return NULL;
            }
            f->nin = f->part[i]->nin;
        }
        f->nout = f->nparts;
        return f;
    }
    f->type = pdf_int(pdf_get(doc, spec, "FunctionType"), -1);
    f->nin = numbers(doc, pdf_get(doc, spec, "Domain"), f->domain,
                     PDF_FUNC_MAX_IN * 2) / 2;
    f->nout = numbers(doc, pdf_get(doc, spec, "Range"), f->range,
                      PDF_FUNC_MAX_OUT * 2) / 2;
    f->has_range = f->nout > 0;
    if (f->nin < 1) {
        f->nin = 1;
        f->domain[0] = 0;
        f->domain[1] = 1;
    }
    if (f->type == 2) {
        int n0 = numbers(doc, pdf_get(doc, spec, "C0"), f->c0,
                         PDF_FUNC_MAX_OUT);
        int n1 = numbers(doc, pdf_get(doc, spec, "C1"), f->c1,
                         PDF_FUNC_MAX_OUT);

        if (n0 == 0) { f->c0[0] = 0; n0 = 1; }
        if (n1 == 0) { f->c1[0] = 1; n1 = 1; }
        f->nout = n0 < n1 ? n0 : n1;
        f->expo = pdf_number(pdf_get(doc, spec, "N"), 1);
    } else if (f->type == 3) {
        pdf_obj_t *parts = pdf_get(doc, spec, "Functions");

        if (parts != NULL && parts->kind == PDF_ARRAY) {
            for (i = 0; i < parts->u.a.n && f->nparts < 16; i++) {
                f->part[f->nparts] = func_load(doc, parts->u.a.item[i],
                                               depth + 1);
                if (f->part[f->nparts] != NULL) {
                    f->nparts++;
                }
            }
        }
        (void)numbers(doc, pdf_get(doc, spec, "Bounds"), f->bounds, 16);
        if (numbers(doc, pdf_get(doc, spec, "Encode"), f->encode3, 32) <
            f->nparts * 2) {
            for (i = 0; i < f->nparts; i++) {
                f->encode3[2 * i] = 0;
                f->encode3[2 * i + 1] = 1;
            }
        }
        f->nout = f->nparts > 0 ? f->part[0]->nout : 0;
    } else if (f->type == 0 && spec->kind == PDF_STREAM) {
        double cells;
        long n = 0;

        f->bps = pdf_int(pdf_get(doc, spec, "BitsPerSample"), 8);
        if (numbers(doc, pdf_get(doc, spec, "Size"), f->encode0,
                    PDF_FUNC_MAX_IN)
            < f->nin) {
            pdf_func_free(f);
            return NULL;
        }
        for (i = 0; i < f->nin; i++) {
            f->size[i] = f->encode0[i] > 1 && f->encode0[i] < SAMPLES_MAX
                         ? (int)f->encode0[i] : 1;
        }
        if (numbers(doc, pdf_get(doc, spec, "Encode"), f->encode0,
                    PDF_FUNC_MAX_IN * 2) < f->nin * 2) {
            for (i = 0; i < f->nin; i++) {
                f->encode0[2 * i] = 0;
                f->encode0[2 * i + 1] = f->size[i] - 1;
            }
        }
        if (numbers(doc, pdf_get(doc, spec, "Decode"), f->decode0,
                    PDF_FUNC_MAX_OUT * 2) < f->nout * 2) {
            memcpy(f->decode0, f->range, sizeof f->decode0);
        }
        if (pdf_stream_data(doc, spec, &f->samples, &n, NULL) != 0 ||
            n > SAMPLES_MAX || (f->bps != 1 && f->bps != 2 && f->bps != 4 &&
            f->bps != 8 && f->bps != 16 && f->bps != 24 && f->bps != 32)) {
            pdf_func_free(f);
            return NULL;
        }
        f->nsamples = n;
        for (i = 0, cells = f->nout; i < f->nin; i++) {
            cells *= f->size[i];
        }
        if (cells * f->bps > 8.0 * SAMPLES_MAX) {
            pdf_func_free(f);       /* past SAMPLES_MAX bytes: not read */
            return NULL;
        }
    } else if (f->type == 4 && spec->kind == PDF_STREAM) {
        unsigned char *data = NULL;
        long n = 0;
        pdf_cur_t c;

        f->code = calloc(PS_MAX_CODE, sizeof *f->code);
        if (f->code == NULL ||
            pdf_stream_data(doc, spec, &data, &n, NULL) != 0) {
            pdf_func_free(f);
            return NULL;
        }
        c.p = data;
        c.n = n;
        c.pos = 0;
        if (ps_compile(f, &c, 0) != 0) {
            free(data);
            pdf_func_free(f);
            return NULL;
        }
        free(data);
        /* The outer braces: the program is one block at the top. */
        if (f->ncode > 0 && f->code[0].op == PS_BLOCK) {
            memmove(f->code, f->code + 1, (size_t)(f->ncode - 1) *
                    sizeof f->code[0]);
            f->ncode--;
            for (i = 0; i < f->ncode; i++) {
                if (f->code[i].op == PS_BLOCK) f->code[i].to--;
            }
        }
    } else {
        pdf_func_free(f);
        return NULL;
    }
    if (f->nout > PDF_FUNC_MAX_OUT) {
        f->nout = PDF_FUNC_MAX_OUT;
    }
    return f;
}

pdf_func_t *
pdf_func_load(tiku_pdf_t *doc, pdf_obj_t *spec)
{
    return func_load(doc, spec, 0);
}

void
pdf_func_free(pdf_func_t *f)
{
    int i;

    if (f == NULL) {
        return;
    }
    for (i = 0; i < f->nparts; i++) {
        pdf_func_free(f->part[i]);
    }
    free(f->samples);
    free(f->code);
    free(f);
}

/** @brief Sample @p i of a type 0 function's table, as 0..1. */
static double
sample_at(const pdf_func_t *f, long i)
{
    long bit = i * f->bps, byte = bit / 8;
    unsigned long v = 0;
    int k;

    if (byte >= f->nsamples) {
        return 0;
    }
    if (f->bps >= 8) {
        for (k = 0; k < f->bps / 8; k++) {
            v = (v << 8) | (byte + k < f->nsamples ? f->samples[byte + k] : 0);
        }
    } else {
        v = ((unsigned)f->samples[byte] >> (8 - f->bps - bit % 8)) &
            ((1u << f->bps) - 1);
    }
    return (double)v / (double)(((unsigned long)1 << f->bps) - 1);
}

/** @brief A type 0 function: nearest sample along every axis but the
 *        first, which is interpolated. */
static int
eval_sampled(const pdf_func_t *f, const double *in, double *out)
{
    long index = 0, stride = 1, i0 = 0;
    double frac = 0;
    int i, j;

    for (i = 0; i < f->nin; i++) {
        double d0 = f->domain[2 * i], d1 = f->domain[2 * i + 1];
        double x = in[i] < d0 ? d0 : in[i] > d1 ? d1 : in[i];
        double e0 = f->encode0[2 * i], e1 = f->encode0[2 * i + 1];
        double e = d1 > d0 ? e0 + (x - d0) * (e1 - e0) / (d1 - d0) : 0;
        long k;

        if (!(e > 0)) e = 0;                        /* a NaN too */
        if (e > f->size[i] - 1) e = f->size[i] - 1;
        k = (long)e;
        if (i == 0) {
            i0 = k;
            frac = e - k;
        }
        index += k * stride;
        stride *= f->size[i];
    }
    for (j = 0; j < f->nout; j++) {
        double s0 = sample_at(f, index * f->nout + j), s = s0;

        if (frac > 0 && i0 + 1 < f->size[0]) {
            double s1 = sample_at(f, (index + 1) * f->nout + j);

            s = s0 + (s1 - s0) * frac;
        }
        out[j] = f->decode0[2 * j] + s * (f->decode0[2 * j + 1] -
                                          f->decode0[2 * j]);
    }
    return f->nout;
}

static int eval(const pdf_func_t *f, const double *in, int nin, double *out,
                int depth);

/** @brief A type 3 function: the part whose bounds hold @p x. */
static int
eval_stitched(const pdf_func_t *f, double x, double *out, int depth)
{
    double d0 = f->domain[0], d1 = f->domain[1], lo, hi, e;
    int k = 0;

    if (f->nparts == 0) {
        return 0;
    }
    if (x < d0) x = d0;
    if (x > d1) x = d1;
    while (k < f->nparts - 1 && x >= f->bounds[k]) {
        k++;
    }
    lo = k == 0 ? d0 : f->bounds[k - 1];
    hi = k == f->nparts - 1 ? d1 : f->bounds[k];
    e = hi > lo ? f->encode3[2 * k] + (x - lo) * (f->encode3[2 * k + 1] -
                                                  f->encode3[2 * k]) / (hi - lo)
                : f->encode3[2 * k];
    return eval(f->part[k], &e, 1, out, depth + 1);
}

/** @brief @p f applied to @p in, by its type; @return how many outputs. */
static int
eval(const pdf_func_t *f, const double *in, int nin, double *out, int depth)
{
    int n = 0, j;

    if (f == NULL || depth > FUNC_DEPTH || in == NULL || out == NULL ||
        nin < f->nin) {
        return 0;
    }
    if (f->type == -2) {
        for (j = 0; j < f->nparts; j++) {
            if (eval(f->part[j], in, nin, out + j, depth + 1) != 1) {
                return 0;
            }
        }
        n = f->nparts;
    } else if (f->type == 2) {
        double x = in[0] < f->domain[0] ? f->domain[0]
                   : in[0] > f->domain[1] ? f->domain[1] : in[0];
        double t = f->expo == 1 ? x : pow(x, f->expo);

        for (j = 0; j < f->nout; j++) {
            out[j] = f->c0[j] + t * (f->c1[j] - f->c0[j]);
        }
        n = f->nout;
    } else if (f->type == 3) {
        n = eval_stitched(f, in[0], out, depth);
    } else if (f->type == 0) {
        n = eval_sampled(f, in, out);
    } else if (f->type == 4) {
        double st[PS_STACK];
        int sp = 0;

        for (j = 0; j < nin && j < PS_STACK; j++) {
            st[sp++] = in[j];
        }
        if (ps_run(f, 0, f->ncode, st, &sp, 0) != 0) {
            return 0;
        }
        n = f->has_range && sp > f->nout ? f->nout : sp;
        if (n > PDF_FUNC_MAX_OUT) n = PDF_FUNC_MAX_OUT;
        for (j = 0; j < n; j++) {
            out[j] = st[sp - n + j];
        }
    }
    if (f->has_range) {
        for (j = 0; j < n && j < f->nout; j++) {
            if (out[j] < f->range[2 * j]) out[j] = f->range[2 * j];
            if (out[j] > f->range[2 * j + 1]) out[j] = f->range[2 * j + 1];
        }
    }
    return n;
}

int
pdf_func_eval(const pdf_func_t *f, const double *in, int nin, double *out)
{
    return eval(f, in, nin, out, 0);
}

/* -------------------------------------------------------- colour spaces  */

/** @brief A 0..1 part as a byte, whatever the file said (NaN is 0). */
static uint32_t
part(double v)
{
    return !(v > 0) ? 0 : v >= 1 ? 255 : (uint32_t)(v * 255 + 0.5);
}

/** @brief Three 0..1 parts as one opaque pixel. */
static uint32_t
rgb(double r, double g, double b)
{
    return 0xff000000u | part(r) << 16 | part(g) << 8 | part(b);
}

/** @brief A space by its family name: the device families, and the
 *         ones read as them (CalGray, CalRGB, Lab, Pattern). */
static pdf_cs_t *
device_space(const char *name)
{
    pdf_cs_t *cs = calloc(1, sizeof *cs);

    if (cs == NULL) {
        return NULL;
    }
    if (!strcmp(name, "DeviceGray") || !strcmp(name, "G") ||
        !strcmp(name, "CalGray")) {
        cs->kind = CS_GRAY;
        cs->n = 1;
    } else if (!strcmp(name, "DeviceRGB") || !strcmp(name, "RGB") ||
               !strcmp(name, "CalRGB")) {
        cs->kind = CS_RGB;
        cs->n = 3;
    } else if (!strcmp(name, "DeviceCMYK") || !strcmp(name, "CMYK")) {
        cs->kind = CS_CMYK;
        cs->n = 4;
        cs->initial[3] = 1;
    } else if (!strcmp(name, "Pattern")) {
        cs->kind = CS_PATTERN;
        cs->n = 1;
    } else if (!strcmp(name, "Lab")) {
        cs->kind = CS_LAB;
        cs->n = 3;
    } else {
        free(cs);
        return NULL;
    }
    return cs;
}

pdf_cs_t *
pdf_cs_device(int n)
{
    return device_space(n == 4 ? "DeviceCMYK" : n == 3 ? "DeviceRGB"
                                                       : "DeviceGray");
}

pdf_cs_t *
pdf_cs_load(tiku_pdf_t *doc, pdf_obj_t *spec, pdf_obj_t *res)
{
    pdf_cs_t *cs;
    pdf_obj_t *fam;
    const char *name;

    spec = pdf_resolve(doc, spec);
    if (spec == NULL) {
        return NULL;
    }
    if (spec->kind == PDF_NAME) {
        pdf_obj_t *named;

        cs = device_space((const char *)spec->u.s.p);
        if (cs != NULL) {
            return cs;
        }
        /* A name the resources define, such as /CS0. */
        named = pdf_get(doc, pdf_get(doc, res, "ColorSpace"),
                        (const char *)spec->u.s.p);
        return (named != NULL && named->kind != PDF_NAME)
               ? pdf_cs_load(doc, named, NULL) : NULL;
    }
    if (spec->kind != PDF_ARRAY || spec->u.a.n < 1) {
        return NULL;
    }
    fam = pdf_resolve(doc, spec->u.a.item[0]);
    if (fam == NULL || fam->kind != PDF_NAME) {
        return NULL;
    }
    name = (const char *)fam->u.s.p;
    if (spec->u.a.n == 1) {
        return device_space(name);
    }
    if (!strcmp(name, "ICCBased")) {
        pdf_obj_t *st = pdf_resolve(doc, spec->u.a.item[1]);
        int n = pdf_int(pdf_get(doc, st, "N"), 0);
        pdf_obj_t *alt = pdf_get(doc, st, "Alternate");

        if (alt != NULL && (cs = pdf_cs_load(doc, alt, res)) != NULL) {
            return cs;
        }
        return device_space(n == 4 ? "DeviceCMYK" : n == 1 ? "DeviceGray"
                            : "DeviceRGB");
    }
    if (!strcmp(name, "CalRGB") || !strcmp(name, "CalGray") ||
        !strcmp(name, "Lab")) {
        return device_space(name);
    }
    if (!strcmp(name, "Indexed") || !strcmp(name, "I")) {
        pdf_obj_t *lookup;

        if (spec->u.a.n < 4) {
            return NULL;
        }
        cs = calloc(1, sizeof *cs);
        if (cs == NULL) {
            return NULL;
        }
        cs->kind = CS_INDEXED;
        cs->n = 1;
        cs->base = pdf_cs_load(doc, spec->u.a.item[1], res);
        cs->hival = pdf_int(pdf_resolve(doc, spec->u.a.item[2]), 0);
        lookup = pdf_resolve(doc, spec->u.a.item[3]);
        if (cs->base == NULL || cs->hival < 0 || cs->hival > 255) {
            pdf_cs_free(cs);
            return NULL;
        }
        cs->lookup = calloc((size_t)(cs->hival + 1) * (size_t)cs->base->n,
                            1);
        if (cs->lookup == NULL) {
            pdf_cs_free(cs);
            return NULL;
        }
        if (lookup != NULL && lookup->kind == PDF_STRING) {
            size_t want = (size_t)(cs->hival + 1) * (size_t)cs->base->n;

            memcpy(cs->lookup, lookup->u.s.p,
                   lookup->u.s.n < want ? lookup->u.s.n : want);
        } else if (lookup != NULL && lookup->kind == PDF_STREAM) {
            unsigned char *data = NULL;
            long n = 0;

            if (pdf_stream_data(doc, lookup, &data, &n, NULL) == 0) {
                size_t want = (size_t)(cs->hival + 1) * (size_t)cs->base->n;

                memcpy(cs->lookup, data, (size_t)n < want ? (size_t)n : want);
                free(data);
            }
        }
        return cs;
    }
    if (!strcmp(name, "Separation") || !strcmp(name, "DeviceN")) {
        pdf_obj_t *names = pdf_resolve(doc, spec->u.a.item[1]);
        int k;

        cs = calloc(1, sizeof *cs);
        if (cs == NULL) {
            return NULL;
        }
        cs->kind = CS_SEP;
        cs->n = (names != NULL && names->kind == PDF_ARRAY) ? names->u.a.n : 1;
        if (cs->n < 1 || cs->n > PDF_FUNC_MAX_IN) {
            cs->n = 1;
        }
        for (k = 0; k < PDF_FUNC_MAX_IN; k++) {
            cs->initial[k] = 1;     /* every ink full, as 8.6.6.4 says */
        }
        if (spec->u.a.n >= 3) {
            cs->base = pdf_cs_load(doc, spec->u.a.item[2], res);
        }
        if (spec->u.a.n >= 4) {
            cs->tint = pdf_func_load(doc, spec->u.a.item[3]);
        }
        if (names != NULL && names->kind == PDF_NAME &&
            !strcmp((const char *)names->u.s.p, "None")) {
            cs->hival = -1;         /* paints nothing */
        }
        return cs;
    }
    if (!strcmp(name, "Pattern")) {
        /* [/Pattern base]: an uncoloured pattern's colour is in base. */
        cs = device_space("Pattern");
        if (cs != NULL) {
            cs->base = pdf_cs_load(doc, spec->u.a.item[1], res);
            cs->n = cs->base != NULL ? cs->base->n : 1;
        }
        return cs;
    }
    return NULL;
}

void
pdf_cs_free(pdf_cs_t *cs)
{
    if (cs == NULL) {
        return;
    }
    pdf_cs_free(cs->base);
    pdf_func_free(cs->tint);
    free(cs->lookup);
    free(cs);
}

uint32_t
pdf_cs_colour(const pdf_cs_t *cs, const double *v, int n)
{
    double c[PDF_FUNC_MAX_OUT];

    if (cs == NULL) {
        return n >= 3 ? rgb(v[0], v[1], v[2]) : n >= 1
               ? rgb(v[0], v[0], v[0]) : 0xff000000u;
    }
    switch (cs->kind) {
    case CS_GRAY:
        return n >= 1 ? rgb(v[0], v[0], v[0]) : 0xff000000u;
    case CS_RGB:
        return n >= 3 ? rgb(v[0], v[1], v[2]) : 0xff000000u;
    case CS_CMYK:
        if (n < 4) {
            return 0xff000000u;
        }
        return rgb((1 - v[0]) * (1 - v[3]), (1 - v[1]) * (1 - v[3]),
                   (1 - v[2]) * (1 - v[3]));
    case CS_LAB: {
        /* L alone, as a grey: a and b are not read. */
        double l = n >= 1 ? v[0] / 100.0 : 0;

        return rgb(l, l, l);
    }
    case CS_INDEXED: {
        double x = n >= 1 ? v[0] + 0.5 : 0;
        int i = x > 0 ? (x < cs->hival ? (int)x : cs->hival) : 0, k;

        for (k = 0; k < cs->base->n; k++) {
            c[k] = cs->lookup[(size_t)i * cs->base->n + k] / 255.0;
        }
        return pdf_cs_colour(cs->base, c, cs->base->n);
    }
    case CS_SEP: {
        int m = 0;

        if (cs->hival < 0) {
            return 0x00000000u;     /* None: not painted */
        }
        if (cs->tint != NULL && cs->base != NULL) {
            m = pdf_func_eval(cs->tint, v, n, c);
        }
        if (cs->base != NULL && m >= cs->base->n) {
            return pdf_cs_colour(cs->base, c, cs->base->n);
        }
        /* No usable transform: a tint of 1 is full ink, 0 is paper. */
        {
            double t = n >= 1 ? 1 - v[0] : 0;

            return rgb(t, t, t);
        }
    }
    case CS_PATTERN:
        /* The colour an uncoloured pattern paints in; grey for the rest,
         * whose colours are their own. */
        return cs->base != NULL ? pdf_cs_colour(cs->base, v, n) : 0xff808080u;
    default:
        return 0xff808080u;
    }
}

void
pdf_cs_sample(const pdf_cs_t *cs, const unsigned char *s, int bpc, long i,
              const double *decode, unsigned char *rgbout)
{
    int n = cs != NULL ? cs->n : 1, k;
    double v[PDF_FUNC_MAX_IN];
    uint32_t col;
    double maxv = (double)((1u << bpc) - 1);

    if (n > PDF_FUNC_MAX_IN) n = PDF_FUNC_MAX_IN;
    for (k = 0; k < n; k++) {
        long bit = (i * n + k) * bpc, byte = bit / 8;
        unsigned raw;

        if (bpc == 8) {
            raw = s[byte];
        } else if (bpc == 16) {
            raw = ((unsigned)s[byte] << 8 | s[byte + 1]) >> 8;
            maxv = 255;
        } else {
            raw = ((unsigned)s[byte] >> (8 - bpc - bit % 8)) &
                  ((1u << bpc) - 1);
        }
        if (cs != NULL && cs->kind == CS_INDEXED) {
            v[k] = decode != NULL ? decode[0] + raw * (decode[1] - decode[0]) /
                                    maxv : raw;
        } else if (cs != NULL && cs->kind == CS_LAB) {
            v[k] = raw / maxv * (k == 0 ? 100 : 255) - (k == 0 ? 0 : 128);
        } else {
            v[k] = raw / maxv;
            if (decode != NULL) {
                v[k] = decode[2 * k] + v[k] * (decode[2 * k + 1] -
                                               decode[2 * k]);
            }
        }
    }
    col = pdf_cs_colour(cs, v, n);
    rgbout[0] = (col >> 16) & 0xff;
    rgbout[1] = (col >> 8) & 0xff;
    rgbout[2] = col & 0xff;
}
