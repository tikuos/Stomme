/* tiku_editor_main.c -- main() for running the Editor as a program of its
 * own; modules and tests use its descriptor, tiku_edit_app.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_editor_private.h"

/** @brief Run the Editor as a client of the desktop, opening the file
 *         named on the command line.  @return 2 when the path is too
 *         long, else what tiku_client_run() returns. */
int
main(int argc, char **argv)
{
    if (argc > 1 && !tiku_editor_startup_path(argv[1])) {
        fprintf(stderr, "tiku-edit: the path is longer than 511 bytes; "
                        "nothing was opened\n");
        return 2;
    }
    return tiku_client_run(&tiku_edit_app);
}
