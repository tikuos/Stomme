/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_pdf_sign.c - digital signatures (ISO 32000-2 12.8): each signature
 * field's value found through the form, the bytes its ByteRange covers
 * checked against the signature in the gap it leaves -- CMS detached
 * (adbe.pkcs7.detached, ETSI.CAdES.detached), a digest inside CMS
 * (adbe.pkcs7.sha1), a bare RSA signature (adbe.x509.rsa_sha1), or a
 * timestamp of the whole (ETSI.RFC3161) -- and what was added after it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_pdf_private.h"
#include "tiku_cms.h"
#include "tiku_crypto.h"
#include "tiku_der.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SIG_MAX    64               /* signatures read from one document */
#define SIG_RANGES 8                /* runs one signature may cover      */
#define SIG_DEPTH  16               /* fields within fields              */

/** @brief A signature found in the form, before it is checked. */
typedef struct {
    pdf_obj_t *field, *v;           /* the field (or its widget); value  */
    char      *name;                /* the field's full name, owned      */
    long       end;                 /* where its ByteRange ends          */
} found_t;

/** @brief The signatures a walk of the form has found. */
typedef struct {
    found_t item[SIG_MAX];
    int     n;
} finds_t;

/* ------------------------------------------------------------- the form  */

/** @brief @p parent's name and a field's own partial name @p t, joined
 *         by a dot.  @return it, owned. */
static char *
full_name(const char *parent, pdf_obj_t *t)
{
    char *own = t != NULL && t->kind == PDF_STRING ? pdf_text_string(t) : NULL;
    size_t n = strlen(parent) + (own ? strlen(own) : 0) + 2;
    char *out = malloc(n);

    if (out != NULL) {
        snprintf(out, n, "%s%s%s", parent, *parent && own ? "." : "",
                 own ? own : "");
    }
    free(own);
    return out;
}

/** @brief Field @p f and those under it, named from @p parent and of
 *         type @p ft unless they say otherwise: each signature field
 *         with a value, into @p finds. */
static void
walk(tiku_pdf_t *doc, pdf_obj_t *f, const char *parent, pdf_obj_t *ft,
     finds_t *finds, int depth)
{
    pdf_obj_t *kids, *v, *own = pdf_get(doc, f, "FT");
    char *name;
    int i;

    if (f == NULL || f->kind != PDF_DICT || depth > SIG_DEPTH ||
        (name = full_name(parent, pdf_get(doc, f, "T"))) == NULL) {
        return;
    }
    if (own != NULL) ft = own;
    v = pdf_get(doc, f, "V");
    for (i = 0; i < finds->n && finds->item[i].v != v; i++) {}
    if (pdf_is_name(ft, "Sig") && v != NULL && v->kind == PDF_DICT &&
        i == finds->n && finds->n < SIG_MAX) {
        finds->item[finds->n].field = f;
        finds->item[finds->n].v = v;
        finds->item[finds->n++].name = name;
        name = NULL;
    }
    kids = pdf_get(doc, f, "Kids");
    for (i = 0; kids != NULL && kids->kind == PDF_ARRAY && i < kids->u.a.n;
         i++) {
        pdf_obj_t *kid = pdf_resolve(doc, kids->u.a.item[i]);

        if (pdf_get(doc, kid, "T") != NULL) {   /* a field, not a widget */
            walk(doc, kid, name ? name : "", ft, finds, depth + 1);
        }
    }
    free(name);
}

/** @brief The widget that shows field @p f: itself, or its first kid. */
static pdf_obj_t *
widget_of(tiku_pdf_t *doc, pdf_obj_t *f)
{
    pdf_obj_t *kids = pdf_get(doc, f, "Kids");

    if (pdf_get(doc, f, "Rect") != NULL || kids == NULL ||
        kids->kind != PDF_ARRAY || kids->u.a.n == 0) {
        return f;
    }
    return pdf_resolve(doc, kids->u.a.item[0]);
}

/** @brief The page widget @p w is on: its /P, else the page whose
 *         annotations hold it.  @return the page, or -1. */
