/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_x11_command_darwin.c - the command key on macOS: Command.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_x11_command.h"

int
tiku_x11_command_is_meta(void)
{
    /* macOS applications close with Command-W and copy with Command-C. */
    return 1;
}
