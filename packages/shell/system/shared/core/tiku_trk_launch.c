/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_launch.c - the application registry, the matching, the launch.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_msg.h"
#include "tiku_report.h"
#include "tiku_state.h"
#include "tiku_trail.h"
#include "tiku_trk_launch.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/** @brief Where a preferred handler is filed: one attribute per type. */
#define PREF_NODE "\x01apps"

void
tiku_trk_launch_clear(tiku_trk_apps_t *r)
{
    if (r != NULL) {
        memset(r, 0, sizeof *r);
    }
}

int
tiku_trk_launch_add(tiku_trk_apps_t *r, const char *name, const char *cmd,
                    const char *types)
{
    tiku_trk_app_t *a;
    const char *p;

    if (r == NULL || name == NULL || name[0] == '\0' ||
        r->count >= TIKU_TRK_APP_MAX) {
        return -1;
    }
    a = &r->app[r->count];
    memset(a, 0, sizeof *a);
    snprintf(a->name, sizeof a->name, "%s", name);
    snprintf(a->cmd, sizeof a->cmd, "%s", (cmd != NULL) ? cmd : "");
    for (p = (types != NULL) ? types : ""; *p != '\0'; ) {
        const char *comma = strchr(p, ',');
        size_t len = (comma != NULL) ? (size_t)(comma - p) : strlen(p);

        while (len > 0u && (*p == ' ' || *p == '\t')) {
            p++;
            len--;
        }
        while (len > 0u && (p[len - 1u] == ' ' || p[len - 1u] == '\t')) {
            len--;
        }
        if (len == 1u && p[0] == '*') {
            a->takes_anything = 1;
        } else if (len > 0u && a->ntype < TIKU_TRK_APP_TYPES_MAX) {
            snprintf(a->type[a->ntype], TIKU_TYPE_MAX, "%.*s", (int)len,
                     p);
            a->ntype++;
        }
        if (comma == NULL) {
            break;
        }
        p = comma + 1;
    }
    r->count++;
    return r->count - 1;
}

int tiku_trk_launch_add_document(tiku_trk_apps_t *r, const char *name,
    const char *runner, const char *module, const char *types)
{
    if (!r || !runner || !*runner || !module || !*module ||
        strlen(runner) >= TIKU_TRK_APP_CMD_MAX ||
        strlen(module) >= TIKU_PATH_MAX) return -1;
    for (int i = 0; i < r->count; i++)
        if (!strcmp(r->app[i].module, module)) return i;
    int at = tiku_trk_launch_add(r, name, runner, types);
    if (at >= 0)
        snprintf(r->app[at].module, sizeof r->app[at].module, "%s", module);
    return at;
}

int
tiku_trk_launch_load(tiku_trk_apps_t *r, const char *path)
{
    FILE *f;
    char line[512];
    int n = 0;

    if (r == NULL || path == NULL) {
        return -1;
    }
    f = fopen(path, "r");
    if (f == NULL) {
        return -1;
    }
    while (fgets(line, sizeof line, f) != NULL) {
        char *name = line, *cmd, *types, *nl;

        nl = strchr(line, '\n');
        if (nl != NULL) {
            *nl = '\0';
        }
        if (line[0] == '#' || line[0] == '\0') {
            continue;
        }
        cmd = strchr(name, '\t');
        if (cmd == NULL) {
            continue;           /* a line that names no command is not one */
        }
        *cmd++ = '\0';
        types = strchr(cmd, '\t');
        if (types != NULL) {
            *types++ = '\0';
        }
        {
            /* A registry entry whose program is gone is skipped (AW-041).
             * Only an absolute command can be checked; a bare name is
             * resolved by PATH at run time. */
            struct stat st;

            if (cmd[0] == '/' && stat(cmd, &st) != 0) {
                continue;
            }
        }
        if (tiku_trk_launch_add(r, name, cmd, types) >= 0) {
            n++;
        }
    }
    (void)fclose(f);
    return n;
}

tiku_trk_relation_t
tiku_trk_launch_relation(const tiku_trk_app_t *app, const char *type)
{
    tiku_trk_relation_t best = TIKU_TRK_REL_NONE;
    int i;

    if (app == NULL) {
        return TIKU_TRK_REL_NONE;
    }
    if (type != NULL && type[0] != '\0') {
        for (i = 0; i < app->ntype; i++) {
            const char *claim = app->type[i];
            size_t len = strlen(claim);

            if (strcmp(claim, type) == 0) {
                return TIKU_TRK_REL_EXACT;   /* nothing beats it */
            }
            /* A claim ending in '/' is the supertype claim: "text/" reads
             * every text there is, which is a weaker match than naming the
             * type itself (MA-017). */
            if (len > 0u && claim[len - 1u] == '/' &&
                strncmp(claim, type, len) == 0) {
                best = TIKU_TRK_REL_SUPERTYPE;
            }
        }
    }
    if (best == TIKU_TRK_REL_NONE && app->takes_anything) {
        best = TIKU_TRK_REL_SUPERHANDLER;
    }
    return best;
}