static int
widget_page(tiku_pdf_t *doc, pdf_obj_t *w)
{
    pdf_obj_t *p = pdf_get(doc, w, "P"), *annots;
    int page, i;

    if (p != NULL && (page = pdf_page_index(doc, p)) >= 0) {
        return page;
    }
    for (page = 0; page < tiku_pdf_pages(doc) && page < 2000; page++) {
        annots = pdf_get(doc, pdf_page_dict(doc, page), "Annots");
        for (i = 0; annots != NULL && annots->kind == PDF_ARRAY &&
                    i < annots->u.a.n; i++) {
            if (pdf_resolve(doc, annots->u.a.item[i]) == w) return page;
        }
    }
    return -1;
}

/** @brief Where signature @p s's field shows: its page, and its box as
 *         the page is drawn at scale 1 (empty for an invisible one). */
static void
place(tiku_pdf_t *doc, pdf_obj_t *field, tiku_pdf_signature_t *s)
{
    pdf_obj_t *w = widget_of(doc, field), *rect = pdf_get(doc, w, "Rect");
    double r[4], x, y;
    pdf_mat_t m;
    int i, pw, ph;

    s->page = widget_page(doc, w);
    if (s->page < 0 || rect == NULL || rect->kind != PDF_ARRAY ||
        rect->u.a.n != 4 ||
        pdf_page_matrix(doc, s->page, 1.0, 0, 0, 0, &m, &pw, &ph) != 0) {
        return;
    }
    for (i = 0; i < 4; i++) {
        r[i] = pdf_number(pdf_resolve(doc, rect->u.a.item[i]), 0);
    }
    s->x0 = s->y0 = 1e9f;
    s->x1 = s->y1 = -1e9f;
    for (i = 0; i < 4; i++) {
        pdf_mat_apply(&m, r[i & 1 ? 2 : 0], r[i & 2 ? 3 : 1], &x, &y);
        if (x < s->x0) s->x0 = (float)x;
        if (x > s->x1) s->x1 = (float)x;
        if (y < s->y0) s->y0 = (float)y;
        if (y > s->y1) s->y1 = (float)y;
    }
    if (s->x1 - s->x0 < 1 || s->y1 - s->y0 < 1) {
        s->x0 = s->y0 = s->x1 = s->y1 = 0;
    }
}

/* ------------------------------------------------------ the byte range  */

/** @brief The runs of the file @p v's ByteRange covers, into @p p.
 *         @return how many, or -1 for a range that is not one: out of
 *         order, off the file, or not starting at its first byte. */
static int
byte_range(tiku_pdf_t *doc, pdf_obj_t *v, tiku_piece_t *p, long *end)
{
    pdf_obj_t *br = pdf_get(doc, v, "ByteRange");
    double at, len, last = 0;
    int i, n;

    if (br == NULL || br->kind != PDF_ARRAY || br->u.a.n < 4 ||
        br->u.a.n % 2 != 0 || br->u.a.n > 2 * SIG_RANGES) {
        return -1;
    }
    for (i = n = 0; i < br->u.a.n; i += 2, n++) {
        at = pdf_number(pdf_resolve(doc, br->u.a.item[i]), -1);
        len = pdf_number(pdf_resolve(doc, br->u.a.item[i + 1]), -1);
        if (at < last || len < 0 || at + len > (double)doc->len ||
            (i == 0 && at != 0)) {
            return -1;
        }
        p[n].p = doc->data + (long)at;
        p[n].n = (size_t)len;
        last = at + len;
    }
    *end = (long)last;
    return n;
}

/** @brief The signature value in the gap after run @p p: a hex string,
 *         read from the file itself, as it was signed around.
 *         @return its bytes to free(), @p n of them, or NULL. */
