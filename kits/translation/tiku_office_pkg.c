/*
 * KintsugiOS v0.01 kits -- the foundation KintsugiDesktop and KintsugiTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 *
 * tiku_office_pkg.c - a document's package: a ZIP of parts that name
 * each other through relationships (ECMA-376 part 2, OPC).  A part is
 * read whole under a ceiling, and all parts together under another, so a
 * small file cannot inflate into all of memory.  A relationship's target
 * is resolved against its part's folder and never above the package's
 * root; one that leaves the package is only ever an address, not read.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_office_private.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REL_PATH 1024               /* a part's name, at most           */

/** @brief One part's relationships, read when first asked for. */
struct of_rels {
    char     *part;                 /* whose ("" for the package's own)  */
    of_rel_t *rel;                  /* in the order written              */
    int       n;                    /* how many                          */
    int       short_memory;         /* 1: one was left out for want of
                                       memory                            */
};

/** @brief Whether @p path is an OLE2 compound file: Word's and
 *         PowerPoint's before 2007, and any of theirs locked with a
 *         password, whose package is sealed inside one. */
static int
ole_file(const char *path)
{
    static const unsigned char magic[8] = {0xd0, 0xcf, 0x11, 0xe0, 0xa1,
                                           0xb1, 0x1a, 0xe1};
    unsigned char head[8];
    FILE *f = fopen(path, "rb");
    int ole = f != NULL && fread(head, 1, 8, f) == 8 &&
              memcmp(head, magic, 8) == 0;

    if (f != NULL) {
        fclose(f);
    }
    return ole;
}

of_pkg_t *
of_pkg_open(const char *path, char *why, size_t max)
{
    of_pkg_t *p = calloc(1, sizeof *p);
    tiku_archive_err_t err;

    if (p == NULL) {
        snprintf(why, max, "%s", OF_NO_MEMORY);
        return NULL;
    }
    err = tiku_archive_open(path, NULL, NULL, &p->zip);
    if (err != TIKU_ARCHIVE_OK || tiku_archive_format(p->zip) !=
        TIKU_ARCHIVE_ZIP) {
        snprintf(why, max, "%s", err == TIKU_ARCHIVE_MISSING
                 ? "That file could not be opened."
                 : err == TIKU_ARCHIVE_NOMEM ? OF_NO_MEMORY
                 : ole_file(path) ? "That file is from before Office 2007, "
                 "or locked with a password: neither is read here."
                 : err == TIKU_ARCHIVE_DAMAGED
                 ? "That document is damaged: its package does not hold."
                 : "That file is not a document read here.");
        of_pkg_close(p);
        return NULL;
    }
    return p;
}

void
of_pkg_close(of_pkg_t *p)
{
    int i, k;

    if (p == NULL) {
        return;
    }
    for (i = 0; i < p->nrels; i++) {
        for (k = 0; k < p->rels[i].n; k++) {
            free(p->rels[i].rel[k].id);
            free(p->rels[i].rel[k].type);
            free(p->rels[i].rel[k].target);
        }
        free(p->rels[i].rel);
        free(p->rels[i].part);
    }
    free(p->rels);
    tiku_archive_close(p->zip);
    free(p);
}

int
of_pkg_has(of_pkg_t *p, const char *part)
{
    return p != NULL && part != NULL && tiku_archive_find(p->zip, part) >= 0;
}

unsigned char *
of_pkg_read(of_pkg_t *p, const char *part, size_t max, size_t *len)
{
    unsigned char *b = NULL;
    long i = p != NULL && part != NULL ? tiku_archive_find(p->zip, part)
                                       : -1;
    tiku_archive_err_t err;

    *len = 0;
    if (i < 0 || p->read >= OF_READ_MAX) {
        return NULL;
    }
    if (max > OF_READ_MAX - p->read) {
        max = OF_READ_MAX - p->read;
    }
    if ((err = tiku_archive_read(p->zip, i, max, &b, len)) !=
        TIKU_ARCHIVE_OK) {
        p->short_memory |= err == TIKU_ARCHIVE_NOMEM;
        return NULL;
    }
    p->read += *len;
    return b;
}

tiku_xml_t *
of_pkg_xml(of_pkg_t *p, const char *part)
{
    size_t n;
    unsigned char *b = of_pkg_read(p, part, OF_XML_MAX, &n);
    tiku_xml_status_t st = TIKU_XML_OK;
    tiku_xml_t *x = b != NULL ? tiku_xml_parse_limited(b, n, NULL, &st, NULL,
                                                       0) : NULL;

    p->short_memory |= st == TIKU_XML_NOMEM;
    free(b);
    return x;
}

