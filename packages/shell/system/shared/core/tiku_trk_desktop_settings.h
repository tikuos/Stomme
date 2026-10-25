/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_desktop_settings.h - background and icon preference ownership.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_DESKTOP_SETTINGS_H_
#define TIKU_TRK_DESKTOP_SETTINGS_H_
#include "tiku_trk_deskprefs.h"
#include "tiku_resource_registry.h"
/** @brief Desktop preference model, copied persistence work and published snapshots. */
typedef struct tiku_trk_desktop_settings tiku_trk_desktop_settings_t;
/** @brief Bind the native Desktop store and GUI model; changed applies accepted values. */
tiku_trk_desktop_settings_t *tiku_trk_desktop_settings_open(tiku_store_t *, const char *node,
    tiku_trk_deskprefs_t *, void (*changed)(void *), void *);
/** @brief Copy native drawing feedback without I/O or registry publication.
 *  @note A NULL background means no image is currently drawn. */
void tiku_trk_desktop_settings_feedback(tiku_trk_desktop_settings_t *, const tiku_trk_bg_t *, int icon_size);
/** @brief Submit the GUI background and icon proposals; saves use separate native attributes. */
int tiku_trk_desktop_settings_submit(tiku_trk_desktop_settings_t *);
/** @brief Reload copied attributes on workers and publish committed owner values. */
void tiku_trk_desktop_settings_poll(tiku_trk_desktop_settings_t *, uint64_t now_ms);
/** @brief Drain accepted saves for five seconds, then detach copied worker data. */
void tiku_trk_desktop_settings_close(tiku_trk_desktop_settings_t *);
/** @brief Pending or failed save feedback; success returns empty text. */
const char *tiku_trk_desktop_settings_status(const tiku_trk_desktop_settings_t *);
/** @brief Descriptor count for the combined settings provider. */
size_t tiku_trk_desktop_settings_count(const tiku_trk_desktop_settings_t *);
/** @brief Fill independently owned native setting snapshots. */
int tiku_trk_desktop_settings_initial(tiku_trk_desktop_settings_t *, tiku_resource_initial_t *);
/** @brief Attach the borrowed combined settings registry. */
void tiku_trk_desktop_settings_bind(tiku_trk_desktop_settings_t *, tiku_resource_registry_t *);
/** @brief Validate a typed native setting proposal without I/O. */
tiku_resource_error_t tiku_trk_desktop_settings_validate(tiku_trk_desktop_settings_t *, const char *, const tiku_msg_t *, int);
/** @brief Copy a validated resource proposal into isolated persistence work. */
tiku_resource_error_t tiku_trk_desktop_settings_start(tiku_trk_desktop_settings_t *, const char *job, const char *, const tiku_msg_t *, int);
#endif
