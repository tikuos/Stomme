/*
 * KintsugiOS v0.01 kits -- the foundation KintsugiDesktop and KintsugiTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 *
 * tiku_pdf_writer.c - pages drawn, written as a PDF.
 *
 * Objects go out as soon as they are whole -- a page when the next one
 * begins, a picture when it is given -- and their offsets are kept for
 * the cross-reference table.  What cannot be whole until the end waits:
 * faces, which take in every glyph drawn with them; links, which may lead
 * to pages not yet drawn; the outline; and the resources.  Each page
 * begins by turning its space over, so everything after is y downwards.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_inflate.h"
#include "tiku_pdf_writer_private.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PW_SHEAR 0.2   /* a made-up italic's slant          */
#define PW_EMBOLD 0.03 /* a made-up bold's stroke, of size  */

/* ----------------------------------------------------------- buffers */

void
pw_put(pw_buf_t *b, const void *s, size_t n)
{
    if (b->bad || n == 0) {
        return;
    }
    if (b->n > (size_t)512 << 20 || n > ((size_t)512 << 20) - b->n) {
        b->bad = 1;
        return;
    }
    if (b->n + n > b->cap) {
        size_t cap = b->cap ? b->cap : 4096;
        unsigned char *g;

        while (cap < b->n + n) {
            cap *= 2;
        }
        if ((g = realloc(b->p, cap)) == NULL) {
            b->bad = 1;
            return;
        }
        b->p = g;
        b->cap = cap;
    }
    memcpy(b->p + b->n, s, n);
    b->n += n;
}

void
pw_printf(pw_buf_t *b, const char *fmt, ...)
{
    char s[512];
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vsnprintf(s, sizeof s, fmt, ap);
    va_end(ap);
    if (n > 0) {
        pw_put(b, s, (size_t)n < sizeof s ? (size_t)n : sizeof s - 1);
    }
}

void
pw_num(pw_buf_t *b, double v)
{
    char s[40];
    long long m;
    int k = 0, frac;

    if (!(v == v) || v > 1e9 || v < -1e9) {
        v = 0;
    }
    m = llround(v * 1000.0);
    if (m < 0) {
        s[k++] = '-';
        m = -m;
    }
    k += snprintf(s + k, sizeof s - (size_t)k, "%lld", m / 1000);
    if ((frac = (int)(m % 1000)) != 0) {
        k += snprintf(s + k, sizeof s - (size_t)k, ".%03d", frac);
        while (s[k - 1] == '0') {
            k--;
        }
    }
    s[k++] = ' ';
    pw_put(b, s, (size_t)k);
}

/** @brief A colour's red, green and blue as PDF's three numbers, then
 *         @p op ("rg" to fill, "RG" to stroke). */
static void
colour(pw_buf_t *b, uint32_t argb, const char *op)
{
    pw_num(b, ((argb >> 16) & 0xff) / 255.0);
    pw_num(b, ((argb >> 8) & 0xff) / 255.0);
    pw_num(b, (argb & 0xff) / 255.0);
    pw_printf(b, "%s\n", op);
}

/** @brief @p s as a PDF text string: UTF-16 with its mark, in hex, so
 *         any character and any byte goes in as it is. */
static void
text_string(pw_buf_t *b, const char *s)
{
    const unsigned char *p = (const unsigned char *)(s ? s : "");

    pw_printf(b, "<FEFF");
    while (*p) {
        uint32_t cp = *p++;
        int more = cp >= 0xf0 ? 3 : cp >= 0xe0 ? 2 : cp >= 0xc0 ? 1 : 0;

        cp &= more == 3 ? 0x07 : more == 2 ? 0x0f : more == 1 ? 0x1f : 0x7f;
        while (more-- > 0 && (*p & 0xc0) == 0x80) {
            cp = cp << 6 | (*p++ & 0x3f);
        }
        if (cp >= 0x10000) {
            cp -= 0x10000;
            pw_printf(b, "%04X%04X", 0xd800 + (cp >> 10),
                      0xdc00 + (cp & 0x3ff));
        } else {
            pw_printf(b, "%04X", cp);
        }
    }
    pw_printf(b, ">");
}

/** @brief @p s as a PDF literal string of its bytes: parentheses and
 *         backslashes escaped, anything unprintable in octal. */
static void
byte_string(pw_buf_t *b, const char *s)
{
    pw_put(b, "(", 1);
    for (; s != NULL && *s; s++) {
        unsigned char c = (unsigned char)*s;

        if (c == '(' || c == ')' || c == '\\') {
            pw_printf(b, "\\%c", c);
        } else if (c < 0x20 || c >= 0x7f) {
            pw_printf(b, "\\%03o", c);
        } else {
            pw_put(b, &c, 1);
        }
    }
    pw_put(b, ")", 1);
}

int
pw_reserve(tiku_pdf_writer_t *w)
{
    if (w->nobj + 1 >= w->xcap) {
        int cap = w->xcap ? w->xcap * 2 : 256;
        long *g = realloc(w->xref, (size_t)cap * sizeof *g);

        if (g == NULL) {
            w->out.bad = 1;
            return 0;
        }
        memset(g + w->xcap, 0, (size_t)(cap - w->xcap) * sizeof *g);
        w->xref = g;
        w->xcap = cap;
    }
    return ++w->nobj;
}