static unsigned char *
gap_value(tiku_pdf_t *doc, const tiku_piece_t *p, size_t *n)
{
    const unsigned char *a = p[0].p + p[0].n, *b = p[1].p, *q;
    unsigned char *out;
    int hi = -1, d;

    if (b - a < 2 || *a != '<' || b[-1] != '>' ||
        (out = malloc((size_t)(b - a) / 2 + 1)) == NULL) {
        return NULL;
    }
    *n = 0;
    for (q = a + 1; q < b - 1; q++) {
        d = *q >= '0' && *q <= '9' ? *q - '0' : (*q | 32) >= 'a' &&
            (*q | 32) <= 'f' ? (*q | 32) - 'a' + 10 : -1;
        if (d < 0) {
            if (*q == ' ' || *q == '\r' || *q == '\n' || *q == '\t') continue;
            free(out);
            return NULL;
        }
        if (hi < 0) {
            hi = d;
        } else {
            out[(*n)++] = (unsigned char)(hi << 4 | d);
            hi = -1;
        }
    }
    (void)doc;
    return out;
}

/** @brief Whether @p der, the value in the gap, is @p v's own /Contents;
 *         a ByteRange whose gap holds another string fails.  An encrypted
 *         document passes unchecked: its strings were decrypted as read,
 *         and a signature's /Contents is never encrypted. */
static int
own_value(tiku_pdf_t *doc, pdf_obj_t *v, const unsigned char *der, size_t n)
{
    pdf_obj_t *c = pdf_get(doc, v, "Contents");

    return doc->encrypted || (c != NULL && c->kind == PDF_STRING &&
                              c->u.s.n == n && memcmp(c->u.s.p, der, n) == 0);
}

/** @brief Whether nothing but white space follows byte @p end. */
static int
nothing_after(tiku_pdf_t *doc, long end)
{
    for (; end < doc->len; end++) {
        unsigned char c = doc->data[end];

        if (c != ' ' && c != '\r' && c != '\n' && c != '\t' && c != 0) {
            return 0;
        }
    }
    return 1;
}

/* --------------------------------------------------------- checking  */

/** @brief Copy @p s, or NULL for none or empty. */
static char *
dup(const char *s)
{
    char *out = s != NULL && *s ? malloc(strlen(s) + 1) : NULL;

    if (out != NULL) strcpy(out, s);
    return out;
}

/** @brief A PDF date (7.9.4), "D:YYYYMMDDHHmmSSOHH'mm'", as seconds since
 *         1970, UTC; 0 for what is not one. */
static int64_t
pdf_date(pdf_obj_t *o)
{
    char text[48], der[48];
    const char *s;
    size_t n = 0;
    tiku_der_t e;
    int64_t t;

    if (o == NULL || o->kind != PDF_STRING || o->u.s.n >= sizeof text) {
        return 0;
    }
    memcpy(text, o->u.s.p, o->u.s.n);
    text[o->u.s.n] = 0;
    s = strncmp(text, "D:", 2) == 0 ? text + 2 : text;
    while (*s && n < sizeof der - 8) {  /* as a GeneralizedTime: no ' */
        if (*s != '\'') der[n++] = *s;
        s++;
    }
    while (n >= 4 && n < 14) {         /* a month and day left out: 1 */
        der[n] = n == 5 || n == 7 ? '1' : '0';
        n++;
    }
    e.cls = 0;
    e.tag = TIKU_DER_GEN_TIME;
    e.body = (const unsigned char *)der;
    e.len = n;
    return tiku_der_time(&e, &t) == 0 ? t : 0;
}

/** @brief A bare RSA signature (adbe.x509.rsa_sha1) over @p p: the value
 *         a DER OCTET STRING in the gap, the signer's certificate first
 *         in /Cert.  SHA-1 is tried first, then SHA-256, SHA-384 and
 *         SHA-512 while the verdict is TIKU_SIG_CHANGED. */
