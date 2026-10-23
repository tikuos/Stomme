/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_desktop_media_jobs.c - the worker jobs for pictures and sound:
 * decoding the next queued thumbnail, and connecting to the sound server.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_desktop_jobs_private.h"

typedef struct {
    desktop_t    *app;
    char          path[TIKU_PATH_MAX];
    int           w, h;
    tiku_image_t  thumb;
    int           rc;
} thumb_t;

/** @brief On a worker: decode the picture and shrink it. */
static void
thumb_run(void *ctx)
{
    thumb_t *t = ctx;

    t->rc = tiku_thumb_make(t->path, &t->w, &t->h, &t->thumb);
}

/**
 * @brief On the loop: hand the thumbnail, empty when the decode failed, to
 *        tiku_thumb_landed(), which caches it and records its size in the
 *        store, and clear thumb_job so the next picture can be posted.
 */
static void
thumb_done(void *ctx)
{
    thumb_t *t = ctx;

    if (t->rc != 0) {
        memset(&t->thumb, 0, sizeof t->thumb);
    }
    tiku_thumb_landed(&t->app->thumbs, t->app->store, t->path, t->w, t->h,
                      &t->thumb);
    t->app->thumb_job = 0;
    desktop_damage_all(t->app);
}

/**
 * @brief Decode the next queued picture on a worker, one at a time.
 * @return 1 when one was sent.
 */
int
desktop_wire_thumb(desktop_t *app)
{
    tiku_work_job_t job;
    thumb_t *t;

    if (app->thumb_job) {
        return 0;
    }
    t = calloc(1, sizeof *t);
    if (t == NULL) {
        return 0;
    }
    if (!tiku_thumb_take(&app->thumbs, t->path, sizeof t->path)) {
        free(t);
        return 0;
    }
    t->app = app;
    memset(&job, 0, sizeof job);
    job.run = thumb_run;
    job.done = thumb_done;
    job.free = free;
    job.ctx = t;
    snprintf(job.key, sizeof job.key, "thumb");
    if (tiku_work_post(&job) != 0) {
        tiku_thumb_landed(&app->thumbs, app->store, t->path, 0, 0, NULL);
        free(t);
        return 0;
    }
    app->thumb_job = 1;
    return 1;
}

/** @brief On a worker: connect to the sound server. */
static void
sound_open_run(void *ctx)
{
    *(int *)ctx = tiku_sound_open_now();
}

/** @brief On the loop: tell the sound kit whether the connect worked. */
static void
sound_open_done(void *ctx)
{
    tiku_sound_opened(*(int *)ctx == 0);
}

int
desktop_jobs_sound_open(void *ctx)
{
    tiku_work_job_t job;
    int *rc = calloc(1, sizeof *rc);

    (void)ctx;
    if (rc == NULL) {
        return 0;
    }
    memset(&job, 0, sizeof job);
    job.run = sound_open_run;
    job.done = sound_open_done;
    job.free = free;
    job.ctx = rc;
    snprintf(job.key, sizeof job.key, "sound");
    if (tiku_work_post(&job) != 0) {
        free(rc);
        return 0;
    }
    return 1;
}