void
pw_begin(tiku_pdf_writer_t *w, int obj)
{
    if (obj > 0 && obj <= w->nobj) {
        w->xref[obj] = (long)w->out.n;
    }
    pw_printf(&w->out, "%d 0 obj\n", obj);
}

void
pw_stream(tiku_pdf_writer_t *w, int obj, const char *dict,
          const unsigned char *data, size_t n, int deflate)
{
    size_t zn = 0;
    unsigned char *z =
        deflate && n > 64 ? tiku_deflate_zlib(data, n, &zn) : NULL;

    if (z != NULL && zn >= n) {
        free(z);
        z = NULL;
    }
    pw_begin(w, obj);
    pw_printf(&w->out, "<< %s%s/Length %zu >>\nstream\n", dict,
              z != NULL ? "/Filter /FlateDecode " : "", z ? zn : n);
    pw_put(&w->out, z ? z : data, z ? zn : n);
    pw_printf(&w->out, "\nendstream\nendobj\n");
    free(z);
}

int
pw_resource(tiku_pdf_writer_t *w, char kind, int key, int obj)
{
    int i, n = 0;

    for (i = 0; i < w->nres; i++) {
        if (w->res[i].kind == kind && w->res[i].key == key) {
            return i;
        }
        n += w->res[i].kind == kind;
    }
    if (w->nres == w->rescap) {
        int cap = w->rescap ? w->rescap * 2 : 32;
        pw_res_t *g = realloc(w->res, (size_t)cap * sizeof *g);

        if (g == NULL) {
            w->out.bad = 1; /* a state or shading left out */
            return -1;
        }
        w->res = g;
        w->rescap = cap;
    }
    if (obj == 0 && (obj = pw_reserve(w)) == 0) {
        return -1;
    }
    w->res[w->nres].kind = kind;
    w->res[w->nres].n = n;
    w->res[w->nres].key = key;
    w->res[w->nres].obj = obj;
    return w->nres++;
}

/** @brief Resource @p r's name, as the pages write it. */
static void
res_name(pw_buf_t *b, const pw_res_t *r)
{
    pw_printf(b, "/%s%d ",
              r->kind == 'F'   ? "F"
              : r->kind == 'I' ? "Im"
              : r->kind == 'G' ? "GS"
                               : "Sh",
              r->n);
}

/** @brief The state painting with alphas @p fill and @p stroke (0-255)
 *         needs, begun: "q /GSn gs".  @return 1 when one was begun. */
static int
alpha_begin(tiku_pdf_writer_t *w, int fill, int stroke)
{
    int r, obj;

    if (fill >= 255 && stroke >= 255) {
        return 0;
    }
    r = pw_resource(w, 'G', fill << 8 | stroke, 0);
    if (r < 0) {
        return 0;
    }
    obj = w->res[r].obj;
    if (w->xref[obj] == 0) {
        pw_begin(w, obj);
        pw_printf(&w->out, "<< /Type /ExtGState /ca ");
        pw_num(&w->out, fill / 255.0);
        pw_printf(&w->out, "/CA ");
        pw_num(&w->out, stroke / 255.0);
        pw_printf(&w->out, ">>\nendobj\n");
    }
    pw_printf(&w->content, "q ");
    res_name(&w->content, &w->res[r]);
    pw_printf(&w->content, "gs\n");
    return 1;
}

/** @brief The open page finished: its drawing and its object written,
 *         its links given the numbers they will be written under. */
static void
end_page(tiku_pdf_writer_t *w)
{
    pw_page_t *p = w->npage > 0 ? &w->page[w->npage - 1] : NULL;
    int content, i, any = 0;

    if (p == NULL) {
        return;
    }
    while (w->depth > 0) {
        pw_printf(&w->content, "Q\n");
        w->depth--;
    }
    w->out.bad |= w->content.bad | w->path.bad; /* a page cut short */
    content = pw_reserve(w);
    pw_stream(w, content, "", w->content.p, w->content.n, 1);
    w->content.n = 0;
    pw_begin(w, p->obj);
    pw_printf(&w->out, "<< /Type /Page /Parent %d 0 R /MediaBox [0 0 ",
              PW_PAGES);
    pw_num(&w->out, p->w);
    pw_num(&w->out, p->h);
    pw_printf(&w->out, "] /Resources %d 0 R /Contents %d 0 R", PW_RESOURCES,
              content);
    for (i = 0; i < w->nlink; i++) {
        if (w->link[i].page == w->npage - 1) {
            w->link[i].obj = pw_reserve(w);
            pw_printf(&w->out, "%s%d 0 R", any++ ? " " : " /Annots [",
                      w->link[i].obj);
        }
    }
    pw_printf(&w->out, "%s >>\nendobj\n", any ? "]" : "");
}

tiku_pdf_writer_t *
tiku_pdf_writer_new(void)
{
    tiku_pdf_writer_t *w = calloc(1, sizeof *w);

    if (w == NULL) {
        return NULL;
    }
    while (w->nobj < PW_FIRST - 1 && !w->out.bad) {
        (void)pw_reserve(w);
    }
    pw_printf(&w->out, "%%PDF-1.7\n%%\xe2\xe3\xcf\xd3\n");
    if (w->out.bad) {
        tiku_pdf_writer_free(w);
        return NULL;
    }
    return w;
}

