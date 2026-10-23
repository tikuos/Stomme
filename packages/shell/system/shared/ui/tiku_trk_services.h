/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_services.h - the table of services a view asks its shell for.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_SERVICES_H_
#define TIKU_TRK_SERVICES_H_

#include "tiku_gfx.h"

struct tiku_trk_app;

/**
 * @brief What a view asks its shell for: which installed application a path
 *        or a name is, the art it wears, a line for the status strip, and a
 *        repaint.  Any entry may be NULL in a shell that has no answer.
 */
typedef struct tiku_trk_services {
    void *ctx;                  /**< the shell's, handed back to each entry */
    /** @brief The registered application @p path is or links to, or NULL. */
    const struct tiku_trk_app *(*app_of)(void *ctx, const char *path);
    /** @brief The registered application named @p name, or NULL. */
    const struct tiku_trk_app *(*app_named)(void *ctx, const char *name);
    /** @brief The art a pose that is an application wears, or NULL. */
    const char *(*art_for)(void *ctx, const char *path);
    /** @brief A line for the status strip. */
    void (*status)(void *ctx, const char *text);
    /** @brief Repaint @p r; a negative width or height asks for all. */
    void (*damage)(void *ctx, tiku_rect_t r);
} tiku_trk_services_t;

#endif
