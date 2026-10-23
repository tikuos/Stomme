/*
 * KintsugiOS v0.01 kits -- the foundation KintsugiDesktop and KintsugiTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 *
 * tiku_xml.c - XML read into a tree.
 *
 * One pass over a private copy of the bytes: text and attribute values
 * are decoded where they lie (a reference is never shorter than what it
 * stands for), so the tree's strings are the copy's own bytes.  Names
 * are made anew -- a known namespace's prefix and the local name -- and
 * kept once each; nodes come from blocks, so none moves.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_xml.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define XML_NODES   2048            /* nodes one block holds             */
#define XML_POOL    65536           /* bytes one pool block holds        */
#define XML_ATTRS   512             /* attributes one tag may carry      */

/** @brief The namespaces readers here know, and the prefix each is given
 *         whatever a file calls it: OOXML's, transitional and strict, with
 *         Microsoft's extensions and VML; the package's own and Dublin
 *         Core; OpenDocument's; XLink and xml. */
static const struct { const char *uri, *prefix; } known[] = {
    {"http://schemas.openxmlformats.org/wordprocessingml/2006/main", "w"},
    {"http://purl.oclc.org/ooxml/wordprocessingml/main", "w"},
    {"http://schemas.openxmlformats.org/officeDocument/2006/relationships",
     "r"},
    {"http://purl.oclc.org/ooxml/officeDocument/relationships", "r"},
    {"http://schemas.openxmlformats.org/drawingml/2006/main", "a"},
    {"http://purl.oclc.org/ooxml/drawingml/main", "a"},
    {"http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing",
     "wp"},
    {"http://purl.oclc.org/ooxml/drawingml/wordprocessingDrawing", "wp"},
    {"http://schemas.openxmlformats.org/drawingml/2006/picture", "pic"},
    {"http://purl.oclc.org/ooxml/drawingml/picture", "pic"},
    {"http://schemas.openxmlformats.org/presentationml/2006/main", "p"},
    {"http://purl.oclc.org/ooxml/presentationml/main", "p"},
    {"http://schemas.openxmlformats.org/drawingml/2006/chart", "c"},
    {"http://purl.oclc.org/ooxml/drawingml/chart", "c"},
    {"http://schemas.openxmlformats.org/drawingml/2006/diagram", "dgm"},
    {"http://schemas.microsoft.com/office/drawing/2008/diagram", "dsp"},
    {"http://schemas.openxmlformats.org/officeDocument/2006/math", "m"},
    {"http://schemas.openxmlformats.org/package/2006/relationships", "rel"},
    {"http://schemas.openxmlformats.org/package/2006/content-types", "ct"},
    {"http://schemas.openxmlformats.org/markup-compatibility/2006", "mc"},
    {"http://schemas.openxmlformats.org/package/2006/metadata/"
     "core-properties", "cp"},
    {"http://purl.org/dc/elements/1.1/", "dc"},
    {"http://purl.org/dc/terms/", "dcterms"},
    {"http://schemas.microsoft.com/office/word/2010/wordprocessingShape",
     "wps"},
    {"http://schemas.microsoft.com/office/word/2010/wordprocessingGroup",
     "wpg"},
    {"http://schemas.microsoft.com/office/word/2010/wordprocessingCanvas",
     "wpc"},
    {"http://schemas.microsoft.com/office/word/2010/wordprocessingDrawing",
     "wp14"},
    {"http://schemas.microsoft.com/office/word/2010/wordml", "w14"},
    {"http://schemas.microsoft.com/office/drawing/2010/main", "a14"},
    {"http://schemas.microsoft.com/office/powerpoint/2010/main", "p14"},
    {"urn:schemas-microsoft-com:vml", "v"},
    {"urn:schemas-microsoft-com:office:office", "o"},
    {"urn:schemas-microsoft-com:office:word", "w10"},
    {"urn:oasis:names:tc:opendocument:xmlns:office:1.0", "office"},
    {"urn:oasis:names:tc:opendocument:xmlns:text:1.0", "text"},
    {"urn:oasis:names:tc:opendocument:xmlns:style:1.0", "style"},
    {"urn:oasis:names:tc:opendocument:xmlns:table:1.0", "table"},
    {"urn:oasis:names:tc:opendocument:xmlns:drawing:1.0", "draw"},
    {"urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0", "fo"},
    {"urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0", "svg"},
    {"urn:oasis:names:tc:opendocument:xmlns:presentation:1.0",
     "presentation"},
    {"urn:oasis:names:tc:opendocument:xmlns:meta:1.0", "meta"},
    {"urn:oasis:names:tc:opendocument:xmlns:datastyle:1.0", "number"},
    {"urn:oasis:names:tc:opendocument:xmlns:manifest:1.0", "manifest"},
    {"http://www.w3.org/1999/xlink", "xlink"},
    {"http://www.w3.org/XML/1998/namespace", "xml"},
};

/** @brief A block of the pool the names and attribute lists live in. */
typedef struct xml_pool {
    struct xml_pool *next;
    size_t used, cap;
    unsigned char data[];
} xml_pool_t;

/** @brief A block of nodes. */
typedef struct xml_block {
    struct xml_block *next;
    int used;
    tiku_xml_node_t node[XML_NODES];
} xml_block_t;