void
tiku_pdf_writer_free(tiku_pdf_writer_t *w)
{
    int i;

    if (w == NULL) {
        return;
    }
    for (i = 0; i < w->nfont; i++) {
        pw_font_free(&w->font[i]);
    }
    for (i = 0; i < w->nlink; i++) {
        free(w->link[i].uri);
    }
    for (i = 0; i < w->nmark; i++) {
        free(w->mark[i].title);
    }
    free(w->out.p);
    free(w->content.p);
    free(w->path.p);
    free(w->xref);
    free(w->page);
    free(w->font);
    free(w->res);
    free(w->link);
    free(w->mark);
    free(w->title);
    free(w->author);
    free(w);
}

void
tiku_pdf_writer_info(tiku_pdf_writer_t *w, const char *title,
                     const char *author)
{
    if (w == NULL) {
        return;
    }
    free(w->title);
    free(w->author);
    w->title = title && *title ? strdup(title) : NULL;
    w->author = author && *author ? strdup(author) : NULL;
    w->out.bad |= (title && *title && w->title == NULL) ||
                  (author && *author && w->author == NULL);
}

int
tiku_pdf_writer_page(tiku_pdf_writer_t *w, double width, double height)
{
    pw_page_t *p;

    if (w == NULL || w->actual || !(width > 0) || !(height > 0)) {
        return -1;
    }
    end_page(w);
    if (w->npage == w->pagecap) {
        int cap = w->pagecap ? w->pagecap * 2 : 16;
        pw_page_t *g = realloc(w->page, (size_t)cap * sizeof *g);

        if (g == NULL) {
            return -1;
        }
        w->page = g;
        w->pagecap = cap;
    }
    p = &w->page[w->npage];
    p->w = width;
    p->h = height;
    if ((p->obj = pw_reserve(w)) == 0) {
        return -1;
    }
    w->path.n = 0;
    pw_printf(&w->content, "1 0 0 -1 0 ");
    pw_num(&w->content, height);
    pw_printf(&w->content, "cm\n");
    return w->npage++;
}

int
tiku_pdf_writer_pages(const tiku_pdf_writer_t *w)
{
    return w != NULL ? w->npage : 0;
}

/** @brief Whether there is a page to draw on. */
static int
drawing(const tiku_pdf_writer_t *w)
{
    return w != NULL && w->npage > 0;
}

void
tiku_pdf_writer_save(tiku_pdf_writer_t *w)
{
    if (drawing(w)) {
        pw_printf(&w->content, "q\n");
        w->depth++;
    }
}

void
tiku_pdf_writer_restore(tiku_pdf_writer_t *w)
{
    if (drawing(w) && w->depth > 0) {
        pw_printf(&w->content, "Q\n");
        w->depth--;
    }
}

void
tiku_pdf_writer_transform(tiku_pdf_writer_t *w, double a, double b, double c,
                          double d, double e, double f)
{
    if (!drawing(w)) {
        return;
    }
    pw_num(&w->content, a);
    pw_num(&w->content, b);
    pw_num(&w->content, c);
    pw_num(&w->content, d);
    pw_num(&w->content, e);
    pw_num(&w->content, f);
    pw_printf(&w->content, "cm\n");
}

void
tiku_pdf_writer_move(tiku_pdf_writer_t *w, double x, double y)
{
    if (drawing(w)) {
        pw_num(&w->path, x);
        pw_num(&w->path, y);
        pw_printf(&w->path, "m\n");
    }
}

void
tiku_pdf_writer_line(tiku_pdf_writer_t *w, double x, double y)
{
    if (drawing(w)) {
        pw_num(&w->path, x);
        pw_num(&w->path, y);
        pw_printf(&w->path, "l\n");
    }
}

void
tiku_pdf_writer_curve(tiku_pdf_writer_t *w, double x1, double y1, double x2,
                      double y2, double x3, double y3)
{
    if (drawing(w)) {
        pw_num(&w->path, x1);
        pw_num(&w->path, y1);
        pw_num(&w->path, x2);
        pw_num(&w->path, y2);
        pw_num(&w->path, x3);
        pw_num(&w->path, y3);
        pw_printf(&w->path, "c\n");
    }
}

void
tiku_pdf_writer_close(tiku_pdf_writer_t *w)
{
    if (drawing(w)) {
        pw_printf(&w->path, "h\n");
    }
}

void
tiku_pdf_writer_rect(tiku_pdf_writer_t *w, double x, double y, double width,
                     double height)
{
    if (drawing(w)) {
        pw_num(&w->path, x);
        pw_num(&w->path, y);
        pw_num(&w->path, width);
        pw_num(&w->path, height);
        pw_printf(&w->path, "re\n");
    }
}

/** @brief The path built so far moved into the drawing, and forgotten. */
static void
flush_path(tiku_pdf_writer_t *w)
{
    pw_put(&w->content, w->path.p, w->path.n);
    w->path.n = 0;
}

/** @brief The stroke's width, dash, cap and join, as @p p says. */
static void
pen(tiku_pdf_writer_t *w, const tiku_pdf_paint_t *p)
{
    int i;

    pw_num(&w->content, p->width > 0 ? p->width : 0);
    pw_printf(&w->content, "w %d J %d j\n", p->cap & 3, p->join & 3);
    if (p->miter >= 1) {
        pw_num(&w->content, p->miter);
        pw_printf(&w->content, "M\n");
    }
    if (p->dash != NULL && p->ndash > 0) {
        pw_printf(&w->content, "[");
        for (i = 0; i < p->ndash && i < 16; i++) {
            pw_num(&w->content, p->dash[i] > 0 ? p->dash[i] : 0.01);
        }
        pw_printf(&w->content, "] 0 d\n");
    }
}

