/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_application.h - the application descriptor of the shell this
 * binary links.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_APPLICATION_H_
#define TIKU_TRK_APPLICATION_H_

#include "tiku_app.h"

/** @brief The shell's descriptor: tiku_trk_desktop.c defines it in
 *         TikuDesktop, tiku_trk_app.c in TikuTracker. */
extern const tiku_app_descriptor_t tiku_trk_application;

#endif /* TIKU_TRK_APPLICATION_H_ */
