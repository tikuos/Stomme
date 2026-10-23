/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richview.h - per-view editing, incremental layout and viewport state.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_RICHVIEW_H_
#define TIKU_RICHVIEW_H_

#include "tiku_richlayout.h"
#include "tiku_richsession.h"
#include "tiku_richcells.h"
#include "tiku_event.h"

/** @brief Owner-thread document view independent of windows and rasterizers. */
typedef struct tiku_richview tiku_richview_t;

/** @brief Select a checked cell rectangle, or clear it with NULL.
 *  @note Zero generation accepts current state. Text selection collapses to
 *        the first cell. Content edits or external session movement clear
 *        this view-local selection; scrolling and resizing preserve it. */
tiku_rich_status_t tiku_richview_select_cells(tiku_richview_t *view,
    uint64_t generation, const tiku_rich_cells_t *cells);
/** @brief Read the current cell rectangle, or NOT_FOUND with cleared output. */
tiku_rich_status_t tiku_richview_cells(const tiku_richview_t *view,
                                     tiku_rich_cells_t *out);
/** @brief Get one document-point selection rectangle per selected row.
 *  @note NULL/0 queries count. Short buffers return LIMIT without writes.
 *        Requires current layout; failure sets count to zero except LIMIT. */
tiku_rich_status_t tiku_richview_cells_rectangles(const tiku_richview_t *view,
    tiku_rich_rect_t *out, size_t capacity, size_t *count);
/** @brief Shade selected cells atomically, retaining selection on success. */
tiku_rich_status_t tiku_richview_cells_shade(tiku_richview_t *view,
                                           uint32_t background);
/** @brief Clear selected cell text/pictures through one undoable transaction.
 *  @note Retains paragraphs and nested grids; at most 1024 nonempty
 *        paragraphs per operation. Failure preserves selection and content. */
tiku_rich_status_t tiku_richview_cells_clear(tiku_richview_t *view);
/** @brief Replace selected cells from TSV and select the affected rectangle.
 *  @note Requires a cell selection; checks document and session read-only
 *        state. Shape/bounds and text refusal preserve content and selection.
 *        A successful paste anchors the resulting rectangle at top-left. */
tiku_rich_status_t tiku_richview_cells_paste_text(tiku_richview_t *view,
    const char *text, size_t length);
/** @brief Paste rich snapshot cells and select the affected rectangle.
 *  @note Applies model/session read-only restrictions. Source remains owned
 *        by the caller. Nested tables follow the model's combined depth limit. */
tiku_rich_status_t tiku_richview_cells_paste(tiku_richview_t *view,
    const tiku_richsnapshot_t *snapshot, const tiku_rich_cells_t *source);
/** @brief Insert copied cells at an empty text caret and select the new grid.
 *  @note Nonempty text selections and cell selections are refused. Checks
 *        document/session restrictions; source remains caller-owned. */
tiku_rich_status_t tiku_richview_cells_insert(tiku_richview_t *view,
    const tiku_richsnapshot_t *snapshot, const tiku_rich_cells_t *source);
/** @brief Extend a cell rectangle and reveal its moving endpoint.
 *  @note Arrows move one cell; HOME/END reach row edges, START/FINISH table
 *        corners. Current layout is required. Selection created with
 *        select_cells starts at top-left and ends at bottom-right. */
tiku_rich_status_t tiku_richview_cells_extend(tiku_richview_t *view,
    tiku_rich_motion_t motion);
/** @brief Advance cell-drag scrolling using host monotonic microseconds.
 *  @note Call from the event loop, including while the pointer is stationary.
 *        Negative time is invalid; backward time resets the clock. Each tick
 *        uses at most 100 ms. OK requests repaint; idle returns NO_CHANGE. */
tiku_rich_status_t tiku_richview_tick(tiku_richview_t *view, int64_t now_us);
/** @brief Query commands against this view's text or cell selection.
 *  @note Cell selections support CELL_SET, DELETE, REPLACE_TEXT (TSV paste)
 *        and history commands.
 *        Other commands are disabled rather than targeting the text caret. */
tiku_rich_status_t tiku_richview_query(tiku_richview_t *view,
    tiku_richcmd_kind_t kind, tiku_richcmd_state_t *out);
/** @brief Execute a selection-aware command; zero generation accepts current.
 *  @note Hosts with cell selection use this instead of session_execute.
 *        Text commands retain the session's typing and history semantics. */
tiku_rich_status_t tiku_richview_execute(tiku_richview_t *view,
    uint64_t generation, const tiku_richcmd_t *command);

/** @brief Selected picture bounds in document points, including drag preview.
 *  @return OK, NOT_FOUND for another selection, or a layout refusal. */
tiku_rich_status_t tiku_richview_picture(const tiku_richview_t *view,
                                       tiku_rich_rect_t *out);

/** @brief Hit a column divider within four viewport pixels; no state changes.
 *  @note Output uses document points. Read-only views refuse the hit. */
tiku_rich_status_t tiku_richview_divider_at(const tiku_richview_t *view,
    double x, double y, tiku_rich_divider_t *out);
