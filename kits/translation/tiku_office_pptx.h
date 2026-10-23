/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_office_pptx.h - what the slide renderer's parts share: the deck,
 * the slide being drawn with its layout and master, and how a
 * placeholder finds the one it inherits from.  Slides are walked in
 * tiku_office_pptx.c, text frames set in tiku_office_pptx_text.c, and
 * tables drawn in tiku_office_pptx_table.c.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_OFFICE_PPTX_H_
#define TIKU_OFFICE_PPTX_H_

#include "tiku_office_dml.h"

/** @brief A deck being drawn. */
typedef struct {
    of_pkg_t              *pkg;     /* the file                          */
    dml_ctx_t              dml;     /* drawing, as DrawingML draws       */
    dml_theme_t            theme;   /* the master's                      */
    char                   theme_part[256];  /* which it is              */
    const tiku_xml_node_t *deftext; /* the deck's default text style     */
    float                  w, h;    /* a slide's size, points            */
    int                    number;  /* the slide being drawn, from 1     */
    int                    drawn;   /* slides drawn: hidden ones are not */
    const tiku_xml_node_t *layout;  /* its layout's shape tree           */
    const tiku_xml_node_t *master;  /* and its master's                  */
    const tiku_xml_node_t *styles;  /* the master's p:txStyles           */
    const char            *layout_part, *master_part, *slide_part;
                                    /* the parts the trees come from     */
    of_doc_t              *d;       /* looks and memory for text         */
} px_t;

/** @brief The placeholder in shape tree @p tree that @p ph (a p:ph)
 *         inherits from: a slide's in its layout by index, else type; a
 *         layout's in its master by type alone (@p by_type).  NULL when
 *         there is none. */
const tiku_xml_node_t *px_placeholder(const tiku_xml_node_t *tree,
                                      const tiku_xml_node_t *ph, int by_type);

/** @brief A shape's p:ph, or NULL when it is not a placeholder. */
const tiku_xml_node_t *px_ph(const tiku_xml_node_t *shape);

/** @brief A shape's text set in its box: dml_text_fn for slides. */
int px_text(void *ctx, const tiku_xml_node_t *shape, const float *box,
            const dml_mat_t *m);

/**
 * @brief A text body set in @p box (left top right bottom, the shape's own
 *        space) through @p m: levels from the deck's defaults and its own
 *        list style, insets @p ins and anchor @p anchor; @p color and
 *        @p bold where it says none -- a table style's.  Drawn when
 *        @p draw.  @return its height, or -1 out of memory.
 */
float px_body(px_t *x, const tiku_xml_node_t *body, const float *box,
              const float *ins, int anchor, const dml_mat_t *m,
              uint32_t color, int bold, int draw);

/** @brief Whether a shape has any text to show. */
int px_has_text(const tiku_xml_node_t *shape);

/** @brief A graphic frame drawn: a table here, anything else as
 *         dml_frame() draws it. */
int px_frame(px_t *x, const tiku_xml_node_t *frame, const dml_mat_t *m);

#endif /* TIKU_OFFICE_PPTX_H_ */
