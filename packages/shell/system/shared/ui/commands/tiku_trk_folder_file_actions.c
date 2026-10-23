/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_folder_file_actions.c - the folder commands that change the
 * filesystem: trash and delete, duplicate, link, identify, restore, the
 * clipboard and paste, drops, and a new folder.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_strings.h"
#include "tiku_trk_folder_file_actions.h"
#include "tiku_trk_shortcuts.h"
#include "tiku_volume.h"
#include <stdio.h>
#include <string.h>

/**
 * @brief The clipboard follows a file the engine has just moved (FS-060).
 *
 * The identity is read again at the new path: a move across devices leaves
 * a different node holding the file.
 */
static void
clip_follow(void *context, const char *from, const char *to)
{
    tiku_trk_folder_file_actions_t *controller = context;
    tiku_model_t model;
    uint64_t now = 0u;

    if (controller == NULL || controller->clip == NULL) {
        return;
    }
    if (controller->backend != NULL &&
        controller->backend->ops->stat(controller->backend, to,
                                       &model) == 0) {
        now = model.node_id;
    }
    (void)tiku_trk_clip_moved_path(controller->clip, from, to, now);
}

void
tiku_trk_folder_file_actions_init(
    tiku_trk_folder_file_actions_t *controller, tiku_fs_t *fs,
    tiku_backend_t *backend, tiku_trk_clip_t *clip, tiku_trk_drag_t *pending,
    char *drop_destination, tiku_alert_t *alert,
    tiku_trk_folder_prefs_fn open_prefs, void *prefs_context,
    char *status, size_t status_size,
    tiku_trk_folder_close_under_fn close_under, void *close_context)
{
    if (controller == NULL) {
        return;
    }
    *controller = (tiku_trk_folder_file_actions_t){
        .fs = fs,
        .backend = backend,
        .clip = clip,
        .pending = pending,
        .drop_destination = drop_destination,
        .alert = alert,
        .open_prefs = open_prefs,
        .prefs_context = prefs_context,
        .status = status,
        .status_size = status_size,
        .close_under = close_under,
        .close_context = close_context
    };
    tiku_fs_set_moved(fs, clip_follow, controller);
}

int
tiku_trk_folder_file_actions_selection(
    const tiku_trk_folder_window_t *folder,
    char paths[][TIKU_PATH_MAX], const char **selection, int maximum)
{
    int i, count, selected = 0;

    if (folder == NULL || folder->view == NULL) {
        return 0;
    }
    count = tiku_trk_poseview_count(folder->view);
    for (i = 0; i < count && selected < maximum; i++) {
        tiku_trk_pose_t *pose = tiku_trk_poseview_at(folder->view, i);

        if (pose == NULL || !pose->selected) {
            continue;
        }
        snprintf(paths[selected], TIKU_PATH_MAX, "%s",
                 pose->model.path);
        selection[selected] = paths[selected];
        selected++;
    }
    return selected;
}

/** @brief Close every window under a selected path, through the hook. */
static void
close_selected(tiku_trk_folder_file_actions_t *controller,
               const char *const *selection, int count)
{
    int i;

    if (controller->close_under == NULL) {
        return;
    }
    for (i = 0; i < count; i++) {
        controller->close_under(controller->close_context, selection[i]);
    }
}

/**
 * @brief What the command acts on: the window's selection, or the one path
 *        a caller named in place of it.
 */
static int
selection_of(tiku_trk_folder_file_actions_t *controller,
             tiku_trk_folder_window_t *folder,
             char paths[][TIKU_PATH_MAX], const char **selection,
             int maximum)
{
    if (controller->only_path != NULL) {
        if (maximum < 1) {
            return 0;
        }
        snprintf(paths[0], TIKU_PATH_MAX, "%s", controller->only_path);
        selection[0] = paths[0];
        return 1;
    }
    return tiku_trk_folder_file_actions_selection(folder, paths, selection,
                                                  maximum);
}

/**
 * @brief Link the selection beside itself (FS-009, CW-035).
 *
 * Into the first item's folder; Shift asks for relative links.
 */
