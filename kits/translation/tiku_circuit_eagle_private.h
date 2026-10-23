/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_circuit_eagle_private.h - what the EAGLE schematic reader's files
 * share: the state for the sheet being drawn and the instance, part and
 * net inside it.  The reader works on bytes; it opens no files.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_CIRCUIT_EAGLE_PRIVATE_H_
#define TIKU_CIRCUIT_EAGLE_PRIVATE_H_

#include "tiku_circuit_private.h"
#include "tiku_xml.h"

#define EAGLE_INK_FRAME   0xff30343b    /* frames and text, ARGB         */
#define EAGLE_INK_WIRE    0xff27846f    /* wires, circles and junctions  */
#define EAGLE_INK_SYMBOL  0xff9c4935    /* pins, rectangles, polygons    */
#define EAGLE_LINE_WIDTH  .15           /* a hairline's width, mm        */

/** @brief A read-only element of the parsed EAGLE XML. */
typedef const tiku_xml_node_t eagle_node_t;

/** @brief One connect of the drawn instance's gate: a pin and its pads. */
typedef struct {
    const char *pin;                /**< the symbol pin's name           */
    const char *pad;                /**< the package pads it reaches     */
    size_t order;                   /**< its place among those connects  */
} eagle_connect_t;

/** @brief The reader's state: the document's sections, the sheet being
 *         drawn, and the instance and net being drawn on it. */
typedef struct {
    tiku_circuit_t *doc;            /**< the sheet's scene               */
    eagle_node_t *schematic;        /**< <schematic>                     */
    eagle_node_t *libraries;        /**< its <libraries>                 */
    eagle_node_t *parts;            /**< its <parts>                     */
    eagle_node_t *instance;         /**< the <instance> drawn, or NULL   */
    eagle_node_t *part;             /**< its <part>                      */
    eagle_node_t *device;           /**< the part's <device>             */
    const char *name;               /**< object name: part, net or Drawing */
    const char *value;              /**< object value                    */
    const char *net;                /**< the net or bus drawn, or NULL   */
    const char *gate;               /**< the instance's gate, or NULL    */
    eagle_connect_t *connects;      /**< the gate's connects, by pin name;
                                         malloc'd while an instance draws */
    size_t connect_count;           /**< how many                        */
    const char *path;               /**< the source path, for DRAWING_NAME */
    unsigned sheet_number;          /**< this sheet, from 1              */
    unsigned sheet_total;           /**< sheets in the schematic         */
    char display_name[256];         /**< part name with its gate, if many */
    double m[6];                    /**< symbol-local source mm to sheet
                                         source mm: x' = m0 x + m2 y + m4,
                                         y' = m1 x + m3 y + m5          */
    unsigned work;                  /**< steps taken, against the budget */
    const tiku_circuit_import_t *options;   /**< cancellation; may be NULL */
    tiku_circuit_diagnostic_t *diag;        /**< the caller's; may be NULL */
    tiku_circuit_err_t error;       /**< the first failure; 0 while none */
} circuit_eagle_t;

/** @brief Record the first failure in @p e and the caller's diagnostic;
 *         later failures keep the first.  @return 0. */
int eagle_fail(circuit_eagle_t *e, tiku_circuit_err_t code,
               const char *message);
/** @brief Count one step against the interpretation budget and check for
 *         cancellation.  @return 1 to go on, 0 after a failure. */
int eagle_step(circuit_eagle_t *e);
/** @brief Attribute @p key of @p n, or @p fallback when it is absent. */
const char *eagle_attr(eagle_node_t *n, const char *key,
                       const char *fallback);
/**
 * @brief Attribute @p key of @p n as a decimal number within 10,000 of
 *        zero.  An absent attribute gives @p fallback, or fails when
 *        @p required; a malformed number fails as BAD, a large one as
 *        LIMIT.
 * @return the number; after a failure, a value to ignore.
 */
double eagle_number(circuit_eagle_t *e, eagle_node_t *n, const char *key,
                    double fallback, int required);
/** @brief A rotation such as "SMR90" into its angle, mirror and spin;
 *         NULL is no rotation.  @return 1, or 0 with the failure recorded. */
int eagle_rotation(circuit_eagle_t *e, const char *s, double *angle,
                   int *mirror, int *spin);
/** @brief The one child of @p parent of element @p kind whose name is
 *         @p name.  @return it, or NULL with the failure recorded. */
eagle_node_t *eagle_named(circuit_eagle_t *e, eagle_node_t *parent,
                          const char *kind, const char *name);
/** @brief Draw drawing element @p n (wire, circle, text, pin, frame and
 *         the like) with its metadata.  @return 1, or 0 on failure. */
int eagle_graphic(circuit_eagle_t *e, eagle_node_t *n);
/** @brief Draw the symbol of instance @p n, placed and turned, and its
 *         visible attributes.  @return 1, or 0 on failure. */
int eagle_instance(circuit_eagle_t *e, eagle_node_t *n);
/** @brief Draw @p n's sections: plain drawing, instances, nets and buses.
 *  @return 1, or 0 on failure. */
int eagle_sheet(circuit_eagle_t *e, eagle_node_t *n);
/** @brief Draw pin @p n of the current symbol, its name and pad labels
 *         and its details.  @return 1, or 0 on failure. */
int eagle_pin(circuit_eagle_t *e, eagle_node_t *n);
/** @brief Draw @p text as placed, sized and aligned by element @p n.
 *  @return 1, or 0 on failure. */
int eagle_text(circuit_eagle_t *e, eagle_node_t *n, const char *text);
/** @brief Draw frame @p n: its border and numbered columns and rows.
 *  @return 1, or 0 on failure. */
int eagle_frame(circuit_eagle_t *e, eagle_node_t *n);
/** @brief Symbol-local source point (@p x, @p y) in scene mm. */
circuit_point_t eagle_point(circuit_eagle_t *e, double x, double y);
/** @brief Give each object from @p start on the current part's
 *         component, value, gate, library and net as its details.
 *  @return 1, or 0 on failure. */
int eagle_metadata(circuit_eagle_t *e, size_t start);
/**
 * @brief Check EAGLE XML bytes (UTF-8, entities, the inert doctype) and
 *        parse them within the reader's limits into @p out.
 * @return 0, or the error, described in @p diag.
 */
tiku_circuit_err_t circuit_eagle_xml(const void *bytes, size_t length,
                                     const tiku_circuit_import_t *opt,
                                     tiku_xml_t **out,
                                     tiku_circuit_diagnostic_t *diag);
tiku_circuit_err_t circuit_project_sheets(tiku_circuit_t *const *scenes,
                                          const char *const *names,
                                          const char *path, size_t count,
                                          tiku_circuit_project_t **out);

#endif /* TIKU_CIRCUIT_EAGLE_PRIVATE_H_ */
