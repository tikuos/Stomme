/*
 * Desktop shell and file manager.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_desktop_jobs_private.h - what the desktop's worker-job files share:
 * the kit headers they use and the helpers one job file defines for
 * another.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_shell_product.h"
#ifndef TIKU_DESKTOP_JOBS_PRIVATE_H_
#define TIKU_DESKTOP_JOBS_PRIVATE_H_

#if TIKU_SHELL_BOARD_SERVICES

#include "tiku_board_basic.h"
#include "tiku_channel.h"

#endif
#include "tiku_fs.h"
#include "tiku_sound.h"
#include "tiku_thumb.h"
#include "tiku_trail.h"
#include "tiku_trk_desktop_private.h"
#include "tiku_trk_poseview.h"
#include "tiku_volume.h"
#include "tiku_work.h"
#include <unistd.h>

#if TIKU_SHELL_BOARD_SERVICES

/**
 * @brief Whether backend @p b is a board on a wire: the "tiku" backend,
 *        which can say what changed.
 */
int desktop_jobs_is_board(const tiku_backend_t *b);
#endif


/**
 * @brief The job key for @p path, into @p out: "board:/devices/<name>",
 *        the key of that board's jobs, for a path under /devices/<name>;
 *        "board:" and the path itself for any other path.
 */
void desktop_jobs_path_key(char *out, size_t max, const char *path);

/**
 * @brief The sound kit's opener: post the connect to the sound server on
 *        a worker.
 * @return 1 when a worker took it.
 */
int desktop_jobs_sound_open(void *ctx);

#endif /* TIKU_DESKTOP_JOBS_PRIVATE_H_ */