static void
link_selection(tiku_trk_folder_file_actions_t *controller,
               tiku_trk_folder_window_t *folder, int command)
{
    char paths[64][TIKU_PATH_MAX];
    const char *selection[64];
    int count = selection_of(controller, folder, paths, selection, 64);
    char dir[TIKU_PATH_MAX];
    const char *slash;

    if (count == 0) {
        return;
    }
    slash = strrchr(selection[0], '/');
    if (slash == NULL) {
        return;
    }
    {
        size_t keep = (slash == selection[0]) ? 1u
                    : (size_t)(slash - selection[0]);

        if (keep >= sizeof dir) {
            return;
        }
        memcpy(dir, selection[0], keep);
        dir[keep] = '\0';
    }
    (void)tiku_fs_link(controller->fs, selection, count, dir,
                           command == TIKU_TRK_CMD_CREATE_REL_LINK);
    snprintf(controller->status, controller->status_size,
             "linking %d item%s", count, count == 1 ? "" : "s");
}

/**
 * @brief Read the selection's type again (CW-042's Identify).
 *
 * Each selected entry is stat'ed again and its row takes the fresh model;
 * the status line counts the types that changed.
 */
static void
identify_selection(tiku_trk_folder_file_actions_t *controller,
                   tiku_trk_folder_window_t *folder)
{
    char paths[64][TIKU_PATH_MAX];
    const char *selection[64];
    int count = selection_of(controller, folder, paths, selection, 64);
    int i, told = 0;

    if (count == 0 || folder == NULL || folder->view == NULL ||
        controller->backend == NULL) {
        return;
    }
    for (i = 0; i < count; i++) {
        tiku_model_t fresh;
        int k, n = tiku_trk_poseview_count(folder->view);

        if (controller->backend->ops->stat(controller->backend, selection[i],
                                           &fresh) != 0) {
            continue;
        }
        for (k = 0; k < n; k++) {
            tiku_trk_pose_t *p = tiku_trk_poseview_at(folder->view, k);

            if (p == NULL || strcmp(p->model.path, selection[i]) != 0) {
                continue;
            }
            if (strcmp(fresh.type, p->model.type) != 0) {
                told++;
            }
            p->model = fresh;
            (void)tiku_desc_parse(fresh.facts.meta, &p->desc);
            break;
        }
    }
    snprintf(controller->status, controller->status_size,
             (told > 0) ? "identified %d item%s, %d changed"
                        : "identified %d item%s", count,
             (count == 1) ? "" : "s", told);
}

/** @brief Copy each item beside itself, under a fresh name (FS-007). */
static void
duplicate_selection(tiku_trk_folder_file_actions_t *controller,
                    tiku_trk_folder_window_t *folder)
{
    char paths[64][TIKU_PATH_MAX];
    const char *selection[64];
    int count = selection_of(controller, folder, paths, selection, 64);

    if (count == 0) {
        return;
    }
    (void)tiku_fs_duplicate(controller->fs, selection, count);
    snprintf(controller->status, controller->status_size,
             "duplicating %d item%s", count, count == 1 ? "" : "s");
}

/** @brief Destroy the selection outright, saying that it cannot be undone. */
static void
delete_outright(tiku_trk_folder_file_actions_t *controller,
                tiku_trk_folder_window_t *folder)
{
    char paths[64][TIKU_PATH_MAX];
    const char *selection[64];
    int count = selection_of(controller, folder, paths, selection, 64);

    if (count == 0) {
        return;
    }
    snprintf(controller->status, controller->status_size,
             "deleting %d item%s -- this cannot be undone", count,
             count == 1 ? "" : "s");
    (void)tiku_fs_delete(controller->fs, selection, count);
    close_selected(controller, selection, count);
}

/**
 * @brief Deselect the Trash before anything is counted (PVL-085): the
 *        Trash cannot be trashed.
 */
static void
prune_trash_pose(tiku_trk_folder_window_t *folder)
{
    int i, n;

    if (folder == NULL || folder->view == NULL) {
        return;
    }
    n = tiku_trk_poseview_count(folder->view);
    for (i = 0; i < n; i++) {
        tiku_trk_pose_t *p = tiku_trk_poseview_at(folder->view, i);

        if (p != NULL && p->selected &&
            p->model.kind == TIKU_KIND_TRASH) {
            p->selected = 0;
        }
    }
}