void
tiku_pdf_writer_paint(tiku_pdf_writer_t *w, const tiku_pdf_paint_t *p)
{
    int fa, sa, gs;

    if (!drawing(w) || p == NULL || w->path.n == 0) {
        if (w != NULL) {
            w->path.n = 0;
        }
        return;
    }
    fa = (int)(p->fill >> 24);
    sa = p->width > 0 ? (int)(p->stroke >> 24) : 0;
    if (fa == 0 && sa == 0) {
        w->path.n = 0;
        return;
    }
    gs = alpha_begin(w, fa ? fa : 255, sa ? sa : 255);
    if (!gs && sa) {
        pw_printf(&w->content, "q\n");
    }
    if (fa) {
        colour(&w->content, p->fill, "rg");
    }
    if (sa) {
        colour(&w->content, p->stroke, "RG");
        pen(w, p);
    }
    flush_path(w);
    pw_printf(&w->content, "%s%s\n",
              fa && sa ? "B"
              : fa     ? "f"
                       : "S",
              fa && p->evenodd ? "*" : "");
    if (gs || sa) {
        pw_printf(&w->content, "Q\n");
    }
}

void
tiku_pdf_writer_clip(tiku_pdf_writer_t *w, int evenodd)
{
    if (!drawing(w) || w->path.n == 0) {
        return;
    }
    flush_path(w);
    pw_printf(&w->content, "W%s n\n", evenodd ? "*" : "");
}

/** @brief The function a gradient's stops make, written into @p b:
 *         one interpolation, or several stitched between the stops. */
static void
stops_function(pw_buf_t *b, const tiku_pdf_stop_t *s, int n)
{
    int i;

    if (n == 2) {
        pw_printf(b, "<< /FunctionType 2 /Domain [0 1] /N 1 /C0 [");
        colour(b, s[0].argb, "] /C1 [");
        colour(b, s[1].argb, "] >>");
        return;
    }
    pw_printf(b, "<< /FunctionType 3 /Domain [0 1] /Functions [");
    for (i = 0; i + 1 < n; i++) {
        stops_function(b, s + i, 2);
    }
    pw_printf(b, "] /Bounds [");
    for (i = 1; i + 1 < n; i++) {
        pw_num(b, s[i].at);
    }
    pw_printf(b, "] /Encode [");
    for (i = 0; i + 1 < n; i++) {
        pw_printf(b, "0 1 ");
    }
    pw_printf(b, "] >>");
}

/** @brief @p g's stops made whole into @p s: in order, between 0 and 1,
 *         with the ends' colours carried to 0 and 1.  @return how many,
 *         and their alpha's average in @p alpha. */
static int
whole_stops(const tiku_pdf_gradient_t *g, tiku_pdf_stop_t *s, int max,
            int *alpha)
{
    int i, n = 0;
    long sum = 0;
    double last = 0;

    for (i = 0; i < g->nstop && n + 2 < max; i++) {
        double at = g->stop[i].at < last ? last
                    : g->stop[i].at > 1  ? 1
                                         : g->stop[i].at;

        if (n == 0 && at > 0) {
            s[n].at = 0;
            s[n++].argb = g->stop[i].argb;
        }
        s[n].at = last = at;
        s[n++].argb = g->stop[i].argb;
        sum += (long)(g->stop[i].argb >> 24);
    }
    if (n > 0 && s[n - 1].at < 1) {
        s[n] = s[n - 1];
        s[n++].at = 1;
    }
    *alpha = g->nstop > 0 ? (int)(sum / g->nstop) : 255;
    return n;
}

void
tiku_pdf_writer_gradient(tiku_pdf_writer_t *w, const tiku_pdf_gradient_t *g)
{
    tiku_pdf_stop_t s[34];
    pw_buf_t dict;
    int n, r, alpha, gs;

    if (!drawing(w) || g == NULL || w->path.n == 0 ||
        (n = whole_stops(g, s, 34, &alpha)) < 2 ||
        (r = pw_resource(w, 'S', w->nshade++, 0)) < 0) {
        if (w != NULL) {
            w->path.n = 0;
        }
        return;
    }
    memset(&dict, 0, sizeof dict);
    pw_printf(&dict, "<< /ShadingType %d /ColorSpace /DeviceRGB /Coords [",
              g->radial ? 3 : 2);
    pw_num(&dict, g->x0);
    pw_num(&dict, g->y0);
    if (g->radial) {
        pw_num(&dict, g->r0);
    }
    pw_num(&dict, g->x1);
    pw_num(&dict, g->y1);
    if (g->radial) {
        pw_num(&dict, g->r1);
    }
    pw_printf(&dict, "] /Extend [true true] /Function ");
    stops_function(&dict, s, n);
    pw_printf(&dict, " >>");
    pw_begin(w, w->res[r].obj);
    pw_put(&w->out, dict.p, dict.n);
    pw_printf(&w->out, "\nendobj\n");
    w->out.bad |= dict.bad;
    free(dict.p);
    gs = alpha_begin(w, alpha, 255);
    if (!gs) {
        pw_printf(&w->content, "q\n");
    }
    flush_path(w);
    pw_printf(&w->content, "W n ");
    res_name(&w->content, &w->res[r]);
    pw_printf(&w->content, "sh\nQ\n");
}

