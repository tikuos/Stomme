/*
 * Desktop shell and file manager.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_app_image.c - images and documents on the local disk, handed
 * from a worker to the running TikuDesktop to open.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_shell_product.h"
#include "tiku_mount.h"
#include "tiku_remote.h"
#include "tiku_trk_app_private.h"
#include "tiku_work.h"

/** @brief One open request in flight; the worker owns it. */
struct browser_image_job {
    app_t *owner;               /* the window, or NULL once it closed */
    char path[TIKU_PATH_MAX];   /* the file, on the local disk        */
    int accepted;               /* TikuDesktop said it will open it   */
    int document;               /* 1: a document; 0: an image         */
};

/** @brief On the worker: send the open request to TikuDesktop and wait up
 *         to two seconds for its answer. */
static void browser_image_run(void *ctx)
{
    struct browser_image_job *j = ctx;
    tiku_remote_client_t client = {.fd = -1};
    if (tiku_remote_connect(&client, TIKU_SHELL_TRACKER_NAME, 0)) return;
    tiku_msg_t *m = tiku_msg_new(j->document ? TIKU_MSG_DOCUMENTOPEN :
                                            TIKU_MSG_IMAGEOPEN);
    if (m) {
        (void)tiku_msg_add_string(m, "path", j->path);
        int sent = tiku_remote_say(&client, m);
        tiku_msg_free(m);
        int64_t until = tiku_trk_now() + 2000000;
        while (sent && tiku_trk_now() < until) {
            uint32_t id = 0;
            int command = 0;
            tiku_event_t event;
            int got = tiku_remote_read(&client, &id, &event, &command);
            if (got < 0) break;
            m = tiku_remote_heard(&client);
            if (m && tiku_msg_what(m) == (j->document ?
                TIKU_MSG_DOCUMENTOPENED : TIKU_MSG_IMAGEOPENED)) {
                int32_t accepted = 0;
                (void)tiku_msg_find_int32(m, "accepted", 0, &accepted);
                j->accepted = accepted != 0;
                tiku_msg_free(m); break;
            }
            tiku_msg_free(m);
            usleep(10000);
        }
    }
    tiku_remote_disconnect(&client);
}

/** @brief On the loop: put the answer in the status line, unless the
 *         window has closed. */
static void browser_image_done(void *ctx)
{
    struct browser_image_job *j = ctx;
    if (!j->owner) return;
    snprintf(j->owner->status, sizeof j->owner->status, j->accepted ?
        TIKU_SHELL_OPENING_FORMAT :
        TIKU_SHELL_OPEN_FAILED_FORMAT,
        j->document ? "document" : "image");
    j->owner->image_job = NULL; j->owner->dirty = 1;
}

/** @brief Free the job, which owns nothing but its copy of the path. */
static void browser_image_free(void *ctx)
{
    free(ctx);
}

/**
 * @brief Hand an image (PNG, JPEG, BMP) or a document on the local disk to
 *        TikuDesktop to open, through its file associations.
 *
 * The documents are the types TikuDesktop's desktop_remote_document()
 * accepts: text, Markdown, HTML, BASIC and projects.  A file on a device is
 * refused with a note in the status line.
 *
 * @return 0 for a type it does not hand over; 1 for one it does, with the
 *         status line saying whether the request went out.
 */
int browser_open_image(app_t *app, const tiku_model_t *model)
{
    int document = !strcmp(model->type, "text/plain") ||
        !strcmp(model->type, "text/markdown") ||
        !strcmp(model->type, "text/html") ||
        !strcmp(model->type, "text/x-basic") ||
        !strcmp(model->type, "application/x-tiku-project");
    if (!document && strcmp(model->type, "image/png") &&
        strcmp(model->type, "image/jpeg") && strcmp(model->type, "image/bmp") &&
        strcmp(model->type, "image/x-ms-bmp")) return 0;
    if (app->image_job) {
        snprintf(app->status, sizeof app->status, "A file is still opening.");
        return 1;
    }
    char local[TIKU_PATH_MAX];
    tiku_backend_t *b = tiku_backend_serving(app->backend, model->path,
                                            local, sizeof local);
    if (!b || !b->ops || strcmp(b->ops->name, "posix") || local[0] != '/') {
        snprintf(app->status, sizeof app->status,
                 "Save the device file on this computer first.");
        return 1;
    }
    struct browser_image_job *j = calloc(1, sizeof *j);
    if (!j) {
        snprintf(app->status, sizeof app->status, "Not enough memory.");
        return 1;
    }
    j->owner = app; j->document = document;
    snprintf(j->path, sizeof j->path, "%s", local);
    tiku_work_job_t work = {.run = browser_image_run,
        .done = browser_image_done, .free = browser_image_free, .ctx = j};
    if (tiku_work_post(&work)) {
        free(j);
        snprintf(app->status, sizeof app->status,
                 "File-opening worker is busy.");
    } else {
        app->image_job = j;
        snprintf(app->status, sizeof app->status, "Opening %s...",
                 document ? "document" : "image");
    }
    return 1;
}

/** @brief Detach the request in flight from the window, so a completion
 *         after the window closes changes nothing. */
void browser_image_detach(app_t *app)
{
    if (app->image_job) app->image_job->owner = NULL;
    app->image_job = NULL;
}