/**
 * @brief Selected volumes are unmounted, not trashed (FS-041).
 *
 * Each is taken out of the selection and unmounted, with an alert for a
 * refusal such as the boot volume's, before the rest of the selection is
 * trashed or deleted.
 */
static void
unmount_selected_volumes(tiku_trk_folder_file_actions_t *controller,
                         tiku_trk_folder_window_t *folder)
{
    tiku_volumes_t vols;
    int scanned = 0;
    int i, n;

    if (folder == NULL || folder->view == NULL) {
        return;
    }
    n = tiku_trk_poseview_count(folder->view);
    for (i = 0; i < n; i++) {
        tiku_trk_pose_t *p = tiku_trk_poseview_at(folder->view, i);
        char why[200];

        if (p == NULL || !p->selected ||
            (p->model.kind != TIKU_KIND_VOLUME &&
             p->model.kind != TIKU_KIND_ROOT)) {
            continue;
        }
        p->selected = 0;
        if (!scanned) {
            scanned = (tiku_volumes_scan(&vols) > 0) ? 1 : -1;
        }
        if (scanned < 0) {
            continue;
        }
        why[0] = '\0';
        if (!tiku_volume_may_unmount(&vols, p->model.path, why,
                                         sizeof why)) {
            if (why[0] != '\0') {
                tiku_alert_open(controller->alert, TIKU_ALERT_STOP,
                    TIKU_TRK_ALERT_TAG_REFUSAL, why, "Cancel");
            }
        } else if (tiku_volume_unmount(&vols, p->model.path, NULL,
                       NULL, why, sizeof why) == TIKU_UNMOUNT_OK) {
            snprintf(controller->status, controller->status_size,
                     "unmounted \"%.60s\"", p->model.name);
        } else if (why[0] != '\0') {
            tiku_alert_open(controller->alert, TIKU_ALERT_STOP,
                TIKU_TRK_ALERT_TAG_REFUSAL, why, "Cancel");
        }
    }
}

/** @brief Move the selection to the Trash, refusing what must not go. */
static void
move_to_trash(tiku_trk_folder_file_actions_t *controller,
              tiku_trk_folder_window_t *folder, int forced)
{
    char paths[64][TIKU_PATH_MAX];
    const char *selection[64];
    int count;
    int i;

    prune_trash_pose(folder);
    count = selection_of(controller, folder, paths, selection, 64);

    if (count == 0) {
        return;
    }
    if (count == 0) {
        return;
    }
    snprintf(controller->status, controller->status_size,
             "moving %d item%s to the Trash", count,
             count == 1 ? "" : "s");
    for (i = 0; i < count; i++) {
        tiku_confirm_t confirmation =
            tiku_fs_confirm_move(selection[i]);

        if (confirmation == TIKU_CONFIRM_OVERRIDE && !forced) {
            char message[300];

            snprintf(message, sizeof message,
                "\"%.60s\" is a system folder.  Hold Shift and choose "
                "Move to confirm.", selection[i]);
            tiku_alert_open(controller->alert, TIKU_ALERT_STOP,
                TIKU_TRK_ALERT_TAG_REFUSAL, message, "Cancel");
            return;
        }
        if (confirmation == TIKU_CONFIRM_ASK) {
            snprintf(controller->status, controller->status_size,
                "'%.60s' holds settings -- moving it anyway", selection[i]);
        }
    }
    (void)tiku_fs_trash(controller->fs, selection, count);
    close_selected(controller, selection, count);
}

void
tiku_trk_folder_file_actions_answer_empty(
    tiku_trk_folder_file_actions_t *controller)
{
    int count;

    if (controller == NULL) {
        return;
    }
    count = tiku_fs_empty_trash(controller->fs);
    snprintf(controller->status, controller->status_size,
             count > 0 ? "emptying the Trash: %d item%s"
                       : "the Trash is empty",
             count, count == 1 ? "" : "s");
}

