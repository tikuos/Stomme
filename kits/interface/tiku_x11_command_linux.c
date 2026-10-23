/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_x11_command_linux.c - the command key on Linux: Control.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_x11_command.h"

int
tiku_x11_command_is_meta(void)
{
    /* A PC keyboard has no Command key; the shortcuts are Control
     * chords. */
    return 0;
}
