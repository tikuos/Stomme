/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_circuit_drill_private.h - what the drill reader's files share: the
 * decoder's state for one XNC/Excellon file, its tools and attribute
 * scopes.  Coordinates here are in mm in source axes, Y up.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_CIRCUIT_DRILL_PRIVATE_H_
#define TIKU_CIRCUIT_DRILL_PRIVATE_H_

#include "tiku_circuit_private.h"

#define DRILL_TOOLS           256     /* tool numbers 1 to 255            */
#define DRILL_ATTRS           8       /* attributes per scope             */
#define DRILL_ATTR_NAME       64      /* an attribute name and its NUL    */
#define DRILL_ATTR_VALUE      192     /* an attribute value and its NUL   */
/* One scope as text: "name: value\n" for each attribute, then a NUL. */
#define DRILL_ATTRIBUTE_TEXT \
    (DRILL_ATTRS * ((DRILL_ATTR_NAME - 1) + 2 + (DRILL_ATTR_VALUE - 1) + 1) + 1)
#define DRILL_NUMBER_MAX      10000   /* any number's magnitude, mm       */
#define DRILL_MM_PER_INCH     25.4

/* circuit_drill_t.state: where the reader is in the file. */
enum {
    DRILL_BEFORE_HEADER,            /* before M48                        */
    DRILL_IN_HEADER,                /* from M48 to % or M95              */
    DRILL_IN_BODY,                  /* from there to M30                 */
    DRILL_AFTER_END                 /* after M30                         */
};

/* What a coordinate command does; drill_emit() draws all but G00. */
enum {
    DRILL_RAPID,                    /* G00: move to a route's start      */
    DRILL_STRAIGHT,                 /* G01: a straight route             */
    DRILL_CLOCKWISE,                /* G02: a clockwise route            */
    DRILL_COUNTERCLOCKWISE,         /* G03: a counterclockwise route     */
    DRILL_HOLE,                     /* a drilled hit                     */
    DRILL_SLOT                      /* a G85 slot                        */
};

/** @brief One attribute in a scope; an empty name is a free slot. */
typedef struct {
    char name[DRILL_ATTR_NAME];     /**< up to 63 bytes                  */
    char value[DRILL_ATTR_VALUE];   /**< up to 191 bytes                 */
} drill_attribute_t;

/** @brief One declared tool. */
typedef struct {
    double diameter;                /**< mm; zero for an undeclared tool */
    char attributes[DRILL_ATTRIBUTE_TEXT];  /**< TA scope at declaration */
} drill_tool_t;

/** @brief The decoder's state for one drill file. */
typedef struct {
    tiku_circuit_t *doc;            /**< the scene being built           */
    const tiku_circuit_import_t *options;   /**< cancellation; may be NULL */
    tiku_circuit_diagnostic_t *diag;        /**< the caller's; may be NULL */
    tiku_circuit_err_t error;       /**< the first failure; 0 while none */
    unsigned line;                  /**< source line being read          */
    int state;                      /**< the place in the file, as above */
    int tool;                       /**< the selected tool; 0 for none   */
    int route;                      /**< G00 has started route mode      */
    int down;                       /**< M15 has lowered the tool        */
    int have_point;                 /**< a current point exists          */
    int decimal;                    /**< a KiCad decimal FORMAT comment  */
    int route_drawn;                /**< the lowered tool has cut        */
    double unit;                    /**< mm per file unit; 0 before units */
    double x, y;                    /**< current point, mm               */
    double format_unit;             /**< the FORMAT comment's unit, or 0 */
    drill_tool_t tools[DRILL_TOOLS];            /**< by tool number      */
    drill_attribute_t file[DRILL_ATTRS];        /**< TF scope            */
    drill_attribute_t aperture[DRILL_ATTRS];    /**< TA scope            */
    drill_attribute_t object[DRILL_ATTRS];      /**< TO scope            */
} circuit_drill_t;

/** @brief Record the first failure in @p d and the caller's diagnostic;
 *         later failures keep the first.  @return 0. */
int drill_fail(circuit_drill_t *d, tiku_circuit_err_t error,
               const char *message);
/**
 * @brief A decimal number at @p *at, advancing it: at most 16 digits, 8
 *        of them fractional, no exponent, independent of the locale,
 *        converted to mm, at most DRILL_NUMBER_MAX in magnitude.  With
 *        @p coordinate set, a nonzero value needs a decimal point unless
 *        KiCad's decimal FORMAT is declared.
 * @return 1, or 0 with the failure recorded.
 */
int drill_number(circuit_drill_t *d, const char **at, double *out,
                 int coordinate);
/** @brief The X and Y fields at @p *at, advancing it; an omitted axis
 *         keeps the current point's.  @return 1, or 0 on failure. */
int drill_point(circuit_drill_t *d, const char **at, double *x, double *y);
/** @brief A comment line after its ';': a FORMAT declaration, an X2
 *         attribute (#@!), or text to pass.  @return 1, or 0 on failure. */
int drill_comment(circuit_drill_t *d, const char *text);
/** @brief One scope's attributes as "name: value" lines into @p out.
 *  @return 1, or 0 when they do not fit. */
int drill_attributes(circuit_drill_t *d, const drill_attribute_t *attrs,
                     char out[DRILL_ATTRIBUTE_TEXT]);
/**
 * @brief Draw a hole, slot or route of @p kind with the selected tool, to
 *        or at (@p x, @p y) from the current point; @p radius is a
 *        circular route's.  Its details go with it.
 * @return 1, or 0 with the failure recorded.
 */
int drill_emit(circuit_drill_t *d, int kind, double x, double y,
               double radius);

#endif /* TIKU_CIRCUIT_DRILL_PRIVATE_H_ */