void
tiku_trk_folder_file_actions_answer_delete(
    tiku_trk_folder_file_actions_t *controller,
    tiku_trk_folder_window_t *folder, tiku_trk_delete_answer_t answer)
{
    if (controller == NULL) {
        return;
    }
    if (controller->asked_path[0] != '\0') {
        /* The question was about one item named by path: the answer acts
         * on it, whatever window has the focus now. */
        controller->only_path = controller->asked_path;
        folder = NULL;
    } else if (folder == NULL) {
        return;
    }
    if (answer == TIKU_TRK_DELETE_ANSWER_TRASH) {
        move_to_trash(controller, folder, 0);
    } else if (answer == TIKU_TRK_DELETE_ANSWER_DELETE) {
        delete_outright(controller, folder);
    }
    controller->only_path = NULL;
    controller->asked_path[0] = '\0';
}

/**
 * @brief Delete the selection the way the store's rule for @p command
 *        says: to the Trash, outright with Shift, or after a question.
 */
static void
delete_selection(tiku_trk_folder_file_actions_t *controller,
                 tiku_trk_folder_window_t *folder, int command)
{
    char paths[64][TIKU_PATH_MAX];
    const char *selection[64];
    int count;
    int shift = command == TIKU_TRK_CMD_DELETE;

    prune_trash_pose(folder);
    unmount_selected_volumes(controller, folder);
    count = selection_of(controller, folder, paths, selection, 64);

    if (count == 0) {
        return;
    }
    if (tiku_fs_delete_question(controller->fs, selection[0], shift) ==
        TIKU_DELETE_STRAIGHT) {
        if (shift) {
            delete_outright(controller, folder);
        } else {
            /* No Trash to offer, or already in one: deleted outright,
             * without a question. */
            delete_outright(controller, folder);
        }
        return;
    }
    if (controller->alert == NULL) {
        /* With no alert layer to ask, the selection goes to the Trash
         * (FS-046 applies to the multi-window shell only). */
        move_to_trash(controller, folder, 0);
        return;
    }
    /* The question offers Cancel, Move to Trash and Delete (FS-046);
     * Cancel is first so Escape lands on it. */
    {
        char message[300];

        snprintf(message, sizeof message,
                 tiku_str("alert.msg.delete_items"), count,
                 count == 1 ? "" : "s");
        tiku_alert_open(controller->alert, TIKU_ALERT_WARN,
                            TIKU_TRK_ALERT_TAG_DELETE, message,
                            tiku_str("alert.btn.trash_delete"));
        controller->alert->dflt = 1;   /* Move to Trash */
        snprintf(controller->asked_path, sizeof controller->asked_path,
                 "%s", controller->only_path != NULL ?
                       controller->only_path : "");
    }
}

/** @brief Put the selection back from the Trash, and say what happened. */
static void
restore_selection(tiku_trk_folder_file_actions_t *controller,
                  tiku_trk_folder_window_t *folder)
{
    char paths[64][TIKU_PATH_MAX], error[200];
    const char *selection[64];
    int count = selection_of(controller, folder, paths, selection, 64);
    int restored;

    error[0] = '\0';
    restored = tiku_fs_restore(controller->fs, selection, count,
                                   error, sizeof error);
    if (restored > 0) {
        snprintf(controller->status, controller->status_size,
                 "put %d item%s back", restored,
                 restored == 1 ? "" : "s");
    } else if (error[0] != '\0') {
        snprintf(controller->status, controller->status_size, "%s", error);
    }
}

/**
 * @brief Put the selection on the clipboard as a cut or a copy, added to
 *        what is there when @p command says so.  With only_path set, the
 *        item at that path is put there instead.
 */
