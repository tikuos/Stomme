/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_circuit_kicad_private.h - the KiCad schematic interpreter shared
 * by its source files: the decode state over a parsed tree, the field
 * and value readers, and one reader per kind of object.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_CIRCUIT_KICAD_PRIVATE_H_
#define TIKU_CIRCUIT_KICAD_PRIVATE_H_

#include "tiku_circuit_private.h"
#include "tiku_circuit_sexpr.h"
#include "tiku_circuit_project.h"

#define KC_IDENTITY   ((const double[6]){1, 0, 0, 1, 0, 0})
#define KC_INK        0xff914432u   /* symbols, text and graphics        */
#define KC_WIRE       0xff187959u   /* wires and junctions               */
#define KC_BLUE       0xff36539cu   /* buses and no-connect marks        */
#define KC_STROKE     0.1524        /* KiCad's default stroke, mm        */
#define KC_TEXT_SIZE  1.27          /* KiCad's default text size, mm     */
#define KC_TEXT_MAX   100           /* the largest text admitted, mm     */
#define KC_LENGTH_MAX 1000          /* the longest pin or label offset   */
#define KC_ANGLE_MAX  3600          /* degrees an angle may reach        */
#define KC_UUID_BYTES 37            /* a UUID's 36 characters and a zero */
#define KC_DNP_FILL   0xffeaeae7u   /* a do-not-populate body's fill    */
#define KC_DNP_INK    0xff999994u   /* its strokes and text              */
#define KC_DNP_CROSS  0xffc83e3eu   /* the cross drawn over it           */
#define KC_DNP_WIDTH  0.4572        /* the cross's stroke, mm            */

/** @brief One linked sheet, as its parent's sheet object names it. */
typedef struct {
    char name[256];                 /**< the Sheetname property          */
    char file[512];                 /**< the Sheetfile property          */
    char uuid[KC_UUID_BYTES];       /**< the sheet object's UUID         */
    char page[64];                  /**< this instance's page, or empty  */
    unsigned line, column;          /**< the sheet object's source location */
} circuit_kc_link_t;

/** @brief The sheets a document links, collected for the project loader. */
typedef struct {
    circuit_kc_link_t link[TIKU_CIRCUIT_SHEETS_MAX];  /**< in file order */
    size_t count;                   /**< links collected                 */
} circuit_kc_links_t;

/** @brief A decode under way. */
typedef struct {
    circuit_sx_t sx;                /**< borrowed syntax, unchanged      */
    tiku_circuit_t *doc;            /**< the scene, published on success */
    const tiku_circuit_import_t *options;   /**< font and cancellation   */
    tiku_circuit_diagnostic_t *diag;        /**< the first failure, or NULL */
    tiku_circuit_err_t error;       /**< the first failure               */
    size_t work;                    /**< checked operations so far       */
    size_t libraries;               /**< the lib_symbols list, or SX_NONE */
    size_t searched;                /**< the list a child was last missing
                                         from: where its absence is noted */
    const char *instance;           /**< this sheet's UUID path, or NULL */
    const char *project;            /**< the preferred project bucket    */
    circuit_kc_links_t *links;      /**< collects sheets; NULL refuses them */
    int child;                      /**< a child sheet: instance tables
                                         must name every symbol          */
} circuit_kc_t;

/** @brief Note the first failure at node @p n's source position.  For
 *         SX_NONE, a list kc_child() did not find, the failure is placed
 *         at the list last searched for one.  @return 0 */
int kc_fail(circuit_kc_t *k, size_t n, tiku_circuit_err_t code,
            const char *text);

/** @brief Count one operation against the work budget, checking for
 *         cancellation every 128.  @return 1, or 0 once failed */
int kc_step(circuit_kc_t *k, size_t n);

/** @brief Atom @p index after list @p n's head, or NULL. */
const char *kc_arg(circuit_kc_t *k, size_t n, size_t index);

/** @brief The first list directly in @p n headed @p name, or SX_NONE,
 *         remembering @p n as where a failure on it is reported. */
size_t kc_child(circuit_kc_t *k, size_t n, const char *name);

/**
 * @brief Check list @p n's children against an allowlist.
 *
 * The first @p args atoms are positional values; any later atom must be a
 * flag in @p flags.  Each child list must be headed by a word of
 * @p fields, once unless the word is also in @p repeat.  Each list of
 * words is separated by single spaces.
 *
 * @return 1, or 0 with the failure noted
 */
int kc_fields(circuit_kc_t *k, size_t n, const char *fields,
              const char *repeat, unsigned args, const char *flags);

/**
 * @brief Whether @p n carries @p flag: as an unquoted atom after its
 *        @p args positional values, or as a child list, (flag) or
 *        (flag yes).
 *
 * (flag no) is clear; any other value is noted as BAD.  A positional
 * value or a string that reads like the flag is not the flag.
 *
 * @return 1 or 0
 */
int kc_flag(circuit_kc_t *k, size_t n, unsigned args, const char *flag);

/** @brief Atom @p index of @p n as a number.  @return 1 with @p out set,
 *         or 0 with BAD noted */
int kc_number(circuit_kc_t *k, size_t n, size_t index, double *out);

/** @brief Read @p n's (at x y [angle]): a position within the
 *         coordinate limit, the angle 0 when absent.  @return 1 or 0 */
