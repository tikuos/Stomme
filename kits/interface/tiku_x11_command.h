/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_x11_command.h - which key a machine gives commands with.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_X11_COMMAND_H
#define TIKU_X11_COMMAND_H

/**
 * @brief Whether commands are given with the Command key; otherwise they
 *        are given with Control.
 *
 * The build links one file per host: tiku_x11_command_darwin.c answers
 * Command, tiku_x11_command_linux.c Control.  Which modifier bit carries
 * the key is the keymap's, read from the X server by tiku_x11.c.
 *
 * @return nonzero when Command is that key, zero when Control is.
 */
int tiku_x11_command_is_meta(void);

#endif /* TIKU_X11_COMMAND_H */