int
tiku_pdf_writer_font(tiku_pdf_writer_t *w, const char *path, int index)
{
    pw_font_t *f;
    int i;

    if (w == NULL || path == NULL || !*path || index < 0 ||
        strlen(path) >= sizeof w->font[0].path) {
        return -1;
    }
    for (i = 0; i < w->nfont; i++) {
        if (w->font[i].index == index && strcmp(w->font[i].path, path) == 0)
            return i;
    }
    if (w->nfont == w->fontcap) {
        int cap = w->fontcap ? w->fontcap * 2 : 8;
        pw_font_t *g = realloc(w->font, (size_t)cap * sizeof *g);

        if (g == NULL) {
            w->out.bad = 1; /* text left out: the file fails */
            return -1;
        }
        w->font = g;
        w->fontcap = cap;
    }
    f = &w->font[w->nfont];
    memset(f, 0, sizeof *f);
    snprintf(f->path, sizeof f->path, "%s", path);
    f->index = index;
    if ((i = pw_font_load(f)) != 0 || (f->obj = pw_reserve(w)) == 0 ||
        pw_resource(w, 'F', w->nfont, f->obj) < 0) {
        w->out.bad |= i != -1; /* short of memory, not of a face */
        pw_font_free(f);
        return -1;
    }
    return w->nfont++;
}

int
tiku_pdf_writer_font_mem(tiku_pdf_writer_t *w, const void *data, size_t length,
                         int index)
{
    pw_font_t *font;

    if (!w || !data || length < 12 || length > (32u << 20) || index < 0)
        return -1;
    for (int i = 0; i < w->nfont; i++)
        if (w->font[i].data && w->font[i].index == index &&
            w->font[i].length == length &&
            !memcmp(w->font[i].data, data, length))
            return i;
    if (w->nfont >= 32)
        return -1;
    if (w->nfont == w->fontcap) {
        int capacity = w->fontcap ? w->fontcap * 2 : 8;
        pw_font_t *grown = realloc(w->font, (size_t)capacity * sizeof *grown);

        if (!grown)
            return -1;
        w->font = grown;
        w->fontcap = capacity;
    }
    font = &w->font[w->nfont];
    memset(font, 0, sizeof *font);
    font->data = malloc(length);
    if (!font->data)
        return -1;
    memcpy(font->data, data, length);
    font->length = length;
    font->index = index;
    if (pw_font_load(font) || !(font->obj = pw_reserve(w)) ||
        pw_resource(w, 'F', w->nfont, font->obj) < 0) {
        pw_font_free(font);
        return -1;
    }
    return w->nfont++;
}

/** @brief The glyphs as a TJ array: their ids in hex, and between two a
 *         nudge wherever a glyph starts other than where the one before
 *         it ends. */
static void
glyph_array(tiku_pdf_writer_t *w, pw_font_t *f, double size,
            const tiku_pdf_glyph_t *g, int n)
{
    double at = g[0].x;
    int i, open = 0;

    pw_printf(&w->content, "[");
    for (i = 0; i < n; i++) {
        unsigned gid = g[i].gid < (unsigned)f->nglyph ? g[i].gid : 0;
        double nudge = (g[i].x - at) * 1000.0 / size;

        if (i > 0 && fabs(nudge) >= 0.05) {
            pw_printf(&w->content, "%s", open ? ">" : "");
            pw_num(&w->content, -nudge);
            open = 0;
            at = g[i].x;
        }
        pw_printf(&w->content, "%s%04X", open ? "" : "<", gid);
        open = 1;
        at += f->adv[gid] * size / f->upem;
        f->used[gid] = 1;
        if (g[i].cp != 0 && f->uni[gid] == 0) {
            f->uni[gid] = g[i].cp;
        }
    }
    pw_printf(&w->content, "%s] TJ\n", open ? ">" : "");
}

void
tiku_pdf_writer_text(tiku_pdf_writer_t *w, const tiku_pdf_run_t *run,
                     const tiku_pdf_glyph_t *g, int n)
{
    pw_font_t *f;
    int r, gs;

    if (!drawing(w) || run == NULL || g == NULL || n <= 0 || run->font < 0 ||
        run->font >= w->nfont || !(run->size > 0) ||
        ((run->argb >> 24) == 0 && !run->invisible) ||
        !isfinite(run->horizontal) || run->horizontal < 0 ||
        (r = pw_resource(w, 'F', run->font, w->font[run->font].obj)) < 0) {
        return;
    }
    f = &w->font[run->font];
    gs = alpha_begin(w, (int)(run->argb >> 24), (int)(run->argb >> 24));
    pw_printf(&w->content, "BT ");
    res_name(&w->content, &w->res[r]);
    pw_num(&w->content, run->size);
    pw_printf(&w->content, "Tf ");
    pw_num(&w->content, run->horizontal > 0 ? run->horizontal * 100 : 100);
    pw_printf(&w->content, "Tz ");
    colour(&w->content, run->argb, "rg");
    if (run->invisible) {
        pw_printf(&w->content, "3 Tr ");
    } else if (run->outline) {
        colour(&w->content, run->argb, "RG");
        pw_num(&w->content, run->size / 16);
        pw_printf(&w->content, "w 1 Tr ");
    } else if (run->bold) { /* the mode outlives the text object */
        colour(&w->content, run->argb, "RG");
        pw_num(&w->content, run->size * PW_EMBOLD);
        pw_printf(&w->content, "w 2 Tr ");
    } else {
        pw_printf(&w->content, "0 Tr ");
    }
    pw_printf(&w->content, "1 0 ");
    pw_num(&w->content, run->italic ? PW_SHEAR : 0);
    pw_printf(&w->content, "-1 ");
    pw_num(&w->content, run->x + g[0].x);
    pw_num(&w->content, run->y);
    pw_printf(&w->content, "Tm ");
    glyph_array(w, f, run->size, g, n);
    pw_printf(&w->content, "ET\n%s", gs ? "Q\n" : "");
}