static void
check_raw(tiku_pdf_t *doc, pdf_obj_t *v, const unsigned char *der,
          size_t dn, const tiku_piece_t *p, int n, tiku_cms_sig_t *out)
{
    static const int algs[] = { TIKU_HASH_SHA1, TIKU_HASH_SHA256,
                                TIKU_HASH_SHA384, TIKU_HASH_SHA512 };
    pdf_obj_t *cert = pdf_get(doc, v, "Cert");
    tiku_der_t sig;
    size_t i;

    if (cert != NULL && cert->kind == PDF_ARRAY && cert->u.a.n > 0) {
        cert = pdf_resolve(doc, cert->u.a.item[0]);
    }
    memset(out, 0, sizeof *out);
    out->status = TIKU_SIG_UNCHECKED;
    if (cert == NULL || cert->kind != PDF_STRING ||
        tiku_der_read(der, dn, &sig) != 0 ||
        !tiku_der_is(&sig, TIKU_DER_OCTETS)) {
        snprintf(out->why, sizeof out->why, "its certificate or value is "
                 "not there");
        return;
    }
    for (i = 0; i < sizeof algs / sizeof algs[0]; i++) {
        if (tiku_cms_verify_raw(cert->u.s.p, cert->u.s.n, sig.body, sig.len,
                                algs[i], p, n, out) != TIKU_SIG_CHANGED) {
            return;
        }
    }
}

/** @brief adbe.pkcs7.sha1: the SignedData over the SHA-1 digest of the
 *         byte range it holds, that digest held against the file's. */
static void
check_sha1(const unsigned char *der, size_t dn, const tiku_piece_t *p, int n,
           tiku_cms_sig_t *out)
{
    unsigned char d[20];
    tiku_hash_t h;
    int i;

    if (tiku_cms_verify(der, dn, NULL, 0, out) != TIKU_SIG_GOOD) {
        return;
    }
    (void)tiku_hash_init(&h, TIKU_HASH_SHA1);
    for (i = 0; i < n; i++) {
        tiku_hash_update(&h, p[i].p, p[i].n);
    }
    tiku_hash_final(&h, d);
    if (out->econtent_len != 20 || memcmp(out->econtent, d, 20) != 0) {
        out->status = TIKU_SIG_CHANGED;
        snprintf(out->why, sizeof out->why, "the content is not what was "
                 "signed");
    }
}

/** @brief Whether signature value @p v certifies the document: the
 *         catalogue's DocMDP names it, or it names DocMDP itself. */
static int
certifies(tiku_pdf_t *doc, pdf_obj_t *v)
{
    pdf_obj_t *refs = pdf_get(doc, v, "Reference");
    int i;

    if (pdf_get(doc, pdf_get(doc, pdf_root(doc), "Perms"), "DocMDP") == v) {
        return 1;
    }
    for (i = 0; refs != NULL && refs->kind == PDF_ARRAY && i < refs->u.a.n;
         i++) {
        pdf_obj_t *r = pdf_resolve(doc, refs->u.a.item[i]);

        if (pdf_is_name(pdf_get(doc, r, "TransformMethod"), "DocMDP")) {
            return 1;
        }
    }
    return 0;
}

/** @brief The CMS checker's verdict @p c, and signature value @p v's own
 *         words, into @p s. */
static void
describe(tiku_pdf_t *doc, pdf_obj_t *v, const tiku_cms_sig_t *c,
         tiku_pdf_signature_t *s)
{
    char *name;

    s->status = c->status;
    snprintf(s->why, sizeof s->why, "%s", c->status == TIKU_SIG_GOOD ? ""
                                                                    : c->why);
    if ((s->signer = dup(c->cert.subject)) == NULL &&
        (name = pdf_text_string(pdf_get(doc, v, "Name"))) != NULL) {
        s->signer = name;
    }
    s->org = dup(c->cert.org);
    s->issuer = dup(c->cert.issuer);
    s->self_signed = c->cert.self_signed;
    s->reason = pdf_text_string(pdf_get(doc, v, "Reason"));
    s->location = pdf_text_string(pdf_get(doc, v, "Location"));
    s->contact = pdf_text_string(pdf_get(doc, v, "ContactInfo"));
    s->signed_at = c->signed_at ? c->signed_at : pdf_date(pdf_get(doc, v, "M"));
    s->stamp = c->stamp;
    s->stamped_at = c->stamp ? c->stamped_at : 0;
    s->stamp_by = c->stamp ? dup(c->stamp_by) : NULL;
}

