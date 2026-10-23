/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_pdf_cmap.c - how a composite font's codes are read: how many
 * bytes each takes, and which glyph of the font (its CID) it selects.
 *
 * A CMap (ISO 32000 9.7.5) gives its code space and maps codes to CIDs
 * in runs.  A file carries one as a stream or names one of Adobe's.
 * Identity and the Unicode schemes are known here without data; the rest
 * are read from the CMap folders (poppler-data's layout) when the
 * machine has them, and are otherwise their code space alone.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_pdf_private.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CM_SPACES   16              /* code space ranges one map keeps   */
#define CM_WRITES   (1L << 22)      /* codes one map may set, in all     */
#define CM_DEPTH    4               /* usecmap within usecmap            */
#define CM_FILE_MAX (4L << 20)      /* a CMap file read, at most         */
#define CM_RUNS_MAX 65536           /* runs of three- and four-byte codes */

enum { CM_CIDS, CM_UCS2, CM_UTF16, CM_UTF32, CM_UTF8 };

/** @brief A run of three- or four-byte codes and its first code's CID. */
typedef struct {
    uint32_t      lo, hi, cid;
    int           len;              /* the codes' bytes                  */
    long          seq;              /* a later run wins over an earlier  */
} cm_run_t;

struct pdf_cmap {
    int            scheme;          /* how its codes are Unicode, if so  */
    int            wmode;           /* 1: written down the page          */
    int            cids;            /* its codes' CIDs are known         */
    int            identity;        /* the CID is the two-byte code      */
    int            nspace;
    struct {
        int      len;
        uint32_t lo, hi;
    } space[CM_SPACES];
    unsigned short one[256];        /* one-byte codes to CIDs            */
    unsigned short *two;            /* two-byte codes to CIDs, 65536     */
    cm_run_t      *run;             /* the longer codes, sorted by code  */
    long           nrun, caprun;
    long           writes;          /* codes set so far, a bound         */
};

/* Where the machine keeps poppler-data, which lays CMaps out by the
 * collection of CIDs they select from. */
static const char *const cm_dirs[] = {
    "/opt/homebrew/share/poppler",
    "/usr/local/share/poppler",
    "/usr/share/poppler",
};
static const char *const cm_colls[] = {
    "Adobe-Japan1", "Adobe-GB1", "Adobe-CNS1", "Adobe-Korea1", "Adobe-KR",
    "Adobe-Japan2",
};
#define CM_NDIRS  (sizeof cm_dirs / sizeof cm_dirs[0])
#define CM_NCOLLS (sizeof cm_colls / sizeof cm_colls[0])

static void cm_named(tiku_pdf_t *doc, pdf_cmap_t *m, const char *name,
                     int depth);

void
tiku_pdf_cmaps(tiku_pdf_t *doc, const char *dir)
{
    if (doc != NULL) {
        snprintf(doc->cmapdir, sizeof doc->cmapdir, "%s", dir ? dir : "");
        doc->cmapdir_set = dir != NULL;
        pdf_fonts_free(doc);        /* read again by the new folder */
        pdf_cid_unicode_free(doc);
    }
}

/* -------------------------------------------------------------- files  */

/** @brief Whether @p name can only name a file inside a folder: letters,
 *         digits and - _ . +, not led by a dot, not long. */
static int
cm_safe(const char *name)
{
    size_t i;

    if (name[0] == '\0' || name[0] == '.') {
        return 0;
    }
    for (i = 0; name[i] != '\0'; i++) {
        unsigned char ch = (unsigned char)name[i];

        if (i >= 64 || !(isalnum(ch) || ch == '-' || ch == '_' ||
                         ch == '.' || ch == '+')) {
            return 0;
        }
    }
    return 1;
}

/** @brief The bytes of file @p path, when it holds 1 to @p max of them;
 *         NUL-terminated, for the caller to free. */
static unsigned char *
cm_read(const char *path, long max, long *n)
{
    FILE *f = fopen(path, "rb");
    unsigned char *data = NULL;
    long size;

    if (f == NULL) {
        return NULL;
    }
    if (fseek(f, 0L, SEEK_END) == 0 && (size = ftell(f)) > 0 &&
        size <= max && fseek(f, 0L, SEEK_SET) == 0 &&
        (data = malloc((size_t)size + 1)) != NULL) {
        if (fread(data, 1, (size_t)size, f) == (size_t)size) {
            data[size] = '\0';
            *n = size;
        } else {
            free(data);
            data = NULL;
        }
    }
    fclose(f);
    return data;
}

/**
 * @brief File @p name from the CMap folders: the one the document was
 *        given, or else poppler-data's; in each, by collection or loose.
 * @param kind  "cMap" or "cidToUnicode".
 */
static unsigned char *
cm_file(tiku_pdf_t *doc, const char *kind, const char *name, long *n)
{
    char path[1024];
    size_t d, k;

    if (!cm_safe(name)) {
        return NULL;
    }
    for (d = 0; d <= CM_NDIRS; d++) {
        const char *dir = d == 0 ? doc->cmapdir : cm_dirs[d - 1];
        unsigned char *data;

        if (dir[0] == '\0' || (d > 0 && doc->cmapdir_set)) {
            continue;
        }
        if (strcmp(kind, "cidToUnicode") == 0) {
            snprintf(path, sizeof path, "%s/%s/%s", dir, kind, name);
            if ((data = cm_read(path, CM_FILE_MAX, n)) != NULL) {
                return data;
            }
            continue;
        }
        snprintf(path, sizeof path, "%s/%s", dir, name);
        if ((data = cm_read(path, CM_FILE_MAX, n)) != NULL) {
            return data;
        }
        for (k = 0; k < CM_NCOLLS; k++) {
            snprintf(path, sizeof path, "%s/%s/%s/%s", dir, kind,
                     cm_colls[k], name);
            if ((data = cm_read(path, CM_FILE_MAX, n)) != NULL) {
                return data;
            }
        }
    }
    return NULL;
}

/* ------------------------------------------------------------- tables  */

/** @brief Code space range @p lo to @p hi of codes @p len bytes long. */
static void
cm_space(pdf_cmap_t *m, int len, uint32_t lo, uint32_t hi)
{
    if (m->nspace < CM_SPACES && len >= 1 && len <= 4 && lo <= hi) {
        m->space[m->nspace].len = len;
        m->space[m->nspace].lo = lo;
        m->space[m->nspace].hi = hi;
        m->nspace++;
    }
}

/** @brief A run of long codes kept for the sorted search. */
static void
cm_run(pdf_cmap_t *m, int len, uint32_t lo, uint32_t hi, long cid)
{
    if (m->nrun == m->caprun) {
        long cap = m->caprun ? m->caprun * 2 : 64;
        cm_run_t *grown;

        if (cap > CM_RUNS_MAX ||
            (grown = realloc(m->run, (size_t)cap * sizeof *grown)) == NULL) {
            return;
        }
        m->run = grown;
        m->caprun = cap;
    }
    m->run[m->nrun].lo = lo;
    m->run[m->nrun].hi = hi;
    m->run[m->nrun].cid = (uint32_t)cid;
    m->run[m->nrun].len = len;
    m->run[m->nrun].seq = m->nrun;
    m->nrun++;
}

/**
 * @brief Codes @p lo to @p hi, each @p len bytes, to CIDs counting from
 *        @p cid.  A map built on Identity is written out first, so what
 *        it adds overrides the codes it names and no others.
 */
static void
cm_map(pdf_cmap_t *m, int len, uint32_t lo, uint32_t hi, long cid)
{
    uint32_t c, top = len == 1 ? 0xFFu : 0xFFFFu;

    if (lo > hi || cid < 0 || cid > 65535 || len < 1 || len > 4) {
        return;
    }
    if (len >= 3) {
        cm_run(m, len, lo, hi, cid);
        m->cids = 1;
        return;
    }
    if (len == 2 && m->two == NULL) {
        if ((m->two = malloc(65536 * sizeof *m->two)) == NULL) {
            return;
        }
        for (c = 0; c <= 0xFFFFu; c++) {
            m->two[c] = m->identity ? (unsigned short)c : 0;
        }
        m->identity = 0;
    }
    m->cids = 1;
    for (c = lo; c <= hi && c <= top && m->writes < CM_WRITES; c++) {
        long v = cid + (long)(c - lo);

        if (v > 65535) {
            break;
        }
        if (len == 1) {
            m->one[c] = (unsigned short)v;
        } else {
            m->two[c] = (unsigned short)v;
        }
        m->writes++;
    }
}

/** @brief Order runs by length, then first code, then when they came. */
static int
cm_run_cmp(const void *a, const void *b)
{
    const cm_run_t *x = a, *y = b;

    if (x->len != y->len) return x->len < y->len ? -1 : 1;
    if (x->lo != y->lo) return x->lo < y->lo ? -1 : 1;
    return x->seq < y->seq ? -1 : x->seq > y->seq;
}

/** @brief The CID of long code @p code: of the runs holding it, the one
 *         read last; 0 (.notdef) when none does. */
static long
cm_run_cid(const pdf_cmap_t *m, uint32_t code, int len)
{
    long lo = 0, hi = m->nrun - 1, at = -1, best = -1, k;

    while (lo <= hi) {              /* the last run starting at or before */
        long mid = lo + (hi - lo) / 2;
        const cm_run_t *r = &m->run[mid];

        if (r->len < len || (r->len == len && r->lo <= code)) {
            at = mid;
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    for (k = at; k >= 0 && m->run[k].len == len; k--) {
        if (code <= m->run[k].hi &&
            (best < 0 || m->run[k].seq > m->run[best].seq)) {
            best = k;
        }
    }
    if (best < 0 || code - m->run[best].lo > 65535u - m->run[best].cid) {
        return 0;                   /* no run, or past the last CID */
    }
    return (long)(m->run[best].cid + (code - m->run[best].lo));
}

/* ------------------------------------------------------------ reading  */

/** @brief A code from a CMap's hex string.  @return its bytes, 0 if it
 *         is no code. */
static int
cm_code(const pdf_obj_t *s, uint32_t *code)
{
    size_t i;

    if (s == NULL || s->kind != PDF_STRING || s->u.s.n < 1 || s->u.s.n > 4) {
        return 0;
    }
    *code = 0;
    for (i = 0; i < s->u.s.n; i++) {
        *code = *code << 8 | s->u.s.p[i];
    }
    return (int)s->u.s.n;
}

/**
 * @brief An entry of the section @p mode opened: 1 code space, 2 a run
 *        of CIDs (cidrange, notdefrange), 3 one code's (cidchar,
 *        notdefchar).  @return 1 when @p st held a whole one.
 */
static int
cm_entry(pdf_cmap_t *m, int mode, pdf_obj_t **st, int sp)
{
    uint32_t lo, hi;
    int n, n2;

    if (mode == 1 && sp >= 2) {
        n = cm_code(st[sp - 2], &lo);
        n2 = cm_code(st[sp - 1], &hi);
        if (n > 0 && n == n2) {
            cm_space(m, n, lo, hi);
        }
        return 1;
    }
    if (mode == 2 && sp >= 3) {
        n = cm_code(st[sp - 3], &lo);
        n2 = cm_code(st[sp - 2], &hi);
        if (n > 0 && n == n2 && st[sp - 1]->kind == PDF_INT) {
            cm_map(m, n, lo, hi, st[sp - 1]->u.i);
        }
        return 1;
    }
    if (mode == 3 && sp >= 2) {
        n = cm_code(st[sp - 2], &lo);
        if (n > 0 && st[sp - 1]->kind == PDF_INT) {
            cm_map(m, n, lo, lo, st[sp - 1]->u.i);
        }
        return 1;
    }
    return 0;
}

/** @brief Operator @p op: a section opening or closing, usecmap on the
 *         name before it, or the writing mode defined. */
static void
cm_op(tiku_pdf_t *doc, pdf_cmap_t *m, const char *op, pdf_obj_t **st,
      int sp, int *mode, int depth)
{
    if (strcmp(op, "begincodespacerange") == 0) {
        *mode = 1;
    } else if (strcmp(op, "begincidrange") == 0 ||
               strcmp(op, "beginnotdefrange") == 0) {
        *mode = 2;
    } else if (strcmp(op, "begincidchar") == 0 ||
               strcmp(op, "beginnotdefchar") == 0) {
        *mode = 3;
    } else if (strncmp(op, "end", 3) == 0) {
        *mode = 0;
    } else if (strcmp(op, "usecmap") == 0 && sp >= 1 &&
               st[sp - 1]->kind == PDF_NAME) {
        cm_named(doc, m, (const char *)st[sp - 1]->u.s.p, depth + 1);
    } else if (strcmp(op, "def") == 0 && sp >= 2 &&
               pdf_is_name(st[sp - 2], "WMode")) {
        m->wmode = pdf_int(st[sp - 1], 0) == 1;
    }
}

/**
 * @brief Read CMap text: its code space, runs and single codes of CIDs,
 *        the map it builds on and its writing mode.  The text is
 *        PostScript; these operators and their operands are what count.
 */
static void
cm_parse(tiku_pdf_t *doc, pdf_cmap_t *m, const unsigned char *p, long n,
         int depth)
{
    pdf_cur_t c;
    pdf_obj_t *st[3] = {NULL, NULL, NULL};
    int sp = 0, mode = 0;

    c.p = p;
    c.n = n;
    c.pos = 0;
    for (;;) {
        long at = c.pos;
        pdf_obj_t *o = pdf_parse(&c, 0);

        if (o == NULL) {
            if (c.pos >= c.n || c.pos == at) {
                break;
            }
            continue;               /* PostScript the PDF lexer skips */
        }
        if (o->kind == PDF_OP) {
            cm_op(doc, m, (const char *)o->u.s.p, st, sp, &mode, depth);
            pdf_free(o);
            while (sp > 0) {
                pdf_free(st[--sp]);
            }
            continue;
        }
        if (sp == 3) {
            pdf_free(st[0]);
            memmove(st, st + 1, 2 * sizeof st[0]);
            sp = 2;
        }
        st[sp++] = o;
        if (cm_entry(m, mode, st, sp)) {
            while (sp > 0) {
                pdf_free(st[--sp]);
            }
        }
    }
    while (sp > 0) {
        pdf_free(st[--sp]);
    }
}

/** @brief The code space a predefined map's name says it has, for when
 *         its file is not here: by its Unicode scheme or its encoding. */
static void
cm_guess(pdf_cmap_t *m, const char *name)
{
    if (m->scheme == CM_UCS2) {
        cm_space(m, 2, 0, 0xFFFF);
    } else if (m->scheme == CM_UTF16) {
        cm_space(m, 2, 0, 0xD7FF);
        cm_space(m, 2, 0xE000, 0xFFFF);
        cm_space(m, 4, 0xD800DC00u, 0xDBFFDFFFu);
    } else if (m->scheme == CM_UTF32) {
        cm_space(m, 4, 0, 0x10FFFF);
    } else if (m->scheme == CM_UTF8) {
        cm_space(m, 1, 0, 0x7F);
        cm_space(m, 2, 0xC280, 0xDFBF);
        cm_space(m, 3, 0xE0A080, 0xEFBFBF);
        cm_space(m, 4, 0xF0908080u, 0xF48FBFBFu);
    } else if (strstr(name, "RKSJ") != NULL) {
        cm_space(m, 1, 0, 0x80);
        cm_space(m, 1, 0xA0, 0xDF);
        cm_space(m, 2, 0x8140, 0x9FFC);
        cm_space(m, 2, 0xE040, 0xFCFC);
    } else if (strstr(name, "GBK2K") != NULL) {
        cm_space(m, 1, 0, 0x80);
        cm_space(m, 2, 0x8140, 0xFE7E);
        cm_space(m, 2, 0x8180, 0xFEFE);
        cm_space(m, 4, 0x81308130u, 0xFE39FE39u);
    } else if (strstr(name, "GBK") != NULL || strstr(name, "UHC") != NULL ||
               strstr(name, "B5") != NULL) {
        cm_space(m, 1, 0, 0x80);
        cm_space(m, 2, 0x8140, 0xFEFE);
    } else if (strstr(name, "EUC") != NULL) {
        cm_space(m, 1, 0, 0x80);
        cm_space(m, 2, 0x8EA0, 0x8EDF);
        cm_space(m, 2, 0xA1A1, 0xFEFE);
    } else {
        cm_space(m, 2, 0, 0xFFFF);
    }
}

/**
 * @brief Predefined map @p name into @p m: Identity as it is, anything
 *        else from its file, or, without the file, its code space and
 *        writing mode from its name and no CIDs.
 */
static void
cm_named(tiku_pdf_t *doc, pdf_cmap_t *m, const char *name, int depth)
{
    size_t len = strlen(name);
    unsigned char *data;
    long n = 0;

    if (depth > CM_DEPTH) {
        return;
    }
    if (strcmp(name, "Identity-H") == 0 || strcmp(name, "Identity-V") == 0) {
        m->identity = m->cids = 1;
        cm_space(m, 2, 0, 0xFFFF);
        m->wmode = name[9] == 'V';
        return;
    }
    if (strncmp(name, "Uni", 3) == 0) {
        m->scheme = strstr(name, "UTF16") ? CM_UTF16 :
                    strstr(name, "UTF32") ? CM_UTF32 :
                    strstr(name, "UTF8") ? CM_UTF8 :
                    strstr(name, "UCS2") ? CM_UCS2 : CM_CIDS;
    }
    if ((data = cm_file(doc, "cMap", name, &n)) != NULL) {
        cm_parse(doc, m, data, n, depth);
        free(data);
        return;
    }
    cm_guess(m, name);
    m->wmode = strcmp(name, "V") == 0 ||
               (len > 2 && name[len - 2] == '-' && name[len - 1] == 'V');
}

/** @brief A map the file carries: what it builds on, then its text. */
static void
cm_stream(tiku_pdf_t *doc, pdf_cmap_t *m, pdf_obj_t *st, int depth)
{
    pdf_obj_t *use = pdf_get(doc, st, "UseCMap");
    unsigned char *data = NULL;
    long n = 0;

    if (depth > CM_DEPTH) {
        return;
    }
    if (use != NULL && use->kind == PDF_NAME) {
        cm_named(doc, m, (const char *)use->u.s.p, depth + 1);
    } else if (use != NULL && use->kind == PDF_STREAM && use != st) {
        cm_stream(doc, m, use, depth + 1);
    }
    m->wmode = pdf_int(pdf_get(doc, st, "WMode"), m->wmode) == 1;
    if (pdf_stream_data(doc, st, &data, &n, NULL) == 0) {
        cm_parse(doc, m, data, n, depth);
        free(data);
    }
}

/* -------------------------------------------------------------- using  */

pdf_cmap_t *
pdf_cmap_load(tiku_pdf_t *doc, pdf_obj_t *enc)
{
    pdf_cmap_t *m = calloc(1, sizeof *m);

    if (m == NULL) {
        return NULL;
    }
    if (enc != NULL && enc->kind == PDF_NAME) {
        cm_named(doc, m, (const char *)enc->u.s.p, 0);
    } else if (enc != NULL && enc->kind == PDF_STREAM) {
        cm_stream(doc, m, enc, 0);
    }
    if (m->nspace == 0) {           /* none said: two bytes, as Identity */
        cm_space(m, 2, 0, 0xFFFF);
        if (!m->cids && m->scheme == CM_CIDS) {
            m->identity = m->cids = 1;
        }
    }
    if (m->nrun > 1) {
        qsort(m->run, (size_t)m->nrun, sizeof *m->run, cm_run_cmp);
    }
    return m;
}

void
pdf_cmap_free(pdf_cmap_t *m)
{
    if (m != NULL) {
        free(m->two);
        free(m->run);
        free(m);
    }
}

int
pdf_cmap_next(const pdf_cmap_t *m, const unsigned char *s, size_t n,
              uint32_t *code)
{
    uint32_t c = 0;
    int k, i, shortest = 4;

    for (k = 0; k < 4 && (size_t)k < n; k++) {
        c = c << 8 | s[k];
        for (i = 0; i < m->nspace; i++) {
            if (m->space[i].len == k + 1 && c >= m->space[i].lo &&
                c <= m->space[i].hi) {
                *code = c;
                return k + 1;
            }
        }
    }
    for (i = 0; i < m->nspace; i++) {
        if (m->space[i].len < shortest) {
            shortest = m->space[i].len;
        }
    }
    if (n < (size_t)shortest) {
        return 0;                   /* a code cut short by the string */
    }
    *code = s[0];                   /* in no range: one byte, .notdef */
    return 1;
}

int
pdf_cmap_len(const pdf_cmap_t *m, uint32_t code)
{
    int i, best = 0;

    for (i = 0; i < m->nspace; i++) {
        if (code >= m->space[i].lo && code <= m->space[i].hi &&
            (best == 0 || m->space[i].len < best)) {
            best = m->space[i].len;
        }
    }
    return best ? best : 2;
}

long
pdf_cmap_cid(const pdf_cmap_t *m, uint32_t code, int len)
{
    if (!m->cids) {
        return -1;
    }
    if (m->identity) {
        return len == 2 ? (long)code : 0;
    }
    if (len == 1) {
        return m->one[code & 0xFFu];
    }
    if (len == 2) {
        return m->two != NULL ? m->two[code & 0xFFFFu] : 0;
    }
    return cm_run_cid(m, code, len);
}

int
pdf_cmap_unicode_codes(const pdf_cmap_t *m)
{
    return m->scheme != CM_CIDS;
}

uint32_t
pdf_cmap_unicode(const pdf_cmap_t *m, uint32_t code, int len)
{
    uint32_t u = 0, b0;

    if (len < 1 || len > 4) {
        return 0;
    }
    b0 = (code >> (8 * (len - 1))) & 0xFFu;
    if (m->scheme == CM_UCS2 && len == 2) {
        u = code;
    } else if (m->scheme == CM_UTF16 && len == 2) {
        u = code;
    } else if (m->scheme == CM_UTF16 && len == 4) {
        uint32_t hi = code >> 16, lo = code & 0xFFFFu;

        if (hi >= 0xD800 && hi < 0xDC00 && lo >= 0xDC00 && lo < 0xE000) {
            u = 0x10000u + ((hi - 0xD800u) << 10) + (lo - 0xDC00u);
        }
    } else if (m->scheme == CM_UTF32 && len == 4) {
        u = code;
    } else if (m->scheme == CM_UTF8) {
        static const uint32_t lead[5] = {0, 0x7F, 0x1F, 0x0F, 0x07};
        int k;

        u = b0 & lead[len];
        for (k = len - 2; k >= 0; k--) {
            u = u << 6 | ((code >> (8 * k)) & 0x3Fu);
        }
    }
    return (u >= 0xD800 && u < 0xE000) || u > 0x10FFFF ? 0 : u;
}

int
pdf_cmap_vertical(const pdf_cmap_t *m)
{
    return m != NULL && m->wmode;
}

/** @brief Which of poppler-data's collections @p name is, or -1. */
static int
cm_coll(const char *name)
{
    size_t k;

    for (k = 0; k < CM_NCOLLS; k++) {
        if (strcmp(cm_colls[k], name) == 0) {
            return (int)k;
        }
    }
    return -1;
}

/** @brief A hex digit's value, or -1. */
static int
cm_hex(int ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

const uint32_t *
pdf_cid_unicode(tiku_pdf_t *doc, const char *collection)
{
    int k = cm_coll(collection);
    unsigned char *data;
    long n = 0, i = 0, cid;
    uint32_t *map;

    if (k < 0 || doc->cid_uni_tried[k]) {
        return k < 0 ? NULL : doc->cid_uni[k];
    }
    doc->cid_uni_tried[k] = 1;
    if ((data = cm_file(doc, "cidToUnicode", collection, &n)) == NULL) {
        return NULL;
    }
    map = calloc(65536, sizeof *map);
    /* A line a CID, from 0: its character in hex, the first number. */
    for (cid = 0; map != NULL && i < n && cid < 65536; cid++, i++) {
        uint32_t u = 0;
        int digits = 0, done = 0;

        for (; i < n && data[i] != '\n'; i++) {
            int h = cm_hex(data[i]);

            if (done) {
                continue;
            }
            if (h < 0) {
                done = digits > 0;
            } else if (digits++ < 8) {
                u = u << 4 | (uint32_t)h;
            }
        }
        map[cid] = u <= 0x10FFFF ? u : 0;
    }
    free(data);
    doc->cid_uni[k] = map;
    return map;
}

void
pdf_cid_unicode_free(tiku_pdf_t *doc)
{
    size_t k;

    for (k = 0; k < CM_NCOLLS; k++) {
        free(doc->cid_uni[k]);
        doc->cid_uni[k] = NULL;
        doc->cid_uni_tried[k] = 0;
    }
}
