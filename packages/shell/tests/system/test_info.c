/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_info.c - the Get Info window's three tabs and what keeps them true.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifdef __linux__
#define _GNU_SOURCE 1           /* statx() and its STATX_BTIME */
#endif
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "tiku_attr.h"
#include "tiku_trk_info.h"
#include "tiku_mount.h"
#include "tiku_thumb.h"

static int fails;

static void
check(int cond, const char *row, const char *what)
{
    printf("  [%s] %-9s %s\n", cond ? "PASS" : "FAIL", row, what);
    if (!cond) { fails++; }
}

static char work[256];

/* A backend that can look but not touch, so the window's "can these be
 * changed" question has a second answer to give.  A real one is a device
 * whose capability bits are its own to declare. */
static tiku_backend_t *lookalike;

static int
ro_stat(tiku_backend_t *b, const char *path, tiku_model_t *out)
{
    (void)b;
    return lookalike->ops->stat(lookalike, path, out);
}

static const tiku_backend_ops_t RO_OPS = {
    "readonly", ro_stat, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL
};

static void
sh(const char *fmt, ...)
{
    char cmd[1024];
    va_list ap;

    va_start(ap, fmt);
    (void)vsnprintf(cmd, sizeof cmd, fmt, ap);
    va_end(ap);
    if (system(cmd) != 0) { /* the caller checks the effect */ }
}

/**
 * @brief The birth time the host reports for @p path, in seconds, or 0
 *        when its filesystem reports none.
 */
static long long
host_birth(const char *path)
{
#if defined(__linux__) && defined(STATX_BTIME)
    struct statx sx;

    if (statx(AT_FDCWD, path, 0, STATX_BTIME, &sx) == 0 &&
        (sx.stx_mask & STATX_BTIME) != 0u) {
        return (long long)sx.stx_btime.tv_sec;
    }
#elif defined(__APPLE__)
    struct stat sb;

    if (stat(path, &sb) == 0 && sb.st_birthtimespec.tv_sec > 0) {
        return (long long)sb.st_birthtimespec.tv_sec;
    }
#else
    (void)path;
#endif
    return 0;
}

/** @brief The value of the first field of kind @p want, or "". */
static const char *
field_of(tiku_trk_info_t *in, tiku_store_t *st, tiku_trk_info_field_t want,
         const char **label)
{
    static char value[TIKU_TRK_INFO_VAL_MAX];
    int i;

    for (i = 0; i < TIKU_TRK_INFO_FIELDS; i++) {
        if (tiku_trk_info_field(in, st, i, label, value, sizeof value) ==
                want) {
            return value;
        }
    }
    value[0] = '\0';
    return value;
}