void
of_part_join(const char *base, const char *target, char *out, size_t max)
{
    char buf[REL_PATH];
    const char *slash = strrchr(base, '/');
    char *seg, *save = NULL;
    size_t k = 0;

    if (target[0] == '/') {
        snprintf(buf, sizeof buf, "%s", target + 1);
    } else {
        snprintf(buf, sizeof buf, "%.*s%s%s", slash ? (int)(slash - base)
                 : 0, base, slash ? "/" : "", target);
    }
    out[0] = '\0';
    for (seg = strtok_r(buf, "/", &save); seg != NULL;
         seg = strtok_r(NULL, "/", &save)) {
        if (strcmp(seg, ".") == 0) {
            continue;
        }
        if (strcmp(seg, "..") == 0) {
            char *up = strrchr(out, '/');

            k = up != NULL ? (size_t)(up - out) : 0;
            out[k] = '\0';          /* never above the root */
            continue;
        }
        k += (size_t)snprintf(out + k, k < max ? max - k : 0, "%s%s",
                              k ? "/" : "", seg);
        if (k >= max) {
            out[max - 1] = '\0';
            return;
        }
    }
}

/** @brief The relationships part for @p part, as
 *         "word/_rels/document.xml.rels"; "_rels/.rels" for the package's
 *         own. */
static void
rels_name(const char *part, char *out, size_t max)
{
    const char *slash = strrchr(part, '/');

    if (slash == NULL) {
        snprintf(out, max, "_rels/%s.rels", part);
    } else {
        snprintf(out, max, "%.*s/_rels/%s.rels", (int)(slash - part), part,
                 slash + 1);
    }
}

/** @brief One Relationship element added to @p r, its target resolved. */
static void
add_rel(struct of_rels *r, const char *part, const tiku_xml_node_t *e)
{
    const char *id = tiku_xml_attr(e, "Id");
    const char *type = tiku_xml_attr(e, "Type");
    const char *target = tiku_xml_attr(e, "Target");
    const char *mode = tiku_xml_attr(e, "TargetMode");
    char whole[REL_PATH];
    of_rel_t *rel = &r->rel[r->n];

    if (id == NULL || target == NULL) {
        return;
    }
    rel->external = mode != NULL && strcmp(mode, "External") == 0;
    if (!rel->external) {
        of_part_join(part, target, whole, sizeof whole);
    }
    rel->id = strdup(id);
    rel->type = strdup(type ? type : "");
    rel->target = strdup(rel->external ? target : whole);
    if (rel->id && rel->type && rel->target) {
        r->n++;
    } else {
        r->short_memory = 1;
        free(rel->id);
        free(rel->type);
        free(rel->target);
    }
}

/** @brief @p part's relationships, read now if not yet.  NULL when there
 *         is no memory; an empty set when the part has none. */
static struct of_rels *
rels_of(of_pkg_t *p, const char *part)
{
    char name[REL_PATH];
    struct of_rels *r;
    tiku_xml_t *x;
    const tiku_xml_node_t *e;
    int i;

    for (i = 0; i < p->nrels; i++) {
        if (strcmp(p->rels[i].part, part) == 0) {
            return &p->rels[i];
        }
    }
    if (p->nrels == p->relcap) {
        int cap = p->relcap ? p->relcap * 2 : 16;
        struct of_rels *g = realloc(p->rels, (size_t)cap * sizeof *g);

        if (g == NULL) {
            p->short_memory = 1;
            return NULL;
        }
        p->rels = g;
        p->relcap = cap;
    }
    r = &p->rels[p->nrels];
    memset(r, 0, sizeof *r);
    if ((r->part = strdup(part)) == NULL) {
        p->short_memory = 1;
        return NULL;
    }
    p->nrels++;
    rels_name(part, name, sizeof name);
    x = of_pkg_xml(p, name);
    for (e = tiku_xml_child(tiku_xml_root(x), NULL), i = 0; e != NULL;
         e = tiku_xml_next(e, NULL)) {
        i++;
    }
    r->rel = calloc((size_t)i + 1, sizeof *r->rel);
    p->short_memory |= r->rel == NULL;
    for (e = tiku_xml_child(tiku_xml_root(x), NULL); r->rel && e != NULL;
         e = tiku_xml_next(e, NULL)) {
        add_rel(r, part, e);
    }
    p->short_memory |= r->short_memory;
    tiku_xml_free(x);
    return r;
}

const of_rel_t *
of_pkg_rel(of_pkg_t *p, const char *part, const char *id)
{
    struct of_rels *r = p && part && id ? rels_of(p, part) : NULL;
    int i;

    for (i = 0; r != NULL && i < r->n; i++) {
        if (strcmp(r->rel[i].id, id) == 0) {
            return &r->rel[i];
        }
    }
    return NULL;
}

const of_rel_t *
of_pkg_rel_type(of_pkg_t *p, const char *part, const char *type)
{
    struct of_rels *r = p && part && type ? rels_of(p, part) : NULL;
    size_t k = type ? strlen(type) : 0;
    int i;

    for (i = 0; r != NULL && i < r->n; i++) {
        size_t n = strlen(r->rel[i].type);

        if (n > k && r->rel[i].type[n - k - 1] == '/' &&
            strcmp(r->rel[i].type + n - k, type) == 0) {
            return &r->rel[i];
        }
    }
    return NULL;
}
