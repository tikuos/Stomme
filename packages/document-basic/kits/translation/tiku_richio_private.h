/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richio_private.h - bounded XML codec state and schema helpers.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_RICHIO_PRIVATE_H_
#define TIKU_RICHIO_PRIVATE_H_
#include "tiku_richio.h"
#include "tiku_richbookmark.h"
#include "tiku_xml.h"

/** @brief Writer style-ID lookup, sorted by source ID. */
typedef struct {
    uint64_t id;               /**< document-local style identity */
    size_t index;              /**< one-based serialized style index */
} tiku_rio_style_t;

/** @brief One decoded image with bounded owned alternate text. */
typedef struct {
    tiku_rich_image_t image;   /**< public import description */
    char alt[1024];           /**< storage used by image.alt */
    int borrowed;             /**< pixels belong to an earlier image slot */
} tiku_rio_image_t;

/** @brief A counting or emitting XML sink with a sticky status. */
typedef struct {
    unsigned char *data;      /**< NULL counts without writing */
    size_t used;              /**< serialized bytes counted or emitted */
    tiku_richio_options_t options; /**< limits and cancellation callback */
    tiku_richio_status_t status; /**< first refused operation */
    tiku_rio_style_t *styles;  /**< owned ID-to-index array */
    size_t count;             /**< number of style mappings */
    int break_feature;        /**< paragraph page-break attributes enabled */
    int outline_feature;      /**< semantic heading attributes enabled */
    int image_feature;        /**< inline images present */
    size_t *image_refs;        /**< prior image ordinal, zero for owned pixels */
    size_t image_count, image_at; /**< prepared image occurrences and cursor */
    int reference_feature;    /**< repeated pixel data uses prior references */
} tiku_rio_writer_t;

/** @brief Owned import staging, borrowing strings from the XML tree. */
typedef struct {
    tiku_richio_options_t options; /**< limits and cancellation callback */
    tiku_richio_status_t status; /**< current decode result */
    size_t used;              /**< staging bytes, excluding the tree */
    tiku_rich_source_t source; /**< public checked-import description */
    tiku_rich_style_source_t *styles; /**< owned style array */
    tiku_rich_paragraph_source_t *paragraphs; /**< owned paragraph array */
    tiku_rich_run_source_t *runs; /**< owned contiguous run array */
    char *text;               /**< joined text-node bytes */
    int page_feature;        /**< required single-section page extension */
    int break_feature;       /**< required paragraph page-break extension */
    int outline_feature;     /**< required semantic heading extension */
    int image_feature;       /**< required inline-image extension */
    int reference_feature;   /**< required backward image-reference extension */
    tiku_rio_image_t *images; /**< decoded pixels and metadata per image run */
    size_t nimages, image_at; /**< allocated capacity and decoded count */
    int table_feature;        /**< required structural table extension */
    int column_feature;       /**< relative column widths */
    int shading_feature;      /**< per-cell background colours */
    int header_feature;       /**< repeated leading table rows */
    tiku_rich_block_source_t *blocks; /**< owned import hierarchy */
    int bookmark_feature;     /**< required named point-location extension */
    tiku_rich_bookmark_source_t *bookmarks; /**< owned import locations */
} tiku_rio_reader_t;

/** @brief Write named positions using zero-based paragraph indices. */
void tiku_rio_write_bookmarks(tiku_rio_writer_t *w,
                              const tiku_richsnapshot_t *snapshot);
/** @brief Decode a bounded required bookmark container for checked import. */
void tiku_rio_read_bookmarks(tiku_rio_reader_t *r, const tiku_xml_node_t *node);

/** @brief Index repeated pixel payloads within the writer's scratch budget. */
void tiku_rio_prepare_images(tiku_rio_writer_t *w,
                              const tiku_richsnapshot_t *snapshot);

/** @brief Encode the ordered hierarchy using file-local parent indices. */
void tiku_rio_write_blocks(tiku_rio_writer_t *w,
                           const tiku_richsnapshot_t *snapshot);
/** @brief Decode a bounded hierarchy for subsequent checked model import. */
void tiku_rio_read_blocks(tiku_rio_reader_t *r, const tiku_xml_node_t *node);
/** @brief Whether any table uses explicit relative column widths. */
int tiku_rio_has_columns(const tiku_richsnapshot_t *snapshot);
/** @brief Whether any cell has a nonzero background colour. */
int tiku_rio_has_shading(const tiku_richsnapshot_t *snapshot);
/** @brief Whether any table declares repeated leading rows. */
int tiku_rio_has_headers(const tiku_richsnapshot_t *snapshot);