const char *
tiku_trk_launch_preferred(struct tiku_store *store, const char *type,
                          char *buf, size_t max)
{
    int n;

    if (buf == NULL || max == 0u) {
        return "";
    }
    buf[0] = '\0';
    if (store == NULL || type == NULL || type[0] == '\0') {
        return buf;
    }
    n = tiku_state_read(store, PREF_NODE, type, buf, max - 1u);
    if (n < 0) {
        n = 0;
    }
    buf[n] = '\0';
    return buf;
}

int
tiku_trk_launch_set_preferred(struct tiku_store *store, const char *type,
                              const char *app_name)
{
    if (store == NULL || type == NULL || type[0] == '\0') {
        return -1;
    }
    return tiku_state_write(store, PREF_NODE, type,
                                (app_name != NULL) ? app_name : "",
                                (app_name != NULL) ? strlen(app_name) : 0u);
}

int
tiku_trk_launch_cand_before(const tiku_trk_cand_t *a,
                            const tiku_trk_cand_t *b)
{
    if (a->preferred != b->preferred) {
        return a->preferred > b->preferred;
    }
    if (a->rel != b->rel) {
        return (int)a->rel > (int)b->rel;
    }
    /* Between applications that claim the type, a shipped default leads
     * an equal match, never a saved choice.  Applications that claim
     * nothing rank by name alone: there is no handler to prefer. */
    if (a->rel != TIKU_TRK_REL_NONE) {
        int ad = a->app->module[0] != 0;
        int bd = b->app->module[0] != 0;

        if (ad != bd) {
            return ad > bd;
        }
    }
    return strcmp(a->app->name, b->app->name) < 0;
}

int
tiku_trk_launch_candidates(const tiku_trk_apps_t *r,
                           const char *const *types, int ntypes,
                           struct tiku_store *store,
                           tiku_trk_cand_t *out, int max)
{
    int i, t, n = 0;

    if (r == NULL || out == NULL || max <= 0) {
        return 0;
    }
    for (i = 0; i < r->count && n < max; i++) {
        tiku_trk_relation_t best = TIKU_TRK_REL_NONE;
        int preferred = 0;

        /* The union: an application that can open any one of the types is
         * a candidate for the selection, and the relation it is shown with
         * is the best one it has for anything in it (AW-040, MA-080). */
        for (t = 0; t < ntypes; t++) {
            tiku_trk_relation_t rel =
                tiku_trk_launch_relation(&r->app[i], types[t]);
            char pref[TIKU_TRK_APP_NAME_MAX];

            if ((int)rel > (int)best) {
                best = rel;
            }
            (void)tiku_trk_launch_preferred(store, types[t], pref,
                                            sizeof pref);
            if (pref[0] != '\0' && strcmp(pref, r->app[i].name) == 0) {
                preferred = 1;
            }
        }
        if (best == TIKU_TRK_REL_NONE) {
            continue;
        }
        out[n].app = &r->app[i];
        out[n].rel = best;
        out[n].preferred = preferred;
        n++;
    }
    /* Insertion sort by tiku_trk_launch_cand_before(); the list is a
     * menu's worth. */
    for (i = 1; i < n; i++) {
        tiku_trk_cand_t hold = out[i];
        int k = i;

        while (k > 0 && tiku_trk_launch_cand_before(&hold, &out[k - 1])) {
            out[k] = out[k - 1];
            k--;
        }
        out[k] = hold;
    }
    return n;
}

const tiku_trk_app_t *
tiku_trk_launch_agreed(const tiku_trk_apps_t *r, const char *const *types,
                       int ntypes, struct tiku_store *store)
{
    char first[TIKU_TRK_APP_NAME_MAX];
    int t, i;

    if (r == NULL || types == NULL || ntypes <= 0 || store == NULL) {
        return NULL;
    }
    (void)tiku_trk_launch_preferred(store, types[0], first, sizeof first);
    if (first[0] == '\0') {
        return NULL;
    }
    for (t = 1; t < ntypes; t++) {
        char other[TIKU_TRK_APP_NAME_MAX];

        (void)tiku_trk_launch_preferred(store, types[t], other,
                                        sizeof other);
        if (strcmp(first, other) != 0) {
            /* Two documents wanting different applications: there is no
             * answer to preselect (AW-043). */
            return NULL;
        }
    }
    for (i = 0; i < r->count; i++) {
        if (strcmp(r->app[i].name, first) == 0) {
            return &r->app[i];
        }
    }
    return NULL;
}

