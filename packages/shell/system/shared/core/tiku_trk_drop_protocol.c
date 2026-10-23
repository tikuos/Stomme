/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_drop_protocol.c - reading a foreign drag's offer, choosing
 * what its drop does, and writing a clipping.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_drop_protocol.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int
tiku_trk_foreign_offer_parse(tiku_trk_foreign_offer_t *o, const char *actions,
                             const char *types, int promise)
{
    if (o == NULL) return -1;
    memset(o, 0, sizeof *o);
    o->promise = promise != 0;
    if (actions == NULL || actions[0] == '\0') o->actions = TIKU_TRK_DROP_ACTION_COPY;
    else {
        if (strstr(actions, "copy") != NULL) o->actions |= TIKU_TRK_DROP_ACTION_COPY;
        if (strstr(actions, "move") != NULL) o->actions |= TIKU_TRK_DROP_ACTION_MOVE;
        if (strstr(actions, "link") != NULL) o->actions |= TIKU_TRK_DROP_ACTION_LINK;
        if (strstr(actions, "trash") != NULL) o->actions |= TIKU_TRK_DROP_ACTION_TRASH;
    }
    if (types != NULL) {
        o->has_refs = strstr(types, "application/x-vnd.Be-directory-ref") != NULL ||
                      strstr(types, "text/uri-list") != NULL;
        o->has_text = strstr(types, "text/plain") != NULL;
        o->has_bitmap = strstr(types, "image/") != NULL;
    }
    return 0;
}

tiku_trk_drop_result_t
tiku_trk_foreign_drop(const tiku_trk_foreign_offer_t *o, int directory,
                      int trash, int root, unsigned requested)
{
    tiku_trk_drop_result_t r = { TIKU_TRK_DROP_REPLY_REJECT, 0, 0, 0 };
    unsigned action;
    if (o == NULL || root || (!directory && !trash)) return r;
    if (trash && (o->actions & TIKU_TRK_DROP_ACTION_TRASH)) {
        r.reply = TIKU_TRK_DROP_REPLY_TRASH_TARGET;
        r.chosen_action = TIKU_TRK_DROP_ACTION_TRASH;
        r.accepted = 1;
        return r;
    }
    if (!directory) return r;
    action = requested & o->actions;
    if (action == 0) action = (o->actions & TIKU_TRK_DROP_ACTION_COPY) ?
                               TIKU_TRK_DROP_ACTION_COPY : o->actions;
    if (action & TIKU_TRK_DROP_ACTION_MOVE) {
        r.reply = TIKU_TRK_DROP_REPLY_MOVE_TARGET;
        r.chosen_action = TIKU_TRK_DROP_ACTION_MOVE;
    } else if (action & TIKU_TRK_DROP_ACTION_COPY) {
        r.reply = TIKU_TRK_DROP_REPLY_COPY_TARGET;
        r.chosen_action = TIKU_TRK_DROP_ACTION_COPY;
    } else if (action & TIKU_TRK_DROP_ACTION_LINK) {
        r.reply = TIKU_TRK_DROP_REPLY_COPY_TARGET;
        r.chosen_action = TIKU_TRK_DROP_ACTION_LINK;
    } else return r;
    r.create_clipping = o->promise || o->has_text || o->has_bitmap;
    r.accepted = o->has_refs || o->has_text || o->has_bitmap || o->promise;
    if (!r.accepted) r.reply = TIKU_TRK_DROP_REPLY_REJECT;
    return r;
}

/**
 * @brief The path a clipping lands at in @p dir: @p name, else an Untitled
 *        one, and from @p serial 1 on the name followed by serial + 1.
 *
 * @return 0, or -1 when it does not fit in @p max.
 */
static int
join_clip_path(char *out, size_t max, const char *dir, const char *name,
               int serial)
{
    const char *base = name != NULL && name[0] != '\0' ? name :
                       (serial == 0 ? "Untitled clipping" : "Untitled bitmap");
    int n;

    if (serial == 0) {
        n = snprintf(out, max, "%s/%s", dir, base);
    } else {
        n = snprintf(out, max, "%s/%s %d", dir, base, serial + 1);
    }

    return n > 0 && (size_t)n < max ? 0 : -1;
}

int
tiku_trk_foreign_clip_create(const char *directory, const char *name,
                             const void *data, size_t size, int bitmap,
                             char *out, size_t out_size)
{
    int fd = -1;
    int serial;
    const char *default_name = bitmap ? "Untitled bitmap" :
                                "Untitled clipping";

    if (directory == NULL || directory[0] == '\0' || data == NULL ||
        out == NULL || out_size == 0u || (bitmap && size > 1024u * 1024u)) {
        return -1;
    }
    for (serial = 0; serial < 10000; serial++) {
        char path[1024];
        ssize_t written;

        if (join_clip_path(path, sizeof path, directory,
                           (name != NULL && name[0] != '\0') ? name :
                               default_name, serial) != 0) {
            return -1;
        }
        fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0644);
        if (fd >= 0) {
            written = write(fd, data, size);
            if (written != (ssize_t)size) {
                (void)close(fd);
                (void)unlink(path);
                return -1;
            }
            if (close(fd) != 0) {
                (void)unlink(path);
                return -1;
            }
            snprintf(out, out_size, "%s", path);
            return 0;
        }
        if (errno != EEXIST) {
            return -1;
        }
    }
    return -1;
}