static void
copy_selection(tiku_trk_folder_file_actions_t *controller,
               tiku_trk_folder_window_t *folder, int command)
{
    tiku_model_t selection[64];
    int i, selected = 0, count;
    int cut = command == TIKU_TRK_CMD_CUT ||
              command == TIKU_TRK_CMD_CUT_MORE;
    int more = command == TIKU_TRK_CMD_CUT_MORE ||
               command == TIKU_TRK_CMD_COPY_MORE;

    if (controller->only_path != NULL) {
        if (controller->backend != NULL &&
            controller->backend->ops != NULL &&
            controller->backend->ops->stat != NULL &&
            controller->backend->ops->stat(controller->backend,
                                            controller->only_path,
                                            &selection[0]) == 0) {
            selected = 1;
        }
        count = 0;
    } else {
        count = (folder != NULL && folder->view != NULL)
                    ? tiku_trk_poseview_count(folder->view) : 0;
    }
    for (i = 0; i < count && selected < 64; i++) {
        tiku_trk_pose_t *pose = tiku_trk_poseview_at(folder->view, i);

        if (pose != NULL && pose->selected) {
            selection[selected++] = pose->model;
        }
    }
    (void)tiku_trk_clip_add(controller->clip, selection, selected,
        cut ? TIKU_TRK_CLIP_MOVE : TIKU_TRK_CLIP_COPY, !more);
    snprintf(controller->status, controller->status_size,
             tiku_str("status.clipboard"),
             tiku_trk_clip_count(controller->clip),
             tiku_trk_clip_count(controller->clip) == 1 ? "" : "s");
}

/**
 * @brief Finish a drop whose menu was answered: copy, move or link the
 *        pending items into the chosen destination, then forget them.
 */
static void
finish_drop(tiku_trk_folder_file_actions_t *controller, int command)
{
    const char *selection[TIKU_TRK_DRAG_MAX];
    int i, count = 0;

    for (i = 0; i < controller->pending->count; i++) {
        selection[count++] = controller->pending->path[i];
    }
    if (count == 0 || controller->drop_destination[0] == '\0') {
        return;
    }
    if (command == TIKU_TRK_CMD_COPY_HERE) {
        (void)tiku_fs_copy(controller->fs, selection, count,
            controller->drop_destination, TIKU_CONFLICT_ASK);
        snprintf(controller->status, controller->status_size,
                 "copying %d item%s", count, count == 1 ? "" : "s");
    } else if (command == TIKU_TRK_CMD_MOVE_HERE) {
        (void)tiku_fs_move(controller->fs, selection, count,
            controller->drop_destination, TIKU_CONFLICT_ASK);
        snprintf(controller->status, controller->status_size,
                 "moving %d item%s", count, count == 1 ? "" : "s");
    } else {
        (void)tiku_fs_link(controller->fs, selection, count,
            controller->drop_destination,
            command == TIKU_TRK_CMD_REL_LINK_HERE);
        snprintf(controller->status, controller->status_size,
                 "linking %d item%s", count, count == 1 ? "" : "s");
    }
    tiku_trk_drag_reset(controller->pending);
    controller->drop_destination[0] = '\0';
}

/** @brief Make a new folder here, and say its name or why there is none. */
static void
new_folder(tiku_trk_folder_file_actions_t *controller,
           tiku_trk_folder_window_t *folder)
{
    char made[TIKU_PATH_MAX], error[200];

    error[0] = '\0';
    if (controller->fs != NULL &&
        tiku_fs_new_folder(controller->fs, folder->path, made,
                               sizeof made, error, sizeof error) == 0) {
        const char *slash = strrchr(made, '/');

        snprintf(controller->status, controller->status_size,
                 "created '%.60s'", slash != NULL ? slash + 1 : made);
    } else {
        snprintf(controller->status, controller->status_size, "%.150s",
                 error[0] != '\0' ? error : "could not create a new folder");
    }
}

/**
 * @brief Paste the clipboard into the window's folder, or into the folder
 *        holding only_path when it is set: cuts move, copies copy, and the
 *        links form pastes links instead.
 */