int kc_at(circuit_kc_t *k, size_t n, double *x, double *y, double *angle);

/** @brief Note @p code at @p n with the code's own sentence, unless it is
 *         OK.  @return 1 for OK, else 0 */
int kc_result(circuit_kc_t *k, size_t n, tiku_circuit_err_t code);

/** @brief Point (@p x, @p y) through the 2x3 matrix @p m. */
circuit_point_t kc_transform(const double m[6], double x, double y);

/** @brief Read colour list @p n, (color r g b a): all zero is KiCad's
 *         default and leaves @p colour as it is; an opaque colour
 *         replaces it.  @return 1, or 0 with any other colour noted */
int kc_colour(circuit_kc_t *k, size_t n, uint32_t *colour);

/**
 * @brief Append graphic @p n -- a polyline, wire, bus, rectangle, circle
 *        or arc -- through @p m, named @p name and @p value.
 *
 * @p colour is drawn unless the stroke gives an opaque colour of its own.
 *
 * @return 1, or 0 with the failure noted
 */
int kc_graphic(circuit_kc_t *k, size_t n, const double m[6],
    const char *name, const char *value, uint32_t colour);

/**
 * @brief Append text object @p n showing @p text, its position through
 *        @p m, drawn as KiCad 7 draws it.
 *
 * Free text whose angle's whole degrees are 90 or 270 reads upward, any
 * other left to right, with its stored alignment.  @p symbol_text marks a
 * symbol library's own text, whose angle is in tenths of a degree: it is
 * centred on its turned box, across or up the sheet.  Hidden or empty
 * text adds nothing.
 *
 * @return 1, or 0 with the failure noted
 */
int kc_text(circuit_kc_t *k, size_t n, const double m[6],
    const char *name, const char *value, const char *text, int symbol_text);

/** @brief Open the supplied substitute font into the scene, once.
 *  @return 1, or 0 with MISSING, LIMIT, NOMEM or BAD noted */
int kc_font(circuit_kc_t *k, size_t n);

/**
 * @brief Append a placed symbol's property @p n showing @p text.
 *
 * Its position is on the sheet; its direction is relative to the symbol,
 * whose matrix is @p m.  The text is kept upright, its alignment
 * reflected where the symbol would turn it over.
 *
 * @return 1, or 0 with the failure noted
 */
int kc_property_text(circuit_kc_t *k, size_t n, const double m[6],
    const char *name, const char *value, const char *text);

/**
 * @brief Append @p text at (@p x, @p y) in sheet millimetres, @p angle
 *        degrees from horizontal and @p height high.
 *
 * @p justify and @p vertical align it: -1 left or top, 0 centre, 1 right
 * or bottom.  @p italic slants it.
 *
 * @return 1, or 0 with the failure noted
 */
int kc_text_at(circuit_kc_t *k, size_t n, const char *name,
    const char *value, const char *text, double x, double y, double angle,
    double height, int justify, int vertical, int bold, int italic);

/** @brief Append placed symbol @p n: its library body through its
 *         placement, visible properties and pins, and its description.
 *  @return 1, or 0 with the failure noted */
int kc_symbol(circuit_kc_t *k, size_t n);

/** @brief Append junction, no-connect or bus-entry mark @p n.
 *  @return 1, or 0 with the failure noted */
int kc_mark(circuit_kc_t *k, size_t n);

/** @brief Append sheet @p n's outline, properties and pins, and collect
 *         its link; refused without a link collector.
 *  @return 1, or 0 with the failure noted */
int kc_sheet(circuit_kc_t *k, size_t n);

/** @brief Append hierarchical label @p n, or a sheet's pin when
 *         @p sheet_pin, which points the other way.
 *  @return 1, or 0 with the failure noted */
int kc_hlabel(circuit_kc_t *k, size_t n, int sheet_pin);

/** @brief Append global label @p n: its tag round substitute-font text,
 *         its visible properties and its description.
 *  @return 1, or 0 with the failure noted */
int kc_glabel(circuit_kc_t *k, size_t n);

/**
 * @brief Object @p n's (instances) entry for this sheet's UUID path,
 *        each entry checked against @p fields.
 *
 * The entry under the preferred project wins, and two there are BAD;
 * otherwise exactly one entry must match, or MISSING is noted.
 *
 * @return the entry; SX_NONE, noting nothing, without an instance path
 *         or an (instances) list; SX_NONE with the failure noted
 */
size_t kc_instance(circuit_kc_t *k, size_t n, const char *fields);

/** @brief Whether @p s is a UUID: 36 hex digits and dashes, 8-4-4-4-12. */
int kc_uuid(const char *s);

/**
 * @brief Decode a parsed schematic into a scene.
 *
 * @p instance is the sheet's UUID path and @p project the preferred
 * instance bucket, both NULL for a single file.  A @p child sheet's
 * symbols must all appear in its instance tables.  @p links, when not
 * NULL, collects linked sheets.
 *
 * @note The tree is borrowed and stays the caller's to free.
 * @return as tiku_circuit_kicad()
 */
tiku_circuit_err_t circuit_kc_decode(const circuit_sx_t *tree,
    const tiku_circuit_import_t *options, const char *instance,
    const char *project, int child, circuit_kc_links_t *links,
    tiku_circuit_t **out, tiku_circuit_diagnostic_t *diagnostic);

#endif
