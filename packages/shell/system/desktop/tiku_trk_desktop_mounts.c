/*
 * Desktop shell and file manager.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_desktop_mounts.c - boards as mounts: naming and mounting them
 * under /devices, their window channels, boards that dial in, the ports on
 * offer to connect, and the pumps that carry change notices to the windows.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_shell_product.h"
#if TIKU_SHELL_BOARD_SERVICES


#include "tiku_link_tcp.h"

#endif
#include "tiku_mount.h"
#include "tiku_report.h"
#include "tiku_trail.h"
#include "tiku_trk_desktop_private.h"

#if TIKU_SHELL_BOARD_SERVICES
#include <sys/socket.h>

#endif
#include <sys/wait.h>
#include <unistd.h>

/**
 * @brief Reload every open folder and the Desktop.  Runs half a second
 *        after a volume mounts, when a link to the new volume resolves as
 *        a volume (PVN-042).
 * @return 1: the task runs once.
 */
int
refresh_after_mount(void *ctx)
{
    desktop_t *app = ctx;
    int i;

    for (i = 0; i < app->folder_count; i++) {
        if (app->folders[i].win != NULL && app->folders[i].win->open &&
            app->folders[i].view != NULL) {
            (void)tiku_trk_poseview_reload(app->folders[i].view);
        }
    }
    tiku_trk_desktop_view_rebuild(&app->desktop_view);
    desktop_damage_all(app);
    return 1;
}

/**
 * @brief Before volume @p dev unmounts (AW-096): save the state of every
 *        window on it and of the Desktop, and close each query window that
 *        targets it, since a running query holds the volume busy.
 */
void
save_before_unmount(dev_t dev, void *context)
{
    desktop_t *app = context;
    int i;

    for (i = 0; i < app->folder_count; i++) {
        folder_t *fl = &app->folders[i];

        if (fl->win == NULL || !fl->win->open || fl->view == NULL) {
            continue;
        }
        if (tiku_trk_poseview_is_query(fl->view) &&
            tiku_query_targets_device(fl->run, (uint64_t)dev)) {
            close_window(app, fl->win);
            continue;
        }
        if (tiku_trk_poseview_on_device(fl->view, (uint64_t)dev)) {
            (void)tiku_trk_poseview_save_state(fl->view);
        }
    }
    {
        folder_t *desk = desktop_folder(app);

        if (desk != NULL && desk->view != NULL &&
            tiku_trk_poseview_on_device(desk->view, (uint64_t)dev)) {
            (void)tiku_trk_poseview_save_state(desk->view);
        }
    }
}
#if TIKU_SHELL_BOARD_SERVICES


/**
 * @brief The leaf a board mounts under in /devices, into @p out.
 *
 * Its processor, else its own name, else its devid, lower-cased and made
 * a path component, so a name such as "my/board" cannot reach another
 * place.  A leaf already mounted takes the port's tty leaf
 * ("nrf54l15-ttyACM3"), then a number.  Saved window state keys on the
 * devid, not on this leaf.
 */
