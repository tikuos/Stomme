/*
 * KintsugiOS v0.01 kits -- the foundation KintsugiDesktop and KintsugiTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 *
 * tiku_xml.h - XML read into a tree.
 *
 * Office documents are XML in a ZIP.  This reads one part into elements
 * and text.  Every namespace a reader here knows gets a fixed prefix --
 * "w:p", whatever prefix the file gave WordprocessingML -- and readers
 * match those names.  No DTD is read and no entity expanded but the five
 * predefined ones and character references, so a file cannot reach
 * outside itself or multiply itself.  Nesting is bounded by
 * TIKU_XML_DEPTH, so readers may recurse.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_XML_H_
#define TIKU_XML_H_

#include <stddef.h>

/** @brief How deep elements may nest; a deeper file is refused. */
#define TIKU_XML_DEPTH 256

typedef struct tiku_xml tiku_xml_t;
typedef struct tiku_xml_node tiku_xml_node_t;

/** @brief How tiku_xml_parse_limited() ended. */
typedef enum {
    TIKU_XML_OK,                    /* read: the tree is returned        */
    TIKU_XML_BAD,                   /* not XML this reads, or no bytes   */
    TIKU_XML_NOMEM,                 /* memory ran short                  */
    TIKU_XML_LIMIT,                 /* a bound in the limits, or one on
                                       a bounded parse's attributes or
                                       name lengths, was hit             */
    TIKU_XML_CANCELLED              /* the cancel callback said stop     */
} tiku_xml_status_t;

/** @brief The bounds a limited parse keeps. */
typedef struct {
    /* The bytes the parse may allocate, counted before each allocation;
     * the parse's copy of the input counts as three times its length. */
    size_t bytes;
    size_t nodes;                   /* elements and runs of text at most;
                                       0 refuses every document          */
    int  (*cancel)(void *);         /* asked with ctx before each piece
                                       of markup and run of text:
                                       nonzero stops the parse; NULL for
                                       none                              */
    void  *ctx;                     /* what cancel is handed             */
} tiku_xml_limits_t;

/**
 * @brief tiku_xml_parse(), kept within @p limits and saying how it ended.
 *
 * A bounded parse is stricter: it refuses a '<' written in an attribute
 * value, an attribute named twice on one tag, more than 512 attributes on
 * a tag, text other than white space outside the root element, and a name
 * of 512 bytes or more with its prefix, which tiku_xml_parse() would cut.
 *
 * @param bytes   the XML, as tiku_xml_parse() reads it
 * @param len     its length in bytes
 * @param limits  the bounds, read during the call only; NULL keeps none
 *                and reads as tiku_xml_parse() does
 * @param status  set to how the parse ended; may be NULL
 * @param why     as tiku_xml_parse() fills it, a bound hit named there
 * @param max     @p why's size
 * @return the tree, or NULL with @p status and @p why saying why
 */
tiku_xml_t *tiku_xml_parse_limited(const void *bytes, size_t len,
                                   const tiku_xml_limits_t *limits,
                                   tiku_xml_status_t *status, char *why,
                                   size_t max);

/** @brief Parse with the same limits while retaining lexical names and xmlns.
 *  @note Namespace declarations remain attributes; callers resolve their scope.
 *        Existing parse entry points retain canonical-name behavior. */
tiku_xml_t *tiku_xml_parse_lexical_limited(const void *bytes, size_t len,
                                           const tiku_xml_limits_t *limits,
                                           tiku_xml_status_t *status, char *why,
                                           size_t max);

/** @brief An element, or a run of text between tags.  Read-only. */
struct tiku_xml_node {
    const char      *name;          /* "w:p"; NULL for text              */
    const char      *text;          /* text's characters, UTF-8; ""      */
    const char     **attr;          /* name, value, name, value ...      */
    int              nattr;
    tiku_xml_node_t *parent, *child, *next;
};

/**
 * @brief Read @p len bytes of XML: UTF-8, UTF-16 found by its mark or its
 *        opening '<', or Latin-1 when the declaration names it.
 * @return the tree, or NULL with a sentence in @p why (which may be
 *         NULL) saying what was wrong and on which line.
 */
tiku_xml_t *tiku_xml_parse(const void *bytes, size_t len, char *why,
                           size_t max);

/** @brief Let a tree go; its nodes and strings go with it. */
void tiku_xml_free(tiku_xml_t *doc);

/** @brief The document's element. */
tiku_xml_node_t *tiku_xml_root(const tiku_xml_t *doc);

/** @brief Whether @p n is an element called @p name. */
int tiku_xml_is(const tiku_xml_node_t *n, const char *name);

/** @brief @p n's attribute @p name ("w:val", or "val" for one with no
 *         namespace), or NULL. */
const char *tiku_xml_attr(const tiku_xml_node_t *n, const char *name);

/** @brief @p n's first child element called @p name (NULL: any). */
tiku_xml_node_t *tiku_xml_child(const tiku_xml_node_t *n, const char *name);

/** @brief The next element after @p n among its siblings called @p name
 *         (NULL: any). */
tiku_xml_node_t *tiku_xml_next(const tiku_xml_node_t *n, const char *name);

/** @brief The element @p path leads to from @p n: child names joined by
 *         '/', as "w:body/w:sectPr/w:pgSz".  NULL when one is missing. */
tiku_xml_node_t *tiku_xml_path(const tiku_xml_node_t *n, const char *path);

/**
 * @brief The text of @p n and everything under it, joined, into @p out
 *        (@p max bytes with its end, cut short on a character's edge).
 * @return the whole length, as snprintf gives it.
 */
size_t tiku_xml_text(const tiku_xml_node_t *n, char *out, size_t max);

#endif /* TIKU_XML_H_ */
