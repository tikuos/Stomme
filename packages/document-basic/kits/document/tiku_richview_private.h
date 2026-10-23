/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richview_private.h - per-view layout scheduling and navigation state.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_RICHVIEW_PRIVATE_H_
#define TIKU_RICHVIEW_PRIVATE_H_

#include "tiku_richview.h"
#include "tiku_richdoc_private.h"
#include "tiku_text_input.h"

/** @brief View-owned geometry, editing session and viewport settings. */
struct tiku_richview {
    tiku_richdoc_t *doc;      /**< retained by session and observer */
    tiku_richsession_t *session;
    tiku_richwatch_t *watch;   /**< document change subscription */
    tiku_richlayout_options_t options; /**< copied provider and layout limits */
    tiku_richlayout_t *layout; /**< completed or pending current attempt */
    tiku_rich_status_t status;
    double width, height, zoom;
    double scroll;           /**< document-point vertical offset */
    double preferred;        /**< vertical-navigation x, NaN when unset */
    uint64_t preferred_revision; /**< caret revision at vertical navigation */
    int dirty;               /**< cancel and replace the previous attempt */
    int cached_attempt;      /**< permit one cold retry after memory refusal */
    tiku_text_input_t input;  /**< bounded committed-text assembly */
    uint64_t input_generation; /**< source generation at transfer start */
    uint64_t input_anchor;   /**< selection anchor revision at start */
    uint64_t input_caret;    /**< selection caret revision at start */
    int dragging;           /**< primary pointer selection in progress */
    int picture_drag;       /**< corner resize preview, not published edits */
    tiku_rich_rect_t picture_box; /**< original selected image bounds */
    double picture_scale;   /**< current aspect-preserving resize factor */
    uint64_t picture_generation; /**< generation before pointer capture */
    uint64_t picture_anchor, picture_caret; /**< selection revisions */
    int divider_drag;        /**< column guide active; model unchanged */
    tiku_rich_divider_t divider; /**< initial hit and content constraints */
    tiku_rich_table_t divider_table; /**< original table properties */
    double divider_x, divider_pointer, divider_unit; /**< preview transforms */
    uint64_t divider_generation; /**< source generation at pointer capture */
    uint64_t divider_anchor, divider_caret; /**< selection revisions */
    tiku_rich_cells_t cells;  /**< generation-checked rectangular selection */
    uint64_t cells_generation, cells_anchor, cells_caret;
    uint64_t cells_origin;   /**< direct-cell ID at selection anchor */
    uint64_t cells_focus;    /**< moving endpoint, independent of range order */
    int cells_drag;          /**< rectangle pointer capture */
    int cells_x, cells_y;    /**< captured viewport-local pointer coordinates */
    int64_t cells_tick;      /**< last host tick; -1 starts a new drag clock */
};

/** @brief Clamp scroll against the completed document and viewport height. */
void tiku_rv_clamp(tiku_richview_t *view);
/** @brief Select from stable cell endpoints, preserving their direction. */
tiku_rich_status_t tiku_rv_cells_endpoints(tiku_richview_t *view,
    uint64_t origin, uint64_t focus);
/** @brief Extend a captured rectangle to the nearest visible direct cell. */
tiku_rich_status_t tiku_rv_cells_point(tiku_richview_t *view,
    int x, int y, int extend);
/** @brief Handle rectangular pointer selection and cell-specific input. */
tiku_rich_status_t tiku_rv_cells_event(tiku_richview_t *view,
    const tiku_event_t *event, int *handled);
/** @brief Select pictures and resize their corner as one undoable command. */
tiku_rich_status_t tiku_rv_picture_event(tiku_richview_t *view,
    const tiku_event_t *event, int *handled);
/** @brief Preview a divider and commit adjacent widths on release. */
tiku_rich_status_t tiku_rv_table_event(tiku_richview_t *view,
    const tiku_event_t *event, int *handled);

#endif /* TIKU_RICHVIEW_PRIVATE_H_ */