static void
paste(tiku_trk_folder_file_actions_t *controller,
      tiku_trk_folder_window_t *folder, int command)
{
    char paths[64][TIKU_PATH_MAX], here[64][TIKU_PATH_MAX];
    char path_parent[TIKU_PATH_MAX];
    const char *selection[64], *same[64];
    const char *destination;
    /* A clipboard holding both cuts and copies is two operations: the cuts
     * move and the copies copy (FS-055). */
    const char *moves[64], *copies[64];
    int move_count = 0, copy_count = 0;
    int i, count = 0, same_count = 0;
    int links = command == TIKU_TRK_CMD_PASTE_LINKS;

    if (controller->only_path != NULL) {
        const char *slash = strrchr(controller->only_path, '/');
        size_t parent_length;

        if (slash == NULL || slash == controller->only_path) {
            snprintf(path_parent, sizeof path_parent, "/");
        } else {
            parent_length = (size_t)(slash - controller->only_path);
            if (parent_length >= sizeof path_parent) {
                parent_length = sizeof path_parent - 1u;
            }
            memcpy(path_parent, controller->only_path, parent_length);
            path_parent[parent_length] = '\0';
        }
        destination = path_parent;
    } else if (folder != NULL) {
        destination = folder->path;
    } else {
        return;
    }
    (void)tiku_trk_clip_prune(controller->clip);
    for (i = 0; i < tiku_trk_clip_count(controller->clip) && count < 64;
         i++) {
        const tiku_trk_clip_entry_t *entry =
            tiku_trk_clip_at(controller->clip, i);
        const char *slash = strrchr(entry->path, '/');
        size_t parent_length = slash != NULL
                             ? (size_t)(slash - entry->path) : 0u;

        if (!links && slash != NULL && strlen(destination) == parent_length &&
            strncmp(destination, entry->path, parent_length) == 0) {
            if (entry->mode != TIKU_TRK_CLIP_MOVE && same_count < 64) {
                snprintf(here[same_count], sizeof here[same_count], "%s",
                         entry->path);
                same[same_count] = here[same_count];
                same_count++;
            }
            continue;
        }
        snprintf(paths[count], sizeof paths[count], "%s", entry->path);
        selection[count] = paths[count];
        if (entry->mode == TIKU_TRK_CLIP_MOVE) {
            moves[move_count++] = paths[count];
        } else {
            copies[copy_count++] = paths[count];
        }
        count++;
    }
    if (same_count > 0) {
        (void)tiku_fs_duplicate(controller->fs, same, same_count);
        snprintf(controller->status, controller->status_size,
                 "duplicating %d item%s already here", same_count,
                 same_count == 1 ? "" : "s");
        if (count == 0) {
            return;
        }
    }
    if (count == 0) {
        snprintf(controller->status, controller->status_size,
                 "the clipboard is empty");
        return;
    }
    if (tiku_fs_in_trash(controller->fs, destination) &&
        (links || copy_count > 0)) {
        snprintf(controller->status, controller->status_size, "%s",
                 links ? "Sorry, you can't create links in the Trash."
                       : "Sorry, you can't copy items to the Trash.");
        return;
    }
    if (links) {
        (void)tiku_fs_link(controller->fs, selection, count,
                               destination, 0);
        snprintf(controller->status, controller->status_size,
                 "linking %d item%s here", count,
                 count == 1 ? "" : "s");
        return;
    }
    if (move_count > 0) {
        (void)tiku_fs_move(controller->fs, moves, move_count,
                               destination, TIKU_CONFLICT_ASK);
    }
    if (copy_count > 0 && move_count == 0) {
        (void)tiku_fs_copy(controller->fs, copies, copy_count,
                               destination, TIKU_CONFLICT_ASK);
    } else if (copy_count > 0) {
        /* The engine runs one job at a time and refuses a start while
         * busy, so the copy half of a mixed paste waits for
         * tiku_trk_folder_file_actions_pump(). */
        int k;

        for (k = 0; k < copy_count; k++) {
            snprintf(controller->deferred[k], TIKU_PATH_MAX, "%s",
                     copies[k]);
            controller->deferred_paths[k] = controller->deferred[k];
        }
        controller->deferred_count = copy_count;
        snprintf(controller->deferred_dest, sizeof controller->deferred_dest,
                 "%s", destination);
    }
    /* A pasted cut becomes a copy entry, and the copied entries stay
     * (FS-055). */
    (void)tiku_trk_clip_spend_cuts(controller->clip);
    if (move_count > 0 && copy_count > 0) {
        snprintf(controller->status, controller->status_size,
                 "moving %d item%s and copying %d here", move_count,
                 move_count == 1 ? "" : "s", copy_count);
    } else {
        snprintf(controller->status, controller->status_size,
                 "%s %d item%s here", move_count > 0 ? "moving" : "copying",
                 count, count == 1 ? "" : "s");
    }
}