struct tiku_xml {
    char *buf;                      /* the bytes, decoded where they lie */
    xml_pool_t *pool;               /* names and attribute lists         */
    xml_block_t *blocks;            /* the nodes                         */
    tiku_xml_node_t *root;          /* the document's element            */
    const char **names;             /* each name once: open addressing   */
    size_t nnames, cap;             /* names kept, and the table's size  */
    /* A bounded parse's limits: NULL when there are none, and once the
     * parse has ended. */
    const tiku_xml_limits_t *limits;
    int lexical_names;              /* keep source QName and xmlns attributes */
    size_t charged;                 /* bytes counted against limits      */
    size_t nodes;                   /* nodes taken, against limits       */
    tiku_xml_status_t status;       /* why the parse failed, once it has */
    const char *limit;              /* the limit it hit, said in why     */
};

/** @brief A bounded parse stopped by the limit @p what: it fails
 *         TIKU_XML_LIMIT, its why naming @p what. */
static void
over_limit(tiku_xml_t *doc, const char *what)
{
    doc->status = TIKU_XML_LIMIT;
    doc->limit = what;
}

/** @brief Count @p n bytes about to be allocated against a bounded
 *         parse's budget.  @return 1, or 0 when they would exceed it, the
 *         parse then failing TIKU_XML_LIMIT. */
static int
xml_room(tiku_xml_t *d, size_t n)
{
    if (d->limits && n > d->limits->bytes - d->charged) {
        over_limit(d, "XML allocation budget exceeded");
        return 0;
    }
    d->charged += n;
    return 1;
}

/** @brief A namespace in scope: the prefix a file wrote (empty for the
 *         default) and the one it is given here, or NULL for unknown. */
typedef struct {
    const char *pfx;
    size_t plen;
    const char *canon;
    int depth;                      /* the element that declared it      */
} xml_bind_t;

/** @brief One attribute of the tag being read, before names resolve. */
typedef struct {
    char *qn, *val;
    size_t qlen;
} xml_att_t;

/** @brief A parse under way. */
typedef struct {
    tiku_xml_t *doc;
    char *p, *end;
    int lt;                         /* a '<' at p was overwritten        */
    int depth;
    tiku_xml_node_t *cur;           /* the element open, NULL outside    */
    tiku_xml_node_t *last[TIKU_XML_DEPTH + 1];  /* each level's last child */
    xml_bind_t *bind;
    int nbind, bindcap;
    xml_att_t *att;                 /* XML_ATTRS of them                 */
    char *why;
    size_t max;
} xml_parse_t;

/* ------------------------------------------------------------ storage   */

/** @brief @p n bytes from the pool, aligned for pointers.  NULL when there
 *         is no memory. */
static void *
pool_take(tiku_xml_t *doc, size_t n)
{
    xml_pool_t *b = doc->pool;
    size_t at;

    n = (n + sizeof(void *) - 1) & ~(sizeof(void *) - 1);
    if (b == NULL || b->used + n > b->cap) {
        size_t cap = n > XML_POOL ? n : XML_POOL;

        if (!xml_room(doc, sizeof *b + cap)) {
            return NULL;
        }
        b = malloc(sizeof *b + cap);
        if (b == NULL) {
            return NULL;
        }
        b->next = doc->pool;
        b->used = 0;
        b->cap = cap;
        doc->pool = b;
    }
    at = b->used;
    b->used += n;
    return b->data + at;
}

/** @brief A fresh node, cleared.  NULL when there is no memory. */
static tiku_xml_node_t *
node_take(tiku_xml_t *doc)
{
    xml_block_t *b = doc->blocks;
    tiku_xml_node_t *n;

    if (doc->limits && doc->nodes == doc->limits->nodes) {
        over_limit(doc, "XML node limit exceeded");
        return NULL;
    }
    doc->nodes++;
    if (b == NULL || b->used == XML_NODES) {
        if (!xml_room(doc, sizeof *b)) {
            return NULL;
        }
        b = malloc(sizeof *b);
        if (b == NULL) {
            return NULL;
        }
        b->next = doc->blocks;
        b->used = 0;
        doc->blocks = b;
    }
    n = &b->node[b->used++];
    memset(n, 0, sizeof *n);
    return n;
}

/** @brief FNV-1a over @p n bytes. */
static size_t
hash(const char *s, size_t n)
{
    uint32_t h = 2166136261u;

    while (n-- > 0) {
        h = (h ^ (unsigned char)*s++) * 16777619u;
    }
    return h;
}

/** @brief The table of names twice as large.  @return 0, or -1. */
static int
names_grow(tiku_xml_t *doc)
{
    size_t cap = doc->cap ? doc->cap * 2 : 256, i;
    const char **t;

    if (!xml_room(doc, cap * sizeof(const char *))) {
        return -1;
    }
    t = calloc(cap, sizeof *t);
    if (t == NULL) {
        return -1;
    }
    for (i = 0; i < doc->cap; i++) {
        if (doc->names[i] != NULL) {
            size_t k = hash(doc->names[i], strlen(doc->names[i])) & (cap - 1);

            while (t[k] != NULL) {
                k = (k + 1) & (cap - 1);
            }
            t[k] = doc->names[i];
        }
    }
    free(doc->names);
    doc->names = t;
    doc->cap = cap;
    return 0;
}

/** @brief The name @p a (@p an bytes) then @p b (@p bn), kept once.
 *  @return it, or NULL when there is no memory. */
