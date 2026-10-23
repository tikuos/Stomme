/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_volume.c - the mount table, and what it means for an operation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "tiku_fs.h"
#include "tiku_volume.h"
#include "tiku_trk_deskicons.h"

static int fails;

/** @brief Stands in for the windows that save themselves before a volume
 *         goes: it records that it was asked, and for which device. */
static void
note_before(dev_t dev, void *ctx)
{
    int *saved = ctx;

    (void)dev;
    if (saved != NULL) {
        (*saved)++;
    }
}


static void
check(int cond, const char *row, const char *what)
{
    printf("  [%s] %-9s %s\n", cond ? "PASS" : "FAIL", row, what);
    if (!cond) { fails++; }
}

/**
 * @brief Run under a home of this test's own.
 *
 * The Trash-union cases resolve trash directories from $HOME, and
 * resolving one CREATES it -- on a host where ~/.Trash is the account's
 * real Trash, that is a directory this test has no business making.
 */
static void
own_home(void)
{
    char fixture[512];
    const char *real = getenv("HOME");

    if (real == NULL || real[0] == '\0') {
        return;
    }
    snprintf(fixture, sizeof fixture, "%s/.tiku_trk_vol_home", real);
    (void)mkdir(fixture, 0755);
    (void)setenv("HOME", fixture, 1);
}

