/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_circuit_eagle_xml.c - EAGLE XML checked before it is parsed: valid
 * UTF-8, the five XML entities and character references only, and no
 * doctype but EAGLE's own, which declares nothing; then a parse within
 * the reader's limits.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_circuit_eagle_private.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EAGLE_SOURCE_MAX (8u * 1024u * 1024u)   /* bytes of one file     */
#define EAGLE_XML_BYTES  (32u * 1024u * 1024u)  /* the parsed tree's     */
#define EAGLE_XML_NODES  65536      /* nodes in the parsed tree          */
#define SCAN_STEP_MASK   1023       /* a budget step per 1,024 bytes     */
#define ENTITY_MAX       16         /* bytes from '&' to ';'             */

/** @brief Whether code point @p c may appear in XML: tab, line feed,
 *         carriage return, or a scalar value from space up other than
 *         U+FFFE and U+FFFF. */
static int
scalar(unsigned c)
{
    return c == 9 || c == 10 || c == 13
           || (c >= 32 && c <= 0x10ffff && !(c >= 0xd800 && c <= 0xdfff)
               && c != 0xfffe && c != 0xffff);
}

/** @brief Whether the @p n bytes at @p p are well-formed UTF-8 in
 *         shortest form, holding only characters scalar() admits. */
static int
utf8(const unsigned char *p, size_t n)
{
    for (size_t i = 0; i < n;) {
        unsigned c = p[i++], k = 0, min = 0;

        if (c >= 0xc2 && c <= 0xdf) {
            c &= 31;
            k = 1;
            min = 128;
        } else if (c >= 0xe0 && c <= 0xef) {
            c &= 15;
            k = 2;
            min = 2048;
        } else if (c >= 0xf0 && c <= 0xf4) {
            c &= 7;
            k = 3;
            min = 65536;
        } else if (c >= 128) {
            return 0;
        }
        if (k > n - i) {
            return 0;
        }
        while (k--) {
            unsigned b = p[i++];

            if ((b & 0xc0) != 0x80) {
                return 0;
            }
            c = (c << 6) | (b & 63);
        }
        if (c < min || !scalar(c)) {
            return 0;
        }
    }
    return 1;
}

/** @brief Whether the @p n bytes at @p p, between '&' and ';', are one of
 *         the five XML entities or a reference to a character scalar()
 *         admits. */
static int
entity(const unsigned char *p, size_t n)
{
    const char *names[] = {"amp", "lt", "gt", "quot", "apos"};

    for (size_t i = 0; i < sizeof names / sizeof *names; i++) {
        if (n == strlen(names[i]) && !memcmp(p, names[i], n)) {
            return 1;
        }
    }
    if (n < 2 || p[0] != '#') {
        return 0;
    }
    unsigned cp = 0, base = 10;
    size_t at = 1;

    if (p[at] == 'x') {
        at++;
        base = 16;
    }
    if (at == n) {
        return 0;
    }
    for (; at < n; at++) {
        unsigned c = p[at];
        unsigned v = c >= '0' && c <= '9' ? c - '0'
                   : c >= 'a' && c <= 'f' ? c - 'a' + 10
                   : c >= 'A' && c <= 'F' ? c - 'A' + 10 : 99;

        if (v >= base || cp > (0x10ffff - v) / base) {
            return 0;
        }
        cp = cp * base + v;
    }
    return scalar(cp);
}