int
tiku_trk_folder_file_actions_pump(tiku_trk_folder_file_actions_t *controller)
{
    if (controller == NULL || controller->deferred_count <= 0 ||
        tiku_fs_busy(controller->fs)) {
        return 0;
    }
    (void)tiku_fs_copy(controller->fs, controller->deferred_paths,
                           controller->deferred_count,
                           controller->deferred_dest,
                           TIKU_CONFLICT_ASK);
    controller->deferred_count = 0;
    return 1;
}

int
tiku_trk_folder_file_actions_invoke(
    tiku_trk_folder_file_actions_t *controller,
    tiku_trk_folder_window_t *folder, int command)
{
    if (controller == NULL ||
        (controller->only_path == NULL &&
         (folder == NULL || folder->view == NULL))) {
        return 0;
    }
    switch (command) {
    case TIKU_TRK_CMD_MOVE_TO_TRASH:
    case TIKU_TRK_CMD_DELETE:
        delete_selection(controller, folder, command);
        return 1;
    case TIKU_TRK_CMD_DUPLICATE:
        duplicate_selection(controller, folder);
        return 1;
    case TIKU_TRK_CMD_CREATE_LINK:
    case TIKU_TRK_CMD_CREATE_REL_LINK:
        link_selection(controller, folder, command);
        return 1;
    case TIKU_TRK_CMD_IDENTIFY:
        identify_selection(controller, folder);
        return 1;
    case TIKU_TRK_CMD_RESTORE:
        restore_selection(controller, folder);
        return 1;
    case TIKU_TRK_CMD_EMPTY_TRASH:
        /* Asked first, with Cancel as the default button, so Return does
         * not empty the Trash. */
        if (controller->alert != NULL) {
            tiku_alert_open(controller->alert, TIKU_ALERT_WARN,
                TIKU_TRK_ALERT_TAG_EMPTY_TRASH,
                tiku_str("alert.msg.empty_trash"),
                tiku_str("alert.btn.empty_trash"));
            controller->alert->dflt = 0;  /* Cancel. */
            return 1;
        }
        tiku_trk_folder_file_actions_answer_empty(controller);
        return 1;
    case TIKU_TRK_CMD_CUT:
    case TIKU_TRK_CMD_CUT_MORE:
    case TIKU_TRK_CMD_COPY:
    case TIKU_TRK_CMD_COPY_MORE:
        copy_selection(controller, folder, command);
        return 1;
    case TIKU_TRK_CMD_MOVE_HERE:
    case TIKU_TRK_CMD_COPY_HERE:
    case TIKU_TRK_CMD_LINK_HERE:
    case TIKU_TRK_CMD_REL_LINK_HERE:
        finish_drop(controller, command);
        return 1;
    case TIKU_TRK_CMD_PREFERENCES:
        if (controller->open_prefs == NULL) {
            return 0;           /* this shell shows them another way */
        }
        controller->open_prefs(controller->prefs_context);
        return 1;
    case TIKU_TRK_CMD_NEW_FOLDER:
        new_folder(controller, folder);
        return 1;
    case TIKU_TRK_CMD_PASTE:
    case TIKU_TRK_CMD_PASTE_LINKS:
        paste(controller, folder, command);
        return 1;
    default:
        return 0;
    }
}

int
tiku_trk_folder_file_actions_invoke_path(
    tiku_trk_folder_file_actions_t *controller, const char *path,
    int command)
{
    int handled;

    if (controller == NULL || path == NULL || path[0] == '\0') {
        return 0;
    }
    controller->only_path = path;
    handled = tiku_trk_folder_file_actions_invoke(controller, NULL, command);
    controller->only_path = NULL;
    return handled;
}