/** @brief Emit one complete inline image, including endian-neutral pixels. */
void tiku_rio_write_image(tiku_rio_writer_t *w,
                          const tiku_rich_image_t *image);
/** @brief Decode one bounded image child and charge its staging allocation. */
const tiku_rich_image_t *tiku_rio_read_image(tiku_rio_reader_t *r,
                                           const tiku_xml_node_t *node);
/** @brief Write a float's binary32 bits as eight lowercase hex digits. */
void tiku_rio_write_float(tiku_rio_writer_t *w, const char *name, float value);
/** @brief Read exactly eight hex digits and refuse non-finite values. */
float tiku_rio_read_float(tiku_rio_reader_t *r, const tiku_xml_node_t *n,
                          const char *name);

/** @brief Copy and normalize codec options; refuse invalid size limits. */
tiku_richio_status_t tiku_rio_options(const tiku_richio_options_t *source,
    tiku_richio_options_t *out);
/** @brief Translate a checked model result into a codec result. */
tiku_richio_status_t tiku_rio_model(tiku_rich_status_t status);
/** @brief Append bytes to a bounded counting or emitting sink. */
void tiku_rio_put(tiku_rio_writer_t *w, const char *text, size_t length);
/** @brief Append a NUL-terminated literal. */
void tiku_rio_literal(tiku_rio_writer_t *w, const char *text);
/** @brief Emit an unsigned decimal attribute. */
void tiku_rio_uint(tiku_rio_writer_t *w, const char *name, size_t value);
/** @brief Emit an escaped attribute or text, preserving tabs and newlines. */
void tiku_rio_escape(tiku_rio_writer_t *w, const char *text, size_t length);
/** @brief Decode schema string escapes, returning bytes excluding NUL.
 *  @note Capacity includes the terminator; NULL output counts bytes. */
size_t tiku_rio_string(tiku_rio_reader_t *r, const char *source,
    char *out, size_t capacity);
/** @brief Validate the namespace-free UTF-8 XML profile before tree parsing. */
tiku_richio_status_t tiku_rio_preflight(const unsigned char *xml,
    size_t length, const tiku_richio_options_t *options);
/** @brief Emit complete canonical character properties. */
void tiku_rio_write_chars(tiku_rio_writer_t *w, const tiku_rich_chars_t *c);
/** @brief Emit complete canonical paragraph properties. */
void tiku_rio_write_para(tiku_rio_writer_t *w, const tiku_rich_para_t *p);
/** @brief Emit paper properties as exact IEEE binary32 attributes. */
void tiku_rio_write_page(tiku_rio_writer_t *w,
                        const tiku_rich_page_setup_t *page);
/** @brief Decode the required, bounded paper-property element. */
void tiku_rio_read_page(tiku_rio_reader_t *r, const tiku_xml_node_t *node);
/** @brief Check exact element name and allowed attributes. */
int tiku_rio_element(tiku_rio_reader_t *r, const tiku_xml_node_t *n,
    const char *name, const char *const *attrs, size_t count);
/** @brief Read a required bounded unsigned decimal attribute. */
size_t tiku_rio_number(tiku_rio_reader_t *r, const tiku_xml_node_t *n,
    const char *name, size_t maximum);
/** @brief Get the next element, rejecting intervening non-whitespace text. */
const tiku_xml_node_t *tiku_rio_next(tiku_rio_reader_t *r,
    const tiku_xml_node_t *node);
/** @brief Decode required canonical character properties. */
void tiku_rio_read_chars(tiku_rio_reader_t *r, const tiku_xml_node_t *node,
    tiku_rich_chars_t *out);
/** @brief Decode required canonical paragraph properties. */
void tiku_rio_read_para(tiku_rio_reader_t *r, const tiku_xml_node_t *node,
    tiku_rich_para_t *out);
/** @brief Decode a schema tree into owned staging and a checked new model. */
tiku_richio_status_t tiku_rio_decode(tiku_rio_reader_t *r,
    const tiku_xml_node_t *root, tiku_richdoc_t **out);
/** @brief Bound rich clipboard XML, scratch and imported model storage. */
tiku_richio_status_t tiku_rio_clip_options(const tiku_richio_options_t *input,
    tiku_richio_options_t *out);
#endif
