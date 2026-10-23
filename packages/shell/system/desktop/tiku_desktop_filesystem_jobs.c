/*
 * Desktop shell and file manager.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_desktop_filesystem_jobs.c - the worker jobs for files and volumes:
 * listing a folder on a board, running the file engine's operation,
 * scanning the volumes and unmounting one.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_shell_product.h"
#include "tiku_desktop_jobs_private.h"

#if TIKU_SHELL_BOARD_SERVICES

typedef struct {
    desktop_t             *app;
    tiku_trk_poseview_t   *view;
    int                    gen;
    int                    kind;
    tiku_backend_t        *dev;
    int                    slot;
    unsigned               mount_gen;
    char                   prefix[TIKU_PATH_MAX];
    char                   local[TIKU_PATH_MAX];
    tiku_model_t          *entries;
    int                    n;
} listing_t;

/**
 * @brief The listing's callback, on a worker: keep the entry, adopted into
 *        the namespace; stop at 4096 entries.
 */
static int
listing_one(const tiku_model_t *m, void *ctx)
{
    listing_t *l = ctx;

    if (l->n >= 4096) {
        return 1;
    }
    l->entries[l->n] = *m;
    tiku_mount_adopt_entry(l->prefix, l->slot, &l->entries[l->n]);
    l->n++;
    return 0;
}

/** @brief On a worker: list the folder on the board. */
static void
listing_run(void *ctx)
{
    listing_t *l = ctx;

    l->n = 0;
    if (l->dev->ops->list(l->dev, l->local, listing_one, l) < 0) {
        l->n = -1;
    }
}

/**
 * @brief On the loop: hand the entries to the view, if the view is alive
 *        and the slot still holds the mount that was listed.
 */
static void
listing_done(void *ctx)
{
    listing_t *l = ctx;

    if (!tiku_trk_poseview_alive(l->view) ||
        tiku_mount_gen_at(l->app->backend, l->slot) != l->mount_gen) {
        return;
    }
    if (l->kind == TIKU_TRK_LIST_RELIST) {
        unsigned nf = tiku_trk_poseview_relist_landed(l->view, l->gen,
                                                      l->entries, l->n,
                                                      tiku_trk_now());

        if (nf != TIKU_TRK_NOTIFY_NOTHING) {
            desktop_damage_all(l->app);
        }
    } else {
        (void)tiku_trk_poseview_load_landed(l->view, l->gen, l->entries,
                                            l->n);
        desktop_damage_all(l->app);
    }
    tiku_trail("list: %s landed, %d entries", l->local, l->n);
}

/** @brief Free a listing, finished or cancelled. */
static void
listing_free(void *ctx)
{
    listing_t *l = ctx;

    free(l->entries);
    free(l);
}

/**
 * @brief The lister every view asks: a folder on a board is listed by a
 *        worker.
 * @return 1 when a worker took the listing; 0 for anything else, which
 *         the view lists itself.
 */
static int
desktop_lister(void *ctx, tiku_trk_poseview_t *v, int gen, int kind)
{
    desktop_t *app = ctx;
    const char *path = tiku_trk_poseview_path(v);
    tiku_backend_t *dev;
    tiku_work_job_t job;
    listing_t *l;
    char local[TIKU_PATH_MAX];
    int i;

    if (path == NULL || strncmp(path, "/devices/", 9) != 0) {
        return 0;
    }
    dev = tiku_backend_serving(app->backend, path, local, sizeof local);
    if (!desktop_jobs_is_board(dev)) {
        return 0;               /* no board serves it: the view lists it */
    }
    for (i = 0; i < TIKU_MOUNT_MAX; i++) {
        if (tiku_mount_at(app->backend, i) == dev) {
            break;
        }
    }
    if (i == TIKU_MOUNT_MAX) {
        return 0;
    }
    l = calloc(1, sizeof *l);
    if (l == NULL) {
        return 0;
    }
    l->entries = calloc(4096, sizeof *l->entries);
    if (l->entries == NULL) {
        free(l);
        return 0;
    }
    l->app = app;
    l->view = v;
    l->gen = gen;
    l->kind = kind;
    l->dev = dev;
    l->slot = i;
    l->mount_gen = tiku_mount_gen_at(app->backend, i);
    snprintf(l->prefix, sizeof l->prefix, "%s",
             tiku_mount_prefix_at(app->backend, i));
    snprintf(l->local, sizeof l->local, "%s", local);
    memset(&job, 0, sizeof job);
    job.run = listing_run;
    job.done = listing_done;
    job.free = listing_free;
    job.ctx = l;
    desktop_jobs_path_key(job.key, sizeof job.key, path);
    if (tiku_work_post(&job) != 0) {
        listing_free(l);
        return 0;
    }
    tiku_trail("list: %s asked", path);
    return 1;
}
#endif



/**
 * @brief Install the desktop's workers as the views' lister and as the
 *        sound kit's opener, and fill the volume cache with a scan.
 */
void
desktop_wire_install(desktop_t *app)
{
    tiku_volumes_t vs;
    int n;
#if TIKU_SHELL_BOARD_SERVICES


    tiku_trk_poseview_set_lister(desktop_lister, app);
#else


    (void)app;
#endif

    tiku_sound_set_opener(desktop_jobs_sound_open, app);
    /* The first scan is the boot's, on the loop, so the cache is filled
     * before anything reads it; every later one is a worker's. */
    n = tiku_volumes_scan_now(&vs);
    tiku_volumes_cache_set(&vs, n);
}

