/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_pdf_filter.c - a stream's bytes, decoded.
 *
 * Flate, LZW, ASCIIHex, ASCII85 and RunLength are decoded here.  A
 * predictor (ISO 32000 7.4.4.4) undoes the row filtering a writer added
 * before compressing.  CCITT fax and JBIG2 are decoded into rows of bits,
 * as a one-bit image's samples are; DCT and JPX end the chain, their
 * bytes left for the picture decoders.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_pdf_private.h"
#include "tiku_fax.h"
#include "tiku_inflate.h"
#include "tiku_jbig2.h"

#include <stdlib.h>
#include <string.h>

/** @brief Output that grows, to the kit's ceiling. */
typedef struct {
    unsigned char *p;
    long           n, cap;
    int            failed;
} out_t;

/** @brief One byte more, doubling the buffer as needed. */
static int
put(out_t *o, int ch)
{
    if (o->n >= o->cap) {
        long cap = o->cap ? o->cap * 2 : 1024;
        unsigned char *p;

        if (o->cap >= PDF_STREAM_MAX) {
            o->failed = 1;
            return -1;
        }
        if (cap > PDF_STREAM_MAX) {
            cap = PDF_STREAM_MAX;
        }
        p = realloc(o->p, (size_t)cap);
        if (p == NULL) {
            o->failed = 1;
            return -1;
        }
        o->p = p;
        o->cap = cap;
    }
    o->p[o->n++] = (unsigned char)ch;
    return 0;
}

