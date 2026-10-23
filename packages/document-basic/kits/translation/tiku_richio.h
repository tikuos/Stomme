/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richio.h - versioned rich document XML and native ZIP persistence.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_RICHIO_H_
#define TIKU_RICHIO_H_
#include "tiku_richdoc.h"

/** @brief Native document translation results. */
typedef enum {
    TIKU_RICHIO_OK,
    TIKU_RICHIO_INVALID,       /**< invalid caller arguments */
    TIKU_RICHIO_DAMAGED,       /**< malformed package or model */
    TIKU_RICHIO_UNSUPPORTED,   /**< unknown schema, feature or content */
    TIKU_RICHIO_LIMIT,         /**< a configured resource ceiling */
    TIKU_RICHIO_NO_MEMORY,
    TIKU_RICHIO_CANCELLED,
    TIKU_RICHIO_IO            /**< read, write or durability failure */
} tiku_richio_status_t;

/** @brief Native codec limits and cancellation; zero sizes select defaults.
 *  @note XML is limited to 64 MiB by default, at most 512 MiB. Scratch
 *        memory defaults to 256 MiB: half for XML parsing, half for import
 *        staging. Encoded XML is capped at half the scratch budget too.
 *        Input XML and the resulting model have separate budgets.
 *        Document limits govern the new model. Callbacks must not mutate
 *        input data; cancellation is polled during conversion and reading. */
typedef struct {
    size_t xml_bytes;
    size_t memory_bytes;
    int (*cancel)(void *ctx);
    void *ctx;
    tiku_rich_limits_t document;
} tiku_richio_options_t;

/** @brief Encode one immutable state as schema-1 UTF-8 XML.
 *  @return OK with malloc-owned, NUL-terminated *out and byte count *length;
 *          failure sets *out NULL and *length zero. Free with free(). */
tiku_richio_status_t tiku_richio_write_xml(
    const tiku_richsnapshot_t *snapshot, const tiku_richio_options_t *options,
    unsigned char **out, size_t *length);

/** @brief Decode schema-1 XML into a new clean document via checked import.
 *  @note Unknown attributes/elements and required features are refused.
 *        The decoder does not change an existing document or open files.
 *  @return OK with *out owned by the caller; failure sets *out NULL. */
tiku_richio_status_t tiku_richio_read_xml(const void *xml, size_t length,
    const tiku_richio_options_t *options, tiku_richdoc_t **out);

/** @brief Import UTF-8 text as an unformatted, clean document.
 *  @note LF, CRLF and CR separate paragraphs; one leading UTF-8 BOM is
 *        removed. A final separator creates an empty final paragraph.
 *        Document text/paragraph limits and codec scratch limits apply.
 *        Blocking worker operation; invalid UTF-8 or NUL is refused.
 *  @return OK with owned *out, otherwise NULL and the failure category. */
tiku_richio_status_t tiku_richio_read_text(const void *text, size_t length,
    const tiku_richio_options_t *options, tiku_richdoc_t **out);

/** @brief Load a .tkrd ZIP containing exactly one document.xml part.
 *  @note Blocking worker operation. Archive sizes, CRC and local/central
 *        headers are checked. Extra parts are refused as unsupported.
 *  @return OK with a new clean document, or an error with *out NULL. */
tiku_richio_status_t tiku_richio_load(const char *path,
    const tiku_richio_options_t *options, tiku_richdoc_t **out);

/** @brief Atomically replace a regular file with a stored .tkrd ZIP.
 *  @note Blocking worker operation; no document state is marked saved.
 *        Cancellation or conversion failure leaves the destination intact.
 *        An I/O error after rename can mean replacement succeeded but its
 *        directory sync failed. This API does not detect external edits;
 *        the caller must resolve conflicts before requesting replacement.
 *  @return OK after durable replacement; otherwise the failure category. */
tiku_richio_status_t tiku_richio_save(const char *path,
    const tiku_richsnapshot_t *snapshot, const tiku_richio_options_t *options);

/** @brief A stable English description of a translation result. */
const char *tiku_richio_strerror(tiku_richio_status_t status);
#endif