/** @brief On a worker: step the engine until it is done, or asks. */
static void
fs_run(void *ctx)
{
    tiku_fs_t *fs = ((tiku_fs_t **)ctx)[0];

    while (tiku_fs_busy(fs) && tiku_fs_step(fs, 1) > 0) {
    }
}

/** @brief On the loop, once the engine has stopped, done or at a question:
 *         clear fs_job so desktop_wire_fs_run() can post again. */
static void
fs_done(void *ctx)
{
    desktop_t *app = ((void **)ctx)[1];

    app->fs_job = 0;
    desktop_damage_all(app);
}

/** @brief Free the job's context. */
static void
fs_free_ctx(void *ctx)
{
    free(ctx);
}

/**
 * @brief Run the file engine's queued operation on a worker, one job
 *        for the whole of it; the loop reads progress, answers a
 *        question when the job stops on one, and posts the next.
 */
void
desktop_wire_fs_run(desktop_t *app)
{
    tiku_work_job_t job;
    void **ctx;

    if (app->fs == NULL || app->fs_job) {
        return;
    }
    ctx = calloc(2, sizeof *ctx);
    if (ctx == NULL) {
        return;
    }
    ctx[0] = app->fs;
    ctx[1] = app;
    memset(&job, 0, sizeof job);
    job.run = fs_run;
    job.done = fs_done;
    job.free = fs_free_ctx;
    job.ctx = ctx;
    snprintf(job.key, sizeof job.key, "fs");
    if (tiku_work_post(&job) == 0) {
        app->fs_job = 1;
    } else {
        free(ctx);
    }
}

typedef struct {
    desktop_t      *app;
    tiku_volumes_t  vs;
    int             count;
} volscan_t;

/** @brief On a worker: scan the volumes. */
static void
volscan_run(void *ctx)
{
    volscan_t *v = ctx;

    v->count = tiku_volumes_scan_now(&v->vs);
}

/**
 * @brief On the loop: a scan that worked replaces the cache the volume
 *        readers are answered from.
 */
static void
volscan_done(void *ctx)
{
    volscan_t *v = ctx;

    if (v->count >= 0) {
        tiku_volumes_cache_set(&v->vs, v->count);
    }
    v->app->volumes_job = 0;
}

/**
 * @brief Scan the volumes on a worker, one scan out at a time; a mount
 *        that never answers hangs the worker, and the loop reads the
 *        last scan.
 */
void
desktop_wire_volumes(desktop_t *app)
{
    tiku_work_job_t job;
    volscan_t *v;

    if (app->volumes_job) {
        return;
    }
    v = calloc(1, sizeof *v);
    if (v == NULL) {
        return;
    }
    v->app = app;
    memset(&job, 0, sizeof job);
    job.run = volscan_run;
    job.done = volscan_done;
    job.free = free;
    job.ctx = v;
    snprintf(job.key, sizeof job.key, "volumes");
    if (tiku_work_post(&job) == 0) {
        app->volumes_job = 1;
    } else {
        free(v);
    }
}

typedef struct {
    desktop_t      *app;
    char            mount[TIKU_PATH_MAX];
    char            name[128];
    char            why[200];
    tiku_unmount_t  rc;
} unmount_t;

/** @brief On a worker: umount(2), which flushes what is dirty. */
static void
unmount_run(void *ctx)
{
    unmount_t *u = ctx;

    u->rc = tiku_volume_umount_now(u->mount, u->name, u->why, sizeof u->why);
}

/**
 * @brief On the loop: report the unmount on the status line, or in an
 *        alert when it was refused, and scan the volumes again.
 */
static void
unmount_done(void *ctx)
{
    unmount_t *u = ctx;
    desktop_t *app = u->app;

    app->unmount_job = 0;
    if (u->rc == TIKU_UNMOUNT_OK) {
        snprintf(app->status, sizeof app->status, "unmounted \"%.60s\"",
                 u->name);
        tiku_trail("unmounted %s", u->mount);
    } else {
        tiku_sound_play(TIKU_SOUND_BEEP);
        tiku_alert_open(alert_model(app), TIKU_ALERT_STOP,
                        TIKU_TRK_ALERT_TAG_REFUSAL, u->why, "Cancel");
    }
    desktop_wire_volumes(app);
    desktop_damage_all(app);
}

/**
 * @brief Unmount @p mount on a worker; the status line or an alert says
 *        how it went.
 * @return 1 when asked; 0 while another unmount is out.
 */
int
desktop_wire_unmount(desktop_t *app, const char *mount, const char *name)
{
    tiku_work_job_t job;
    unmount_t *u;

    if (app->unmount_job) {
        return 0;
    }
    u = calloc(1, sizeof *u);
    if (u == NULL) {
        return 0;
    }
    u->app = app;
    snprintf(u->mount, sizeof u->mount, "%s", mount);
    snprintf(u->name, sizeof u->name, "%s", name);
    memset(&job, 0, sizeof job);
    job.run = unmount_run;
    job.done = unmount_done;
    job.free = free;
    job.ctx = u;
    snprintf(job.key, sizeof job.key, "volumes");
    if (tiku_work_post(&job) != 0) {
        free(u);
        return 0;
    }
    app->unmount_job = 1;
    snprintf(app->status, sizeof app->status, "unmounting \"%.60s\"...",
             name);
    return 1;
}
