/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_circuit_gerber_private.h - what the Gerber reader's files share:
 * the decoder's state for one artwork layer, its apertures, macros,
 * attribute scopes and open repeat blocks.  The current point is kept in
 * source axes (Y up); scene points, a region's contour among them, have
 * Y negated.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_CIRCUIT_GERBER_PRIVATE_H_
#define TIKU_CIRCUIT_GERBER_PRIVATE_H_

#include "tiku_circuit_private.h"

#define GERBER_APERTURES      256     /* D codes 10 to 255                */
#define GERBER_APERTURE_FIRST 10      /* the lowest D code an AD defines  */
#define GERBER_APERTURE_MAX   1000    /* an aperture's width or height, mm */
#define GERBER_POLYGON_MIN    3       /* regular polygon vertices         */
#define GERBER_POLYGON_MAX    12
#define GERBER_ATTRIBUTES     8       /* attributes per scope             */
#define GERBER_ATTR_NAME      64      /* an attribute name and its NUL    */
#define GERBER_ATTR_VALUE     192     /* an attribute value and its NUL   */
#define GERBER_REGION_POINTS  1025    /* 1,024 edges and the closing point */
#define GERBER_MACROS         64      /* AM definitions per file          */
#define GERBER_MACRO_BYTES    65536   /* AM definition text per file      */
#define GERBER_MACRO_NAME     128     /* a macro name and its NUL         */
#define GERBER_REPEAT_DEPTH   8       /* SR blocks open at once           */
/* One scope as text: "name: value\n" for each attribute, then a NUL. */
#define GERBER_ATTRIBUTE_TEXT \
    (GERBER_ATTRIBUTES                                                  \
     * ((GERBER_ATTR_NAME - 1) + 2 + (GERBER_ATTR_VALUE - 1) + 1) + 1)
#define GERBER_INK            0xffa06626  /* artwork display colour, ARGB */
#define GERBER_CHORD_ERROR    .00001  /* curves as chords this close, mm:
                                         0.041 px at the greatest zoom   */

/** @brief One AM definition: its name and the statements after it. */
typedef struct {
    char name[GERBER_MACRO_NAME];   /**< as written after AM             */
    char *body;                     /**< the statements, malloc'd        */
} gerber_macro_t;

/** @brief One open SR block. */
typedef struct {
    size_t begin;                   /**< first scene object in the block */
    unsigned x, y;                  /**< columns and rows                */
    double dx, dy;                  /**< their spacing, mm               */
} gerber_repeat_t;

/** @brief One attribute in a scope; an empty name is a free slot. */
typedef struct {
    char name[GERBER_ATTR_NAME];    /**< up to 63 bytes                  */
    char value[GERBER_ATTR_VALUE];  /**< up to 191 bytes                 */
} gerber_attribute_t;

/** @brief One aperture, by its D code. */
typedef struct {
    char kind;                      /**< C, R, O, P or M (macro); zero for
                                         an undefined D code             */
    double w;                       /**< width or diameter, mm; a macro's
                                         bounds width                    */
    double h;                       /**< height, mm; its bounds height   */
    double hole;                    /**< centred hole diameter, mm       */
    double rotation;                /**< polygon rotation, degrees       */
    int vertices;                   /**< regular polygon vertex count    */
    char attributes[GERBER_ATTRIBUTE_TEXT];  /**< TA scope at definition */
    tiku_circuit_t *macro;          /**< a macro's own scene, or NULL    */
    char macro_name[GERBER_MACRO_NAME];     /**< the macro it instantiates */
} gerber_aperture_t;

/** @brief The decoder's state for one artwork layer. */
typedef struct {
    tiku_circuit_t *doc;            /**< the scene being built           */
    const tiku_circuit_import_t *options;   /**< cancellation; may be NULL */
    tiku_circuit_diagnostic_t *diag;        /**< the caller's; may be NULL */
    tiku_circuit_err_t error;       /**< the first failure; 0 while none */
    unsigned line;                  /**< source line of the command      */
    int integers;                   /**< FS integer digits; 0 before FS  */
    int decimals;                   /**< FS fractional digits            */
    int aperture;                   /**< the selected D code             */
    int mode;                       /**< G01, G02 or G03 as 1 to 3; 0 unset */
    int operation;                  /**< the last D01, D02 or D03 as 1-3 */
    int have_point;                 /**< a current point exists          */
    int quadrants;                  /**< G75 has been seen               */
    int clear;                      /**< LPC is in effect                */
    int ended;                      /**< M02 has been seen               */
    int region;                     /**< inside G36 ... G37              */
    double unit;                    /**< mm per file unit; 0 before MO   */
    double x, y;                    /**< current point, mm, source axes  */
    gerber_aperture_t apertures[GERBER_APERTURES];  /**< by D code       */
    gerber_attribute_t file[GERBER_ATTRIBUTES];     /**< TF scope        */
    gerber_attribute_t tool[GERBER_ATTRIBUTES];     /**< TA scope        */
    gerber_attribute_t object[GERBER_ATTRIBUTES];   /**< TO scope        */
    circuit_point_t contour[GERBER_REGION_POINTS];  /**< region in scene axes */
    size_t count;                   /**< points in contour               */
    gerber_macro_t macros[GERBER_MACROS];           /**< AM definitions  */
    size_t macro_count;             /**< definitions held                */
    size_t macro_bytes;             /**< their text, NULs included       */
    size_t aperture_bytes;          /**< scene bytes of macro apertures  */
    gerber_repeat_t repeats[GERBER_REPEAT_DEPTH];   /**< open SR blocks  */
    size_t repeat_depth;            /**< how many are open               */
} circuit_gerber_t;

