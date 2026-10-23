/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_run.c - the application runner: loads one installed application
 * module in a process of its own and runs it.
 *
 * Installed applications are loaded here, never in the desktop process.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_app.h"
#include "tiku_client.h"
#include "tiku_manifest.h"
#include "tiku_msg.h"
#include "tiku_report.h"
#include "tiku_trail.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/**
 * @brief Export the help page the bundle's app.manifest names as
 *        TIKU_APP_HELP when the manifest is this module's; unset it
 *        otherwise.
 */
static void bundle_help(const char *directory, const char *module)
{
    char file[PATH_MAX];
    tiku_manifest_t manifest;
    const char *leaf = strrchr(module, '/');
    leaf = leaf ? leaf + 1 : module;
    unsetenv("TIKU_APP_HELP");
    if (snprintf(file, sizeof file, "%s/app.manifest", directory) >=
        (int)sizeof file) return;
    if (tiku_manifest_read(file, &manifest, NULL, 0) == 0 &&
        !strcmp(manifest.module, leaf) && manifest.help[0])
        setenv("TIKU_APP_HELP", manifest.help, 1);
}

/**
 * @brief The launch message on descriptor @p number, which must be a
 *        regular file of 1 to 32768 bytes holding one flattened message.
 *        A descriptor that passes those checks is closed.
 * @return the message, or NULL.
 */
static tiku_msg_t *read_message(const char *number)
{
    char *end = NULL;
    errno = 0;
    long fd = strtol(number, &end, 10);
    struct stat st;
    if (errno || !*number || *end || fd < 3 || fd > INT_MAX ||
        fstat((int)fd, &st) || !S_ISREG(st.st_mode) ||
        st.st_size < 1 || st.st_size > 32768) return NULL;
    size_t n = (size_t)st.st_size, got = 0, used = 0;
    unsigned char *bytes = malloc(n);
    if (!bytes) { close((int)fd); return NULL; }
    while (got < n) {
        ssize_t k = read((int)fd, bytes + got, n - got);
        if (k < 0 && errno == EINTR) continue;
        if (k <= 0) break;
        got += (size_t)k;
    }
    close((int)fd);
    tiku_msg_t *m = got == n ? tiku_msg_unflatten(bytes, n, &used) : NULL;
    free(bytes);
    if (used != n) { tiku_msg_free(m); return NULL; }
    return m;
}

/**
 * @brief Put the module's bundle directory in TIKU_APP_BUNDLE and first on
 *        PATH, for this process and its children, and export its help.
 * @return 1, or 0 when the module's path cannot be resolved.
 */
static int bundle_environment(const char *module)
{
    char resolved[PATH_MAX];
    if (!realpath(module, resolved)) return 0;
    char *slash = strrchr(resolved, '/');
    if (!slash) return 0;
    *slash = 0;
    const char *path = getenv("PATH");
    if (!path || !*path) path = "/usr/bin:/bin";
    size_t cap = strlen(resolved) + strlen(path) + 2;
    char *next = malloc(cap);
    if (!next) return 0;
    snprintf(next, cap, "%s:%s", resolved, path);
    int ok = setenv("TIKU_APP_BUNDLE", resolved, 1) == 0 &&
             setenv("PATH", next, 1) == 0;
    if (ok) bundle_help(resolved, module);
    free(next);
    return ok;
}

/**
 * @brief tiku-run <application.so> [document | --message-fd fd]: load the
 *        module and run it with a document or a launch message.
 * @return the application's exit code, or 2 for bad usage, a bad launch
 *         message, a bundle path that cannot be resolved or a module that
 *         cannot be loaded.
 */
int
main(int argc, char **argv)
{
    const tiku_app_descriptor_t *app;
    char why[256];

    if (argc < 2 || argc > 4 ||
        (argc == 4 && strcmp(argv[2], "--message-fd"))) {
        fprintf(stderr, "usage: tiku-run <application.so> [document | --message-fd fd]\n");
        return 2;
    }
    tiku_msg_t *message = argc == 4 ? read_message(argv[3]) : NULL;
    if (argc == 4 && !message) {
        fprintf(stderr, "tiku-run: invalid launch message\n");
        return 2;
    }
    {
        /* The crash report is named for the application module. */
        const char *leaf = strrchr(argv[1], '/');

        tiku_report_install((leaf != NULL) ? leaf + 1 : argv[1], NULL);
    }
    if (!bundle_environment(argv[1])) {
        fprintf(stderr, "tiku-run: cannot resolve application bundle\n");
        tiku_report_now("application bundle setup failed");
        tiku_msg_free(message);
        return 2;
    }
    tiku_trail("loading module %s", argv[1]);
    app = tiku_app_load(argv[1], why, sizeof why);
    if (app == NULL) {
        fprintf(stderr, "tiku-run: %s: %s\n", argv[1], why);
        tiku_trail("module load failed: %s", why);
        tiku_report_now("application module load failed");
        tiku_msg_free(message);
        return 2;
    }
    tiku_trail("loaded module %s ABI %u", argv[1], TIKU_APP_ABI);
    int result = message ? tiku_client_run_message(app, message) :
        tiku_client_run_document(app, argc == 3 ? argv[2] : NULL);
    tiku_msg_free(message);
    return result;
}
