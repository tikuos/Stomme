/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_about_window.h - the About box.
 *
 * One modeless window that says what this system is: the wordmark, the
 * release, the processor count, free memory with its gauge, the newest
 * report, the connected boards and the build.  Closed by its tab's close
 * box or Escape.  It is an application descriptor, run by the shell
 * through the services of tiku_app.h.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_ABOUT_WINDOW_H_
#define TIKU_TRK_ABOUT_WINDOW_H_

#include "tiku_app.h"

/** @brief The About box, as an application descriptor the shell hosts. */
extern const tiku_app_descriptor_t tiku_trk_about_app;

#endif /* TIKU_TRK_ABOUT_WINDOW_H_ */
