/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_splash.h - the splash screen a program shows while it starts.
 *
 * Artwork across the top half, the program's name below it, a status line
 * saying what is loading, a block of credit lines, the version in one
 * bottom corner and the mark in the other.  One painter serves every
 * program; the built-in artwork is drawn from the mark's palettes.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_SPLASH_H_
#define TIKU_SPLASH_H_

#include "tiku_gfx.h"

/** @brief The most credit lines a splash screen draws. */
#define TIKU_SPLASH_LINES 6

/**
 * @brief Paint a splash screen over all of @p s.
 *
 * The top half is @p art, scaled to cover it and cropped, or without it a
 * sweep of the mark's palettes with the mark at the right.  Below come
 * @p name in a large face, @p status, and up to TIKU_SPLASH_LINES of
 * @p lines as a block; @p version and a small mark sit in the bottom
 * corners.
 *
 * @param status  What is loading now, or NULL for the finished screen an
 *                About box shows
 * @param art     A picture the caller has loaded, or NULL
 */
void tiku_splash_paint(tiku_surface_t *s, const char *name,
                            const char *const *lines, int nlines,
                            const char *version, const char *status,
                            const tiku_surface_t *art);

/**
 * @brief Paint the Apache License's notice in small grey type under the
 *        name and the status line of a splash screen.
 *
 * @note Paint it over a splash that tiku_splash_paint() drew with no
 *       lines: the notice takes the place of the credit lines.
 */
void tiku_splash_license(tiku_surface_t *s);

#endif /* TIKU_SPLASH_H_ */