int
main(void)
{
    tiku_volumes_t vs;
    const tiku_volume_t *v;
    char why[200];
    int n, i, boots = 0;

    own_home();
    printf("reading the partitions\n");
    {
        /* The Mount menu asks on every frame a folder window draws, so
         * asking again must be cheap and must say the same thing: on
         * Darwin each disk is a mach round trip, kept between asks. */
        tiku_partition_t first[32], again[32];
        struct timespec a, b;
        int pn = tiku_partitions_scan(first, 32), qn = 0, same = 1, k;
        double ms;

        clock_gettime(CLOCK_MONOTONIC, &a);
        for (k = 0; k < 50; k++) {
            qn = tiku_partitions_scan(again, 32);
        }
        clock_gettime(CLOCK_MONOTONIC, &b);
        ms = (double)(b.tv_sec - a.tv_sec) * 1e3 +
             (double)(b.tv_nsec - a.tv_nsec) / 1e6;
        for (k = 0; k < pn && k < qn; k++) {
            same = same && strcmp(first[k].name, again[k].name) == 0 &&
                   first[k].kb == again[k].kb &&
                   first[k].mounted == again[k].mounted &&
                   strcmp(first[k].mount, again[k].mount) == 0;
        }
        check(pn == qn && same, "CW-034",
              "asked again, the partitions are the same partitions");
        printf("  50 scans in %.1f ms\n", ms);
        check(ms < 100.0, "CW-034",
              "and asking fifty times costs under a tenth of a second");
    }
    printf("reading the mount table\n");
    n = tiku_volumes_scan(&vs);
    check(n > 0, "MA-021", "at least one volume is mounted");
    for (i = 0; i < vs.n; i++) {
        boots += vs.v[i].is_boot;
    }
    check(boots == 1, "FS-041",
          "exactly one of them is the boot volume: the system booted from "
          "one place");
    {   /* The kernel's own bookkeeping filesystems are not places files
         * live, and listing them as volumes would fill the Disks window
         * with things nobody can browse. */
        int junk = 0;

        for (i = 0; i < vs.n; i++) {
            if (strcmp(vs.v[i].mount, "/proc") == 0 ||
                strcmp(vs.v[i].mount, "/sys") == 0) {
                junk++;
            }
        }
        check(junk == 0, "TS-046",
              "and /proc and /sys are not offered as volumes");
    }

    printf("\nwhich volume a path is on\n");
    v = tiku_volume_of(&vs, "/tmp");
    check(v != NULL, "FS-012", "a real path resolves to a volume");
    check(tiku_volume_of(&vs, "/no/such/path") == NULL, "FS-012",
          "and one that does not exist resolves to none");
    check(tiku_volume_is_root(&vs, "/"), "FS-059",
          "\"/\" is a volume's mount point");
    check(!tiku_volume_is_root(&vs, "/tmp/somefile"), "FS-059",
          "an ordinary path is not");

    printf("\nwhat a volume refuses (FS-045, FS-041)\n");
    check(tiku_volume_may_write(&vs, "/tmp", 1, why, sizeof why),
          "FS-045", "a writable volume takes a change");
    check(tiku_volume_may_write(&vs, "/tmp", 0, why, sizeof why),
          "FS-013", "and reading is always allowed -- a copy FROM a "
          "read-only volume is still a copy");
    check(!tiku_volume_may_unmount(&vs, "/", why, sizeof why),
          "FS-041", "the boot volume can never be unmounted");
    check(strstr(why, "boot volume") != NULL, "FS-041",
          "and says so, naming it");
    check(!tiku_volume_may_unmount(&vs, "/tmp/notavolume", why,
                                       sizeof why),
          "FS-041", "while something that is not a volume is not this "
          "gesture at all");

    {   /* A synthesised read-only volume: the rule must not depend on
         * this host happening to have one mounted. */
        tiku_volumes_t ro;

        memset(&ro, 0, sizeof ro);
        ro.n = 1;
        snprintf(ro.v[0].mount, sizeof ro.v[0].mount, "/");
        snprintf(ro.v[0].name, sizeof ro.v[0].name, "Disc");
        ro.v[0].read_only = 1;
        ro.v[0].dev = vs.v[0].dev;
        {
            /* Point it at a path that really is on that device. */
            const tiku_volume_t *w;

            ro.v[0].dev = tiku_volume_of(&vs, "/tmp")->dev;
            w = tiku_volume_of(&ro, "/tmp");
            check(w != NULL && w->read_only, "FS-045",
                  "a read-only volume is recognised as one");
            check(!tiku_volume_may_write(&ro, "/tmp", 1, why,
                                             sizeof why),
                  "FS-045", "and refuses a move or a delete");
            check(strstr(why, "read-only") != NULL &&
                  strstr(why, "Disc") != NULL, "FS-045",
                  "naming the volume, because a user with several mounted "
                  "needs to know which refused");
            check(tiku_volume_may_write(&ro, "/tmp", 0, why,
                                            sizeof why),
                  "FS-013", "but still allows reading, which is what makes "
                  "a move off it become a copy");
        }
    }



    printf("\nwhich volumes get an icon (PVN-041)\n");
    {
        tiku_volumes_t t;

        memset(&t, 0, sizeof t);
        t.n = 4;
        snprintf(t.v[0].mount, sizeof t.v[0].mount, "/");
        t.v[0].is_boot = 1;
        snprintf(t.v[1].mount, sizeof t.v[1].mount, "/media/stick");
        snprintf(t.v[2].mount, sizeof t.v[2].mount, "/media/stick/inner");
        snprintf(t.v[3].mount, sizeof t.v[3].mount, "/media/stick-two");

        check(tiku_volume_shows_icon(&t, &t.v[0]), "PVN-041",
              "the boot volume always shows: everything is inside it, and "
              "it is the disk");
        check(tiku_volume_shows_icon(&t, &t.v[1]), "PVN-041",
              "a volume mounted under the boot volume still shows -- "
              "otherwise nothing but the boot volume ever would");
        check(!tiku_volume_shows_icon(&t, &t.v[2]), "PVN-041",
              "one mounted INSIDE another does not: it is a detail of that "
              "volume, not a second disk");
        check(tiku_volume_shows_icon(&t, &t.v[3]), "PVN-041",
              "while one whose path merely starts with another's does -- "
              "the test is on a whole path component");
    }

    printf("\na volume renamed under the port's feet (MA-021)\n");
    {
        tiku_volumes_t was, now;
        tiku_volchange_t ch[4];
        int n;

        memset(&was, 0, sizeof was);
        memset(&now, 0, sizeof now);
        was.n = now.n = 1;
        snprintf(was.v[0].mount, sizeof was.v[0].mount, "/media/disk");
        snprintf(now.v[0].mount, sizeof now.v[0].mount, "/media/disk");
        was.v[0].dev = now.v[0].dev = 42;
        snprintf(was.v[0].name, sizeof was.v[0].name, "Untitled");
        snprintf(now.v[0].name, sizeof now.v[0].name, "Photos");
        n = tiku_volwatch_diff(&was, &now, ch, 4);
        check(n == 1 && ch[0].renamed && !ch[0].mounted &&
              strcmp(ch[0].v.name, "Photos") == 0, "MA-021",
              "a label edited in place is ONE rename notice carrying the "
              "new name, not a departure and an arrival");
        snprintf(now.v[0].name, sizeof now.v[0].name, "Untitled");
        check(tiku_volwatch_diff(&was, &now, ch, 4) == 0, "MA-021",
              "and an unchanged volume reports nothing");
    }

    printf("\nthe space bar (PVL-072, TS-054)\n");
    {
        tiku_volume_t s1;
        int pct = -1;

        memset(&s1, 0, sizeof s1);
        s1.total = 1000;
        s1.avail = 500;
        check(tiku_volume_space(&s1, &pct) == TIKU_SPACE_USED &&
              pct == 50, "PVL-072",
              "a half-full volume reads half full, in the ordinary colour");

        s1.avail = 60;              /* 6% free */
        check(tiku_volume_space(&s1, &pct) == TIKU_SPACE_USED &&
              pct == 94, "PVL-072",
              "six per cent free is not yet a warning");
        s1.avail = 40;              /* 4% free */
        check(tiku_volume_space(&s1, &pct) == TIKU_SPACE_WARNING,
              "TS-054", "four per cent free is");
        s1.avail = 50;              /* exactly 5% */
        check(tiku_volume_space(&s1, &pct) == TIKU_SPACE_USED,
              "TS-054", "and exactly five per cent is not -- the threshold "
              "is BELOW, and it is measured on what is left rather than on "
              "a rounded used-share");

        s1.total = 0;
        s1.avail = 0;
        check(tiku_volume_space(&s1, &pct) == TIKU_SPACE_FREE &&
              pct == 0, "PVL-072",
              "a volume that will not say its size shows an empty bar, not "
              "a full one -- guessing full would warn about every device "
              "whose driver has no free-space call");

        s1.total = 3;
        s1.avail = 0;
        check(tiku_volume_space(&s1, &pct) == TIKU_SPACE_WARNING &&
              pct == 100, "PVL-072", "and a full one is full");
    }

    printf("\na whole disk is not an item (FS-041)\n");
    {
        tiku_backend_t *b = tiku_backend_posix_open();
        tiku_fs_t *fs = tiku_fs_new(b);
        char msg[240];
        const char *one[1];

        /* "/" is a mount point on any host this runs on. */
        check(tiku_fs_check_move(fs, "/", "/tmp", msg, sizeof msg) ==
              TIKU_REFUSE_VOLUME, "FS-041",
              "a volume is refused by the gate every copy and move passes "
              "-- and NOT as a failure afterwards: a mount point moved off "
              "its own device gets EXDEV, and the fallback copies every "
              "byte of the disk and then deletes what it copied from");
        check(strstr(msg, "disk") != NULL &&
              strstr(msg, "unmounted") != NULL, "FS-041",
              "and it says what the gesture MEANS -- unmount -- rather "
              "than reporting an error for something it should not have "
              "attempted");
        check(tiku_fs_check_move(fs, "/tmp", "/", msg, sizeof msg) ==
              TIKU_REFUSE_VOLUME ||
              tiku_fs_check_move(fs, "/etc", "/tmp", msg,
                                     sizeof msg) == TIKU_REFUSE_NONE,
              "FS-041",
              "an ordinary directory is not refused by this rule");
        one[0] = "/";
        (void)tiku_fs_trash(fs, one, 1);
        {
            const tiku_progress_t *pr = tiku_fs_progress(fs);

            check(pr->skipped == 1 && strstr(pr->error, "disk") != NULL,
                  "FS-041",
                  "and the Trash refuses it too, which the copy/move gate "
                  "cannot say because it never runs for a trash or a "
                  "delete -- a disk trashed as a directory is a disk "
                  "emptied into its own Trash");
        }
        tiku_fs_free(fs);
        tiku_backend_close(b);
    }

    printf("\nwatching what is mounted (PVN-008)\n");
    {
        tiku_volwatch_t w;
        tiku_volchange_t ch[TIKU_VOLWATCH_MAX];

        {   /* Never looked before: the first poll is the baseline, not a
             * report that every volume on the machine has just arrived. */
            tiku_volwatch_t fresh;

            memset(&fresh, 0, sizeof fresh);
            check(tiku_volwatch_poll(&fresh, ch,
                                         TIKU_VOLWATCH_MAX) == 0,
                  "PVN-008",
                  "the first look reports nothing: it IS the baseline, and "
                  "a shell that opened to a flood of mount notices would "
                  "act on every disk in the machine at startup");
            check(fresh.primed && fresh.was.n > 0, "PVN-008",
                  "having taken one");
        }
        tiku_volwatch_init(&w);
        check(tiku_volwatch_poll(&w, ch, TIKU_VOLWATCH_MAX) == 0,
              "PVN-008",
              "a poll with nothing moved reports nothing");
        {   /* One taken out of the baseline by hand reads as an unmount. */
            tiku_volume_t kept = w.was.v[w.was.n - 1];

            w.was.n--;
            check(tiku_volwatch_poll(&w, ch, TIKU_VOLWATCH_MAX) == 1
                  && ch[0].mounted == 1 &&
                  strcmp(ch[0].v.mount, kept.mount) == 0, "PVN-008",
                  "a volume the baseline has not seen reads as a mount");
            check(tiku_volwatch_poll(&w, ch,
                                         TIKU_VOLWATCH_MAX) == 0,
                  "PVN-008",
                  "and only once: the baseline moves whether or not the "
                  "caller had room, or a change dropped for want of space "
                  "would be reported for ever");
        }
        {   /* The same mount point with a different device is two events. */
            tiku_volwatch_t w2;

            tiku_volwatch_init(&w2);
            w2.was.v[0].dev ^= 0xffu;
            {
                int n = tiku_volwatch_poll(&w2, ch,
                                               TIKU_VOLWATCH_MAX);
                int went = 0, came = 0, k;

                for (k = 0; k < n; k++) {
                    if (ch[k].mounted) { came++; } else { went++; }
                }
                check(went == 1 && came == 1, "PVN-008",
                      "a mount point whose DEVICE changed is one volume "
                      "leaving and another arriving, not a volume that "
                      "stayed -- which is what an eject-and-replace is");
            }
        }
    }

    printf("\na disk's name is the disk's (MA-052)\n");
    {
        tiku_backend_t *b2 = tiku_backend_posix_open();
        tiku_fs_t *fs2 = tiku_fs_new(b2);
        char err[240];

        check(tiku_fs_rename(fs2, "/", "Boot", err, sizeof err) != 0 &&
              strstr(err, "disk") != NULL, "MA-052",
              "renaming a volume is refused rather than attempted: the "
              "entry it is mounted on is not its name, so renaming the "
              "entry would move the mount point and leave the disk called "
              "what it was");
        tiku_fs_free(fs2);
        tiku_backend_close(b2);
    }

    printf("\na share is not a disk (IV-006)\n");
    {
        tiku_volume_t s2;
        tiku_model_t m;

        check(tiku_volume_shared_type("nfs4") &&
              tiku_volume_shared_type("cifs") &&
              tiku_volume_shared_type("fuse.sshfs"), "IV-006",
              "a filesystem served over a network is recognised by its "
              "TYPE -- nothing about the mount point says it");
        check(!tiku_volume_shared_type("ext4") &&
              !tiku_volume_shared_type("vfat") &&
              !tiku_volume_shared_type(""), "IV-006",
              "and an ordinary one is not");

        memset(&s2, 0, sizeof s2);
        memset(&m, 0, sizeof m);
        m.kind = TIKU_KIND_VOLUME;
        s2.shared = 1;
        m.facts.vol_flags = tiku_volume_flags(&s2);
        check(strcmp(tiku_model_icon_name(&m), "share") == 0, "IV-006",
              "a share gets the share art, which the set has always "
              "carried and nothing ever selected");
        s2.shared = 0;
        s2.is_boot = 1;
        m.facts.vol_flags = tiku_volume_flags(&s2);
        check(strcmp(tiku_model_icon_name(&m), "root") == 0, "IV-006",
              "the boot volume gets the root art -- read off the volume "
              "table rather than off the path, so a boot volume mounted "
              "somewhere unusual is still the boot volume");
        s2.is_boot = 0;
        m.facts.vol_flags = tiku_volume_flags(&s2);
        check(strcmp(tiku_model_icon_name(&m), "volume") == 0, "IV-006",
              "and any other disk gets the plain one");
    }

    printf("\none Trash per filesystem (FS-081, PVN-027)\n");
    {
        tiku_backend_t *b3 = tiku_backend_posix_open();
        tiku_fs_t *fs3 = tiku_fs_new(b3);
        char dirs[TIKU_VOLUME_MAX][TIKU_PATH_MAX];
        int n = tiku_fs_trash_dirs(fs3, dirs, TIKU_VOLUME_MAX);
        int i, k, dupes = 0;

        check(tiku_fs_prepare_trash(fs3) >= 0, "FS-081",
              "startup preparation is an explicit pass over writable mounts");

        check(n > 0, "FS-081",
              "every writable volume has a Trash of its own -- which is "
              "what keeps trashing a rename rather than a copy");
        for (i = 1; i < n; i++) {
            for (k = 0; k < i; k++) {
                if (strcmp(dirs[i], dirs[k]) == 0) { dupes++; }
            }
        }
        check(dupes == 0, "PVN-027",
              "and each of this machine's appears once");
        {   /* A table under the test's control, so the two rules can be
             * ASKED rather than hoped for: whatever this machine has
             * mounted may happen to satisfy them by accident. */
            tiku_volumes_t made;
            char got[8][TIKU_PATH_MAX];
            int m;

            memset(&made, 0, sizeof made);
            snprintf(made.v[0].mount, sizeof made.v[0].mount, "%s",
                     getenv("HOME"));
            snprintf(made.v[1].mount, sizeof made.v[1].mount, "%s",
                     getenv("HOME"));   /* the same place twice */
            snprintf(made.v[2].mount, sizeof made.v[2].mount, "%s",
                     getenv("HOME"));
            made.v[2].read_only = 1;
            made.n = 3;
            m = tiku_fs_trash_dirs_of(fs3,
                    (const struct tiku_volumes *)&made, got, 8);
            check(m == 1, "PVN-027",
                  "three mounts, one writable place: the Trash is a UNION "
                  "of places, and a place listed twice would show every "
                  "item in it twice");
            memset(&made, 0, sizeof made);
            snprintf(made.v[0].mount, sizeof made.v[0].mount, "%s",
                     getenv("HOME"));
            made.v[0].read_only = 1;
            made.n = 1;
            {   /* PVN-040: which volume earns a Desktop pose.  A table
                 * under the test's control, so every gate can be asked. */
                tiku_volumes_t t;

                memset(&t, 0, sizeof t);
                snprintf(t.v[0].mount, sizeof t.v[0].mount, "/");
                t.v[0].is_boot = 1;
                t.v[0].persistent = 1;
                snprintf(t.v[1].mount, sizeof t.v[1].mount, "/data");
                t.v[1].persistent = 1;
                snprintf(t.v[2].mount, sizeof t.v[2].mount, "/scratch");
                t.v[2].persistent = 0;      /* a tmpfs */
                snprintf(t.v[3].mount, sizeof t.v[3].mount, "/net");
                t.v[3].persistent = 1;
                t.v[3].shared = 1;
                t.n = 4;
                check(!tiku_trk_deskicons_volume_pose(&t, &t.v[0], 1, 1),
                      "PVN-040", "the boot volume is already the Disks "
                      "pose, not a second icon");
                check(tiku_trk_deskicons_volume_pose(&t, &t.v[1], 1, 1),
                      "PVN-040", "an ordinary persistent volume earns one");
                check(!tiku_trk_deskicons_volume_pose(&t, &t.v[2], 1, 1),
                      "PVN-040", "a RAM filesystem does not -- an icon for "
                      "a thing that forgets");
                check(!tiku_trk_deskicons_volume_pose(&t, &t.v[3], 1, 0) &&
                      tiku_trk_deskicons_volume_pose(&t, &t.v[3], 1, 1),
                      "PVN-040", "a shared volume follows its own setting");
                check(!tiku_trk_deskicons_volume_pose(&t, &t.v[1], 0, 1),
                      "PVN-040", "and the master switch turns them all off");
            }
            check(tiku_fs_trash_dirs_of(fs3,
                      (const struct tiku_volumes *)&made, got, 8) == 0,
                  "FS-081",
                  "and a read-only volume gets none at all: nothing can be "
                  "trashed onto it, and asking would try to make a "
                  "directory on a disc");
        }
        tiku_fs_free(fs3);
        tiku_backend_close(b3);
    }

    printf("\nunmounting, and what has to happen first (AW-096, Q-046)\n");
    {
        tiku_volumes_t vs2;
        char why[128];
        static int saved_for;

        struct stat sb_root, sb_other;

        /* Real paths on real devices: a volume is found by DEVICE, so a
         * table of invented ones would answer NULL for everything and the
         * rules below would never be reached.  The harness names one it
         * mounted; without it, /dev/shm is a volume of its own on every
         * Linux. */
        const char *other = getenv("TRK_VOLUME2");

        if (other == NULL || other[0] == '\0') {
            other = "/dev/shm";
        }
        memset(&vs2, 0, sizeof vs2);
        if (stat("/", &sb_root) != 0 || stat(other, &sb_other) != 0 ||
            sb_root.st_dev == sb_other.st_dev) {
            printf("  (skipped: this host has no second device to test "
                   "against)\n");
            goto after_unmount;
        }
        vs2.n = 2;
        snprintf(vs2.v[0].mount, sizeof vs2.v[0].mount, "/");
        snprintf(vs2.v[0].name, sizeof vs2.v[0].name, "Boot");
        vs2.v[0].is_boot = 1;
        vs2.v[0].dev = sb_root.st_dev;
        snprintf(vs2.v[1].mount, sizeof vs2.v[1].mount, "%s", other);
        snprintf(vs2.v[1].name, sizeof vs2.v[1].name, "stick");
        vs2.v[1].removable = 1;
        vs2.v[1].dev = sb_other.st_dev;

        saved_for = 0;
        check(tiku_volume_unmount(&vs2, "/", note_before, &saved_for,
                                      why, sizeof why) ==
              TIKU_UNMOUNT_REFUSED && why[0] != '\0', "AW-096",
              "the boot volume is refused, and said -- the system is "
              "running from it");
        check(saved_for == 0, "AW-096",
              "and nothing was saved for it: a refusal must not have made "
              "the windows write themselves out first");
        saved_for = 0;
        {
            tiku_unmount_t r =
                tiku_volume_unmount(&vs2, other, note_before,
                                        &saved_for, why, sizeof why);

            check(r != TIKU_UNMOUNT_REFUSED, "AW-096",
                  "a removable volume is not refused by the rules -- "
                  "whether the system lets go of it is a different "
                  "answer");
            check(saved_for == 1, "AW-096",
                  "and everything open on it saves BEFORE it goes: "
                  "afterwards there is nowhere to write, and the "
                  "arrangement is lost the moment it leaves");
        }
        check(tiku_volume_unmount(&vs2, "/nowhere", note_before,
                                      &saved_for, why, sizeof why) ==
              TIKU_UNMOUNT_REFUSED, "AW-096",
              "a path that is no volume is not an unmount at all");
after_unmount:
        (void)0;
    }

    printf("\n%s\n", fails ? "FAILURES" : "the volume model holds");
    return fails ? 1 : 0;
}