static const char *
intern(tiku_xml_t *doc, const char *a, size_t an, const char *b, size_t bn)
{
    char key[512];
    size_t n = an + bn, k;
    char *s;

    if (n >= sizeof key) {
        if (doc->limits) {
            over_limit(doc, "XML name length limit exceeded");
            return NULL;
        }
        n = sizeof key - 1;         /* a name longer is cut, not refused */
        bn = n > an ? n - an : 0;
        an = n - bn;
    }
    memcpy(key, a, an);
    memcpy(key + an, b, bn);
    key[n] = '\0';
    if ((doc->nnames + 1) * 2 > doc->cap && names_grow(doc) != 0) {
        return NULL;
    }
    for (k = hash(key, n) & (doc->cap - 1); doc->names[k] != NULL;
         k = (k + 1) & (doc->cap - 1)) {
        if (strcmp(doc->names[k], key) == 0) {
            return doc->names[k];
        }
    }
    if ((s = pool_take(doc, n + 1)) == NULL) {
        return NULL;
    }
    memcpy(s, key, n + 1);
    doc->names[k] = s;
    doc->nnames++;
    return s;
}

/** @brief Where @p pat (@p pn bytes) first stands in @p s (@p n bytes):
 *         memmem, which a C library need not declare.  @return it, or
 *         NULL. */
static char *
find(const void *s, size_t n, const char *pat, size_t pn)
{
    const char *p = s, *e = p + n;

    while (pn > 0 && (size_t)(e - p) >= pn &&
           (p = memchr(p, pat[0], (size_t)(e - p) - pn + 1)) != NULL) {
        if (memcmp(p, pat, pn) == 0) {
            return (char *)p;
        }
        p++;
    }
    return NULL;
}

/* ----------------------------------------------------------- encoding   */

/** @brief Code point @p cp as UTF-8 at @p out.  @return the bytes. */
static int
put_utf8(unsigned char *out, uint32_t cp)
{
    if (cp < 0x80) {
        out[0] = (unsigned char)cp;
        return 1;
    }
    if (cp < 0x800) {
        out[0] = (unsigned char)(0xc0 | cp >> 6);
        out[1] = (unsigned char)(0x80 | (cp & 0x3f));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (unsigned char)(0xe0 | cp >> 12);
        out[1] = (unsigned char)(0x80 | (cp >> 6 & 0x3f));
        out[2] = (unsigned char)(0x80 | (cp & 0x3f));
        return 3;
    }
    out[0] = (unsigned char)(0xf0 | cp >> 18);
    out[1] = (unsigned char)(0x80 | (cp >> 12 & 0x3f));
    out[2] = (unsigned char)(0x80 | (cp >> 6 & 0x3f));
    out[3] = (unsigned char)(0x80 | (cp & 0x3f));
    return 4;
}

/**
 * @brief UTF-16 (@p big endian) as UTF-8 into a new buffer: pairs joined,
 *        a lone half made U+FFFD.  @return it, with its length in @p n.
 */
static char *
from_utf16(const unsigned char *s, size_t len, int big, size_t *n)
{
    unsigned char *out = malloc(len / 2 * 3 + 1), *o = out;
    size_t i;

    if (out == NULL) {
        return NULL;
    }
    for (i = 0; i + 1 < len; i += 2) {
        uint32_t u = big ? (uint32_t)(s[i] << 8 | s[i + 1])
                         : (uint32_t)(s[i + 1] << 8 | s[i]);

        if (u >= 0xd800 && u < 0xdc00 && i + 3 < len) {
            uint32_t lo = big ? (uint32_t)(s[i + 2] << 8 | s[i + 3])
                              : (uint32_t)(s[i + 3] << 8 | s[i + 2]);

            if (lo >= 0xdc00 && lo < 0xe000) {
                u = 0x10000 + ((u - 0xd800) << 10) + (lo - 0xdc00);
                i += 2;
            }
        }
        if (u >= 0xd800 && u < 0xe000) {
            u = 0xfffd;
        }
        o += put_utf8(o, u);
    }
    *o = '\0';
    *n = (size_t)(o - out);
    return (char *)out;
}

/** @brief Declared single-byte encoding: 1 for Latin-1, 2 for Windows-1252. */
static int
declared_latin(const unsigned char *s, size_t len)
{
    static const char *latin[] = {"iso-8859-1", "latin1", "latin-1",
                                  "windows-1252", "cp1252", "iso8859-1"};
    char enc[24];
    const unsigned char *e, *q;
    size_t i, k;

    if (len < 6 || memcmp(s, "<?xml", 5) != 0) {
        return 0;
    }
    e = memchr(s, '>', len < 200 ? len : 200);
    q = e ? (const unsigned char *)find(s, (size_t)(e - s), "encoding", 8)
          : NULL;
    if (q == NULL) {
        return 0;
    }
    for (q += 8; q < e && (*q == ' ' || *q == '=' || *q == '"' ||
                           *q == '\''); q++) {}
    for (k = 0; q < e && *q != '"' && *q != '\'' && k + 1 < sizeof enc;) {
        enc[k++] = (char)(*q >= 'A' && *q <= 'Z' ? *q++ + 32 : *q++);
    }
    enc[k] = '\0';
    for (i = 0; i < sizeof latin / sizeof latin[0]; i++) {
        if (strcmp(enc, latin[i]) == 0) {
            return i == 3 || i == 4 ? 2 : 1;
        }
    }
    return 0;
}