/** @brief Hex pairs to bytes; white space ignored; > ends it. */
static int
ascii_hex(const unsigned char *src, long n, out_t *o)
{
    int hi = -1;
    long i;

    for (i = 0; i < n && src[i] != '>'; i++) {
        int c = src[i], v;

        v = (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f')
            ? c - 'a' + 10 : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
        if (v < 0) {
            continue;
        }
        if (hi < 0) {
            hi = v;
        } else {
            put(o, hi * 16 + v);
            hi = -1;
        }
    }
    if (hi >= 0) {
        put(o, hi * 16);
    }
    return o->failed ? -1 : 0;
}

/** @brief Five digits base 85 to four bytes; z is four zeros; ~> ends. */
static int
ascii_85(const unsigned char *src, long n, out_t *o)
{
    unsigned long v = 0;
    int k = 0, j;
    long i = 0;

    if (n >= 2 && src[0] == '<' && src[1] == '~') {
        i = 2;
    }
    for (; i < n; i++) {
        int c = src[i];

        if (c == '~') {
            break;
        }
        if (c == 'z' && k == 0) {
            for (j = 0; j < 4; j++) {
                put(o, 0);
            }
            continue;
        }
        if (c < '!' || c > 'u') {
            continue;
        }
        v = v * 85 + (unsigned long)(c - '!');
        if (++k == 5) {
            for (j = 3; j >= 0; j--) {
                put(o, (int)((v >> (8 * j)) & 0xff));
            }
            v = 0;
            k = 0;
        }
    }
    if (k > 0) {
        /* A short last group: pad with u's, and keep k-1 bytes. */
        for (j = k; j < 5; j++) {
            v = v * 85 + 84;
        }
        for (j = 3; j >= 5 - k; j--) {
            put(o, (int)((v >> (8 * j)) & 0xff));
        }
    }
    return o->failed ? -1 : 0;
}

/** @brief Run lengths: a count byte, then a run or a literal span. */
static int
run_length(const unsigned char *src, long n, out_t *o)
{
    long i = 0;

    while (i < n) {
        int len = src[i++];

        if (len == 128) {
            break;
        }
        if (len < 128) {
            int k;

            for (k = 0; k <= len && i < n; k++) {
                put(o, src[i++]);
            }
        } else if (i < n) {
            int k, b = src[i++];

            for (k = 0; k < 257 - len; k++) {
                put(o, b);
            }
        }
    }
    return o->failed ? -1 : 0;
}

/** @brief The string for @p code, last byte first, onto @p stack. */
static int
lzw_string(const unsigned short *prefix, const unsigned char *suffix,
           int code, unsigned char *stack)
{
    int sp = 0;

    while (code >= 258 && sp < 4096) {
        stack[sp++] = suffix[code];
        code = prefix[code];
    }
    stack[sp++] = (unsigned char)code;
    return sp;
}

/**
 * @brief LZW as PDF and TIFF use it: codes of 9 to 12 bits, most
 *        significant bit first, 256 clears the table and 257 ends.
 *
 * With EarlyChange (the default) the code width grows one code sooner
 * than the table would need.
 */
static int
lzw(const unsigned char *src, long n, int early, out_t *o)
{
    unsigned short *prefix = malloc(4096 * sizeof *prefix);
    unsigned char *suffix = malloc(4096), *stack = malloc(4096);
    unsigned long bitbuf = 0;
    int bits = 0, width = 9, next = 258, prev = -1;
    long i = 0;

    if (prefix == NULL || suffix == NULL || stack == NULL) {
        free(prefix);
        free(suffix);
        free(stack);
        return -1;
    }
    for (;;) {
        int code, sp, first, again = 0;

        while (bits < width && i < n) {
            bitbuf = (bitbuf << 8) | src[i++];
            bits += 8;
        }
        if (bits < width) {
            break;
        }
        code = (int)((bitbuf >> (bits - width)) & ((1u << width) - 1));
        bits -= width;
        if (code == 256) {
            width = 9;
            next = 258;
            prev = -1;
            continue;
        }
        if (code == 257) {
            break;
        }
        if (prev < 0 && code > 255) {
            break;
        }
        if (code < next) {
            sp = lzw_string(prefix, suffix, code, stack);
        } else if (code == next && prev >= 0) {
            sp = lzw_string(prefix, suffix, prev, stack);
            again = 1;              /* prev's string, then its first byte */
        } else {
            break;                  /* a code the table cannot have */
        }
        first = stack[sp - 1];
        while (sp > 0) {
            put(o, stack[--sp]);
        }
        if (again) {
            put(o, first);
        }
        if (prev >= 0 && next < 4096) {
            prefix[next] = (unsigned short)prev;
            suffix[next] = (unsigned char)first;
            next++;
        }
        if (next + (early ? 1 : 0) >= (1 << width) && width < 12) {
            width++;
        }
        prev = code;
        if (o->failed) {
            break;
        }
    }
    free(prefix);
    free(suffix);
    free(stack);
    return o->failed ? -1 : 0;
}

/** @brief PNG and TIFF predictors undone in place; @return the new size. */
static long
predict(unsigned char *p, long n, int pred, int colors, int bpc, int columns)
{
    long bpp, rowlen, rows, r, i;

    if (colors < 1 || colors > 64 || bpc < 1 || bpc > 16 || columns < 1 ||
        columns > 1000000) {
        return n;
    }
    bpp = (colors * bpc + 7) / 8;
    rowlen = ((long)columns * colors * bpc + 7) / 8;
    if (pred == 2) {
        if (bpc != 8) {
            return n;               /* undone at 8 bits a sample only */
        }
        for (r = 0; r + rowlen <= n; r += rowlen) {
            for (i = bpp; i < rowlen; i++) {
                p[r + i] = (unsigned char)(p[r + i] + p[r + i - bpp]);
            }
        }
        return n;
    }
    if (pred < 10) {
        return n;
    }
    rows = n / (rowlen + 1);
    for (r = 0; r < rows; r++) {
        unsigned char *src = p + r * (rowlen + 1), *dst = p + r * rowlen;
        unsigned char *up = r > 0 ? dst - rowlen : NULL;
        int type = src[0];

        for (i = 0; i < rowlen; i++) {
            int a = i >= bpp ? dst[i - bpp] : 0;
            int b = up != NULL ? up[i] : 0;
            int c = (up != NULL && i >= bpp) ? up[i - bpp] : 0;
            int x = src[i + 1];

            switch (type) {
            case 1: x += a; break;
            case 2: x += b; break;
            case 3: x += (a + b) / 2; break;
            case 4: {
                int pa = abs(b - c), pb = abs(a - c), pc = abs(a + b - 2 * c);
                x += (pa <= pb && pa <= pc) ? a : (pb <= pc) ? b : c;
                break;
            }
            default: break;
            }
            dst[i] = (unsigned char)x;
        }
    }
    return rows * rowlen;
}

/** @brief The i-th filter name and its parameters, resolved. */
static int
filter_at(tiku_pdf_t *doc, pdf_obj_t *filters, pdf_obj_t *parms, int i,
          const char **name, pdf_obj_t **parm)
{
    pdf_obj_t *f = filters;

    if (filters->kind == PDF_ARRAY) {
        if (i >= filters->u.a.n) {
            return 0;
        }
        f = pdf_resolve(doc, filters->u.a.item[i]);
    } else if (i > 0) {
        return 0;
    }
    if (f == NULL || f->kind != PDF_NAME) {
        return -1;
    }
    *name = (const char *)f->u.s.p;
    *parm = parms;
    if (parms != NULL && parms->kind == PDF_ARRAY) {
        *parm = i < parms->u.a.n ? pdf_resolve(doc, parms->u.a.item[i]) : NULL;
    }
    if (*parm != NULL && (*parm)->kind != PDF_DICT) {
        *parm = NULL;
    }
    return 1;
}

/** @brief The name an image filter goes by, or NULL for a byte filter. */
static const char *
image_filter(const char *name)
{
    if (!strcmp(name, "DCTDecode") || !strcmp(name, "DCT")) return "DCTDecode";
    if (!strcmp(name, "JPXDecode")) return "JPXDecode";
    return NULL;
}

/** @brief Whether @p key of @p d is true, or @p dflt when it is absent. */
static int
flag(tiku_pdf_t *doc, pdf_obj_t *d, const char *key, int dflt)
{
    pdf_obj_t *o = pdf_get(doc, d, key);

    return o != NULL && o->kind == PDF_BOOL ? o->u.b : dflt;
}

/**
 * @brief CCITTFaxDecode (ISO 32000 Table 11): rows of bits, as many as
 *        Rows says, or the image's height when it does not.
 */
static long
fax(tiku_pdf_t *doc, pdf_obj_t *stream, pdf_obj_t *parm,
    const unsigned char *in, long n, unsigned char **out)
{
    tiku_fax_t p;

    p.k = pdf_int(pdf_get(doc, parm, "K"), 0);
    p.columns = pdf_int(pdf_get(doc, parm, "Columns"), 1728);
    p.rows = pdf_int(pdf_get(doc, parm, "Rows"), 0);
    if (p.rows <= 0) {
        p.rows = pdf_int(pdf_get(doc, stream, "Height"), 0);
    }
    if (p.rows <= 0) {
        p.rows = pdf_int(pdf_get(doc, stream, "H"), 0);    /* inline */
    }
    p.byte_align = flag(doc, parm, "EncodedByteAlign", 0);
    p.black_is_1 = flag(doc, parm, "BlackIs1", 0);
    return tiku_fax_decode(in, n, &p, out, PDF_STREAM_MAX);
}

/** @brief Rows of @p w bits cut or widened to the image's @p iw, and as
 *         many rows as it has: what a page of another size gives it. */
static long
recut(unsigned char **data, long made, int w, int h, int iw, int ih)
{
    long rb = (w + 7) / 8, irb = (iw + 7) / 8, y, x;
    unsigned char *o;

    if (iw <= 0 || ih <= 0 || (iw == w && ih == h)) {
        return made;
    }
    if ((o = malloc((size_t)(irb * ih))) == NULL) {
        return -1;
    }
    memset(o, 0xff, (size_t)(irb * ih));        /* white beyond the page */
    for (y = 0; y < h && y < ih; y++) {
        for (x = 0; x < w && x < iw; x++) {
            if (!((*data)[y * rb + x / 8] & (0x80 >> (x & 7)))) {
                o[y * irb + x / 8] &= (unsigned char)~(0x80 >> (x & 7));
            }
        }
    }
    free(*data);
    *data = o;
    return irb * ih;
}

/**
 * @brief JBIG2Decode (ISO 32000 7.4.7): the page its segments code,
 *        those of JBIG2Globals first.  JBIG2's 1 is black and a one-bit
 *        sample's 0 is, so the bits are turned over.
 */
static long
jbig2(tiku_pdf_t *doc, pdf_obj_t *stream, pdf_obj_t *parm,
      const unsigned char *in, long n, unsigned char **out)
{
    pdf_obj_t *gs = pdf_get(doc, parm, "JBIG2Globals");
    unsigned char *glob = NULL;
    long gn = 0, made, i;
    int w = 0, h = 0;

    if (gs != NULL && gs->kind == PDF_STREAM && gs != stream &&
        doc->nested < 2) {
        doc->nested++;
        if (pdf_stream_data(doc, gs, &glob, &gn, NULL) != 0) {
            glob = NULL;
            gn = 0;
        }
        doc->nested--;
    }
    made = tiku_jbig2_decode(glob, gn, in, n, out, PDF_STREAM_MAX, &w, &h);
    free(glob);
    for (i = 0; i < made; i++) {
        (*out)[i] ^= 0xff;
    }
    return made < 0 ? -1 : recut(out, made, w, h,
                                  pdf_int(pdf_get(doc, stream, "Width"), 0),
                                  pdf_int(pdf_get(doc, stream, "Height"), 0));
}

int
pdf_stream_data(tiku_pdf_t *doc, pdf_obj_t *stream, unsigned char **out,
                long *n, const char **image)
{
    const unsigned char *raw;
    long rawn;

    if (pdf_stream_raw(doc, stream, &raw, &rawn) != 0) {
        if (image != NULL) {
            *image = NULL;
        }
        return -1;
    }
    if (doc->crypt != NULL && stream->u.st.num >= 0) {
        /* Encrypted bytes are decrypted before the first filter. */
        unsigned char *clear = malloc((size_t)rawn + 1);
        int rc;

        if (clear == NULL) {
            return -1;
        }
        memcpy(clear, raw, (size_t)rawn);
        rawn = pdf_crypt_stream(doc, stream, clear, rawn);
        rc = pdf_filter_bytes(doc, stream, clear, rawn, out, n, image);
        free(clear);
        return rc;
    }
    return pdf_filter_bytes(doc, stream, raw, rawn, out, n, image);
}

int
pdf_filter_bytes(tiku_pdf_t *doc, pdf_obj_t *stream, const unsigned char *raw,
                 long rawn, unsigned char **out, long *n, const char **image)
{
    unsigned char *cur;
    long curn;
    pdf_obj_t *filters, *parms;
    int i;

    if (image != NULL) {
        *image = NULL;
    }
    cur = malloc((size_t)rawn + 1);
    if (cur == NULL) {
        return -1;
    }
    memcpy(cur, raw, (size_t)rawn);
    curn = rawn;
    filters = pdf_get(doc, stream, "Filter");
    if (filters == NULL) {
        filters = pdf_get(doc, stream, "F");
        if (filters != NULL && filters->kind != PDF_NAME &&
            filters->kind != PDF_ARRAY) {
            filters = NULL;         /* F as a file specification */
        }
    }
    parms = pdf_get(doc, stream, "DecodeParms");
    if (parms == NULL) {
        parms = pdf_get(doc, stream, "DP");
    }
    for (i = 0; filters != NULL; i++) {
        const char *name;
        pdf_obj_t *parm;
        out_t o = {0};
        unsigned char *next = NULL;
        long nextn = -1;
        int r = filter_at(doc, filters, parms, i, &name, &parm), pred;

        if (r <= 0) {
            if (r < 0) {
                free(cur);
                return -1;
            }
            break;
        }
        if (image_filter(name) != NULL) {
            if (image != NULL) {
                *image = image_filter(name);
            }
            break;
        }
        if (!strcmp(name, "FlateDecode") || !strcmp(name, "Fl")) {
            nextn = tiku_inflate_alloc(cur, curn, &next, PDF_STREAM_MAX, 1);
        } else if (!strcmp(name, "LZWDecode") || !strcmp(name, "LZW")) {
            int early = pdf_int(pdf_get(doc, parm, "EarlyChange"), 1);

            nextn = lzw(cur, curn, early, &o) == 0 ? o.n : -1;
            next = o.p;
        } else if (!strcmp(name, "ASCIIHexDecode") || !strcmp(name, "AHx")) {
            nextn = ascii_hex(cur, curn, &o) == 0 ? o.n : -1;
            next = o.p;
        } else if (!strcmp(name, "ASCII85Decode") || !strcmp(name, "A85")) {
            nextn = ascii_85(cur, curn, &o) == 0 ? o.n : -1;
            next = o.p;
        } else if (!strcmp(name, "RunLengthDecode") || !strcmp(name, "RL")) {
            nextn = run_length(cur, curn, &o) == 0 ? o.n : -1;
            next = o.p;
        } else if (!strcmp(name, "CCITTFaxDecode") || !strcmp(name, "CCF")) {
            nextn = fax(doc, stream, parm, cur, curn, &next);
        } else if (!strcmp(name, "JBIG2Decode")) {
            nextn = jbig2(doc, stream, parm, cur, curn, &next);
        } else if (!strcmp(name, "Crypt")) {
            continue;               /* Identity: nothing was encrypted */
        } else {
            free(cur);
            return -1;
        }
        if (nextn < 0) {
            free(next);
            if (i == 0 && !strcmp(name, "FlateDecode") && curn == 0) {
                break;              /* an empty stream is empty */
            }
            free(cur);
            return -1;
        }
        free(cur);
        cur = next;
        curn = nextn;
        pred = pdf_int(pdf_get(doc, parm, "Predictor"), 1);
        if (pred > 1) {
            curn = predict(cur, curn, pred,
                           pdf_int(pdf_get(doc, parm, "Colors"), 1),
                           pdf_int(pdf_get(doc, parm, "BitsPerComponent"),
                                   8),
                           pdf_int(pdf_get(doc, parm, "Columns"), 1));
        }
    }
    *out = cur;
    *n = curn;
    return 0;
}
