/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_doc_paint.h - a page laid out by tiku_doc, drawn.
 *
 * Drawing uses the document's block, line and piece geometry. Optional
 * callbacks supply image pixels and custom text painting. The core
 * painter does not measure text or decode resources.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_DOC_PAINT_H_
#define TIKU_DOC_PAINT_H_

#include "tiku_doc.h"
#include "tiku_gfx.h"

/** @brief Put a picture down in @p where.  The caller owns the pixels. */
typedef void (*tiku_doc_draw_image_fn)(void *ctx, const char *src,
                                       tiku_rect_t where);
/** @brief Draw a piece's text, decorations and focus using its laid-out baseline.
 *  @note The painter supplies the current piece clip and resolved default ink. */
typedef void (*tiku_doc_draw_text_fn)(void *ctx, tiku_surface_t *surface,
    const tiku_doc_t *doc, int piece, tiku_rect_t where, int baseline, tiku_rgb_t ink);

/** @brief What one drawing of a page is told. */
typedef struct {
    int scroll;                     /* the first row of the page shown  */
    int focus_link;                 /* a link to ring, or TIKU_DOC_NONE */
    /* What to say where the page was cut; the kit owns no words, so
     * NULL draws the notice as a bare rule. */
    const char *cut;
    tiku_doc_draw_image_fn image;   /* NULL frames a picture's place    */
    void *image_ctx;                /* handed to image                  */
    tiku_doc_draw_text_fn text;     /**< optional replacement for text/decorations */
    void *text_ctx;
} tiku_doc_paint_t;

/**
 * @brief Draw the part of @p d that falls inside @p r.
 *
 * Visible lines and intersecting container/cell backgrounds are painted.
 * @p how may be NULL, which draws the top with nothing focused.
 */
void tiku_doc_draw(tiku_surface_t *s, const tiku_doc_t *d, tiku_rect_t r,
                   const tiku_doc_paint_t *how);

/** @brief Optional decoration of a piece, before its text is painted. */
typedef void (*tiku_doc_mark_fn)(void *ctx, tiku_surface_t *s,
                               int piece, tiku_rect_t where);
/** @brief tiku_doc_draw(), calling @p mark with @p ctx for each piece
 *         drawn, before its text. */
void tiku_doc_draw_marked(tiku_surface_t *s, const tiku_doc_t *d,
                         tiku_rect_t r, const tiku_doc_paint_t *how,
                         tiku_doc_mark_fn mark, void *ctx);

/** @brief The shade a run of a page means, under the live theme. */
tiku_rgb_t tiku_doc_ink(int doc_ink);

#endif /* TIKU_DOC_PAINT_H_ */