/** @brief Latin-1 or Windows-1252 bytes as UTF-8 in a new buffer. */
static char *
from_latin(const unsigned char *s, size_t len, size_t *n, int encoding)
{
    static const unsigned short windows[32] = {
        0x20ac, 0x0081, 0x201a, 0x0192, 0x201e, 0x2026, 0x2020, 0x2021,
        0x02c6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008d, 0x017d, 0x008f,
        0x0090, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014,
        0x02dc, 0x2122, 0x0161, 0x203a, 0x0153, 0x009d, 0x017e, 0x0178
    };
    unsigned char *out = malloc(len * (encoding == 2 ? 3 : 2) + 1), *o = out;
    size_t i;

    if (out == NULL) {
        return NULL;
    }
    for (i = 0; i < len; i++) {
        unsigned cp = s[i];

        if (encoding == 2 && cp >= 0x80 && cp <= 0x9f) {
            cp = windows[cp - 0x80];
        }
        o += put_utf8(o, cp);
    }
    *o = '\0';
    *n = (size_t)(o - out);
    return (char *)out;
}

/**
 * @brief The bytes as UTF-8 in a buffer of the tree's own, its byte-order
 *        mark taken off.  @return it, NUL-ended, with its length in @p n.
 */
static char *
utf8_copy(const unsigned char *s, size_t len, size_t *n)
{
    char *out;
    int encoding;

    if (len >= 2 && ((s[0] == 0xff && s[1] == 0xfe) ||
                     (s[0] == '<' && s[1] == 0))) {
        return from_utf16(s + (s[0] == 0xff ? 2 : 0),
                          len - (s[0] == 0xff ? 2 : 0), 0, n);
    }
    if (len >= 2 && ((s[0] == 0xfe && s[1] == 0xff) ||
                     (s[0] == 0 && s[1] == '<'))) {
        return from_utf16(s + (s[0] == 0xfe ? 2 : 0),
                          len - (s[0] == 0xfe ? 2 : 0), 1, n);
    }
    if (len >= 3 && s[0] == 0xef && s[1] == 0xbb && s[2] == 0xbf) {
        s += 3;
        len -= 3;
    }
    if ((encoding = declared_latin(s, len)) != 0) {
        return from_latin(s, len, n, encoding);
    }
    if ((out = malloc(len + 1)) == NULL) {
        return NULL;
    }
    memcpy(out, s, len);
    out[len] = '\0';
    *n = len;
    return out;
}

/* ----------------------------------------------------------- decoding   */

/** @brief The character reference at @p s ("&#65;", "&#x41;") up to @p e.
 *  @return its code point (U+FFFD when it names none), with @p used set
 *          to its length; 0 when it is not one. */
static uint32_t
char_ref(const char *s, const char *e, size_t *used)
{
    const char *q = s + 2;
    int hex = q < e && (*q == 'x' || *q == 'X');
    uint32_t v = 0;
    int digits = 0;

    for (q += hex; q < e && *q != ';' && digits < 8; q++, digits++) {
        int d = *q >= '0' && *q <= '9' ? *q - '0' :
                hex && *q >= 'a' && *q <= 'f' ? *q - 'a' + 10 :
                hex && *q >= 'A' && *q <= 'F' ? *q - 'A' + 10 : -1;

        if (d < 0) {
            return 0;
        }
        v = v * (hex ? 16u : 10u) + (uint32_t)d;
    }
    if (q >= e || *q != ';' || digits == 0) {
        return 0;
    }
    *used = (size_t)(q + 1 - s);
    if (v == 0 || v > 0x10ffff || (v >= 0xd800 && v < 0xe000)) {
        return 0xfffd;
    }
    return v;
}

/** @brief The named reference at @p s, one of the five.  @return its
 *         character, with @p used set; 0 when it is none of them. */
static char
named_ref(const char *s, const char *e, size_t *used)
{
    static const struct { const char *name; char c; } five[] = {
        {"&lt;", '<'}, {"&gt;", '>'}, {"&amp;", '&'}, {"&quot;", '"'},
        {"&apos;", '\''}
    };
    size_t i;

    for (i = 0; i < sizeof five / sizeof five[0]; i++) {
        size_t n = strlen(five[i].name);

        if ((size_t)(e - s) >= n && memcmp(s, five[i].name, n) == 0) {
            *used = n;
            return five[i].c;
        }
    }
    return 0;
}

/**
 * @brief Decode @p s up to @p e where it lies: references replaced, line
 *        ends made '\n'.  An '&' that starts no reference is kept as it
 *        is.  @return the new end, NUL written there.
 */
static char *
decode(char *s, char *e)
{
    char *o = s;

    while (s < e) {
        size_t used = 0;

        if (*s == '&' && s + 1 < e && s[1] == '#') {
            uint32_t cp = char_ref(s, e, &used);

            if (cp != 0) {
                o += put_utf8((unsigned char *)o, cp);
                s += used;
                continue;
            }
        } else if (*s == '&') {
            char c = named_ref(s, e, &used);

            if (c != 0) {
                *o++ = c;
                s += used;
                continue;
            }
        } else if (*s == '\r') {
            *o++ = '\n';
            s += s + 1 < e && s[1] == '\n' ? 2 : 1;
            continue;
        }
        *o++ = *s++;
    }
    *o = '\0';
    return o;
}

/* ------------------------------------------------------------- parsing   */

/** @brief Stop with @p what, on the line @p where is on; a limit the
 *         parse hit is said in its place.  @return NULL. */