/** @brief Validate a bounded UTF-8 string without reading past its terminator.
 */
static int
actual_valid(const char *text)
{
    const unsigned char *p = (const unsigned char *)text;
    size_t bytes = 0;

    if (!text)
        return 0;
    while (*p) {
        uint32_t cp = *p++;
        unsigned more = cp < 0x80                  ? 0
                        : cp >= 0xc2 && cp <= 0xdf ? 1
                        : cp >= 0xe0 && cp <= 0xef ? 2
                        : cp >= 0xf0 && cp <= 0xf4 ? 3
                                                   : 4;
        unsigned minimum = more == 1 ? 0x80 : more == 2 ? 0x800 : 0x10000;
        if (more == 4 || (bytes += more + 1) > (1u << 20))
            return 0;
        if (more)
            cp &= (1u << (6 - more)) - 1;
        for (unsigned i = 0; i < more; i++) {
            if ((*p & 0xc0) != 0x80)
                return 0;
            cp = cp << 6 | (*p++ & 63);
        }
        if ((more && cp < minimum) || cp > 0x10ffff ||
            (cp >= 0xd800 && cp <= 0xdfff))
            return 0;
    }
    return 1;
}

int
tiku_pdf_writer_actual_begin(tiku_pdf_writer_t *w, const char *text)
{
    if (!drawing(w) || w->actual || !actual_valid(text))
        return -1;
    pw_printf(&w->content, "/Span << /ActualText ");
    text_string(&w->content, text);
    pw_printf(&w->content, " >> BDC\n");
    if (w->content.bad)
        return -1;
    w->actual = 1;
    return 0;
}

void
tiku_pdf_writer_actual_end(tiku_pdf_writer_t *w)
{
    if (!drawing(w) || !w->actual)
        return;
    pw_printf(&w->content, "EMC\n");
    w->actual = 0;
}

int
tiku_pdf_writer_draw_crop(tiku_pdf_writer_t *w, int image,
                          const double matrix[6], const double crop[4])
{
    double width, height, determinant;
    if (!w || !matrix || !crop || image < 0 || image >= w->nres ||
        w->res[image].kind != 'I')
        return -1;
    for (unsigned i = 0; i < 6; i++)
        if (!isfinite(matrix[i]))
            return -1;
    for (unsigned i = 0; i < 4; i++)
        if (!isfinite(crop[i]) || crop[i] < 0 || crop[i] >= 1)
            return -1;
    determinant = matrix[0] * matrix[3] - matrix[1] * matrix[2];
    if (!isfinite(determinant) || determinant == 0)
        return -1;
    width = 1 - crop[0] - crop[2];
    height = 1 - crop[1] - crop[3];
    if (width <= 0 || height <= 0 || !drawing(w))
        return -1;
    pw_printf(&w->content, "q ");
    for (unsigned i = 0; i < 6; i++)
        pw_num(&w->content, matrix[i]);
    pw_printf(&w->content, "cm 0 0 1 1 re W n ");
    pw_num(&w->content, 1 / width);
    pw_printf(&w->content, "0 0 ");
    pw_num(&w->content, -1 / height);
    pw_num(&w->content, -crop[0] / width);
    pw_num(&w->content, (1 - crop[1]) / height);
    pw_printf(&w->content, "cm ");
    res_name(&w->content, &w->res[image]);
    pw_printf(&w->content, "Do Q\n");
    return w->content.bad ? -1 : 0;
}

void
tiku_pdf_writer_draw(tiku_pdf_writer_t *w, int image, double x, double y,
                     double width, double height)
{
    if (!drawing(w) || image < 0 || image >= w->nres ||
        w->res[image].kind != 'I') {
        return;
    }
    pw_printf(&w->content, "q ");
    pw_num(&w->content, width);
    pw_printf(&w->content, "0 0 ");
    pw_num(&w->content, -height);
    pw_num(&w->content, x);
    pw_num(&w->content, y + height);
    pw_printf(&w->content, "cm ");
    res_name(&w->content, &w->res[image]);
    pw_printf(&w->content, "Do Q\n");
}

/** @brief Room for one link more, on the open page, its box turned to
 *         PDF's way up.  @return it, or NULL. */
