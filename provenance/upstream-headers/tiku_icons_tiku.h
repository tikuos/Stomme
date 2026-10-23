/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_icons_tiku.h - TikuOS's own additions to the icon set: a board,
 * this machine, a magnifier, and the icons of TikuOS's applications,
 * tools, toolbars and Devices Preferences sections.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_ICONS_TIKU_H_
#define TIKU_ICONS_TIKU_H_

/**
 * @brief Put TikuOS's own icons into the set, once.
 *
 * @return how many the set took: tiku_icons_tiku_count() unless it refused
 *         one, and 0 on every later call.
 */
int tiku_icons_tiku_register(void);

/** @brief How many icons TikuOS registers: what tiku_icons_tiku_register()
 *         returns when the set takes them all. */
int tiku_icons_tiku_count(void);

#endif /* TIKU_ICONS_TIKU_H_ */
