/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_clip.c - the file clipboard.
 *
 * The rules worth checking are the ones that follow from keying entries by
 * IDENTITY rather than by path: an item cannot be on the clipboard twice, a
 * cut item survives being renamed, and an entry whose file has gone is
 * dropped rather than pasted.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "tiku_trk_clip.h"

static int fails;

static void
check(int cond, const char *row, const char *what)
{
    printf("  [%s] %-9s %s\n", cond ? "PASS" : "FAIL", row, what);
    if (!cond) { fails++; }
}

/** @brief A model standing for a real file, with an identity. */
static tiku_model_t
node(const char *path, uint64_t id, tiku_kind_t kind)
{
    tiku_model_t m;
    const char *slash = strrchr(path, '/');

    memset(&m, 0, sizeof m);
    snprintf(m.path, sizeof m.path, "%s", path);
    snprintf(m.name, sizeof m.name, "%s", (slash != NULL) ? slash + 1 : path);
    m.node_id = id;
    m.kind = kind;
    return m;
}

int
main(void)
{
    tiku_trk_clip_t c;
    tiku_model_t set[3];
    const char *dir = "/tmp/trk_clip_test";
    char a[256], b[256];

    (void)mkdir(dir, 0755);
    snprintf(a, sizeof a, "%s/one.txt", dir);
    snprintf(b, sizeof b, "%s/two.txt", dir);
    { FILE *f = fopen(a, "w"); if (f != NULL) { (void)fputc('x', f); (void)fclose(f); } }
    { FILE *f = fopen(b, "w"); if (f != NULL) { (void)fputc('y', f); (void)fclose(f); } }

    printf("the file clipboard\n");
    tiku_trk_clip_init(&c);
    check(tiku_trk_clip_count(&c) == 0, "FS-051", "it starts empty");

    set[0] = node(a, 101u, TIKU_KIND_FILE);
    set[1] = node(b, 102u, TIKU_KIND_FILE);
    check(tiku_trk_clip_add(&c, set, 2, TIKU_TRK_CLIP_COPY, 1) == 2,
          "FS-051", "Copy puts the selection on it");
    check(tiku_trk_clip_mode_of(&c, 101u) == TIKU_TRK_CLIP_COPY, "FS-051",
          "and remembers that a Paste should copy");
    check(tiku_trk_clip_mode_of(&c, 999u) == TIKU_TRK_CLIP_NONE, "FS-051",
          "an item that is not on it has no mode");

    /* Cut over the same item replaces, it does not accumulate (FS-053). */
    check(tiku_trk_clip_add(&c, set, 1, TIKU_TRK_CLIP_MOVE, 0) == 2,
          "FS-053", "cutting an item already on the clipboard adds no "
          "second entry");
    check(tiku_trk_clip_mode_of(&c, 101u) == TIKU_TRK_CLIP_MOVE, "FS-053",
          "and the mode is the one from the later gesture");
    check(tiku_trk_clip_mode_of(&c, 102u) == TIKU_TRK_CLIP_COPY, "FS-051",
          "while the other entry keeps its own");

    /* Cut replaces the whole clipboard; Cut more appends (FS-051). */
    set[2] = node("/tmp/trk_clip_test/three.txt", 103u, TIKU_KIND_FILE);
    check(tiku_trk_clip_add(&c, &set[2], 1, TIKU_TRK_CLIP_MOVE, 1) == 1,
          "FS-051", "a plain Cut clears what was there");
    check(tiku_trk_clip_add(&c, set, 2, TIKU_TRK_CLIP_MOVE, 0) == 3,
          "FS-051", "and \"Cut more\" adds to it instead");

    /* The places cannot be cut (FS-052). */
    {
        tiku_model_t places[3];

        /* A volume that is not the root, so the exclusion has to be about
         * the kind rather than about the path. */
        places[0] = node("/mnt/stick", 200u, TIKU_KIND_VOLUME);
        places[1] = node("/tmp/.Trash", 201u, TIKU_KIND_TRASH);
        places[2] = node("/tmp/Desktop", 202u, TIKU_KIND_DESKTOP);
        check(tiku_trk_clip_add(&c, places, 3, TIKU_TRK_CLIP_MOVE, 1) == 0,
              "FS-052",
              "a volume, the Trash and the Desktop are silently excluded -- "
              "none of them could be pasted anywhere");
    }

    /* An entry follows its file (FS-060). */
    {
        char moved[256];

        snprintf(moved, sizeof moved, "%s/renamed.txt", dir);
        (void)tiku_trk_clip_add(&c, set, 2, TIKU_TRK_CLIP_MOVE, 1);
        check(tiku_trk_clip_moved(&c, 101u, moved), "FS-060",
              "a cut item that is renamed is followed");
        check(strcmp(tiku_trk_clip_at(&c, 0)->path, moved) == 0, "FS-060",
              "the entry points at where the file is now");
        check(tiku_trk_clip_mode_of(&c, 101u) == TIKU_TRK_CLIP_MOVE,
              "FS-060",
              "and it is still CUT -- following a file is not a new gesture");
        check(!tiku_trk_clip_moved(&c, 555u, moved), "FS-060",
              "a move of something not on the clipboard changes nothing");
    }

    /* And what a SHELL can call, which is told two paths and no identity
     * (FS-060). */
    {
        char there[256], back[256], inside[300], landed[256];
        tiku_model_t tree[1];

        snprintf(there, sizeof there, "%s/away.txt", dir);
        snprintf(back, sizeof back, "%s/renamed.txt", dir);
        (void)tiku_trk_clip_add(&c, set, 2, TIKU_TRK_CLIP_MOVE, 1);
        check(tiku_trk_clip_moved_path(&c, set[0].path, there, 0u) == 1,
              "FS-060",
              "an entry is followed by the path it used to have, the "
              "engine reporting nothing else");
        check(strcmp(tiku_trk_clip_at(&c, 0)->path, there) == 0, "FS-060",
              "the entry points at where the file is now");
        check(tiku_trk_clip_moved_path(&c, there, back, 909u) == 1 &&
              tiku_trk_clip_mode_of(&c, 909u) == TIKU_TRK_CLIP_MOVE,
              "FS-060",
              "and takes the identity it is given: a move across a device "
              "is a copy and a delete, so what arrives is a new node");
        check(tiku_trk_clip_moved_path(&c, "/nowhere/at/all", back, 0u) == 0,
              "FS-060",
              "a move of something not on the clipboard changes nothing");

        snprintf(inside, sizeof inside, "%s/box/held.txt", dir);
        snprintf(landed, sizeof landed, "%s/moved-box", dir);
        tree[0] = node(inside, 777u, TIKU_KIND_FILE);
        (void)tiku_trk_clip_add(&c, tree, 1, TIKU_TRK_CLIP_MOVE, 1);
        {
            char folder[256], want[300];

            snprintf(folder, sizeof folder, "%s/box", dir);
            snprintf(want, sizeof want, "%s/held.txt", landed);
            check(tiku_trk_clip_moved_path(&c, folder, landed, 0u) == 1 &&
                  strcmp(tiku_trk_clip_at(&c, 0)->path, want) == 0,
                  "FS-060",
                  "a folder that moved takes the cut items inside it "
                  "along: the entry is under the folder's new name");
        }
    }

    /* A dead entry is dropped rather than pasted (FS-058). */
    {
        int before = tiku_trk_clip_count(&c);

        (void)tiku_trk_clip_add(&c, set, 2, TIKU_TRK_CLIP_COPY, 1);
        check(tiku_trk_clip_prune(&c) == 0, "FS-058",
              "pruning leaves entries whose files are there");
        (void)unlink(a);
        check(tiku_trk_clip_prune(&c) == 1 &&
              tiku_trk_clip_count(&c) == 1, "FS-058",
              "and drops the one whose file has gone");
        check(tiku_trk_clip_mode_of(&c, 102u) == TIKU_TRK_CLIP_COPY,
              "FS-058", "leaving the rest of the clipboard alone");
        (void)before;
    }

    /* Escape clears it; Shift+Escape removes only the selection (FS-062). */
    {
        tiku_model_t two[1];

        { FILE *f = fopen(a, "w"); if (f != NULL) { (void)fputc('x', f); (void)fclose(f); } }
        (void)tiku_trk_clip_add(&c, set, 2, TIKU_TRK_CLIP_COPY, 1);
        two[0] = set[0];
        check(tiku_trk_clip_remove(&c, two, 1) == 1 &&
              tiku_trk_clip_count(&c) == 1, "FS-062",
              "removing one entry leaves the others");
        tiku_trk_clip_clear(&c);
        check(tiku_trk_clip_count(&c) == 0, "FS-062", "and clearing empties it");
    }

    /* A paste spends the cuts and leaves the copies (FS-055). */
    {
        int s0;

        (void)tiku_trk_clip_add(&c, set, 2, TIKU_TRK_CLIP_COPY, 1);
        (void)tiku_trk_clip_add(&c, &set[2], 1, TIKU_TRK_CLIP_MOVE, 0);
        s0 = c.serial;
        check(tiku_trk_clip_spend_cuts(&c) == 1, "FS-055",
              "pasting spends the CUT entries -- the one item that was to "
              "leave where it is");
        check(tiku_trk_clip_mode_of(&c, 103u) == TIKU_TRK_CLIP_COPY,
              "FS-055",
              "and what it becomes is a copy, so a second paste copies "
              "rather than moving a file that is no longer there");
        check(tiku_trk_clip_mode_of(&c, 101u) == TIKU_TRK_CLIP_COPY &&
              tiku_trk_clip_count(&c) == 3, "FS-055",
              "the copies are untouched and nothing is dropped: emptying "
              "the clipboard would lose them with the cut");
        check(c.serial != s0, "FS-055",
              "and the change is announced, so anything drawing cut items "
              "stops dimming them");
        check(tiku_trk_clip_spend_cuts(&c) == 0, "FS-055",
              "a second paste has no cuts left to spend");
    }

    /* The serial moves whenever the contents do, so a view can tell (FS-061). */
    {
        int s0;

        s0 = c.serial;
        (void)tiku_trk_clip_add(&c, set, 2, TIKU_TRK_CLIP_COPY, 1);
        check(c.serial != s0, "FS-061",
              "the clipboard says when it has changed, so a window drawing "
              "cut items knows to stop");
        s0 = c.serial;
        tiku_trk_clip_clear(&c);
        check(c.serial != s0, "FS-061", "and says so when it is emptied");
    }

    (void)unlink(a);
    (void)unlink(b);
    (void)rmdir(dir);
    printf("\n%s\n", fails ? "FAILURES" : "the clipboard holds");
    return fails ? 1 : 0;
}