int
main(void)
{
    tiku_backend_t *b = tiku_backend_posix_open();
    tiku_store_t *st = tiku_store_memory_open();
    tiku_trk_info_t in;
    char path[512], err[160], buf[TIKU_PATH_MAX], sel[TIKU_NAME_MAX];
    const char *label = NULL;

    lookalike = b;
    snprintf(work, sizeof work, "/tmp/trk-info-%d", (int)getpid());
    sh("rm -rf %s && mkdir -p %s/deep/deeper", work, work);
    sh("head -c 3000 /dev/zero > %s/big.bin", work);
    sh("head -c 100 /dev/zero > %s/deep/small.bin", work);
    sh("head -c 7 /dev/zero > %s/deep/deeper/tiny.bin", work);

    printf("what the window is about (AW-065, AW-066)\n");
    snprintf(path, sizeof path, "%s/big.bin", work);
    check(tiku_trk_info_open(&in, b, path) == 0, "AW-065",
          "a window opens on an entry that is there");
    {
        tiku_trk_info_t missing;

        check(tiku_trk_info_open(&missing, b, "/no/such/thing") != 0,
              "AW-065", "and refuses to open on one that is not, rather "
              "than showing a window full of nothing");
    }
    tiku_trk_info_title(&in, buf, sizeof buf);
    check(strcmp(buf, "big.bin info") == 0, "AW-066",
          "the title names the entry, since one window is one node");
    check(strcmp(tiku_trk_info_tab_name(TIKU_TRK_INFO_TAB_INFO),
                 "Information") == 0 &&
          strcmp(tiku_trk_info_tab_name(TIKU_TRK_INFO_TAB_PERMISSIONS),
                 "Permissions") == 0 &&
          strcmp(tiku_trk_info_tab_name(TIKU_TRK_INFO_TAB_ATTRIBUTES),
                 "Attributes") == 0, "AW-066",
          "three tabs, in the order they are shown");

    printf("\nthe Information tab (AW-067, MA-041)\n");
    check(strstr(field_of(&in, st, TIKU_TRK_INFO_F_SIZE, &label),
                 "2.9") != NULL &&
          strcmp(label, "Size") == 0, "AW-067",
          "a file reports its size, scaled as the Size column scales it "
          "-- the same formatter, so the two cannot word one fact "
          "differently");
    check(strcmp(field_of(&in, st, TIKU_TRK_INFO_F_KIND, &label),
                 tiku_model_kind_string(&in.model)) == 0, "AW-067",
          "Kind is the same string the Kind column shows");
    check(strcmp(field_of(&in, st, TIKU_TRK_INFO_F_LOCATION, &label),
                 work) == 0, "AW-067", "Location is the folder it is in");
    check(field_of(&in, st, TIKU_TRK_INFO_F_MODIFIED, &label)[0] != '\0' &&
          strcmp(label, "Modified") == 0, "MA-041",
          "and Modified reads through the columns' date formatter");
    printf("\nthe Created row (D-007)\n");
    {
        tiku_trk_info_t made, bare;
        char created[TIKU_TRK_INFO_VAL_MAX];
        char when[TIKU_TRK_INFO_VAL_MAX];
        char madepath[512];
        long long birth;

        /* Beside the fixture, not in it: the folder's counts below are
         * of the fixture's own files.  The Modified date is moved forward:
         * APFS moves the birth time back with a Modified date set before
         * it, and BSD touch reads only the ISO form. */
        snprintf(madepath, sizeof madepath, "%s-made.bin", work);
        sh("head -c 10 /dev/zero > %s && touch -m -d 2030-01-01T00:00:00 %s",
           madepath, madepath);
        birth = host_birth(madepath);
        check(tiku_trk_info_open(&made, b, madepath) == 0, "D-007",
              "a window opens on a file whose date was moved");
        snprintf(created, sizeof created, "%s",
                 field_of(&made, st, TIKU_TRK_INFO_F_CREATED, &label));
        if (birth > 0) {
            (void)tiku_attr_time_fit(birth, 40, NULL, NULL, when,
                                     sizeof when);
            check(strcmp(created, when) == 0 &&
                  strcmp(created, field_of(&made, st,
                      TIKU_TRK_INFO_F_MODIFIED, &label)) != 0, "D-007",
                  "Created shows the file's birth time, not the Modified "
                  "date it was moved to");
        } else {
            check(strcmp(created, "-") == 0, "D-007",
                  "on a filesystem with no birth time, Created says \"-\"");
        }
        memset(&bare, 0, sizeof bare);
        bare.used = 1;
        bare.model.kind = TIKU_KIND_FILE;
        bare.model.facts.mtime = 1000000000;
        (void)unlink(madepath);
        check(strcmp(field_of(&bare, st, TIKU_TRK_INFO_F_CREATED, &label),
                     "-") == 0 &&
              strcmp(label, "Created") == 0, "D-007",
              "an entry whose store reports no birth time shows \"-\" "
              "for Created, not its Modified date");
    }
    check(field_of(&in, st, TIKU_TRK_INFO_F_CAPACITY, &label)[0] == '\0',
          "AW-067",
          "a FILE has no capacity row: capacity is what a volume reports "
          "where a file reports size");
    check(field_of(&in, st, TIKU_TRK_INFO_F_LINK, &label)[0] == '\0',
          "AW-067", "and no link row, not being a link");

    printf("\nan image says how big the picture is (IV-035)\n");
    check(field_of(&in, st, TIKU_TRK_INFO_F_DIMENSIONS, &label)[0] == '\0',
          "IV-035",
          "nothing is claimed before the picture has been measured");
    {
        uint32_t dim[2] = { 640u, 480u };

        (void)tiku_state_write(st, path, TIKU_ATTR_THUMB_DIM, dim,
                                   sizeof dim);
        check(strcmp(field_of(&in, st, TIKU_TRK_INFO_F_DIMENSIONS, &label),
                     "640 x 480") == 0 &&
              strcmp(label, "Dimensions") == 0, "IV-035",
              "once the thumbnail has recorded it, the window reads it "
              "back -- which is the first thing that ever has");
    }

    printf("\na folder is counted in the background (AW-068)\n");
    check(tiku_trk_info_open(&in, b, work) == 0 &&
          in.calc == TIKU_TRK_INFO_CALC_RUNNING, "AW-068",
          "opening a folder starts the walk");
    check(strstr(field_of(&in, st, TIKU_TRK_INFO_F_SIZE, &label),
                 "calculating") != NULL, "AW-068",
          "and the field says so rather than showing a number it does not "
          "have yet");
    {
        int turns = 0;

        while (tiku_trk_info_calc_step(&in, b) && turns < 100) {
            turns++;
        }
        check(turns > 0, "AW-068",
              "the walk takes more than one step, which is why it is "
              "stepped rather than done in the open");
        check(in.calc == TIKU_TRK_INFO_CALC_DONE, "AW-068",
              "and it finishes");
        check(in.calc_bytes == 3107 && in.calc_files == 3 &&
              in.calc_dirs == 2, "AW-068",
              "counting every file at every depth: 3107 bytes, 3 files, "
              "2 folders");
        check(strstr(field_of(&in, st, TIKU_TRK_INFO_F_SIZE, &label),
                     "3 files in 2 folders") != NULL, "AW-068",
              "which the field then reports");
    }
    tiku_trk_info_calc_start(&in);
    (void)tiku_trk_info_calc_step(&in, b);
    tiku_trk_info_calc_stop(&in);
    check(in.calc == TIKU_TRK_INFO_CALC_IDLE &&
          strcmp(field_of(&in, st, TIKU_TRK_INFO_F_SIZE, &label), "-") == 0,
          "AW-068",
          "stopping half way reports NOTHING rather than the part that had "
          "been counted -- a smaller number than the truth, stated with the "
          "same confidence, is the worst of the three answers");

    printf("\nwhat re-reads itself (AW-069)\n");
    snprintf(path, sizeof path, "%s/big.bin", work);
    (void)tiku_trk_info_open(&in, b, path);
    check(!tiku_trk_info_refresh(&in, b, 1000), "AW-069",
          "nothing to report a moment after opening");
    sh("head -c 5000 /dev/zero > %s/big.bin", work);
    check(!tiku_trk_info_refresh(&in, b, TIKU_TRK_INFO_REFRESH_US / 2),
          "AW-069", "and not before the second is up");
    check(tiku_trk_info_refresh(&in, b, TIKU_TRK_INFO_REFRESH_US + 1) &&
          in.model.facts.size == 5000, "AW-069",
          "a file being written grows under the window, and nothing tells "
          "it so -- a size is not reported until the file is closed, so it "
          "asks again");
    check(!tiku_trk_info_refresh(&in, b, TIKU_TRK_INFO_REFRESH_US * 2 + 2),
          "AW-069",
          "and a second later, with nothing having moved, it says so -- a "
          "refresh that always reported a change would repaint the window "
          "once a second forever");
    {
        tiku_trk_info_t folder;

        (void)tiku_trk_info_open(&folder, b, work);
        /* The held facts are set wrong, so a re-stat would see a change.
         * A folder is not re-polled, so nothing is reported. */
        folder.model.facts.mtime = 0;
        folder.model.facts.size = -12345;
        check(!tiku_trk_info_refresh(&folder, b,
                                     TIKU_TRK_INFO_REFRESH_US * 4) &&
              folder.model.facts.mtime == 0, "AW-069",
              "a FOLDER is not re-polled even when what it holds is plainly "
              "stale: its contents arrive as notices, and a poll would be a "
              "directory read a second for nothing");
    }

    printf("\nthe entry moves or goes (AW-077)\n");
    snprintf(path, sizeof path, "%s/big.bin", work);
    (void)tiku_trk_info_open(&in, b, path);
    {
        char moved[512];

        snprintf(moved, sizeof moved, "%s/renamed", work);
        sh("mv %s %s", path, moved);
        check(tiku_trk_info_moved(&in, b, path, moved) &&
              strcmp(in.path, moved) == 0 &&
              strcmp(in.model.name, "renamed") == 0, "AW-077",
              "a rename is followed, and the model re-read with it -- the "
              "title and the Location both come off it");
        tiku_trk_info_title(&in, buf, sizeof buf);
        check(strcmp(buf, "renamed info") == 0, "AW-077",
              "so the title follows too");
        check(!tiku_trk_info_gone(&in, "/somewhere/else"), "AW-077",
              "an unrelated removal is not this entry going");
        check(tiku_trk_info_gone(&in, moved), "AW-077",
              "its own removal is");
        check(tiku_trk_info_gone(&in, work), "AW-077",
              "and so is a FOLDER above it going: the entry is gone "
              "whether or not anyone said its name");
        sh("mv %s %s", moved, path);
    }
    {   /* An ancestor moving carries it. */
        tiku_trk_info_t deep;
        char inner[512];

        snprintf(inner, sizeof inner, "%s/deep/small.bin", work);
        (void)tiku_trk_info_open(&deep, b, inner);
        check(tiku_trk_info_moved(&deep, b, "/tmp", "/tmp") == 1, "AW-077",
              "an ancestor moving takes it along");
        {
            char partial[512];

            /* A real prefix of the path that stops MID-COMPONENT. */
            snprintf(partial, sizeof partial, "%s/de", work);
            check(!tiku_trk_info_moved(&deep, b, partial, "/nope"),
                  "AW-077",
                  "and a path that merely starts the same does not: it has "
                  "to end at a component boundary, or \"/a/b\" would claim "
                  "\"/a/bc\"");
        }
    }

    {   /* Found again by IDENTITY after a rename. */
        tiku_trk_info_t moved;
        char to[512];

        (void)tiku_trk_info_open(&moved, b, path);
        snprintf(to, sizeof to, "%s/under-another-name", work);
        sh("mv %s %s", path, to);
        check(tiku_trk_info_refind(&moved, b) &&
              strcmp(moved.path, to) == 0 &&
              strcmp(moved.model.name, "under-another-name") == 0,
              "AW-077",
              "a window whose entry stopped being where it was finds it "
              "again by IDENTITY, since a rename changes the name and "
              "nothing else");
        sh("mv %s %s", to, path);
        (void)tiku_trk_info_open(&moved, b, path);
        sh("rm -f %s", path);
        check(!tiku_trk_info_refind(&moved, b), "AW-077",
              "and an entry that is really gone is not found, which is "
              "what closes the window rather than leaving it describing "
              "nothing");
        sh("head -c 5000 /dev/zero > %s", path);
        check(!tiku_trk_info_refind(&moved, b), "AW-077",
              "not even when a DIFFERENT file has taken the same name: the "
              "window is about a node, and adopting the impostor would "
              "show one entry's facts under another's history");
    }

    printf("\nthe Permissions tab (AW-078)\n");
    (void)tiku_trk_info_open(&in, b, path);
    check(in.perm_editable, "AW-078",
          "a local file's permissions can be set, so the grid is live");
    check(tiku_trk_info_perm(&in, TIKU_TRK_INFO_PERM_READ) &&
          tiku_trk_info_perm(&in, TIKU_TRK_INFO_PERM_WRITE), "AW-078",
          "and it starts showing what the entry actually allows");
    check(tiku_trk_info_perm_toggle(&in, b, TIKU_TRK_INFO_PERM_WRITE, err,
                                    sizeof err) == 0 &&
          !tiku_trk_info_perm(&in, TIKU_TRK_INFO_PERM_WRITE), "AW-078",
          "a box is applied the moment it is clicked -- there is no OK "
          "button, so there is no moment at which a change is pending");
    {
        tiku_trk_info_t again;

        (void)tiku_trk_info_open(&again, b, path);
        check(!tiku_trk_info_perm(&again, TIKU_TRK_INFO_PERM_WRITE),
              "AW-078", "and it really reached the file");
    }
    (void)tiku_trk_info_perm_toggle(&in, b, TIKU_TRK_INFO_PERM_WRITE, err,
                                    sizeof err);
    {   /* A backend that cannot set them says so up front. */
        tiku_backend_t nope;
        tiku_trk_info_t ro;

        memset(&nope, 0, sizeof nope);
        nope.ops = &RO_OPS;
        check(tiku_trk_info_open(&ro, &nope, path) == 0 &&
              !ro.perm_editable, "AW-078",
              "a backend with no way to set them says so, and the grid is "
              "shown as it is rather than offering an edit that fails "
              "after the fact");
        check(tiku_trk_info_perm_toggle(&ro, &nope, TIKU_TRK_INFO_PERM_READ,
                                        err, sizeof err) != 0 &&
              err[0] != '\0', "AW-078", "and a click on it refuses aloud");
        {
            /*
             * And still says so through a NAMESPACE, which is where it
             * stopped saying it: a namespace always carries a setperm --
             * it has to, in order to route one -- so asking the outer
             * table made the grid editable over a store that cannot be
             * told, which is the after-the-fact failure AW-078 exists to
             * prevent.
             */
            tiku_backend_t *ns = tiku_mount_open();
            tiku_trk_info_t through;

            check(ns != NULL && tiku_mount_add(ns, "/", &nope, 0) == 0 &&
                  tiku_trk_info_open(&through, ns, path) == 0 &&
                  !through.perm_editable, "AW-078",
                  "reached through a namespace it is STILL not editable: "
                  "the question is what serves this node, not what the "
                  "shell happens to hold");
            if (ns != NULL) {
                ns->ops->close(ns);
            }
        }
    }
    {   /* A toggle the store REFUSES must not move the box. */
        tiku_trk_info_t doomed;
        char ghost[512];
        int was;

        snprintf(ghost, sizeof ghost, "%s/vanishes", work);
        sh("touch %s", ghost);
        (void)tiku_trk_info_open(&doomed, b, ghost);
        was = tiku_trk_info_perm(&doomed, TIKU_TRK_INFO_PERM_EXEC);
        sh("rm -f %s", ghost);
        check(tiku_trk_info_perm_toggle(&doomed, b, TIKU_TRK_INFO_PERM_EXEC,
                                        err, sizeof err) != 0 &&
              tiku_trk_info_perm(&doomed, TIKU_TRK_INFO_PERM_EXEC) == was,
              "AW-078",
              "a refused toggle leaves the tick where it was: with no OK "
              "button the tick IS the claim, and one the store refused "
              "would be a lie the window keeps telling");
    }

    printf("\nthe Attributes tab (AW-079)\n");
    (void)tiku_state_write(st, path, "_trk/note", "hello", 5);
    {
        uint32_t n = 42u;
        unsigned char blob[64];

        (void)tiku_state_write(st, path, "_trk/count", &n, sizeof n);
        memset(blob, 0xab, sizeof blob);
        (void)tiku_state_write(st, path, "_trk/blob", blob, sizeof blob);
    }
    check(tiku_trk_info_attrs(&in, st) == 4, "AW-079",
          "every attribute the entry carries is listed -- the three just "
          "written and the dimensions the thumbnail recorded earlier, "
          "which is one attribute like any other");
    {
        int i, seen_text = 0, seen_int = 0, seen_raw = 0;

        for (i = 0; i < in.nattr; i++) {
            if (strcmp(in.attr[i].name, "_trk/note") == 0) {
                seen_text = (strcmp(in.attr[i].value, "hello") == 0 &&
                             strcmp(in.attr[i].type, "text") == 0);
            }
            if (strcmp(in.attr[i].name, "_trk/count") == 0) {
                seen_int = (strcmp(in.attr[i].value, "42") == 0 &&
                            strcmp(in.attr[i].type, "int32") == 0);
            }
            if (strcmp(in.attr[i].name, "_trk/blob") == 0) {
                seen_raw = (strstr(in.attr[i].value, "64 bytes") != NULL &&
                            strcmp(in.attr[i].type, "raw") == 0);
            }
        }
        check(seen_text, "AW-079", "text reads as text");
        check(seen_int, "AW-079", "a number reads as a number");
        check(seen_raw, "AW-079",
              "and a blob is DESCRIBED rather than spelled: printing a "
              "thumbnail as mojibake tells the reader nothing and costs a "
              "screenful");
    }

    printf("\nwhere the clickable fields lead (AW-072)\n");
    check(tiku_trk_info_location(&in, buf, sizeof buf, sel, sizeof sel) &&
          strcmp(buf, work) == 0 && strcmp(sel, "big.bin") == 0, "AW-072",
          "Location opens the parent with the entry SELECTED, not just the "
          "parent -- otherwise the user has to find it again");
    {
        tiku_trk_info_t root;

        if (tiku_trk_info_open(&root, b, "/") == 0) {
            check(!tiku_trk_info_location(&root, buf, sizeof buf, sel,
                                          sizeof sel), "AW-072",
                  "and the root is in nothing");
        }
    }

    printf("\na link, and re-pointing a broken one (AW-072, AW-074)\n");
    {
        char link[512], target[512];
        tiku_trk_info_t li;

        snprintf(link, sizeof link, "%s/alias", work);
        snprintf(target, sizeof target, "%s/big.bin", work);
        sh("ln -sf %s %s", target, link);
        (void)tiku_trk_info_open(&li, b, link);
        check(tiku_trk_info_link_target(&li, b, buf, sizeof buf, sel,
                                        sizeof sel) &&
              strcmp(sel, "big.bin") == 0, "AW-072",
              "Link to leads to the target, selected in its folder");
        check(strstr(field_of(&li, st, TIKU_TRK_INFO_F_LINK, &label),
                     "big.bin") != NULL, "AW-072",
              "and the field says where it points");

        sh("ln -sf %s/nothing-here %s", work, link);
        (void)tiku_trk_info_open(&li, b, link);
        check(!tiku_trk_info_link_target(&li, b, buf, sizeof buf, sel,
                                         sizeof sel), "AW-072",
              "a BROKEN link leads nowhere, which is the cue to offer a "
              "picker rather than an error");
        check(strstr(field_of(&li, st, TIKU_TRK_INFO_F_LINK, &label),
                     "broken") != NULL, "AW-072",
              "and the field says it is broken rather than showing a path "
              "that resolves to nothing");
        /* The path-keyed store keeps a link's state under the link's
         * path, so the state is intact after the link is replaced. */
        (void)tiku_state_write(st, link, "_trk/note", "kept", 4);
        {
            uint32_t n = 7u;

            (void)tiku_state_write(st, link, "_trk/count", &n,
                                       sizeof n);
        }
        check(tiku_trk_info_relink(&li, b, target, err, sizeof err) == 0,
              "AW-074",
              "it can be pointed at something real");
        check(tiku_trk_info_link_target(&li, b, buf, sizeof buf, sel,
                                        sizeof sel) &&
              strcmp(sel, "big.bin") == 0, "AW-074",
              "and then it resolves");
        check(strcmp(li.model.name, "alias") == 0, "AW-074",
              "under its own name still: the link was replaced, not "
              "renamed");
        (void)tiku_trk_info_attrs(&li, st);
        {
            int i, kept_text = 0, kept_int = 0;

            for (i = 0; i < li.nattr; i++) {
                if (strcmp(li.attr[i].name, "_trk/note") == 0) {
                    kept_text = (strcmp(li.attr[i].value, "kept") == 0);
                }
                if (strcmp(li.attr[i].name, "_trk/count") == 0) {
                    kept_int = (strcmp(li.attr[i].value, "7") == 0);
                }
            }
            check(kept_text, "AW-074",
                  "everything it carried is carried across");
            check(kept_int, "AW-074",
                  "RAW, so a number comes back a number rather than the "
                  "text the window was showing for it");
        }
        {
            tiku_trk_info_t nf;

            (void)tiku_trk_info_open(&nf, b, target);
            check(tiku_trk_info_relink(&nf, b, target, err,
                                       sizeof err) != 0, "AW-074",
                  "and a plain file is not a link to re-point");
        }
        {
            /* The xattr store keeps state on the file a link resolves to,
             * so the new target keeps its own state after a re-point. */
            tiku_store_t *xs = tiku_store_xattr_open();
            tiku_trk_info_t xl;
            char xlink[512], next[512], locked[512], got[512], value[16];
            ssize_t n;

            snprintf(xlink, sizeof xlink, "%s/xattr-alias", work);
            snprintf(next, sizeof next, "%s/deep/small.bin", work);
            sh("ln -sf %s %s", target, xlink);
            if (xs != NULL &&
                tiku_state_write(xs, target, "_trk/pos", "old", 3) == 0 &&
                tiku_state_write(xs, next, "_trk/pos", "mine", 4) == 0) {
                (void)tiku_trk_info_open(&xl, b, xlink);
                check(tiku_trk_info_relink(&xl, b, next, err,
                                           sizeof err) == 0 &&
                      err[0] == '\0', "AW-074",
                      "a link with state on its target is re-pointed");
                memset(value, 0, sizeof value);
                check(tiku_state_read(xs, next, "_trk/pos", value,
                                      sizeof value - 1u) == 4 &&
                      strcmp(value, "mine") == 0, "AW-074",
                      "and the new target keeps its own state");
                snprintf(locked, sizeof locked, "%s/locked.bin", work);
                sh("rm -f %s && touch %s && chmod 0444 %s", locked,
                   locked, locked);
                (void)tiku_trk_info_open(&xl, b, xlink);
                check(tiku_trk_info_relink(&xl, b, locked, err,
                                           sizeof err) == 0 &&
                      err[0] == '\0', "AW-074",
                      "a read-only target takes the link");
                n = readlink(xlink, got, sizeof got - 1u);
                if (n > 0) {
                    got[n] = '\0';
                }
                check(n > 0 && strcmp(got, locked) == 0, "AW-074",
                      "and the link points at it");
            } else {
                printf("  [SKIP] AW-074    this filesystem has no xattrs\n");
            }
            tiku_store_free(xs);
        }
    }

    printf("\nand the same entry reached through a namespace (AW-066)\n");
    {
        /*
         * Every model a shell handles belongs to the NAMESPACE now, not
         * to the store it came from -- so a row that asked the model's
         * own backend what it was called answered "mount", for a local
         * file and for a device alike, which is the one distinction this
         * row exists to draw.  Opened on a plain backend the fault is
         * invisible; that is why it is opened on a mounted one here.
         */
        tiku_backend_t *ns = tiku_mount_open();
        tiku_trk_info_t through;
        const char *got = NULL;

        /*
         * A VOLUME, because the Filesystem row is only offered for one:
         * a plain file has no such row and the check would pass on an
         * empty string without ever reaching the code it is about.
         */
        if (ns != NULL && tiku_mount_add(ns, "/", b, 0) == 0 &&
            tiku_trk_info_open(&through, ns, "/") == 0) {
            got = field_of(&through, st, TIKU_TRK_INFO_F_FILESYSTEM,
                           &label);
        }
        check(got != NULL && strcmp(got, "posix") == 0, "AW-066",
              "the Filesystem row names the store the entry is really ON "
              "rather than the namespace it was reached through");
        if (ns != NULL) {
            ns->ops->close(ns);
        }
    }

    tiku_store_free(st);
    tiku_backend_close(b);
    sh("rm -rf %s", work);
    printf("\n%s\n", fails ? "FAILURES" : "the info window tells the truth");
    return fails ? 1 : 0;
}
