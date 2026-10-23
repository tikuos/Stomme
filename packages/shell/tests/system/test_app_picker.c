/* Local-only picks and session reuse across the app/desktop boundary. */
#include <assert.h>
#include "../../system/desktop/tiku_trk_desktop_panels.c"
/** @brief The painter is not linked: a full frame is the flag, as before. */
void desktop_damage_all(desktop_t *app) { app->dirty = 1; }

static int panels, folder_panels, replies, saves;
static char panel_start[TIKU_PATH_MAX], answer[TIKU_PATH_MAX];

void tiku_trk_panel_window_open_file(tiku_trk_panel_window_t *p, void *target,
    int command, const char *start, const char *filter)
{
    (void)p; (void)target; (void)filter;
    assert(command == TIKU_TRK_CMD_APP_PICK);
    panels++; snprintf(panel_start, sizeof panel_start, "%s", start);
}

/** @brief The folder chooser: the panel's own way of asking. */
void tiku_trk_panel_window_open(tiku_trk_panel_window_t *p, void *target,
    int command, const char *start)
{
    (void)p; (void)target;
    assert(command == TIKU_TRK_CMD_APP_PICK);
    folder_panels++; snprintf(panel_start, sizeof panel_start, "%s", start);
}

/** @brief The panel put the saving way. */
void tiku_trk_panel_window_open_save(tiku_trk_panel_window_t *p, void *target,
    int command, const char *start, const char *name)
{
    (void)p; (void)target; (void)start; (void)name;
    assert(command == TIKU_TRK_CMD_APP_PICK);
    saves++;
}

int tiku_remote_tell(tiku_remote_listener_t *l, int session, const tiku_msg_t *m)
{
    (void)l; assert(session == 0 && tiku_msg_what(m) == TIKU_MSG_PICKED);
    snprintf(answer, sizeof answer, "%s", tiku_msg_find_string(m, "path", 0));
    replies++; return 1;
}

int main(void)
{
    desktop_t *app = calloc(1, sizeof *app); assert(app);
    tiku_backend_t fake = {0};
    const tiku_backend_ops_t ops = {.name = "device"};
    fake.ops = &ops;
    app->backend = tiku_mount_open(); assert(app->backend);
    assert(!tiku_mount_add(app->backend, "/", &fake, 0));
    assert(!tiku_mount_add(app->backend, "/local", tiku_backend_posix_open(), 1));
    app->remote.session[0].used = 1;
    app->remote.session[0].generation = 7;
    tiku_msg_t *m = tiku_msg_new(TIKU_MSG_PICK); assert(m);
    assert(tiku_msg_add_int32(m, "window", 1));
    assert(tiku_msg_add_int32(m, "mode", TIKU_APP_PICK_LOCAL_OPEN));
    assert(tiku_msg_add_string(m, "start", "/tmp"));
    desktop_remote_pick(app, 0, m);
    assert(panels == 1 && !strcmp(panel_start, "/local/tmp"));
    deliver_pick(app, "/local/tmp/document.md");
    assert(replies == 1 && !strcmp(answer, "/tmp/document.md"));

    desktop_remote_pick(app, 0, m);
    deliver_pick(app, "/data/device.md");
    assert(replies == 1 && strstr(app->status, "local files"));
    assert(app->pending_pick.kind == 0);
    desktop_remote_pick(app, 0, m);
    app->remote.session[0].generation++;
    deliver_pick(app, "/local/tmp/stale.md");
    assert(replies == 1 && app->pending_pick.kind == 0);
    desktop_remote_pick(app, 0, m);
    deliver_pick(app, NULL);
    assert(replies == 1 && app->pending_pick.kind == 0);
    /* A local folder: the folder chooser, mapped both ways, as files are. */
    tiku_msg_t *f = tiku_msg_new(TIKU_MSG_PICK); assert(f);
    assert(tiku_msg_add_int32(f, "window", 1));
    assert(tiku_msg_add_int32(f, "mode", TIKU_APP_PICK_LOCAL_FOLDER));
    assert(tiku_msg_add_string(f, "start", "/tmp"));
    desktop_remote_pick(app, 0, f);
    assert(folder_panels == 1 && panels == 4 &&
           !strcmp(panel_start, "/local/tmp"));
    deliver_pick(app, "/local/tmp/release-1.3");
    assert(replies == 2 && !strcmp(answer, "/tmp/release-1.3"));
    tiku_msg_free(f);
    assert(!tiku_mount_remove(app->backend, "/local"));
    desktop_remote_pick(app, 0, m);
    assert(app->pending_pick.kind == 0 && strstr(app->status, "No local"));
    /* A window asking again while its own question is up replaces that
     * question, so the answer belongs to the one it asked last; another
     * session is still refused while the panel is taken. */
    tiku_msg_t *o = tiku_msg_new(TIKU_MSG_PICK), *s = tiku_msg_new(TIKU_MSG_PICK);
    assert(o && s);
    assert(tiku_msg_add_int32(o, "window", 1) &&
           tiku_msg_add_int32(o, "mode", TIKU_APP_PICK_OPEN) &&
           tiku_msg_add_string(o, "start", "/data"));
    assert(tiku_msg_add_int32(s, "window", 1) &&
           tiku_msg_add_int32(s, "mode", TIKU_APP_PICK_SAVE) &&
           tiku_msg_add_string(s, "start", "/data") &&
           tiku_msg_add_string(s, "name", "notes.txt"));
    desktop_remote_pick(app, 0, o);
    assert(panels == 5 && app->pending_pick.kind == 2);
    app->remote.session[1].used = 1;
    desktop_remote_pick(app, 1, s);
    assert(saves == 0 && app->pending_pick.session == 0);
    desktop_remote_pick(app, 0, s);
    assert(saves == 1 && app->pending_pick.kind == 2 &&
           app->pending_pick.session == 0);
    desktop_remote_pick(app, 0, o);
    assert(panels == 6 && app->pending_pick.kind == 2);
    deliver_pick(app, "/data/notes.txt");
    assert(replies == 3 && !strcmp(answer, "/data/notes.txt"));
    tiku_msg_free(o); tiku_msg_free(s);
    tiku_msg_free(m); app->backend->ops->close(app->backend); free(app);
    puts("PASS: local picker namespace mapping, local folders, device "
         "refusal, cancellation, session reuse, a window's newer question");
}