/** @brief Record an AM definition: @p name and its statements @p body.
 *  @return 1, or 0 with the failure recorded. */
int gerber_macro_define(circuit_gerber_t *g, const char *name,
                        const char *body);
/** @brief Instantiate the macro an AD names into @p a; @p text is the AD
 *         after its D code: the macro name and its X-separated values.
 *         The result is held to the standard apertures' size limit.
 *  @return 1, or 0 with the failure recorded. */
int gerber_macro_aperture(circuit_gerber_t *g, gerber_aperture_t *a,
                          const char *text);
/** @brief An SR command after its letters: open a block, or close the
 *         innermost one and expand it.  @return 1, or 0 on failure. */
int gerber_repeat(circuit_gerber_t *g, const char *text);
/** @brief Record the first failure in @p g and the caller's diagnostic;
 *         later failures keep the first.  @return 0. */
int gerber_fail(circuit_gerber_t *g, tiku_circuit_err_t error,
                const char *message);
/**
 * @brief A number at @p *at, advancing it.  A @p coordinate is the FS
 *        fixed-point digits, converted to mm; anything else is a decimal
 *        in file units.  Either is within 10,000 and read the same in
 *        every locale.
 * @return 1, or 0 with the failure recorded.
 */
int gerber_number(circuit_gerber_t *g, const char **at, double *out,
                  int coordinate);
/** @brief A TF, TA, TO or TD command, or a G04 #@! comment carrying one.
 *  @return 1, or 0 with the failure recorded. */
int gerber_attribute(circuit_gerber_t *g, const char *text);
/** @brief One scope's attributes as "name: value" lines into @p out.
 *  @return 1, or 0 when they do not fit. */
int gerber_attributes(circuit_gerber_t *g, const gerber_attribute_t *attrs,
                      char out[GERBER_ATTRIBUTE_TEXT]);
/** @brief An AD command after its letters: define the aperture, of at
 *         most GERBER_APERTURE_MAX across; a zero size is allowed.
 *  @return 1, or 0 with the failure recorded. */
int gerber_aperture(circuit_gerber_t *g, const char *text);
/** @brief A D01 draw or D03 flash with the selected aperture, ending or
 *         placed at (@p x, @p y); @p i, @p j are an arc's centre offset.
 *         An aperture that covers nothing makes no object.
 *  @return 1, or 0 with the failure recorded. */
int gerber_draw(circuit_gerber_t *g, int operation, double x, double y,
                double i, double j);
/** @brief An operation inside a region (D02 starts a contour, a linear
 *         D01 extends it, D03 is refused), or with @p operation 0 the
 *         close of the contour, as at G37.
 *  @return 1, or 0 with the failure recorded. */
int gerber_region(circuit_gerber_t *g, int operation, double x, double y);
/** @brief A circular D01 inside a region, tessellated into its contour.
 *  @return 1, or 0 with the failure recorded. */
int gerber_region_arc(circuit_gerber_t *g, double x, double y, double i,
                      double j);
/**
 * @brief Check a G02/G03 arc from the current point to (@p x, @p y) and
 *        give it in scene axes: centre x and y, start and end radius,
 *        start angle and signed sweep in @p arc.
 * @return 1, or 0 with the failure recorded.
 */
int gerber_arc(circuit_gerber_t *g, double x, double y, double i, double j,
               double arc[6]);
/** @brief Give the scene's last object the current polarity, and as its
 *         details the polarity, @p description, units, source line and
 *         the attribute scopes.  @return 1, or 0 with the failure recorded. */
int gerber_details(circuit_gerber_t *g, const char *description,
                   const char *attrs);

#endif /* TIKU_CIRCUIT_GERBER_PRIVATE_H_ */