void
device_mount_name(const tiku_backend_t *dev, const tiku_backend_t *ns,
                  char *out, size_t max)
{
        const tiku_identity_t *id = tiku_backend_tiku_identity(dev);
    char base[64];
    size_t i, n = 0;
    const char *from;
    int tries;

    /* The processor names the board (nrf54l15, nrf54lm20b): boards ship
     * with the same default name, so the processor is what tells two
     * apart. */
    from = (id != NULL && id->mcu[0] != '\0')  ? id->mcu
         : (id != NULL && id->name[0] != '\0') ? id->name
                                                : dev->devid;
    for (i = 0; from[i] != '\0' && n + 1u < sizeof base; i++) {
        unsigned char c = (unsigned char)from[i];

        if (c >= 'A' && c <= 'Z') {
            c = (unsigned char)(c - 'A' + 'a');
        }
        if ((c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-') {
            base[n++] = (char)c;
        } else if (n > 0u && base[n - 1u] != '-') {
            base[n++] = '-';
        }
    }
    while (n > 0u && base[n - 1u] == '-') {
        n--;
    }
    base[n] = '\0';
    if (base[0] == '\0') {
        snprintf(base, sizeof base, "%s", "device");
    }
        for (tries = 1; tries < 100; tries++) {
        char prefix[TIKU_PATH_MAX];
        int slot, taken = 0;

        if (tries == 1) {
            snprintf(out, max, "%s", base);
        } else if (tries == 2) {
            /* Two of one processor: the tty leaf of the port tells them
             * apart (nrf54l15-ttyACM3). */
            char port[256], real[PATH_MAX];
            const char *leaf = NULL;

            if (tiku_backend_tiku_port(dev, port, sizeof port) == 0 &&
                realpath(port, real) != NULL) {
                leaf = strrchr(real, '/');
            }
            if (leaf != NULL && leaf[1] != '\0') {
                snprintf(out, max, "%s-%s", base, leaf + 1);
            } else {
                snprintf(out, max, "%s-%d", base, tries);
            }
        } else {
            snprintf(out, max, "%s-%d", base, tries);
        }
        snprintf(prefix, sizeof prefix, "/devices/%s", out);
        for (slot = 0; slot < tiku_mount_slots(ns); slot++) {
            const char *have = tiku_mount_prefix_at(ns, slot);

            if (have != NULL && strcmp(have, prefix) == 0) {
                taken = 1;
            }
        }
        if (!taken) {
            return;
        }
    }
}

int
channel_attach(desktop_t *app, int k)
{
    tiku_channel_t *ch = (k >= 0) ? app->channels[k] : NULL;

    if (ch == NULL) {
        return 0;
    }
    if (tiku_remote_adopt(&app->remote, tiku_channel_fd(ch)) != 0) {
        tiku_channel_close(ch);
        app->channels[k] = NULL;
        return 0;
    }
    tiku_channel_greet(ch);
    return 1;
}

void
channels_attach(desktop_t *app)
{
    int k;

    for (k = 0; k < TIKU_MOUNT_MAX; k++) {
        (void)channel_attach(app, k);
    }
}

/* How long, in ms, a port with no prompt is given to acknowledge a link;
 * the link's own retries give up after 2.5 s.  Not used in this file: the
 * probe in tiku_desktop_device_jobs.c defines its own PROBE_MS. */
#define PROBE_MS 3000

/** @brief Keep @p ch in a free slot, or close it when there is none.
 *         @return the slot, or -1. */
int
channel_keep(desktop_t *app, tiku_channel_t *ch)
{
    int k;

    for (k = 0; ch != NULL && k < TIKU_MOUNT_MAX; k++) {
                if (app->channels[k] == NULL) {
            app->channels[k] = ch;
            app->channel_mount[k][0] = '\0';
            app->channel_board[k][0] = '\0';
            return k;
        }
    }
    tiku_channel_close(ch);     /* no slot: NULL is a no-op */
    return -1;
}


/** @brief Each turn: pump every channel, and unmount a dialled board whose
 *         link is lost and close its channel. */
void
channels_tend(desktop_t *app)
{
    int k;

    for (k = 0; k < TIKU_MOUNT_MAX; k++) {
        tiku_channel_t *ch = app->channels[k];

        if (ch == NULL) {
            continue;
        }
        (void)tiku_channel_pump(ch);
        /* A lost dial-in link is not restored: the board dials again and
         * is taken in as new.  Its mount is removed with the link, so a
         * rebooted board appears once under /devices. */
        if (app->channel_mount[k][0] != '\0' &&
            tiku_channel_losses(ch) > 0) {
            tiku_trail("unmount %s: the link was lost",
                       app->channel_mount[k]);
            if (strncmp(app->channel_mount[k], "/devices/", 9) == 0) {
                desktop_devtray_failed(app, NULL,
                                       app->channel_mount[k] + 9,
                                       "the link was lost");
            }
            (void)desktop_board_unmount(app, app->channel_mount[k]);
            app->channel_mount[k][0] = '\0';
            tiku_channel_close(ch);
            app->channels[k] = NULL;
        }
    }
}


/*---------------------------------------------------------------------------*/
/* BOARDS THAT DIAL IN                                                       */
/*---------------------------------------------------------------------------*/

/** @brief Listen on TCP @p port for boards that dial in.
 *  @return 1 when the socket is up. */
int
boards_listen(desktop_t *app, int port)
{
    /* board_listen <= 0 means no listener.  stdin is open, so the socket
     * is never descriptor 0. */
    app->board_listen = tiku_link_tcp_listen(port);
    return app->board_listen > 0;
}

/**
 * @brief Mount the shell of a board that dialled in, over the channel in
 *        slot @p slot.
 *
 * @note Runs on the loop.  The shell's transport is fed by pumping this
 *       channel, which the loop also pumps, and one link must not be
 *       pumped from two threads.
 */
static void
mount_over(desktop_t *app, int slot)
{
    tiku_channel_t *ch = app->channels[slot];
    tiku_tx_t *tx = tiku_channel_shell_tx(ch);
    tiku_backend_t *dev;
    char name[64], prefix[TIKU_PATH_MAX];

    if (tx == NULL) {
        return;
    }
    dev = tiku_backend_tiku_open_tx(tx);
    if (dev == NULL) {
        return;
    }
    device_mount_name(dev, app->backend, name, sizeof name);
    snprintf(prefix, sizeof prefix, "/devices/%s", name);
    tiku_backend_tiku_set_write_timeout(dev, 4000);
    if (tiku_mount_add(app->backend, prefix, dev, 1) != 0) {
        tiku_backend_close(dev);
        return;
    }
    snprintf(app->channel_mount[slot], TIKU_PATH_MAX, "%s", prefix);
    tiku_trail("mount %s over a dial-in", prefix);
}

/**
 * @brief Take in every board that dialled since the last turn: wrap its
 *        connection in a channel, adopt the channel into the remote
 *        listener, greet it and mount the board's shell over it.
 *
 * Boards that dial in arrive after channels_attach() has run at startup,
 * so each is adopted here.
 */
void
boards_accept(desktop_t *app)
{
    for (;;) {
        tiku_channel_t *ch;
        int fd;

        if (app->board_listen <= 0) {
            return;
        }
        fd = accept(app->board_listen, NULL, NULL);
        if (fd < 0) {
            return;
        }
        int slot;

        ch = tiku_channel_adopt(tiku_link_tcp_open(fd));
        if (ch == NULL) {
            continue;
        }
        /* A slot first: channel_keep() closes a channel it has no slot
         * for, and the remote listener must not hold its descriptor. */
        slot = channel_keep(app, ch);
        if (slot < 0) {
            continue;
        }
        if (tiku_remote_adopt(&app->remote, tiku_channel_fd(ch)) != 0) {
            tiku_channel_close(ch);
            app->channels[slot] = NULL;
            continue;
        }
                        tiku_channel_greet(ch);
        mount_over(app, slot);
    }
}

/** @brief Stop listening; the channels already taken in are untouched. */
void
boards_unlisten(desktop_t *app)
{
    if (app->board_listen > 0) {
        close(app->board_listen);
    }
    app->board_listen = -1;
}

/**
 * @brief Whether a board is already mounted from @p port.
 *
 * A port this desktop holds must not be opened again: two owners on one
 * cable corrupt each other, and the board would be mounted twice.
 */
int
port_is_mounted(desktop_t *app, const char *port)
{
    char want[PATH_MAX], real[PATH_MAX];
    int i, slots = tiku_mount_slots(app->backend);

    /* By what they resolve to: a scan names a port by its stable id,
     * -p by its node, and they are the same cable. */
    if (realpath(port, want) == NULL) {
        snprintf(want, sizeof want, "%s", port);
    }
    for (i = 0; i < slots; i++) {
        tiku_backend_t *b = tiku_mount_at(app->backend, i);
        char have[256];

        if (b == NULL || tiku_backend_tiku_port(b, have, sizeof have) <= 0) {
            continue;
        }
        if (realpath(have, real) == NULL) {
            snprintf(real, sizeof real, "%s", have);
        }
        if (strcmp(real, want) == 0) {
            return 1;
        }
    }
    return 0;
}


/** @brief Where a port really is: its tty leaf, ttyACM3. */
void
device_port_where(const char *port, char *out, size_t max)
{
    char real[PATH_MAX];
    const char *leaf;

    if (realpath(port, real) != NULL &&
        (leaf = strrchr(real, '/')) != NULL && leaf[1] != '\0') {
        snprintf(out, max, "%s", leaf + 1);
        return;
    }
    leaf = strrchr(port, '/');
    snprintf(out, max, "%s",
             (leaf != NULL && leaf[1] != '\0') ? leaf + 1 : port);
}

/* A port leaf this long or shorter is shown whole; a longer one keeps at
 * least this many of its last characters. */
#define PORT_LABEL_FULL 14
#define PORT_LABEL_TAIL 4

/** @brief Port leaf @p where without the "cu." or "tty." a Mac puts
 *         first. */
static const char *
port_unprefixed(const char *where)
{
    if (strncmp(where, "cu.", 3) == 0) {
        return where + 3;
    }
    if (strncmp(where, "tty.", 4) == 0) {
        return where + 4;
    }
    return where;
}

/**
 * @brief The name the Devices menu gives the port with leaf @p where among
 *        the @p n leaves in @p all: the leaf without "cu." or "tty.", and a
 *        long one cut to its letters, an ellipsis and as many of its last
 *        characters as tell it from the others ("usbmodem…69363").
 */
void
device_port_label(const char *where, const char *const *all, int n,
                  char *out, size_t max)
{
    const char *base = port_unprefixed(where);
    size_t len = strlen(base), lead = 0, tail;
    int i;

    while (lead < len && lead < 12 && (base[lead] < '0' || base[lead] > '9')) {
        lead++;
    }
    for (tail = PORT_LABEL_TAIL; tail < len; tail++) {
        int clash = 0;

        for (i = 0; i < n && !clash; i++) {
            const char *other = port_unprefixed(all[i]);
            size_t olen = strlen(other);

            clash = strcmp(other, base) != 0 && olen >= tail &&
                    strcmp(other + olen - tail, base + len - tail) == 0;
        }
        if (!clash) {
            break;
        }
    }
    if (len <= PORT_LABEL_FULL || lead + 3u + tail >= len) {
        snprintf(out, max, "%s", base);
    } else {
        snprintf(out, max, "%.*s\xe2\x80\xa6%s", (int)lead, base,
                 base + len - tail);
    }
}

/**
 * @brief Remember a port that nothing mounted, for the Devices menu to
 *        offer to connect.
 *
 * A scan knows only where the port is and passes no @p name; a look's
 * probe calls this again with the board's processor name.
 */
void
device_candidate_record(desktop_t *app, const char *port, const char *name)
{
    int i;

    if (app == NULL || port == NULL || port[0] == '\0') {
        return;
    }
    for (i = 0; i < app->ncandidates; i++) {
        if (strcmp(app->candidates[i].port, port) == 0) {
            if (name != NULL && name[0] != '\0') {
                snprintf(app->candidates[i].name,
                         sizeof app->candidates[i].name, "%s", name);
            }
            return;
        }
    }
    if (app->ncandidates >= TIKU_MOUNT_MAX) {
        return;
    }
    i = app->ncandidates++;
    snprintf(app->candidates[i].port, sizeof app->candidates[i].port,
             "%s", port);
    device_port_where(port, app->candidates[i].where,
                    sizeof app->candidates[i].where);
    snprintf(app->candidates[i].name, sizeof app->candidates[i].name,
             "%s", (name != NULL) ? name : "");
}

/**
 * @brief Replace the candidate list with the serial ports found, without
 *        opening any.  A port already mounted is connected, not a
 *        candidate.
 * @return how many candidates are on offer.
 */
int
device_scan(desktop_t *app)
{
    char scan[TIKU_MOUNT_MAX][256];
    int found, i;

    if (app == NULL) {
        return 0;
    }
    app->ncandidates = 0;
    found = tiku_tx_scan_serial(scan, TIKU_MOUNT_MAX);
    for (i = 0; i < found && i < TIKU_MOUNT_MAX; i++) {
        if (port_is_mounted(app, scan[i])) {
            continue;
        }
        device_candidate_record(app, scan[i], NULL);
    }
    return app->ncandidates;
}

/**
 * @brief Bring up the devices at boot.
 *
 * A port named with -p is mounted now, at @p at when that is given, and
 * the boot waits up to a minute for it.  With no port named the ports are
 * only listed: a board is mounted when the Devices menu connects it, not
 * when it is found.
 *
 * @return how many mounts are under /devices, or with no port named, how
 *         many ports are on offer.
 */
int
mount_devices(desktop_t *app, const char *port, int baud, const char *at)
{
    int i, mounted = 0;

    app->board_baud = baud;     /* for later looks, connects, reclaims */
    if (port != NULL && port[0] != '\0') {
        /* The loop does not run during boot: wait here for the probe,
         * counted with a look's accounting. */
        app->look.going = 1;
        app->look.gen++;
        app->look.session = -1;
        app->look.slot = NULL;
        app->look.pending = 0;
        app->look.count = 0;
        app->look.landed = 0;
        if (desktop_wire_probe(app, DESKTOP_ASK_CONNECT, port, baud, 0,
                               at, 1) == 0) {
            app->look.pending++;
            app->look.count++;
        }
        desktop_wire_wait(app, &app->look.pending, 60000L);
        app->look.going = 0;
        for (i = 0; i < TIKU_MOUNT_MAX; i++) {
            const char *prefix = tiku_mount_prefix_at(app->backend, i);

            mounted += (prefix != NULL &&
                        strncmp(prefix, "/devices/", 9) == 0);
        }
        return mounted;
    }
    /* No port named: list the ports and mount nothing. */
    return device_scan(app);
}

/**
 * @brief A board answered on @p port: mount it under /devices (or at
 *        @p at), make it current when no device is, and keep a channel
 *        for a board with a /gui.  The loop's side of a connect probe.
 * @return 1 when mounted.
 */
int
mount_landed(desktop_t *app, tiku_backend_t *dev, const char *port,
             const char *at)
{
    char name[64], prefix[TIKU_PATH_MAX];

    if (port_is_mounted(app, port)) {
        /* Another probe of this port mounted it first. */
        tiku_backend_close(dev);
        return 0;
    }
    if (at != NULL && at[0] == '/') {
        snprintf(prefix, sizeof prefix, "%s", at);
    } else {
        device_mount_name(dev, app->backend, name, sizeof name);
        snprintf(prefix, sizeof prefix, "/devices/%s", name);
    }
    /* A write fails after four seconds; a control's write is a few
     * bytes. */
    tiku_backend_tiku_set_write_timeout(dev, 4000);
    if (tiku_mount_add(app->backend, prefix, dev, 1) != 0) {
        tiku_trail("mount %s refused", prefix);
        tiku_backend_close(dev);
        desktop_devtray_failed(app, port, NULL, "the mount was refused");
        return 0;
    }
    tiku_trail("mount %s on %s", prefix, port);
    desktop_devtray_landed(app, port, prefix);
    /* A board mounted while no device is current becomes the current
     * device; a later board does not replace it. */
    if (app->current_device[0] == '\0' &&
        strncmp(prefix, "/devices/", 9) == 0) {
        device_set_current(app, prefix + 9);
    }
    /* A board with a /gui has windows: a channel beside its shell carries
     * them.  The stat is answered from the manifest read at the mount,
     * with no wire traffic. */
    {
        tiku_model_t gui;

        if (dev->ops->stat(dev, "/gui", &gui) == 0) {
            int slot = channel_keep(app,
                                    tiku_channel_open(
                                        tiku_backend_tiku_session(dev)));

            if (slot >= 0) {
                snprintf(app->channel_board[slot], TIKU_PATH_MAX, "%s",
                         prefix);
                /* Adopted into the remote listener here: a board
                 * connected after startup gets no other adoption.  At
                 * boot the listener is set up later, and channels_attach
                 * adopts every channel again. */
                (void)channel_attach(app, slot);
            }
        }
    }
    desktop_damage_all(app);
    return 1;
}
#endif


/**
 * @brief Raise an alert for each program launched from here that ended
 *        since the last call by a signal or with a nonzero status, unless
 *        that end was expected: its signal or exit status, and whether its
 *        exit report was saved.  At most four per call.
 */
static void
reap_deaths(desktop_t *app)
{
    tiku_trk_death_t gone[4];
    int n = tiku_trk_launch_reap(gone, 4), g;

    for (g = 0; g < n && g < 4; g++) {
        char says[256];

        if ((!gone[g].signal && !gone[g].status) || gone[g].expected) {
            continue;   /* a clean or expected exit raises nothing */
        }
        snprintf(says, sizeof says,
                 "%s stopped: %s %d. %s",
                 gone[g].name, gone[g].signal ? "signal" : "exit status",
                 gone[g].signal ? gone[g].signal : gone[g].status,
                 gone[g].report[0] ?
                 "An application exit report was saved in system reports." :
                 "The application exit report could not be saved.");
        tiku_alert_open(alert_model(app), TIKU_ALERT_WARN, 0, says,
                        tiku_str("alert.btn.ok"));
        desktop_damage_all(app);
    }
}

/**
 * @brief Every 400 ms: report programs that ended abnormally, refresh the
 *        Deskbar's recent lists, hand the stores' change notices to every
 *        window, act on volumes mounted and unmounted, and refresh the
 *        Info windows, the Deskbar and its replicants; every 3 s, re-run
 *        the live queries.
 */
void
desktop_notice_pump(desktop_t *app, desktop_loop_t *lp)
{
    int i;

        if (tiku_trk_now() - lp->last_pump > 400000) {
        int changed = 0;

                        reap_deaths(app);
        {
            /* The Deskbar's recent documents, folders and applications,
             * copied each pump from the lists the Open panel keeps
             * (DB-002). */
            static const tiku_trk_recent_t kWhich[] = {
                TIKU_TRK_RECENT_DOCS, TIKU_TRK_RECENT_FOLDERS,
                TIKU_TRK_RECENT_APPS
            };
            char paths[TIKU_TRK_DB_RECENT_MAX][TIKU_PATH_MAX];
            int k;

            for (k = 0; k < 3; k++) {
                int nr = tiku_trk_recent_list(app->store, kWhich[k],
                             TIKU_TRK_DB_RECENT_MAX, paths,
                             TIKU_TRK_DB_RECENT_MAX);

                tiku_trk_deskbar_set_recent(app->deskbar,
                    (tiku_trk_db_recent_t)k, paths, nr);
            }
        }
        /*
         * Drained once per pump for every reader: reading a device's
         * ring removes the records, so every window, the Desktop and the
         * panels are handed this one batch.
         */
                tiku_model_change_t ch[32];
        int nch = -1, complete = 1;
#if TIKU_SHELL_BOARD_SERVICES


        /* A board's records are its worker's last answer, a local
         * store's are read now; the next asking is posted once every
         * reader has had these. */
        nch = desktop_wire_changes(app, ch, 32, &complete);
#else


        /* Share one batch of filesystem changes across the open views. */
        if (app->backend->ops->changes != NULL) {
            nch = app->backend->ops->changes(app->backend, ch, 32,
                                               &complete);
        }
#endif

        for (i = 0; i < app->folder_count; i++) {
            folder_t *fl = &app->folders[i];
            unsigned nf;

            if (fl->win == NULL || !fl->win->open) {
                continue;
            }
            changed += tiku_trk_poseview_pump(fl->view, 5);
            /* Membership changes go through the notice handler, which
             * keeps the selection, the scroll position and the
             * arrangement. */
            nf = tiku_trk_poseview_poll(fl->view, tiku_trk_now(),
                                        ch, nch, complete);
            if ((nf & TIKU_TRK_NOTIFY_GONE) != 0u) {
                /* The folder is gone: its window closes, and fl->gone
                 * keeps its position from being saved, here and at
                 * teardown. */
                fl->gone = 1;
                close_window(app, fl->win);
                changed++;
            } else if (nf != TIKU_TRK_NOTIFY_NOTHING) {
                if ((nf & TIKU_TRK_NOTIFY_RETITLE) != 0u) {
                    tiku_trk_folder_title_for(prefs_model(app),
                        tiku_trk_poseview_path(fl->view),
                        fl->win->title, sizeof fl->win->title);
                    snprintf(fl->path, sizeof fl->path, "%s",
                             tiku_trk_poseview_path(fl->view));
                }
                changed++;
            }
        }
        changed += tiku_trk_desktop_view_poll(&app->desktop_view,
            tiku_trk_now(), ch, nch, complete);
        changed += tiku_trk_panel_window_poll(&app->file_panel,
            tiku_trk_now(), ch, nch, complete);
                changed += panels_poll(app, ch, nch);
#if TIKU_SHELL_BOARD_SERVICES

        desktop_wire_ask(app);
#endif

        {   /* The volumes mounted or unmounted since the last poll. */
            tiku_volchange_t vc[TIKU_VOLWATCH_MAX];
                        int nv;

            desktop_wire_volumes(app);  /* the watch reads a worker's scan */
            nv = tiku_volwatch_poll(&app->volwatch, vc, TIKU_VOLWATCH_MAX);
            int k;

            for (k = 0; k < nv; k++) {
                if (vc[k].renamed) {
                    /* Only the label changed: the Desktop union is
                     * rebuilt, which re-reads every volume's name
                     * (MA-021). */
                    tiku_trk_desktop_view_rebuild(&app->desktop_view);
                    changed++;
                    continue;
                }
                if (vc[k].mounted) {
                    /* A folder window showing the parent lists the
                     * volume through the poll above.  The Desktop union
                     * gains the volume's Desktop folder as a source
                     * (PVN-040, PVN-051). */
                    tiku_trk_desktop_view_rebuild(&app->desktop_view);
                    /* Half a second later the folders reload, when a
                     * link to the new volume resolves as a volume
                     * (PVN-042). */
                    (void)tiku_trk_run_later(app->loop,
                        refresh_after_mount, app, NULL, 500000);
                    changed++;
                    continue;
                }
                /* Unmounted: the Desktop union drops the volume's
                 * Desktop folder, so a source that has gone is not
                 * polled. */
                tiku_trk_desktop_view_rebuild(&app->desktop_view);
                /* A window on the volume closes, and its arrangement is
                 * not saved, since its disk has gone; a window showing
                 * rows from the volume drops those rows (PVN-043). */
                for (i = 0; i < app->folder_count; i++) {
                    folder_t *fl = &app->folders[i];

                    if (fl->win == NULL || !fl->win->open) {
                        continue;
                    }
                    if (tiku_trk_poseview_is_query(fl->view) &&
                        tiku_query_targets_device(
                            fl->run, (uint64_t)vc[k].v.dev)) {
                        /* The run is the query's subscription.  Stop it
                         * with the window so no watch survives a target
                         * volume disappearing (Q-046). */
                        tiku_query_stop(fl->run);
                        fl->run = NULL;
                        fl->live = 0;
                        fl->gone = 1;
                        close_window(app, fl->win);
                        changed++;
                    } else if (strncmp(fl->path, vc[k].v.mount,
                                strlen(vc[k].v.mount)) == 0) {
                        fl->gone = 1;
                        close_window(app, fl->win);
                        changed++;
                    } else if (tiku_trk_poseview_device_gone(fl->view,
                                   (uint64_t)vc[k].v.dev) > 0) {
                        changed++;
                    }
                }
                changed += tiku_trk_panel_window_device_gone(
                    &app->file_panel, vc[k].v.mount);
                changed += tiku_trk_info_windows_device_gone(
                    &app->info_windows, (uint64_t)vc[k].v.dev);
            }
        }
        changed += tiku_trk_info_windows_refresh(&app->info_windows,
                                                  tiku_trk_now());
#if TIKU_SHELL_BOARD_SERVICES

                changed += tiku_trk_deskbar_refresh_except(app->deskbar,
                                                   "/devices/");
        desktop_wire_replicants(app);
#else

                changed += tiku_trk_deskbar_refresh(app->deskbar);
#endif

        /* Live queries re-run at most every 3 s: a re-walk costs a
         * directory listing per subtree. */
        if (tiku_trk_now() - lp->last_requery > 3000000) {
            for (i = 0; i < app->folder_count; i++) {
                if (app->folders[i].win != NULL &&
                    app->folders[i].win->open) {
                    changed += refresh_results(app, &app->folders[i]);
                    if (app->folders[i].trash_union) {
                        changed += trash_union_refresh(app,
                                                       &app->folders[i]);
                    }
                }
            }
            changed += tiku_trk_desktop_view_trash_check(
                &app->desktop_view);
            lp->last_requery = tiku_trk_now();
        }
        lp->last_pump = tiku_trk_now();
        if (changed > 0) {
            desktop_damage_all(app);
        }
    }
}

/**
 * @brief Once a minute, when the machine is not busy: reap the stored
 *        searches nobody named, keeping those a window shows (Q-037,
 *        AW-090).
 */
void
desktop_sweep(desktop_t *app, desktop_loop_t *lp)
{
    int i;

    if (tiku_trk_now() - lp->last_sweep > 60000000) {
        /* The searches open in a window are kept.  The sweep itself
         * skips the first half hour of uptime. */
        const char *open[TIKU_WINDOW_MAX];
        int nopen = 0;

        for (i = 0; i < app->folder_count; i++) {
            if (app->folders[i].qname[0] != '\0' &&
                app->folders[i].win != NULL && app->folders[i].win->open) {
                open[nopen++] = app->folders[i].qname;
            }
        }
        if (!tiku_trk_task_system_busy()) {
            /* Deleting user data waits for both the half-hour hold-off
             * and an idle machine (AW-090). */
            (void)tiku_query_sweep(app->store, (int64_t)time(NULL),
                                       (tiku_trk_now() - lp->started_at) /
                                           1000000, open, nopen);
        }
        lp->last_sweep = tiku_trk_now();
    }
}

/**
 * @brief Step every view still loading by one batch: each open folder
 *        window, the file panel and the Desktop.
 */
void
desktop_device_pump(desktop_t *app)
{
    int i;

    for (i = 0; i < app->folder_count; i++) {
        /* One batch per window per turn, so the loop keeps turning while
         * a large folder arrives. */
        if (app->folders[i].win != NULL && app->folders[i].win->open &&
            tiku_trk_poseview_loading(app->folders[i].view)) {
                        (void)tiku_trk_poseview_load_step(app->folders[i].view,
                                              tiku_trk_now());
            desktop_damage_all(app);
        }
    }
    /* The file panel and the Desktop hold views too, and a listing that
     * arrives for either is stepped the same way. */
    if (app->file_panel.view != NULL &&
        tiku_trk_poseview_loading(app->file_panel.view)) {
        (void)tiku_trk_poseview_load_step(app->file_panel.view,
                                          tiku_trk_now());
        desktop_damage_all(app);
    }
    if (app->desktop_view.folder.view != NULL &&
        tiku_trk_poseview_loading(app->desktop_view.folder.view)) {
        (void)tiku_trk_poseview_load_step(app->desktop_view.folder.view,
                                          tiku_trk_now());
        desktop_damage_all(app);
    }
}