/** @brief Read the active divider preview, or NOT_FOUND without a drag.
 *  @note Uses document points; no model edit occurs until release. */
tiku_rich_status_t tiku_richview_divider(const tiku_richview_t *view,
                                       tiku_rich_divider_t *out);

/** @brief Create a view retaining its document and font provider.
 *  @note Initial viewport width is options->width logical pixels, zoom 1,
 *        height 400. The host calls step from its event loop until complete.
 *        Failure clears *out. Views have independent selection and scroll. */
tiku_rich_status_t tiku_richview_create(tiku_richdoc_t *doc,
    const tiku_richlayout_options_t *options, tiku_richview_t **out);
/** @brief Destroy a view and cancel pending layout; BUSY in notification. */
tiku_rich_status_t tiku_richview_destroy(tiku_richview_t *view);
/** @brief Borrow the view's session for commands, clipboard and typing.
 *  @note Do not destroy the borrowed session. Its edits invalidate layout
 *        through the document observer without a separate refresh call. */
tiku_richsession_t *tiku_richview_session(tiku_richview_t *view);
/** @brief Set logical viewport size and zoom, preserving document scroll.
 *  @note Each dimension is 1..1,000,000; zoom is 0.1..8. Width divided by
 *        zoom must be in the layout's accepted range. A width/zoom change
 *        invalidates geometry immediately and schedules reflow. */
tiku_rich_status_t tiku_richview_viewport(tiku_richview_t *view,
    double width, double height, double zoom);
/** @brief Request a fresh layout, including retry after a resource refusal. */
tiku_rich_status_t tiku_richview_reflow(tiku_richview_t *view);
/** @brief Advance pending reflow with the layout's work-unit budget.
 *  @note Old-generation geometry is never returned for editing. Refused
 *        jobs keep their error until another edit, resize or reflow request. */
tiku_rich_status_t tiku_richview_step(tiku_richview_t *view, size_t work);
/** @brief Borrow current completed geometry; NULL while stale or refused. */
const tiku_richlayout_t *tiku_richview_layout(const tiku_richview_t *view);
/** @brief Current layout/job status; PENDING after invalidation. */
tiku_rich_status_t tiku_richview_status(const tiku_richview_t *view);
/** @brief Set document-point scroll, clamped when layout is ready. */
tiku_rich_status_t tiku_richview_scroll(tiku_richview_t *view, double y);
/** @brief Current vertical scroll in document points; zero for NULL. */
double tiku_richview_scroll_y(const tiku_richview_t *view);
/** @brief Current logical pixels per document point; one for NULL. */
double tiku_richview_zoom(const tiku_richview_t *view);
/** @brief Select from viewport-local logical pixel coordinates.
 *  @note Extend preserves the selection anchor, for shift-click or dragging.
 *        Pending or refused layout does not change the selection. */
tiku_rich_status_t tiku_richview_point(tiku_richview_t *view,
    double x, double y, int extend);
/** @brief Select the word segment or visual line under viewport coordinates.
 *  @note line=0 uses Unicode word boundaries intersected with grapheme
 *        boundaries; nonzero selects the wrapped visual line. Pending layout
 *        or allocation failure leaves selection and typing state unchanged. */
tiku_rich_status_t tiku_richview_select_at(tiku_richview_t *view,
    double x, double y, int line);
/** @brief Move or extend selection through current grapheme/line geometry.
 *  @note Consecutive Up/Down retain a preferred horizontal caret coordinate.
 *        Movement reveals the caret vertically and ends a typing group. */
tiku_rich_status_t tiku_richview_move(tiku_richview_t *view,
    tiku_rich_motion_t motion, int extend);
/** @brief Scroll the current caret into view after layout completion. */
tiku_rich_status_t tiku_richview_reveal(tiku_richview_t *view);

/** @brief Select the complete text body without requiring finished layout. */
tiku_rich_status_t tiku_richview_select_all(tiku_richview_t *view);
/** @brief Delete the selection or the neighboring complete grapheme.
 *  @note Backward is zero for Delete, nonzero for Backspace. At paragraph
 *        boundaries the separator is removed. Current layout is required.
 *        Refusal preserves text, selection, typing format and undo history. */
tiku_rich_status_t tiku_richview_delete(tiku_richview_t *view, int backward);
/** @brief Handle shared keyboard, pointer, wheel and committed-text events.
 *  @note Coordinates are viewport-local. The host supplies resize geometry,
 *        paints, runs incremental layout and handles clipboard/file actions.
 *        PENDING on navigation/deletion means reflow then retry that event;
 *        PENDING on TEXT_INPUT means wait for the remaining commit pieces.
 *        Commits are refused if edits or selection changes occur mid-transfer.
 *  @return Operation status and *handled=1 for recognized input; NO_CHANGE
 *          with *handled=0 for host commands and unsupported events. */
tiku_rich_status_t tiku_richview_event(tiku_richview_t *view,
    const tiku_event_t *event, int *handled);

#endif /* TIKU_RICHVIEW_H_ */