static pw_link_t *
link_room(tiku_pdf_writer_t *w, double x, double y, double width, double height)
{
    pw_link_t *l;
    double ph;

    if (!drawing(w) || !(width > 0) || !(height > 0)) {
        return NULL;
    }
    if (w->nlink == w->linkcap) {
        int cap = w->linkcap ? w->linkcap * 2 : 32;
        pw_link_t *g = realloc(w->link, (size_t)cap * sizeof *g);

        if (g == NULL) {
            w->out.bad = 1;
            return NULL;
        }
        w->link = g;
        w->linkcap = cap;
    }
    l = &w->link[w->nlink++];
    memset(l, 0, sizeof *l);
    ph = w->page[w->npage - 1].h;
    l->page = w->npage - 1;
    l->x0 = x;
    l->x1 = x + width;
    l->y0 = ph - (y + height);
    l->y1 = ph - y;
    l->to = -1;
    return l;
}

void
tiku_pdf_writer_link(tiku_pdf_writer_t *w, double x, double y, double width,
                     double height, const char *uri)
{
    pw_link_t *l;

    if (uri != NULL && *uri && (l = link_room(w, x, y, width, height)) &&
        (l->uri = strdup(uri)) == NULL) {
        w->out.bad = 1;
    }
}

void
tiku_pdf_writer_jump(tiku_pdf_writer_t *w, double x, double y, double width,
                     double height, int page, double top)
{
    pw_link_t *l;

    if (page >= 0 && (l = link_room(w, x, y, width, height)) != NULL) {
        l->to = page;
        l->top = top;
    }
}

void
tiku_pdf_writer_mark(tiku_pdf_writer_t *w, int level, const char *title,
                     int page, double top)
{
    pw_mark_t *m;

    if (w == NULL || title == NULL || page < 0) {
        return;
    }
    if (w->nmark == w->markcap) {
        int cap = w->markcap ? w->markcap * 2 : 32;
        pw_mark_t *g = realloc(w->mark, (size_t)cap * sizeof *g);

        if (g == NULL) {
            w->out.bad = 1;
            return;
        }
        w->mark = g;
        w->markcap = cap;
    }
    m = &w->mark[w->nmark];
    m->level = level < 0 ? 0 : level > 8 ? 8 : level;
    m->page = page;
    m->top = top;
    if ((m->title = strdup(title)) != NULL) {
        w->nmark++;
    } else {
        w->out.bad = 1;
    }
}

/** @brief A destination: page @p page's object, @p top points down it. */
static void
dest(tiku_pdf_writer_t *w, pw_buf_t *b, int page, double top)
{
    const pw_page_t *p = &w->page[page < w->npage ? page : w->npage - 1];

    pw_printf(b, "[%d 0 R /XYZ 0 ", p->obj);
    pw_num(b, p->h - top > 0 ? p->h - top : p->h);
    pw_printf(b, "null]");
}

/** @brief Every link's annotation, now every page has its number. */
static void
write_links(tiku_pdf_writer_t *w)
{
    int i;

    for (i = 0; i < w->nlink; i++) {
        pw_link_t *l = &w->link[i];

        if (l->obj == 0) {
            continue;
        }
        pw_begin(w, l->obj);
        pw_printf(&w->out, "<< /Type /Annot /Subtype /Link /Border [0 0 0] "
                           "/Rect [");
        pw_num(&w->out, l->x0);
        pw_num(&w->out, l->y0);
        pw_num(&w->out, l->x1);
        pw_num(&w->out, l->y1);
        if (l->uri != NULL) {
            pw_printf(&w->out, "] /A << /S /URI /URI ");
            byte_string(&w->out, l->uri);
            pw_printf(&w->out, " >>");
        } else {
            pw_printf(&w->out, "] /Dest ");
            dest(w, &w->out, l->to, l->top);
        }
        pw_printf(&w->out, " >>\nendobj\n");
    }
}

/** @brief The entry after @p i at its level under the same parent, or
 *         -1. */
static int
next_sibling(const tiku_pdf_writer_t *w, int i)
{
    int k;

    for (k = i + 1; k < w->nmark && w->mark[k].level > w->mark[i].level; k++) {
    }
    return k < w->nmark && w->mark[k].level == w->mark[i].level ? k : -1;
}

/** @brief The outline written, entry @p i at object @p base + 1 + i, the
 *         root at @p base.  Levels that skip are read as one deeper. */
static void
write_outline(tiku_pdf_writer_t *w, int base)
{
    int i, k, parent[10], last[10];

    for (i = 0; i < w->nmark; i++) {
        if (i > 0 && w->mark[i].level > w->mark[i - 1].level + 1) {
            w->mark[i].level = w->mark[i - 1].level + 1;
        } else if (i == 0) {
            w->mark[i].level = 0;
        }
    }
    parent[0] = base;
    for (i = 0; i < 10; i++) {
        last[i] = -1;
    }
    for (i = 0; i < w->nmark; i++) {
        int lv = w->mark[i].level, next = next_sibling(w, i), kids = 0;

        pw_begin(w, base + 1 + i);
        pw_printf(&w->out, "<< /Title ");
        text_string(&w->out, w->mark[i].title);
        pw_printf(&w->out, " /Parent %d 0 R", parent[lv]);
        if (last[lv] >= 0) {
            pw_printf(&w->out, " /Prev %d 0 R", base + 1 + last[lv]);
        }
        if (next >= 0) {
            pw_printf(&w->out, " /Next %d 0 R", base + 1 + next);
        }
        for (k = i + 1; k < w->nmark && w->mark[k].level > lv; k++) {
            kids++;
        }
        if (kids > 0) {
            int lastkid = i + 1;

            for (k = i + 1; k >= 0 && k < w->nmark; k = next_sibling(w, k)) {
                lastkid = k;
            }
            pw_printf(&w->out, " /First %d 0 R /Last %d 0 R /Count %d",
                      base + 2 + i, base + 1 + lastkid, kids);
        }
        pw_printf(&w->out, " /Dest ");
        dest(w, &w->out, w->mark[i].page, w->mark[i].top);
        pw_printf(&w->out, " >>\nendobj\n");
        last[lv] = next >= 0 ? i : -1;
        if (lv + 1 < 10) {
            parent[lv + 1] = base + 1 + i;
            last[lv + 1] = -1;
        }
    }
}

