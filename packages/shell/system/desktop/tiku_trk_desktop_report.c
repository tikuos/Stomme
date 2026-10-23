/*
 * Desktop shell and file manager.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_desktop_report.c - the desktop's report on demand.
 *
 * SIGUSR2 asks, and the loop writes the report on its next turn, since
 * only the loop may read this state: the frame statistics, the mounts and
 * their ports, the channels, the sessions, the applications hosted in this
 * process, a run, a look or a reclaim in progress, and then the trail.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_shell_product.h"
#include "tiku_report.h"
#include "tiku_trail.h"
#include "tiku_trk_desktop_private.h"

/**
 * @brief Print the frame statistics to @p out: frame counts and pixels,
 *        draw and present time, the median and 95th percentile of the last
 *        128 frames, the content cache, and the bytes presented.
 */
void desktop_frame_report(desktop_t *app, FILE *out)
{
    int64_t samples[128];
    unsigned n = app->frame_sample_count < 128 ? app->frame_sample_count : 128;
    uint64_t draws, hits, frames, bytes, rects;
    size_t cache;
    tiku_workspace_cache_stats(app->desk, &draws, &hits, &cache);
    tiku_host_present_stats(app->host, &frames, &bytes, &rects);
    for (unsigned i = 0; i < n; i++) {
        unsigned j = i;
        int64_t value = app->frame_samples[i];
        while (j && samples[j-1] > value) {
            samples[j] = samples[j-1]; j--;
        }
        samples[j] = value;
    }
    fprintf(out, "rendering: full=%llu partial=%llu logical_pixels=%llu\n",
        (unsigned long long)app->full_frames,
        (unsigned long long)app->partial_frames,
        (unsigned long long)app->frame_pixels);
    fprintf(out, "  draw_us=%llu present_us=%llu over_16ms=%llu "
        "recent_median_us=%lld recent_p95_us=%lld\n",
        (unsigned long long)app->draw_us,
        (unsigned long long)app->present_us,
        (unsigned long long)app->slow_frames,
        (long long)(n ? samples[n/2] : 0),
        (long long)(n ? samples[(n*95-1)/100] : 0));
    fprintf(out, "  content_draws=%llu cache_hits=%llu cache_bytes=%zu\n",
        (unsigned long long)draws, (unsigned long long)hits, cache);
    fprintf(out, "  present_calls=%llu upload_rectangles=%llu "
        "upload_bytes=%llu (pixel payload, excludes X11 overhead)\n",
        (unsigned long long)frames, (unsigned long long)rects,
        (unsigned long long)bytes);
}
#if TIKU_SHELL_BOARD_SERVICES


/** @brief A run's state, a tiku_board_basic_state_t, as an upper-case name;
 *         "?" out of range. */
static const char *
run_state_name(int state)
{
    static const char *const names[] = {
        "SENDING", "RUNNING", "ASKING", "ENDED", "STOPPED", "FAILED"
    };

    return (state >= 0 && state < 6) ? names[state] : "?";
}
#endif


/** @brief Print the desktop's state for the report. */
static void
snapshot(FILE *f, void *ctx)
{
    desktop_t *app = ctx;
    int i, n = 0;

    desktop_frame_report(app, f);
    fprintf(f, "mounts:\n");
    if (app->backend != NULL) {
        for (i = 0; i < tiku_mount_slots(app->backend); i++) {
            const char *prefix = tiku_mount_prefix_at(app->backend, i);
            tiku_backend_t *b = tiku_mount_at(app->backend, i);
#if TIKU_SHELL_BOARD_SERVICES

            char port[128] = "";
#endif


            if (prefix == NULL || b == NULL) {
                continue;
            }
#if TIKU_SHELL_BOARD_SERVICES

                        (void)tiku_backend_tiku_port(b, port, sizeof port);
            fprintf(f, TIKU_SHELL_MOUNT_REPORT_FORMAT, prefix,
                    (port[0] != '\0') ? " on " : "", port);
            n++;
        }
    }
    if (n == 0) {
        fprintf(f, "  none\n");
    }

    /* Each window channel, by its descriptor, with its count of losses;
     * channels have slots of their own, apart from the mounts'. */
    fprintf(f, "channels:\n");
    for (i = 0, n = 0; i < TIKU_MOUNT_MAX; i++) {
        if (app->channels[i] != NULL) {
            fprintf(f, "  fd %d, %d lost\n",
                    tiku_channel_fd(app->channels[i]),
                    tiku_channel_losses(app->channels[i]));
#else

            fprintf(f, TIKU_SHELL_MOUNT_REPORT_FORMAT, prefix);
#endif

            n++;
        }
    }
    if (n == 0) {
        fprintf(f, "  none\n");
    }

    fprintf(f, "sessions:\n");
    for (i = 0, n = 0; i < TIKU_REMOTE_SESSIONS; i++) {
        const tiku_remote_session_t *s = &app->remote.session[i];
        int w, windows = 0;

        if (!s->used) {
            continue;
        }
        for (w = 0; w < TIKU_REMOTE_WINDOWS; w++) {
            windows += (s->win[w].window != NULL);
        }
        fprintf(f, "  %s, %d window%s\n",
                (s->name[0] != '\0') ? s->name : "(unnamed)",
                windows, (windows == 1) ? "" : "s");
        n++;
    }
    if (n == 0) {
        fprintf(f, "  none\n");
    }

    fprintf(f, "hosted here:\n");
    for (i = 0, n = 0; i < TIKU_TRK_EMBED_MAX; i++) {
        if (app->embeds[i].running && app->embeds[i].descriptor != NULL) {
#if TIKU_SHELL_BOARD_SERVICES

            fprintf(f, "  %s\n", app->embeds[i].descriptor->name);
#else

            fprintf(f, TIKU_SHELL_MOUNT_REPORT_FORMAT,
                    app->embeds[i].descriptor->name);
#endif

            n++;
        }
    }
    if (n == 0) {
        fprintf(f, "  none\n");
#if TIKU_SHELL_BOARD_SERVICES

    }

    if (app->board_run != NULL) {
        fprintf(f, "a run in progress: %s on %s, %s%s\n",
                app->board_run_saving ? "a save" : "a program",
                app->board_run_name, run_state_name(app->board_run_state),
                app->board_run_asked ? ", at an INPUT" : "");
    } else {
        fprintf(f, "no run in progress\n");
    }
        if (app->look.going) {
        fprintf(f, "a look in progress: %d of %d ports answered\n",
                app->look.landed, app->look.count);
    } else {
        fprintf(f, "no look in progress\n");
    }
    if (app->reclaim.going) {
        fprintf(f, "a reclaim in progress: %s, %d ports to answer\n",
                app->reclaim.name, app->reclaim.pending);
#endif

    }
}

/**
 * @brief Write a report now, with @p why as its reason: the snapshot and the
 *        trail; the report's path goes into @p out.
 * @return 0 when written.
 */
int
desktop_report_write(desktop_t *app, const char *why, char *out,
                     size_t max)
{
    int rc = tiku_report_write(why, snapshot, app, out, max);

    tiku_trail("report %s: %s", (rc == 0) ? "written" : "refused",
               (rc == 0) ? out : why);
    return rc;
}

/** @brief Write a report when SIGUSR2 has asked since the last turn. */
void
desktop_report_poll(desktop_t *app)
{
    char path[512];

    if (tiku_report_asked()) {
        (void)desktop_report_write(app, "SIGUSR2", path, sizeof path);
    }
}
