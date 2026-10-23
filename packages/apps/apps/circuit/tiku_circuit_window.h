/* tiku_circuit_window.h -- TikuCircuit's window: read-only schematics,
 * Gerber artwork, drill drawings and Gerber jobs, imported on workers.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#ifndef TIKU_CIRCUIT_WINDOW_H_
#define TIKU_CIRCUIT_WINDOW_H_

#include "tiku_window.h"

/** @brief The viewer: its window, the document shown and its jobs. */
typedef struct tiku_circuitview tiku_circuitview_t;

/**
 * @brief An empty viewer, made on the UI thread; it opens no window.
 *
 * @param workspace  Where the window opens; borrowed
 * @param dirty      Set whenever the window wants redrawing; borrowed
 * @return The viewer, or NULL when memory runs out
 * @note Both borrowed objects must outlive tiku_circuitview_destroy().
 */
tiku_circuitview_t *tiku_circuitview_new(tiku_workspace_t *workspace,
                                         int *dirty);
/** @brief Open the viewer's one window, or bring it to the front. */
void tiku_circuitview_open(tiku_circuitview_t *viewer);
/**
 * @brief Close the window, cancel its jobs and drop the caller's
 *        reference.  NULL is permitted.
 *
 * @note A job still running holds the viewer, which is freed when
 *       tiku_work_collect() retires the last of them.
 */
void tiku_circuitview_destroy(tiku_circuitview_t *viewer);
/** @brief The open window, or NULL once it has closed. */
tiku_window_t *tiku_circuitview_handle(tiku_circuitview_t *viewer);
/**
 * @brief One turn of the UI loop: post the import asked for last, repeat
 *        a held scrollbar arrow, and post a render of the newest view
 *        when none is in flight.
 */
void tiku_circuitview_tick(tiku_circuitview_t *viewer);
/** @brief Say @p text in the status line and ask for a redraw. */
void tiku_circuitview_note(tiku_circuitview_t *viewer,
                           const char *text);
/** @brief The application's file picker, asked by Open..., Open Gerber
 *         Job..., Add Layer... and Export View as PNG...; NULL disables
 *         them. */
void tiku_circuitview_picker(tiku_circuitview_t *viewer,
                             void (*choose)(void *), void *ctx);
/** @brief Whether the ask in flight is Export View as PNG..., which wants
 *         a place to save; every other ask wants a file to open. */
int tiku_circuitview_picking_export(const tiku_circuitview_t *viewer);
/** @brief Keep per-document view settings in @p directory, an absolute
 *         path the caller owns; NULL, or a path that is not absolute or
 *         too long, turns them off.  It opens nothing and makes no file. */
void tiku_circuitview_settings(tiku_circuitview_t *viewer, const char *directory);
/**
 * @brief Open @p path read-only on a worker.
 *
 * The shown document stays until the new one and its first view are
 * ready, and stays when the file cannot be opened.  The path is copied;
 * one import runs at a time and a newer request replaces an older one.
 */
void tiku_circuitview_file(tiku_circuitview_t *viewer,
                           const char *path);
/**
 * @brief The file chosen for the last ask, used for the command that
 *        asked: Open opens it, Open Gerber Job opens it as a job, Add
 *        Layer adds it to the stack shown (refused, with a note, when there
 *        is no room or no stack), Export writes the PNG there.  NULL or ""
 *        forgets the ask.
 *
 * @note The desktop does not answer a cancelled panel, so an ask may get
 *       no answer; nothing waits for one, and a newer ask replaces it.
 */
void tiku_circuitview_picked(tiku_circuitview_t *viewer,
                             const char *path);

#endif