/** @brief The outline's root and entries.  @return the root's number, or
 *         0 for none. */
static int
outline(tiku_pdf_writer_t *w)
{
    int base, i, first_last = -1, top = 0;

    if (w->nmark == 0 || (base = pw_reserve(w)) == 0) {
        return 0;
    }
    for (i = 0; i < w->nmark; i++) {
        if (pw_reserve(w) == 0) {
            return 0;
        }
    }
    write_outline(w, base);
    for (i = 0; i >= 0 && i < w->nmark; i = next_sibling(w, i)) {
        first_last = i;
        top++;
    }
    pw_begin(w, base);
    pw_printf(&w->out,
              "<< /Type /Outlines /First %d 0 R /Last %d 0 R "
              "/Count %d >>\nendobj\n",
              base + 1, base + 1 + first_last, w->nmark);
    (void)top;
    return base;
}

/** @brief The dictionary every page shares: each resource by its name. */
static void
write_resources(tiku_pdf_writer_t *w)
{
    static const char *const group[] = {"Font", "XObject", "ExtGState",
                                        "Shading"};
    static const char kinds[] = "FIGS";
    int g, i;

    pw_begin(w, PW_RESOURCES);
    pw_printf(&w->out, "<< /ProcSet [/PDF /Text /ImageB /ImageC]");
    for (g = 0; g < 4; g++) {
        int any = 0;

        for (i = 0; i < w->nres; i++) {
            if (w->res[i].kind != kinds[g]) {
                continue;
            }
            pw_printf(&w->out, "%s", any++ ? "" : " /");
            if (any == 1) {
                pw_printf(&w->out, "%s <<", group[g]);
            }
            res_name(&w->out, &w->res[i]);
            pw_printf(&w->out, "%d 0 R ", w->res[i].obj);
        }
        pw_printf(&w->out, "%s", any ? ">>" : "");
    }
    pw_printf(&w->out, " >>\nendobj\n");
}

/** @brief The catalog, the page tree and the Info dictionary. */
static void
write_catalog(tiku_pdf_writer_t *w, int outlines)
{
    int i;

    pw_begin(w, PW_PAGES);
    pw_printf(&w->out, "<< /Type /Pages /Count %d /Kids [", w->npage);
    for (i = 0; i < w->npage; i++) {
        pw_printf(&w->out, "%s%d 0 R", i ? " " : "", w->page[i].obj);
    }
    pw_printf(&w->out, "] >>\nendobj\n");
    pw_begin(w, PW_CATALOG);
    pw_printf(&w->out, "<< /Type /Catalog /Pages %d 0 R", PW_PAGES);
    if (outlines) {
        pw_printf(&w->out, " /Outlines %d 0 R /PageMode /UseOutlines",
                  outlines);
    }
    pw_printf(&w->out, " >>\nendobj\n");
    pw_begin(w, PW_INFO);
    pw_printf(&w->out, "<< /Producer (TikuOS)");
    if (w->title != NULL) {
        pw_printf(&w->out, " /Title ");
        text_string(&w->out, w->title);
    }
    if (w->author != NULL) {
        pw_printf(&w->out, " /Author ");
        text_string(&w->out, w->author);
    }
    pw_printf(&w->out, " >>\nendobj\n");
}

unsigned char *
tiku_pdf_writer_finish(tiku_pdf_writer_t *w, size_t *len)
{
    unsigned char *bytes;
    long xref;
    int i, outlines;

    *len = 0;
    if (w == NULL || w->npage == 0 || w->actual) {
        tiku_pdf_writer_free(w);
        return NULL;
    }
    end_page(w);
    for (i = 0; i < w->nfont; i++) {
        pw_font_write(w, &w->font[i]);
    }
    write_links(w);
    outlines = outline(w);
    write_resources(w);
    write_catalog(w, outlines);
    xref = (long)w->out.n;
    pw_printf(&w->out, "xref\n0 %d\n0000000000 65535 f \n", w->nobj + 1);
    for (i = 1; i <= w->nobj; i++) {
        pw_printf(&w->out, "%010ld %05d %c \n", w->xref[i],
                  w->xref[i] ? 0 : 65535, w->xref[i] ? 'n' : 'f');
    }
    pw_printf(&w->out,
              "trailer\n<< /Size %d /Root %d 0 R /Info %d 0 R >>\n"
              "startxref\n%ld\n%%%%EOF\n",
              w->nobj + 1, PW_CATALOG, PW_INFO, xref);
    if (w->out.bad) {
        tiku_pdf_writer_free(w);
        return NULL;
    }
    bytes = w->out.p;
    *len = w->out.n;
    w->out.p = NULL;
    tiku_pdf_writer_free(w);
    return bytes;
}
