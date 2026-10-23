/*
 * Desktop shell and file manager.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_background.h - the picture behind a folder or the desktop.
 *
 * A background is a node's own property: which picture, laid out which way.
 * It is kept in the state store beside the folder's other state, so a
 * namespace node that has no attributes can still carry one.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_shell_product.h"
#ifndef TIKU_TRK_BACKGROUND_H_
#define TIKU_TRK_BACKGROUND_H_

#include "tiku_model.h"
#include "tiku_state.h"
#include <stddef.h>

/**
 * @brief The node a folder with no background of its own inherits from.
 *
 * A folder with no background record shows this node's, as the source
 * looks a picture up on the folder and then on a template node.  It names
 * no directory: the store is keyed by name and needs no file for a record.
 */
#define TIKU_TRK_BG_TEMPLATE_NODE "\x01template"

/** @brief The state record a background is kept under. */
#define TIKU_TRK_ATTR_BACKGROUND "_trk/background"

/** @brief How a picture is laid over the view it backs. */
typedef enum {
    TIKU_TRK_BG_AT_OFFSET = 0,   /* once, at a point of its own          */
    TIKU_TRK_BG_CENTERED,
    TIKU_TRK_BG_SCALED,          /* stretched to the whole view          */
    TIKU_TRK_BG_TILED
} tiku_trk_bg_mode_t;

/** @brief One background: the picture, how it is laid, and where. */
typedef struct {
    char               path[TIKU_PATH_MAX];
    tiku_trk_bg_mode_t mode;
    int                x, y;     /* AT_OFFSET: where its corner sits     */
} tiku_trk_bg_t;

/**
 * @brief Read a record, "<mode> <x> <y> <path>".
 *
 * A record with an empty path keeps the chosen layout mode while showing no
 * picture.
 *
 * @return 1 for a well-formed record, 0 otherwise; @p out is not touched on
 *         0.
 */
int tiku_trk_bg_parse(const char *rec, tiku_trk_bg_t *out);

/** @brief Write @p bg as a record.  @return its length, 0 on bad input. */
int tiku_trk_bg_format(const tiku_trk_bg_t *bg, char *out, size_t max);

/**
 * @brief Whether @p node carries a background record at all, even one with
 *        no picture (the guard tiku_trk_bg_install_default() uses, AW-016).
 */
int tiku_trk_bg_have(tiku_store_t *store, const char *node);

/**
 * @brief Read @p node's background record.
 * @return 1 for a well-formed record, including one that keeps only a
 *         layout mode and names no picture; 0 otherwise.
 */
int tiku_trk_bg_load(tiku_store_t *store, const char *node,
                     tiku_trk_bg_t *out);

/**
 * @brief Read @p node's background when the record names a picture.
 * @return 1 and the record in @p out; 0, with @p out untouched, for no
 *         record or one with an empty path.
 */
int tiku_trk_bg_load_picture(tiku_store_t *store, const char *node,
                             tiku_trk_bg_t *out);

/** @brief Give @p node a background.  @return 1 when it was written. */
int tiku_trk_bg_save(tiku_store_t *store, const char *node,
                     const tiku_trk_bg_t *bg);

/**
 * @brief Where blit @p i of this background goes, in view coordinates.
 *
 * One rectangle for every mode but TILED, which walks the grid; the caller
 * stops when this answers 0.  The rectangle may fall outside the view, and
 * the caller clips: only SCALED changes the picture's size.
 *
 * @return 1 when @p dx, @p dy, @p dw and @p dh were filled, 0 when there is
 *         nothing more to draw.
 */
int tiku_trk_bg_place(const tiku_trk_bg_t *bg, int view_w, int view_h,
                      int img_w, int img_h, int i, int *dx, int *dy,
                      int *dw, int *dh);

/** @brief Where a default desktop picture is looked for, under $HOME
 *         (AW-016). */
#define TIKU_TRK_BG_DEFAULT_FILE TIKU_SHELL_BACKGROUND_PATH

/**
 * @brief Install a default desktop picture, once (AW-016).
 *
 * Only when @p node carries no background record yet, so a desktop the user
 * has set, or cleared, is not overwritten on the next boot.  The picture
 * goes at a golden-ratio x and nine tenths of the height, as the source's
 * logo does.  Nothing is installed when the default file is not readable.
 *
 * @return 1 when one was installed.
 */
int tiku_trk_bg_install_default(tiku_store_t *store, const char *node,
                                int screen_w, int screen_h);

#endif /* TIKU_TRK_BACKGROUND_H_ */
