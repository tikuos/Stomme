/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_der.h - reading DER (X.690), and the BER some writers leave in
 * it: an element at a time, by a cursor over what contains it, never
 * copying but to join a string BER cut in pieces.  What certificates,
 * key files and signatures are written in.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_DER_H_
#define TIKU_DER_H_

#include <stddef.h>
#include <stdint.h>

/** @brief The universal tags read here. */
enum { TIKU_DER_BOOLEAN = 1, TIKU_DER_INTEGER = 2, TIKU_DER_BITS = 3,
       TIKU_DER_OCTETS = 4, TIKU_DER_NULL = 5, TIKU_DER_OID = 6,
       TIKU_DER_UTF8 = 12, TIKU_DER_SEQUENCE = 16, TIKU_DER_SET = 17,
       TIKU_DER_PRINTABLE = 19, TIKU_DER_T61 = 20, TIKU_DER_IA5 = 22,
       TIKU_DER_UTC_TIME = 23, TIKU_DER_GEN_TIME = 24,
       TIKU_DER_UNIVERSAL_STRING = 28, TIKU_DER_BMP = 30 };
#define TIKU_DER_CONTEXT 2          /* the class of [0], [1] ...        */

/** @brief One element: what it is, its contents, and all of it. */
typedef struct {
    int                  cls;       /* 0 universal, 1 application,
                                       2 context-specific, 3 private    */
    int                  cons;      /* constructed                      */
    unsigned             tag;
    const unsigned char *body;      /* the contents                     */
    size_t               len;
    const unsigned char *raw;       /* the whole element, header too    */
    size_t               rawlen;
} tiku_der_t;

/** @brief Where a walk over a run of elements is. */
typedef struct {
    const unsigned char *p;
    size_t               n;         /* bytes left                       */
} tiku_der_cursor_t;

/** @brief The element @p p starts with, of the @p n bytes there; an
 *         indefinite length is found by its end-of-contents.
 *         @return 0, or -1 when no whole element is there (@p e is then
 *         all zeros: no tag this reads, and no contents). */
int tiku_der_read(const unsigned char *p, size_t n, tiku_der_t *e);

/** @brief A cursor over @p e's contents. */
void tiku_der_enter(const tiku_der_t *e, tiku_der_cursor_t *c);

/** @brief The next element under @p c.  @return 0, or -1 at the end or at
 *         what does not parse, @p e zeroed and @p c left at its end. */
int tiku_der_next(tiku_der_cursor_t *c, tiku_der_t *e);

/** @brief The next element, which must be universal @p tag, or context
 *         [@p tag - 0x80] for a @p tag of 0x80 and up.  @return 0, or -1;
 *         the element is taken whatever its tag. */
int tiku_der_take(tiku_der_cursor_t *c, unsigned tag, tiku_der_t *e);

/** @brief Whether @p e is universal @p tag, or context [@p tag - 0x80]. */
int tiku_der_is(const tiku_der_t *e, unsigned tag);

/** @brief Whether @p e is the object identifier @p dotted. */
int tiku_der_oid(const tiku_der_t *e, const char *dotted);

/** @brief Which of the @p n dotted identifiers @p e is; a NULL entry
 *         matches nothing.  @return its index, or -1. */
int tiku_der_oid_in(const tiku_der_t *e, const char *const *dotted, int n);

/** @brief A string's bytes, joined when BER cut it in pieces.
 *         @return the length with @p out to free(), or -1. */
long tiku_der_octets(const tiku_der_t *e, unsigned char **out);

/** @brief An INTEGER that fits a long.  @return 0, or -1. */
int tiku_der_int(const tiku_der_t *e, long *v);

/** @brief An INTEGER's magnitude: its bytes, leading zeros skipped.
 *         @return 0, or -1 for a negative or empty one. */
int tiku_der_uint(const tiku_der_t *e, const unsigned char **b, size_t *n);

/** @brief Any of the string types, as UTF-8 into @p out.  @return 0, or
 *         -1 for what is not a string (@p out is "" then). */
int tiku_der_string(const tiku_der_t *e, char *out, size_t cap);

/** @brief A UTCTime or GeneralizedTime, as seconds since 1970, UTC.
 *         @return 0, or -1. */
int tiku_der_time(const tiku_der_t *e, int64_t *t);

/** @brief @p t as "2026-09-28 14:05:09 UTC" into @p out. */
void tiku_der_time_text(int64_t t, char *out, size_t cap);

#endif /* TIKU_DER_H_ */