tiku_circuit_err_t
circuit_eagle_xml(const void *data, size_t n, const tiku_circuit_import_t *opt,
                  tiku_xml_t **out, tiku_circuit_diagnostic_t *diag)
{
    circuit_eagle_t e = {.options = opt, .diag = diag};

    if (!data || !out || !n) {
        eagle_fail(&e, TIKU_CIRCUIT_BAD, "Missing EAGLE XML input");
        return e.error;
    }
    if (n > EAGLE_SOURCE_MAX) {
        eagle_fail(&e, TIKU_CIRCUIT_LIMIT, "EAGLE source exceeds 8 MiB");
        return e.error;
    }
    if (!utf8(data, n)) {
        eagle_fail(&e, TIKU_CIRCUIT_BAD,
                   "EAGLE input must be valid UTF-8 XML");
        return e.error;
    }
    const unsigned char *p = data;
    /* A declaration naming another encoding is refused. */
    size_t declaration = n >= 3 && p[0] == 0xef && p[1] == 0xbb
                         && p[2] == 0xbf ? 3 : 0;

    if (n - declaration >= 5 && !memcmp(p + declaration, "<?xml", 5)) {
        size_t end = declaration;

        while (end < n && p[end] != '>') {
            end++;
        }
        for (size_t i = declaration; i + 8 < end; i++) {
            if (!memcmp(p + i, "encoding", 8)) {
                size_t j = i + 8;

                while (j < end && (p[j] == ' ' || p[j] == '\t'
                                   || p[j] == '\n' || p[j] == '\r')) {
                    j++;
                }
                if (j < end && p[j] == '=') {
                    j++;
                } else {
                    eagle_fail(&e, TIKU_CIRCUIT_BAD,
                               "Invalid XML encoding declaration");
                    break;
                }
                while (j < end && (p[j] == ' ' || p[j] == '\t'
                                   || p[j] == '\n' || p[j] == '\r')) {
                    j++;
                }
                /* A quoted "utf-8" in either case. */
                if (j + 7 > end || (p[j] != '\"' && p[j] != '\'')
                    || p[j + 6] != p[j]
                    || ((p[j + 1] != 'u' && p[j + 1] != 'U')
                        || (p[j + 2] != 't' && p[j + 2] != 'T')
                        || (p[j + 3] != 'f' && p[j + 3] != 'F')
                        || p[j + 4] != '-' || p[j + 5] != '8')) {
                    eagle_fail(&e, TIKU_CIRCUIT_UNSUPPORTED,
                               "Use UTF-8 encoding for EAGLE XML");
                }
                break;
            }
        }
    }
    for (size_t i = 0; i < n && !e.error; i++) {
        if (!(i & SCAN_STEP_MASK) && !eagle_step(&e)) {
            break;
        }
        if (i + 4 <= n && !memcmp(p + i, "<!--", 4)) {
            i += 4;
            while (i + 3 <= n && memcmp(p + i, "-->", 3)) {
                i++;
            }
            if (i + 3 > n) {
                eagle_fail(&e, TIKU_CIRCUIT_BAD, "Unclosed XML comment");
                break;
            }
            i += 2;
        } else if (i + 9 <= n && !memcmp(p + i, "<![CDATA[", 9)) {
            i += 9;
            while (i + 3 <= n && memcmp(p + i, "]]>", 3)) {
                i++;
            }
            if (i + 3 > n) {
                eagle_fail(&e, TIKU_CIRCUIT_BAD, "Unclosed CDATA");
                break;
            }
            i += 2;
        } else if (i + 2 <= n && !memcmp(p + i, "<!", 2)) {
            const char *a = "<!DOCTYPE eagle SYSTEM \"eagle.dtd\">";
            const char *b = "<!DOCTYPE eagle SYSTEM 'eagle.dtd'>";
            size_t len = strlen(a);

            if (i + len > n
                || (memcmp(p + i, a, len) && memcmp(p + i, b, len))) {
                eagle_fail(&e, TIKU_CIRCUIT_UNSUPPORTED,
                           "Only the inert eagle.dtd doctype is accepted; "
                           "entity declarations are refused");
                break;
            }
            i += len - 1;
        } else if (p[i] == '&') {
            size_t end = i + 1;

            while (end < n && end - i < ENTITY_MAX && p[end] != ';') {
                end++;
            }
            if (end == n || p[end] != ';'
                || !entity(p + i + 1, end - i - 1)) {
                eagle_fail(&e, TIKU_CIRCUIT_BAD,
                           "Invalid XML character/entity reference");
                break;
            }
            i = end;
        }
    }
    if (e.error) {
        return e.error;
    }
    tiku_xml_limits_t limits = {EAGLE_XML_BYTES, EAGLE_XML_NODES,
                                opt ? opt->cancel : NULL,
                                opt ? opt->ctx : NULL};
    tiku_xml_status_t status;
    char why[256];
    tiku_xml_t *doc = tiku_xml_parse_limited(data, n, &limits, &status, why,
                                             sizeof why);

    if (!doc) {
        eagle_fail(&e, status == TIKU_XML_NOMEM ? TIKU_CIRCUIT_NOMEM
                       : status == TIKU_XML_LIMIT ? TIKU_CIRCUIT_LIMIT
                       : status == TIKU_XML_CANCELLED ? TIKU_CIRCUIT_CANCELLED
                       : TIKU_CIRCUIT_BAD, why);
        return e.error;
    }
    *out = doc;
    return TIKU_CIRCUIT_OK;
}
