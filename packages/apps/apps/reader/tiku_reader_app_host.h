/* tiku_reader_app_host.h -- the application calls TikuReader, Help and
 * TikuMarker share: start, stop, tick, input, menus and file picks.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#ifndef TIKU_READER_APP_HOST_H_
#define TIKU_READER_APP_HOST_H_
#include "tiku_app.h"
#include "tiku_reader_window.h"

/**
 * @brief Start a viewer's process: its window, menus and Help page.
 *
 * A NULL @p profile starts TikuReader: a startup message from a Help
 * request opens Help on the page or the generated HTML it carries, and any
 * other start opens a document window.  A profile opens a document window
 * with that application's name, icon, welcome page, Help page and
 * bookmarks.
 * @return 0, or -1 when the window could not be made.
 */
int tiku_reader_app_start(void **state, const tiku_app_services_t *services,
                          const tiku_reader_profile_t *profile);
/** @brief Destroy the window, collect finished jobs and free the state. */
void tiku_reader_app_stop(void *state);
/** @brief Collect finished jobs and send the frame when it changed. */
void tiku_reader_app_tick(void *state, int64_t now);
/** @brief Hand a desktop event to the window.
 *  @return nonzero once the window is gone, which ends the app. */
int tiku_reader_app_event(void *state, uint32_t id, const tiku_event_t *ev);
/** @brief Run a menu command.
 *  @return nonzero once the window is gone, which ends the app. */
int tiku_reader_app_pick(void *state, uint32_t id, int command);
/** @brief Open the file the picker chose; NULL, a cancelled pick, does
 *         nothing. */
void tiku_reader_app_picked(void *state, uint32_t id, const char *path);
/** @brief The desktop closed window @p id. */
void tiku_reader_app_closed(void *state, uint32_t id);
#endif