const char *
tiku_trk_launch_relation_label(tiku_trk_relation_t rel)
{
    switch (rel) {
    case TIKU_TRK_REL_EXACT:        return "opens this type";
    case TIKU_TRK_REL_SUPERTYPE:    return "opens this kind of file";
    case TIKU_TRK_REL_SUPERHANDLER: return "opens anything";
    default:                        return "";
    }
}

const char *
tiku_trk_launch_reason(tiku_trk_launch_err_t err)
{
    switch (err) {
    case TIKU_TRK_LAUNCH_NO_APP:
        return "There is no application that opens this.";
    case TIKU_TRK_LAUNCH_NOTHING:
        return "There is nothing to open.";
    case TIKU_TRK_LAUNCH_IS_TRACKER:
        return "That is this Tracker, which is already running.";
    case TIKU_TRK_LAUNCH_FAILED:
        return "That application could not be started.";
    default:
        return "";
    }
}

int
tiku_trk_launch_self(const tiku_model_t *m, tiku_trk_app_t *out)
{
    const char *leaf;

    if (m == NULL || out == NULL || m->path[0] == '\0' ||
        tiku_model_is_container(m) ||
        (m->facts.perm & TIKU_P_EXEC) == 0u) {
        return 0;
    }
    memset(out, 0, sizeof *out);
    snprintf(out->cmd, sizeof out->cmd, "%s", m->path);
    snprintf(out->path, sizeof out->path, "%s", m->path);
    leaf = strrchr(m->path, '/');
    snprintf(out->name, sizeof out->name, "%s",
             (leaf != NULL && leaf[1] != '\0') ? leaf + 1 : m->path);
    return 1;
}

/**
 * @brief Fork and exec @p app over @p paths, or with @p message_fd for a
 *        message launch.
 *
 * The arguments go to execvp() as they are, never through a shell.
 */
static tiku_trk_launch_err_t
launch_process(const tiku_trk_app_t *app, const char *const *paths,
               int n, int message_fd)
{
    const char *argv[TIKU_TRK_APP_MAX + 5];
    char descriptor[32];
    int argc = 0, i;
    pid_t pid;

    if (app == NULL) {
        return TIKU_TRK_LAUNCH_NO_APP;
    }
    if (app->is_tracker) {
        /* Choosing this Tracker is not a launch: the running one takes it,
         * so no second copy of the desktop starts (AW-035).  Its entry may
         * have no command. */
        return TIKU_TRK_LAUNCH_IS_TRACKER;
    }
    if (app->cmd[0] == '\0') {
        return TIKU_TRK_LAUNCH_NO_APP;
    }
    argv[argc++] = app->cmd;
    if (app->module[0]) {
        /* One document per launch of a module: a batch is refused. */
        if (n < 0 || n > 1 || (n == 1 &&
            (!paths || !paths[0] || !*paths[0])))
            return TIKU_TRK_LAUNCH_NOTHING;
        argv[argc++] = app->module;
    }
    if (message_fd >= 0) {
        if (!app->module[0] || n) return TIKU_TRK_LAUNCH_NOTHING;
        snprintf(descriptor, sizeof descriptor, "%d", message_fd);
        argv[argc++] = "--message-fd";
        argv[argc++] = descriptor;
    }
    for (i = 0; i < n && argc < (int)(sizeof argv / sizeof argv[0]) - 1;
         i++) {
        if (paths[i] == NULL || paths[i][0] == '\0') {
            continue;
        }
        /* An application is never handed itself (AW-038). */
        if (app->path[0] != '\0' && strcmp(paths[i], app->path) == 0) {
            continue;
        }
        argv[argc++] = paths[i];
    }
    argv[argc] = NULL;
    pid = fork();
    if (pid < 0) {
        return TIKU_TRK_LAUNCH_FAILED;
    }
    if (pid == 0) {
        const char *home = getenv("HOME");
        if (message_fd >= 0 && fcntl(message_fd, F_SETFD, 0) < 0) _exit(127);

        /* Everything launched starts in the user's home directory
         * (AW-002). */
        if (home != NULL && home[0] != '\0') {
            if (chdir(home) != 0) {
                /* Not fatal: the document still opens. */
                (void)0;
            }
        }
        execvp(argv[0], (char *const *)(void *)argv);
        _exit(127);
    }
            {
        /* By its leaf, so a long path does not push the name off the
         * line. */
        const char *leaf = strrchr(app->name, '/');

        leaf = (leaf != NULL) ? leaf + 1 : app->name;
        tiku_trail("launch %s pid %d", leaf, (int)pid);
        tiku_trk_launch_note(pid, leaf);
    }
    /* Only launch_reap consumes the status, including immediate failures. */
    return TIKU_TRK_LAUNCH_OK;
}

