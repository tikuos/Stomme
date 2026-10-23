/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_client.h - run one application descriptor as its own process.
 *
 * The runner connects to the desktop over the window session and gives
 * the descriptor a tiku_app_services_t whose calls travel over that
 * session.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_CLIENT_H_
#define TIKU_CLIENT_H_

#include "tiku_app.h"

/** @brief The longest the runner waits for a desktop message between two
 *         calls of tick(), in milliseconds. */
#define TIKU_CLIENT_TICK_MS 10

/**
 * @brief Connect to the desktop, start @p app and run it until it is done.
 *
 * Events, picks and the shell's messages reach the descriptor's own
 * hooks; tick() runs every turn.
 * @return 0 when the desktop shut the session down, the application said
 *         it was done or its last window closed; 1 when no desktop
 *         answered, start() failed or the desktop went away while a
 *         window was open.
 */
int tiku_client_run(const tiku_app_descriptor_t *app);

/** @brief As tiku_client_run(), giving start() @p path as the services'
 *         startup_document. */
int tiku_client_run_document(const tiku_app_descriptor_t *app,
                             const char *path);
/** @brief As tiku_client_run(), giving start() @p message as the services'
 *         startup_message; 1 at once when @p message is NULL. */
int tiku_client_run_message(const tiku_app_descriptor_t *app,
                            const struct tiku_msg *message);

/** @brief As tiku_client_run(), over @p fd already connected to a desktop
 *         (a serial line or a pty). */
int tiku_client_run_fd(const tiku_app_descriptor_t *app, int fd);

#endif /* TIKU_CLIENT_H_ */
