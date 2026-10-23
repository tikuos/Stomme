/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_window_app.h - toolkit windows in an application's own process,
 * published to the desktop through the application services.
 *
 * The workspace and its windows stay in the application; window content,
 * titles, sizes, menus, clipboard text and events cross the session.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_WINDOW_APP_H
#define TIKU_WINDOW_APP_H
#include "tiku_app.h"

/** @brief A private workspace of toolkit windows, each published as a
 *         desktop window. */
typedef struct tiku_window_app tiku_window_app_t;
/**
 * @brief A controller over a copy of @p services, with an empty workspace
 *        of its own.
 * @return the controller, or NULL when @p services lacks open() or frame().
 * @note Call this and every function here on the application's UI thread,
 *       never a device worker.
 */
tiku_window_app_t *tiku_window_app_new(const tiku_app_services_t *services);
/** @brief Free the controller, its workspace and its windows' surfaces;
 *         nothing is sent to the desktop. */
void tiku_window_app_free(tiku_window_app_t *app);
/** @brief The controller's workspace, where the application opens its
 *         windows. */
tiku_workspace_t *tiku_window_app_workspace(tiku_window_app_t *app);
/** @brief The desktop's id for @p window, or 0 while it is unpublished. */
uint32_t tiku_window_app_id(tiku_window_app_t *app, tiku_window_t *window);
/**
 * @brief Publish the workspace: close what closed, open what is new, send
 *        changed titles, sizes and menus, and frames where @p dirty or a
 *        window's surface is new.  A focus change raises the window.
 *
 * A new surface is allocated before the old one is freed.
 * @return 0 when a window could not be published (later ones are not
 *         tried) or no slot was free; else nonzero while the workspace
 *         holds a window.
 */
int tiku_window_app_sync(tiku_window_app_t *app, int dirty);
/**
 * @brief Deliver desktop event @p event for window @p id to its toolkit
 *        window; Cmd-W and Cmd-Q ask the window to close.
 * @return what the window's handler returns, 1 for Cmd-W or Cmd-Q, or 0
 *         when no open window has @p id.
 */
int tiku_window_app_event(tiku_window_app_t *app, uint32_t id,
                          const tiku_event_t *event);
/** @brief Hand menu command @p command to window @p id's menu_pick(). */
void tiku_window_app_pick(tiku_window_app_t *app, uint32_t id, int command);
/** @brief The desktop closed window @p id: close it in the workspace. */
void tiku_window_app_closed(tiku_window_app_t *app, uint32_t id);
#endif