tiku_trk_launch_err_t
tiku_trk_launch_run(const tiku_trk_app_t *app, const char *const *paths, int n)
{
    return launch_process(app, paths, n, -1);
}

/**
 * @brief Flatten @p message into an unlinked temporary file, rewound and
 *        close-on-exec.
 *
 * The child reads it at its own pace, so the launch never blocks on the
 * message's size.
 *
 * @return the file, or NULL when the message is empty, over 32 KiB or cannot
 *         be written.
 */
static FILE *launch_message_file(const tiku_msg_t *message)
{
    size_t size = tiku_msg_flat_size(message);
    if (!size || size > 32768) return NULL;
    void *bytes = malloc(size);
    FILE *f = bytes ? tmpfile() : NULL;
    if (!f) { free(bytes); return NULL; }
    int ok = tiku_msg_flatten(message, bytes, size, NULL) &&
             fwrite(bytes, 1, size, f) == size && !fflush(f) &&
             !fseek(f, 0, SEEK_SET) &&
             fcntl(fileno(f), F_SETFD, FD_CLOEXEC) >= 0;
    free(bytes);
    if (!ok) { fclose(f); return NULL; }
    return f;
}

int tiku_trk_launch_message(const tiku_trk_app_t *app,
                            const tiku_msg_t *message)
{
    FILE *f = launch_message_file(message);
    if (!f) return TIKU_TRK_LAUNCH_FAILED;
    int result = launch_process(app, NULL, 0, fileno(f));
    fclose(f);
    return result;
}

/*---------------------------------------------------------------------------*/
/* WHAT WAS LAUNCHED, UNTIL IT ENDS                                          */
/*---------------------------------------------------------------------------*/

/** @brief One launched program, until it is reaped. */
typedef struct launched_app {
    pid_t pid;
    char  name[64];
    int expected;                   /* a Force Quit was requested */
    struct launched_app *next;
} launched_app_t;

/* Every launched program not yet reaped, newest first. */
static launched_app_t *launched;

void tiku_trk_launch_expect_stop(pid_t pid)
{
    for (launched_app_t *a = launched; a; a = a->next)
        if (pid > 1 && a->pid == pid) a->expected = 1;
}

int
tiku_trk_launch_running(void)
{
    /* A child stays on the list from its start until it is reaped, the
     * span in which it may still be starting up and about to say
     * something. */
    int n = 0;

    for (launched_app_t *a = launched; a; a = a->next) n++;
    return n;
}

void
tiku_trk_launch_note(pid_t pid, const char *name)
{
    if (pid <= 1) return;
    for (launched_app_t *a = launched; a; a = a->next)
        if (a->pid == pid) return;
    launched_app_t *a = calloc(1, sizeof *a);
    if (!a) { tiku_trail("cannot track child %ld", (long)pid); return; }
    a->pid = pid;
    snprintf(a->name, sizeof a->name, "%s", name ? name : "?");
    a->next = launched; launched = a;
}

int
tiku_trk_launch_reap(tiku_trk_death_t *out, int max)
{
    int n = 0;
    launched_app_t **slot = &launched;

    while (*slot && (!out || n < max)) {
        launched_app_t *a = *slot;
        int status = 0;
        pid_t gone;
        gone = waitpid(a->pid, &status, WNOHANG);
        if (gone < 0 && errno == ECHILD) {
            *slot = a->next; free(a); continue;
        }
        if (gone <= 0) {
            slot = &a->next; continue;
        }
        if (WIFSIGNALED(status)) {
            tiku_trail("%s (pid %d) died: signal %d", a->name,
                       (int)gone, WTERMSIG(status));
        } else {
            tiku_trail("%s (pid %d) ended: %d", a->name, (int)gone,
                       WIFEXITED(status) ? WEXITSTATUS(status) : -1);
        }
        tiku_trk_death_t death = {0};
        snprintf(death.name, sizeof death.name, "%s", a->name);
        death.pid = (long)gone;
        death.signal = WIFSIGNALED(status) ? WTERMSIG(status) : 0;
        death.status = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        death.expected = a->expected && death.signal == SIGKILL;
        if (!death.expected && (death.signal || death.status))
            tiku_report_child(a->name, death.pid, death.signal,
                death.status, death.report, sizeof death.report);
        if (out) out[n] = death;
        *slot = a->next; free(a);
        n++;
    }
    return n;
}
