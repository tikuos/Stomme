/*
 * Desktop shell and file manager.
 *
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 *
 * tiku_desktop_panel_jobs.c - background reads and writes for local panels,
 * for a desktop built without board services; with board services the
 * desktop's own board jobs supply these functions.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_desktop_jobs_private.h"

#if !TIKU_SHELL_BOARD_SERVICES

/**
 * @brief Collect finished jobs until @p pending reaches zero or @p ms pass;
 *        used at boot, before the loop runs.
 */
void
desktop_wire_wait(desktop_t *app, int *pending, long ms)
{
    long waited = 0L;

    (void)app;
    while (*pending > 0 && waited < ms) {
        (void)tiku_work_collect();
        usleep(10000);
        waited += 10L;
    }
}

void
desktop_jobs_path_key(char *out, size_t max, const char *path)
{
    snprintf(out, max, "file:%s", path);
}

typedef struct {
    desktop_t             *app;
    tiku_trk_described_t  *p;
    tiku_window_t         *window;  /* the panel's identity when asked */
    int                    row;
    tiku_backend_t        *dev;
    char                   local[TIKU_PATH_MAX];
    char                   value[256];
    char                   err[160];
    int                    rc;
    /* The read-back: each bound row's place and what it said. */
    int                    nrow;
    char                   place[TIKU_FORM_ROWS][TIKU_PATH_MAX];
    char                   said[TIKU_FORM_ROWS][64];
    int                    known[TIKU_FORM_ROWS];
} pwrite_t;

/** @brief On a worker: the write, then every bound place read back. */
static void
pwrite_run(void *ctx)
{
    pwrite_t *w = ctx;
    int i;

    w->rc = w->dev->ops->write(w->dev, w->local, w->value,
                               strlen(w->value), w->err, sizeof w->err);
    for (i = 0; i < w->nrow; i++) {
        if (w->place[i][0] == '\0') {
            continue;
        }
        w->known[i] = tiku_backend_text(w->dev, w->place[i], w->said[i],
                                        sizeof w->said[i]);
    }
}

/**
 * @brief On the loop, when the panel is still the one that asked: put a
 *        refused write in its status line, give each bound row the value
 *        read back, and pass the counts to panel_refreshed().
 */
static void
pwrite_done(void *ctx)
{
    pwrite_t *w = ctx;
    tiku_trk_described_t *p = w->p;
    int i, asked = 0, answered = 0;

    if (p->window != w->window || p->window == NULL || !p->window->open) {
        return;                 /* closed, or another panel in its place */
    }
    if (w->rc != 0) {
        snprintf(p->status, sizeof p->status, "%s: %s",
                 p->form.row[w->row].label,
                 w->err[0] != '\0' ? w->err : "refused");
        tiku_sound_play(TIKU_SOUND_BEEP);
    } else {
        p->status[0] = '\0';
    }
    for (i = 0; i < w->nrow && i < p->form.nrow; i++) {
        tiku_form_row_t *row = &p->form.row[i];

        if (w->place[i][0] == '\0') {
            continue;
        }
        asked++;
        if (w->known[i]) {
            size_t n;

            answered++;
            snprintf(row->value, sizeof row->value, "%s", w->said[i]);
            n = strlen(row->value);
            while (n > 0u && (row->value[n - 1u] == '\n' ||
                              row->value[n - 1u] == '\r' ||
                              row->value[n - 1u] == ' ')) {
                row->value[--n] = '\0';
            }
            row->known = 1;
        } else {
            row->known = 0;
        }
    }
    panel_refreshed(p, asked, answered);
    desktop_damage_all(w->app);
}

/**
 * @brief Write row @p row of panel @p p on a worker and read the panel
 *        back.
 * @return 1 when asked; 0 when nothing serves the place or no worker took
 *         the job.
 */
int
desktop_wire_panel_write(desktop_t *app, tiku_trk_described_t *p, int row)
{
    tiku_work_job_t job;
    pwrite_t *w;
    char path[TIKU_PATH_MAX], local[TIKU_PATH_MAX];
    const char *bind = bind_path(p, &p->form.row[row], path, sizeof path);
    tiku_backend_t *dev = tiku_backend_serving(app->backend, bind, local,
                                               sizeof local);
    int i;

    if (dev == NULL) {
        return 0;
    }
    w = calloc(1, sizeof *w);
    if (w == NULL) {
        return 0;
    }
    w->app = app;
    w->p = p;
    w->window = p->window;
    w->row = row;
    w->dev = dev;
    snprintf(w->local, sizeof w->local, "%s", local);
    snprintf(w->value, sizeof w->value, "%s", p->form.row[row].value);
    w->nrow = (p->form.nrow < TIKU_FORM_ROWS) ? p->form.nrow
                                              : TIKU_FORM_ROWS;
    for (i = 0; i < w->nrow; i++) {
        const tiku_form_row_t *r = &p->form.row[i];
        char rp[TIKU_PATH_MAX], rl[TIKU_PATH_MAX];

        w->place[i][0] = '\0';
        if (r->bind[0] == '\0' || r->kind == TIKU_FORM_BUTTON) {
            continue;
        }
        /* Read back through the same backend, by its own path, so the
         * worker never walks the mount table. */
        if (tiku_backend_serving(app->backend,
                                 bind_path(p, r, rp, sizeof rp),
                                 rl, sizeof rl) == dev) {
            snprintf(w->place[i], sizeof w->place[i], "%s", rl);
        }
    }
    memset(&job, 0, sizeof job);
    job.run = pwrite_run;
    job.done = pwrite_done;
    job.free = free;
    job.ctx = w;
    desktop_jobs_path_key(job.key, sizeof job.key, bind);
    if (tiku_work_post(&job) != 0) {
        free(w);
        return 0;
    }
    tiku_trail("panel: write %s", bind);
    return 1;
}

#endif /* !TIKU_SHELL_BOARD_SERVICES */