static void *
fail(xml_parse_t *x, const char *where, const char *what)
{
    const char *s;
    int line = 1;

    if (!x->doc->status) {
        x->doc->status = !strcmp(what, "out of memory") ? TIKU_XML_NOMEM
                                                         : TIKU_XML_BAD;
    }
    if (x->doc->status == TIKU_XML_LIMIT && x->doc->limit != NULL) {
        what = x->doc->limit;
    }
    for (s = x->doc->buf; s < where && s < x->end; s++) {
        line += *s == '\n';
    }
    if (x->why != NULL && x->max > 0) {
        snprintf(x->why, x->max, "line %d: %s", line, what);
    }
    return NULL;
}

/** @brief Whether @p c is XML's whitespace. */
static int
space(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

/** @brief The character at @p p, a '<' that text's end overwrote
 *         counted as one. */
static char
at(const xml_parse_t *x, const char *p)
{
    return p == x->p && x->lt ? '<' : *p;
}

/** @brief Past a name's characters.  @return where it ends. */
static char *
name_end(char *p, const char *e)
{
    while (p < e && !space(*p) && *p != '>' && *p != '/' && *p != '=' &&
           *p != '<') {
        p++;
    }
    return p;
}

/** @brief The prefix @p pfx (@p plen bytes) as known here: the canonical
 *         one bound to it, NULL when unknown, or @p *found 0 unbound. */
static const char *
resolve(const xml_parse_t *x, const char *pfx, size_t plen, int *found)
{
    int i;

    *found = 1;
    if (plen == 3 && memcmp(pfx, "xml", 3) == 0) {
        return "xml";
    }
    for (i = x->nbind - 1; i >= 0; i--) {
        if (x->bind[i].plen == plen &&
            memcmp(x->bind[i].pfx, pfx, plen) == 0) {
            return x->bind[i].canon;
        }
    }
    *found = 0;
    return NULL;
}

/** @brief "@p canon:" then the local name @p local (@p n bytes), kept
 *         once.  @return it, or NULL. */
static const char *
prefixed(tiku_xml_t *doc, const char *canon, const char *local, size_t n)
{
    char pre[24];
    size_t k = strlen(canon);

    memcpy(pre, canon, k);
    pre[k] = ':';
    return intern(doc, pre, k + 1, local, n);
}

/**
 * @brief Qualified name @p qn (@p n bytes) as the tree names it: a known
 *        namespace's prefix, else the prefix written; an element with no
 *        prefix takes the default namespace, an attribute none.
 */
static const char *
qualify(xml_parse_t *x, const char *qn, size_t n, int element)
{
    const char *colon = memchr(qn, ':', n), *canon;
    size_t plen = colon ? (size_t)(colon - qn) : 0;
    int found;

    if (x->doc->lexical_names)
        return intern(x->doc, qn, n, "", 0);
    if (colon == NULL && !element) {
        return intern(x->doc, qn, n, "", 0);
    }
    canon = resolve(x, qn, plen, &found);
    if (canon == NULL) {
        return intern(x->doc, qn, n, "", 0);    /* as written */
    }
    if (colon != NULL) {
        qn = colon + 1;
        n -= plen + 1;
    }
    return prefixed(x->doc, canon, qn, n);
}

/** @brief Bind the namespace attribute @p a declares, for this element
 *         and those under it.  @return 0, or -1 out of memory. */
static int
bind(xml_parse_t *x, const xml_att_t *a)
{
    size_t i;
    xml_bind_t *b;

    if (x->nbind == x->bindcap) {
        int cap = x->bindcap ? x->bindcap * 2 : 32;
        xml_bind_t *g;

        if (!xml_room(x->doc, (size_t)cap * sizeof(xml_bind_t))) {
            return -1;
        }
        g = realloc(x->bind, (size_t)cap * sizeof *g);
        if (g == NULL) {
            return -1;
        }
        x->bind = g;
        x->bindcap = cap;
    }
    b = &x->bind[x->nbind++];
    b->pfx = a->qlen > 6 ? a->qn + 6 : "";
    b->plen = a->qlen > 6 ? a->qlen - 6 : 0;
    b->canon = NULL;
    b->depth = x->depth + 1;
    for (i = 0; i < sizeof known / sizeof known[0]; i++) {
        if (strcmp(a->val, known[i].uri) == 0) {
            b->canon = known[i].prefix;
            break;
        }
    }
    return 0;
}

/** @brief Whether attribute @p a declares a namespace. */
static int
is_xmlns(const xml_att_t *a)
{
    return (a->qlen == 5 || (a->qlen > 6 && a->qn[5] == ':')) &&
           memcmp(a->qn, "xmlns", 5) == 0;
}

/**
 * @brief A tag's attributes from @p p: each name, and its value decoded.
 * @return where the tag's end is ('>' or "/>"), or NULL, the parse failed,
 *         for a tag that is not well formed; @p n set to how many were
 *         kept, XML_ATTRS at most.
 */
static char *
read_attrs(xml_parse_t *x, char *p, int *n)
{
    *n = 0;
    for (;;) {
        char *qn, *v, q;

        while (p < x->end && space(*p)) {
            p++;
        }
        if (p >= x->end) {
            return fail(x, p, "a tag is not closed");
        }
        if (*p == '>' || (*p == '/' && p + 1 < x->end && p[1] == '>')) {
            return p;
        }
        qn = p;
        p = name_end(p, x->end);
        if (p == qn) {
            return fail(x, p, "an attribute has no name");
        }
        if (*n < XML_ATTRS) {
            x->att[*n].qn = qn;
            x->att[*n].qlen = (size_t)(p - qn);
        }
        while (p < x->end && space(*p)) {
            p++;
        }
        if (p >= x->end || *p != '=') {
            return fail(x, p, "an attribute has no value");
        }
        for (p++; p < x->end && space(*p); p++) {}
        if (p >= x->end || (*p != '"' && *p != '\'')) {
            return fail(x, p, "an attribute's value is not quoted");
        }
        q = *p++;
        v = p;
        p = memchr(p, q, (size_t)(x->end - p));
        if (p == NULL) {
            return fail(x, v, "an attribute's value does not end");
        }
        if (x->doc->limits) {
            int i;

            if (memchr(v, '<', (size_t)(p - v))) {
                return fail(x, v, "unescaped less-than in attribute");
            }
            if (*n == XML_ATTRS) {
                over_limit(x->doc, "too many attributes");
                return fail(x, v, "too many attributes");
            }
            for (i = 0; i < *n; i++) {
                if (x->att[i].qlen == x->att[*n].qlen &&
                    !memcmp(x->att[i].qn, qn, x->att[i].qlen)) {
                    return fail(x, qn, "duplicate attribute");
                }
            }
        }
        decode(v, p++);
        if (*n < XML_ATTRS) {
            x->att[(*n)++].val = v;
        }
    }
}

/** @brief @p node added as the open element's last child, or as the
 *         root.  @return 0, or -1 for a second root. */
static int
link_node(xml_parse_t *x, tiku_xml_node_t *node)
{
    node->parent = x->cur;
    if (x->cur == NULL) {
        if (x->doc->root != NULL) {
            return -1;
        }
        x->doc->root = node;
        return 0;
    }
    if (x->last[x->depth] == NULL) {
        x->cur->child = node;
    } else {
        x->last[x->depth]->next = node;
    }
    x->last[x->depth] = node;
    return 0;
}

/** @brief The attributes as the node keeps them: names qualified, the
 *         namespace declarations left out.  @return 0, or -1. */
static int
keep_attrs(xml_parse_t *x, tiku_xml_node_t *node, int n)
{
    int i, k = 0;

    for (i = 0; i < n; i++) {
        k += !is_xmlns(&x->att[i]) || (x->doc->lexical_names);
    }
    if (k == 0) {
        return 0;
    }
    node->attr = pool_take(x->doc, (size_t)k * 2 * sizeof *node->attr);
    if (node->attr == NULL) {
        return -1;
    }
    for (i = 0; i < n; i++) {
        if (is_xmlns(&x->att[i]) && !(x->doc->lexical_names)) {
            continue;
        }
        node->attr[node->nattr * 2] = qualify(x, x->att[i].qn,
                                              x->att[i].qlen, 0);
        node->attr[node->nattr * 2 + 1] = x->att[i].val;
        if (node->attr[node->nattr * 2] == NULL) {
            return -1;
        }
        node->nattr++;
    }
    return 0;
}

/** @brief A start tag at @p p, past its '<'.  @return where reading goes
 *         on, or NULL. */
static char *
start_tag(xml_parse_t *x, char *p)
{
    char *qn = p, *end;
    size_t qlen;
    int n, i, empty;
    tiku_xml_node_t *node;

    p = name_end(p, x->end);
    qlen = (size_t)(p - qn);
    if (qlen == 0) {
        return fail(x, p, "a tag has no name");
    }
    if ((end = read_attrs(x, p, &n)) == NULL) {
        return NULL;
    }
    empty = *end == '/';
    if (x->depth >= TIKU_XML_DEPTH) {
        return fail(x, qn, "elements nest too deeply");
    }
    for (i = 0; i < n && i < XML_ATTRS; i++) {
        if (is_xmlns(&x->att[i]) && bind(x, &x->att[i]) != 0) {
            return fail(x, qn, "out of memory");
        }
    }
    node = node_take(x->doc);
    if (node == NULL || (node->name = qualify(x, qn, qlen, 1)) == NULL ||
        keep_attrs(x, node, n < XML_ATTRS ? n : XML_ATTRS) != 0) {
        return fail(x, qn, "out of memory");
    }
    node->text = "";
    if (link_node(x, node) != 0) {
        return fail(x, qn, "a second element outside the first");
    }
    if (!empty) {
        x->cur = node;
        x->last[++x->depth] = NULL;
    } else {
        while (x->nbind > 0 && x->bind[x->nbind - 1].depth > x->depth) {
            x->nbind--;
        }
    }
    return end + (empty ? 2 : 1);
}

/** @brief An end tag at @p p, past its "</".  @return where reading goes
 *         on, or NULL for one that closes nothing open. */
static char *
end_tag(xml_parse_t *x, char *p)
{
    char *qn = p;
    const char *name;

    p = name_end(p, x->end);
    if (x->cur == NULL) {
        return fail(x, qn, "an end tag closes nothing");
    }
    name = qualify(x, qn, (size_t)(p - qn), 1);
    if (name != x->cur->name) {
        return fail(x, qn, "an end tag closes another element");
    }
    while (p < x->end && space(*p)) {
        p++;
    }
    if (p >= x->end || *p != '>') {
        return fail(x, p, "an end tag is not closed");
    }
    while (x->nbind > 0 && x->bind[x->nbind - 1].depth >= x->depth) {
        x->nbind--;
    }
    x->cur = x->cur->parent;
    x->depth--;
    return p + 1;
}

/** @brief Text from @p s up to @p e, decoded, as the open element's.
 *  @return 0, or -1 out of memory. */
static int
text_node(xml_parse_t *x, char *s, char *e, int raw)
{
    tiku_xml_node_t *node;

    if (x->cur == NULL) {
        return 0;                   /* around the root: nothing to hold it */
    }
    if ((node = node_take(x->doc)) == NULL) {
        return -1;
    }
    if (raw) {
        *e = '\0';
    } else {
        decode(s, e);
    }
    node->text = s;
    return link_node(x, node);
}

/** @brief Past a declaration, doctype and internal subset included:
 *         nothing in it is read.  @return where reading goes on. */
static char *
skip_decl(xml_parse_t *x, char *p)
{
    int depth = 0;

    for (; p < x->end; p++) {
        if (*p == '"' || *p == '\'') {
            char *q = memchr(p + 1, *p, (size_t)(x->end - p - 1));

            if (q == NULL) {
                break;
            }
            p = q;
        } else if (*p == '[') {
            depth++;
        } else if (*p == ']') {
            depth--;
        } else if (*p == '>' && depth <= 0) {
            return p + 1;
        }
    }
    return fail(x, p, "a declaration does not end");
}

/** @brief Past something @p close ends ("-->", "?>").  @return where
 *         reading goes on, or NULL. */
static char *
skip_to(xml_parse_t *x, char *p, const char *close)
{
    char *q = find(p, (size_t)(x->end - p), close, strlen(close));

    if (q == NULL) {
        return fail(x, p, "a comment or instruction does not end");
    }
    return q + strlen(close);
}

/** @brief What starts with '<' at @p p: a tag, a comment, CDATA, an
 *         instruction or a declaration.  @return where reading goes on. */
static char *
markup(xml_parse_t *x, char *p)
{
    char *s;

    x->lt = 0;
    p++;
    if (p >= x->end) {
        return fail(x, p, "the file ends inside a tag");
    }
    if (*p == '/') {
        return end_tag(x, p + 1);
    }
    if (*p == '?') {
        return skip_to(x, p, "?>");
    }
    if (*p != '!') {
        return start_tag(x, p);
    }
    if (x->end - p >= 3 && memcmp(p, "!--", 3) == 0) {
        return skip_to(x, p + 3, "-->");
    }
    if (x->end - p >= 8 && memcmp(p, "![CDATA[", 8) == 0) {
        s = p + 8;
        p = find(s, (size_t)(x->end - s), "]]>", 3);
        if (p == NULL) {
            return fail(x, s, "a CDATA section does not end");
        }
        if (text_node(x, s, p, 1) != 0) {
            return fail(x, s, "out of memory");
        }
        return p + 3;
    }
    return skip_decl(x, p);
}

/** @brief The whole document, markup and text in turn.  @return 0, or
 *         -1 with why set. */
static int
parse(xml_parse_t *x)
{
    char *p = x->p;

    while (p != NULL && p < x->end) {
        char *s = p;

        if (x->doc->limits && x->doc->limits->cancel &&
            x->doc->limits->cancel(x->doc->limits->ctx)) {
            x->doc->status = TIKU_XML_CANCELLED;
            (void)fail(x, p, "cancelled");
            return -1;
        }
        x->p = p;
        if (at(x, p) == '<') {
            p = markup(x, p);
            continue;
        }
        p = memchr(p, '<', (size_t)(x->end - p));
        if (p == NULL) {
            p = x->end;
        }
        if (x->cur != NULL && text_node(x, s, p, 0) != 0) {
            (void)fail(x, s, "out of memory");
            return -1;
        }
        if (x->cur == NULL && x->doc->limits) {
            char *q;

            for (q = s; q < p; q++) {
                if (!space(*q)) {
                    (void)fail(x, q, "text outside the root element");
                    return -1;
                }
            }
        }
        x->p = p;
        x->lt = p < x->end;         /* decoding wrote its end over '<' */
    }
    if (p == NULL) {
        return -1;
    }
    if (x->cur != NULL) {
        (void)fail(x, x->end, "the file ends inside an element");
        return -1;
    }
    if (x->doc->root == NULL) {
        (void)fail(x, x->end, "there is no element");
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------ the API   */

/** @brief Parse canonical or lexical names using identical ownership and
 * limits. */
static tiku_xml_t *
parse_limited(const void *bytes, size_t len, const tiku_xml_limits_t *limits,
              tiku_xml_status_t *status, char *why, size_t max, int lexical)
{
    xml_parse_t x;
    size_t n = 0;
    tiku_xml_t *doc;

    if (bytes == NULL) {
        if (status) {
            *status = TIKU_XML_BAD;
        }
        if (why != NULL && max > 0) {
            snprintf(why, max, "no bytes");
        }
        return NULL;
    }
    if (status) {
        *status = TIKU_XML_NOMEM;
    }
    doc = calloc(1, sizeof *doc);
    /* The copy of the bytes is charged at three times their length,
     * more than it takes made UTF-8 from any encoding read here. */
    if (doc) {
        doc->limits = limits;
        doc->lexical_names = lexical;
        if (len > (SIZE_MAX - 1) / 3 ||
            !xml_room(doc, sizeof *doc + XML_ATTRS * sizeof(xml_att_t)) ||
            !xml_room(doc, len * 3 + 1)) {
            if (status) {
                *status = TIKU_XML_LIMIT;
            }
            if (why && max) {
                snprintf(why, max, "XML allocation budget exceeded");
            }
            free(doc);
            return NULL;
        }
    }
    memset(&x, 0, sizeof x);
    x.why = why;
    x.max = max;
    if (why != NULL && max > 0) {
        why[0] = '\0';
    }
    x.att = malloc(XML_ATTRS * sizeof *x.att);
    if (doc == NULL || x.att == NULL ||
        (doc->buf = utf8_copy(bytes, len, &n)) == NULL) {
        if (why != NULL && max > 0) {
            snprintf(why, max, "out of memory");
        }
        free(x.att);
        tiku_xml_free(doc);
        return NULL;
    }
    x.doc = doc;
    x.p = doc->buf;
    x.end = doc->buf + n;
    if (parse(&x) != 0) {
        if (status) {
            *status = doc->status ? doc->status : TIKU_XML_BAD;
        }
        free(x.att);
        free(x.bind);
        tiku_xml_free(doc);
        return NULL;
    }
    free(x.att);
    free(x.bind);
    doc->limits = NULL;             /* the caller's: not kept past here */
    if (status) {
        *status = TIKU_XML_OK;
    }
    return doc;
}

tiku_xml_t *
tiku_xml_parse_limited(const void *bytes, size_t len,
                       const tiku_xml_limits_t *limits,
                       tiku_xml_status_t *status, char *why, size_t max)
{
    return parse_limited(bytes, len, limits, status, why, max, 0);
}

tiku_xml_t *
tiku_xml_parse_lexical_limited(const void *bytes, size_t len,
                               const tiku_xml_limits_t *limits,
                               tiku_xml_status_t *status, char *why, size_t max)
{
    return parse_limited(bytes, len, limits, status, why, max, 1);
}

tiku_xml_t *
tiku_xml_parse(const void *bytes, size_t len, char *why, size_t max)
{
    return tiku_xml_parse_limited(bytes, len, NULL, NULL, why, max);
}

void
tiku_xml_free(tiku_xml_t *doc)
{
    if (doc == NULL) {
        return;
    }
    while (doc->pool != NULL) {
        xml_pool_t *next = doc->pool->next;

        free(doc->pool);
        doc->pool = next;
    }
    while (doc->blocks != NULL) {
        xml_block_t *next = doc->blocks->next;

        free(doc->blocks);
        doc->blocks = next;
    }
    free(doc->names);
    free(doc->buf);
    free(doc);
}

tiku_xml_node_t *
tiku_xml_root(const tiku_xml_t *doc)
{
    return doc != NULL ? doc->root : NULL;
}

int
tiku_xml_is(const tiku_xml_node_t *n, const char *name)
{
    return n != NULL && n->name != NULL && strcmp(n->name, name) == 0;
}

const char *
tiku_xml_attr(const tiku_xml_node_t *n, const char *name)
{
    int i;

    for (i = 0; n != NULL && i < n->nattr; i++) {
        if (strcmp(n->attr[2 * i], name) == 0) {
            return n->attr[2 * i + 1];
        }
    }
    return NULL;
}

tiku_xml_node_t *
tiku_xml_child(const tiku_xml_node_t *n, const char *name)
{
    tiku_xml_node_t *c;

    for (c = n != NULL ? n->child : NULL; c != NULL; c = c->next) {
        if (c->name != NULL && (name == NULL || strcmp(c->name, name) == 0)) {
            return c;
        }
    }
    return NULL;
}

tiku_xml_node_t *
tiku_xml_next(const tiku_xml_node_t *n, const char *name)
{
    tiku_xml_node_t *c;

    for (c = n != NULL ? n->next : NULL; c != NULL; c = c->next) {
        if (c->name != NULL && (name == NULL || strcmp(c->name, name) == 0)) {
            return c;
        }
    }
    return NULL;
}

tiku_xml_node_t *
tiku_xml_path(const tiku_xml_node_t *n, const char *path)
{
    char part[128];
    tiku_xml_node_t *c = (tiku_xml_node_t *)n;

    while (c != NULL && path != NULL && *path) {
        const char *slash = strchr(path, '/');
        size_t k = slash ? (size_t)(slash - path) : strlen(path);

        if (k >= sizeof part) {
            return NULL;
        }
        memcpy(part, path, k);
        part[k] = '\0';
        c = tiku_xml_child(c, part);
        path = slash ? slash + 1 : NULL;
    }
    return c;
}

size_t
tiku_xml_text(const tiku_xml_node_t *n, char *out, size_t max)
{
    const tiku_xml_node_t *c = n;
    size_t total = 0;

    if (max > 0) {
        out[0] = '\0';
    }
    while (c != NULL) {
        if (c->name == NULL) {
            size_t k = strlen(c->text);

            if (total + 1 < max) {
                size_t room = max - 1 - total, take = k < room ? k : room;

                while (take < k && take > 0 &&
                       ((unsigned char)c->text[take] & 0xc0) == 0x80) {
                    take--;         /* never half a character */
                }
                memcpy(out + total, c->text, take);
                out[total + take] = '\0';
                if (take < k) {
                    max = total + take + 1;     /* nothing more fits */
                }
            }
            total += k;
        }
        if (c->child != NULL) {
            c = c->child;
            continue;
        }
        while (c != n && c->next == NULL) {
            c = c->parent;
        }
        c = c != n ? c->next : NULL;
    }
    return total;
}