/** @brief Signature @p f checked, into @p s. */
static void
check(tiku_pdf_t *doc, const found_t *f, tiku_pdf_signature_t *s)
{
    tiku_piece_t p[SIG_RANGES];
    tiku_cms_sig_t *c = calloc(1, sizeof *c);
    pdf_obj_t *sub = pdf_get(doc, f->v, "SubFilter");
    unsigned char *der = NULL;
    size_t dn = 0;
    long end;
    int n = byte_range(doc, f->v, p, &end);

    s->page = -1;
    s->field = dup(f->name);
    s->kind = pdf_is_name(pdf_get(doc, f->v, "Type"), "DocTimeStamp") ||
              pdf_is_name(sub, "ETSI.RFC3161") ? TIKU_PDF_SIG_TIMESTAMP :
              certifies(doc, f->v) ? TIKU_PDF_SIG_CERTIFY :
              TIKU_PDF_SIG_APPROVAL;
    place(doc, f->field, s);
    if (c == NULL) return;
    c->status = TIKU_SIG_BROKEN;
    snprintf(c->why, sizeof c->why, "its byte range is not one");
    if (n >= 2 && (der = gap_value(doc, p, &dn)) != NULL &&
        !own_value(doc, f->v, der, dn)) {
        snprintf(c->why, sizeof c->why, "its byte range leaves out "
                 "something other than its value");
    } else if (der != NULL) {
        s->whole = nothing_after(doc, end);
        if (pdf_is_name(sub, "adbe.pkcs7.detached") ||
            pdf_is_name(sub, "ETSI.CAdES.detached") ||
            pdf_is_name(sub, "ETSI.RFC3161")) {
            (void)tiku_cms_verify(der, dn, p, n, c);
        } else if (pdf_is_name(sub, "adbe.pkcs7.sha1")) {
            check_sha1(der, dn, p, n, c);
        } else if (pdf_is_name(sub, "adbe.x509.rsa_sha1")) {
            check_raw(doc, f->v, der, dn, p, n, c);
        } else {
            c->status = TIKU_SIG_UNCHECKED;
            snprintf(c->why, sizeof c->why, "it is made in a way not read");
        }
    }
    describe(doc, f->v, c, s);
    free(der);
    free(c);
}

/** @brief qsort's order for signatures: by where their range ends. */
static int
by_end(const void *a, const void *b)
{
    const found_t *x = a, *y = b;

    return x->end < y->end ? -1 : x->end > y->end;
}

int
tiku_pdf_signatures(tiku_pdf_t *doc, tiku_pdf_signature_t **out)
{
    finds_t *finds = calloc(1, sizeof *finds);
    pdf_obj_t *fields;
    tiku_piece_t p[SIG_RANGES];
    int i, n = 0;

    *out = NULL;
    fields = pdf_get(doc, pdf_get(doc, pdf_root(doc), "AcroForm"), "Fields");
    if (finds == NULL || fields == NULL || fields->kind != PDF_ARRAY) {
        free(finds);
        return 0;
    }
    for (i = 0; i < fields->u.a.n; i++) {
        walk(doc, pdf_resolve(doc, fields->u.a.item[i]), "", NULL, finds, 0);
    }
    for (i = 0; i < finds->n; i++) {
        if (byte_range(doc, finds->item[i].v, p, &finds->item[i].end) < 0) {
            finds->item[i].end = doc->len + 1;
        }
    }
    qsort(finds->item, (size_t)finds->n, sizeof finds->item[0], by_end);
    if (finds->n > 0 && (*out = calloc((size_t)finds->n, sizeof **out))) {
        for (n = 0; n < finds->n; n++) {
            check(doc, &finds->item[n], &(*out)[n]);
        }
    }
    for (i = 0; i < finds->n; i++) {
        free(finds->item[i].name);
    }
    free(finds);
    return n;
}

void
tiku_pdf_signatures_free(tiku_pdf_signature_t *sigs, int n)
{
    int i;

    for (i = 0; sigs != NULL && i < n; i++) {
        free(sigs[i].field);
        free(sigs[i].signer);
        free(sigs[i].org);
        free(sigs[i].issuer);
        free(sigs[i].reason);
        free(sigs[i].location);
        free(sigs[i].contact);
        free(sigs[i].stamp_by);
    }
    free(sigs);
}
