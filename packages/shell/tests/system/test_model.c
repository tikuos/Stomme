/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_model.c - N1's exit test: one Model API over both backends, headless.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "tiku_ident.h"
#include "tiku_model.h"
#include "tiku_trk_nodeicon.h"
#include "tiku_state.h"

static int fails;

static void
check(int cond, const char *what)
{
    printf("  [%s] %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond) { fails++; }
}

struct tally { int files; int dirs; int nodes; int virt; char first[128]; };

static int
count_entry(const tiku_model_t *m, void *ctx)
{
    struct tally *t = ctx;

    if (t->first[0] == '\0') {
        snprintf(t->first, sizeof t->first, "%s", m->name);
    }
    switch (m->kind) {
    case TIKU_KIND_FILE:        t->files++; break;
    case TIKU_KIND_DIRECTORY:   t->dirs++;  break;
    case TIKU_KIND_DEVICE_NODE: t->nodes++; break;
    case TIKU_KIND_VIRTUAL_DIR: t->virt++;  break;
    default: break;
    }
    return 0;
}

static void
test_ident(void)
{
    tiku_identity_t a, b;
    char k1[TIKU_DEVID_MAX], k2[TIKU_DEVID_MAX];

    printf("device identity\n");
    memset(&a, 0, sizeof a);
    memset(&b, 0, sizeof b);
    snprintf(a.uid, sizeof a.uid, "DEADBEEF12345678");
    snprintf(a.mcu, sizeof a.mcu, "ra8p1");
    snprintf(a.name, sizeof a.name, "bench");

    tiku_ident_key(&a, k1, sizeof k1);
    check(strncmp(k1, "uid-", 4) == 0, "a uid gives a uid- key");
    check(!tiku_ident_provisional(&a), "a uid is not provisional");

    b = a;
    check(tiku_ident_matches(&a, &b), "same board matches itself");

    snprintf(b.name, sizeof b.name, "renamed");
    check(tiku_ident_matches(&a, &b), "a rename does not break the match");

    snprintf(b.uid, sizeof b.uid, "0000000000000000");
    check(tiku_ident_score(&a, &b) == -1,
          "a different uid vetoes, however much else agrees");

    memset(&b, 0, sizeof b);
    snprintf(b.mcu, sizeof b.mcu, "ra8p1");
    snprintf(b.name, sizeof b.name, "bench");
    tiku_ident_key(&b, k2, sizeof k2);
    check(strncmp(k2, "prov-", 5) == 0, "no uid gives a provisional key");
    check(tiku_ident_provisional(&b), "and is reported as provisional");
    check(!tiku_ident_matches(&a, &b),
          "mcu+name alone (3) does not reach the bind threshold");
}

static void
test_state(void)
{
    tiku_store_t *s;
    char devid[] = "uid-TESTBOARD0001";
    char buf[64];
    int n;

    printf("state store\n");
    s = tiku_store_sidecar_open(devid);
    check(s != NULL, "sidecar opens");
    if (s == NULL) { return; }

    (void)tiku_state_forget(s, "/dev/led0");
    check(tiku_state_write(s, "/dev/led0", TIKU_ATTR_POSE_INFO,
                               "\x10\x00\x20\x00", 4) == 0,
          "pose info writes for a device node");
    n = tiku_state_read(s, "/dev/led0", TIKU_ATTR_POSE_INFO, buf,
                            sizeof buf);
    check(n == 4 && memcmp(buf, "\x10\x00\x20\x00", 4) == 0,
          "and reads back byte-exact");
    check(tiku_state_read(s, "/dev/led1", TIKU_ATTR_POSE_INFO, buf,
                              sizeof buf) == -1,
          "an unknown node has no state");

    /* the reserved slots keep the desktop's arrangement out of any device */
    check(tiku_state_write(s, TIKU_KEY_DEV_ROOT,
                               TIKU_ATTR_VIEW_STATE, "icon", 4) == 0,
          "the device root has its own slot");
    check(tiku_store_flush(s) == 0, "flush persists");
    tiku_store_free(s);

    s = tiku_store_sidecar_open(devid);
    n = tiku_state_read(s, "/dev/led0", TIKU_ATTR_POSE_INFO, buf,
                            sizeof buf);
    check(n == 4, "state survives close and reopen");
    check(tiku_store_count(s) == 2, "both records came back");

    /* the invariant that protects the user's arrangement */
    check(tiku_state_forget(s, "/dev/led0") == 1,
          "explicit forget removes one node");
    check(tiku_state_read(s, "/dev/led0", TIKU_ATTR_POSE_INFO, buf,
                              sizeof buf) == -1, "and it is gone");
    tiku_store_free(s);
}

static void
test_posix(void)
{
    tiku_backend_t *b = tiku_backend_posix_open();
    tiku_model_t m;
    struct tally t;
    char dir[] = "/tmp/trk_n1_test";
    char f1[64], sub[64];

    printf("posix backend\n");
    check(b != NULL, "opens");
    if (b == NULL) { return; }

    (void)mkdir(dir, 0755);
    snprintf(sub, sizeof sub, "%s/folder", dir);
    snprintf(f1, sizeof f1, "%s/file.txt", dir);
    (void)mkdir(sub, 0755);
    {
        FILE *f = fopen(f1, "w");
        if (f != NULL) { fputs("hello", f); (void)fclose(f); }
    }

    check(b->ops->stat(b, dir, &m) == 0, "stat a directory");
    check(m.kind == TIKU_KIND_DIRECTORY, "it is a DIRECTORY");
    check(tiku_model_is_container(&m), "a directory contains");
    check(tiku_model_is_mutable_container(&m),
          "a writable local directory accepts new entries");

    {   /* The Trash is recognised by name, and its icon is the one piece
         * of state it exists to show (AW-029, IV-046). */
        char tr[80], junk[128];

        snprintf(tr, sizeof tr, "%s/.Trash", dir);
        (void)mkdir(tr, 0755);
        check(b->ops->stat(b, tr, &m) == 0 && m.kind == TIKU_KIND_TRASH,
              "a directory called .Trash IS the Trash");
        check(m.facts.trash_full == 0 &&
              strcmp(tiku_model_icon_name(&m), "trash_empty") == 0,
              "an empty Trash reports empty and draws the empty icon");

    {   /* IV-042: a symlink wears its TARGET's icon; only a broken one
         * wears the link art. */
        tiku_model_t lm;

        memset(&lm, 0, sizeof lm);
        lm.kind = TIKU_KIND_SYMLINK;
        snprintf(lm.type, sizeof lm.type, "text/plain");
        check(strcmp(tiku_model_icon_name(&lm), "text_file") == 0,
              "IV-042: a link to a text file wears the text-file art");
        snprintf(lm.type, sizeof lm.type,
                 "application/x-vnd.Be-directory");
        check(strcmp(tiku_model_icon_name(&lm), "folder") == 0,
              "IV-042: a link to a folder wears the folder art");
        lm.facts.link_broken = 1;
        check(strcmp(tiku_model_icon_name(&lm), "symlink") == 0,
              "IV-042: only a broken link wears the link art");
        /* MA-008: a link to a folder is a container for sorting and drop
         * targeting; a broken one is not. */
        lm.facts.link_broken = 0;
        check(tiku_model_is_container(&lm),
              "MA-008: a link to a folder sorts and drops as a folder");
        snprintf(lm.type, sizeof lm.type, "text/plain");
        check(!tiku_model_is_container(&lm),
              "MA-008: a link to a file stays a file");
        snprintf(lm.type, sizeof lm.type,
                 "application/x-vnd.Be-directory");
        lm.facts.link_broken = 1;
        check(!tiku_model_is_container(&lm),
              "MA-008: a broken link is nothing's container");
    }


        snprintf(junk, sizeof junk, "%s/thrown.txt", tr);
        {
            FILE *f = fopen(junk, "w");
            if (f != NULL) { fputs("x", f); (void)fclose(f); }
        }
        check(b->ops->stat(b, tr, &m) == 0 && m.facts.trash_full == 1 &&
              strcmp(tiku_model_icon_name(&m), "trash_full") == 0,
              "one item in it makes it full, and the icon follows");
        (void)remove(junk);
        check(b->ops->stat(b, tr, &m) == 0 && m.facts.trash_full == 0,
              "and emptying it again makes it empty -- the fact is read, "
              "not remembered");
        (void)rmdir(tr);
    }

    {   /* A mount point is a VOLUME, not a folder that happens to sit
         * where one is (PVN-041, PVL-072). */
        check(b->ops->stat(b, "/", &m) == 0 &&
              m.kind == TIKU_KIND_ROOT,
              "the filesystem root is the synthetic Disks root");
        check(strcmp(tiku_model_display_name(&m), "Disks") == 0,
              "the root presents Disks without changing its real name");
        check(m.facts.total > 0u && m.facts.avail <= m.facts.total,
              "and carries its capacity, which is what the space bar "
              "is drawn from");
        check(b->ops->stat(b, dir, &m) == 0 &&
              m.kind == TIKU_KIND_DIRECTORY &&
              m.facts.total == 0u,
              "while an ordinary directory is neither");
    }

    {   /* Display names are one model answer, not labels duplicated by
         * each surface.  name stays the filesystem spelling for operations. */
        tiku_model_t special;

        memset(&special, 0, sizeof special);
        snprintf(special.name, sizeof special.name, ".Trash");
        special.kind = TIKU_KIND_TRASH;
        check(strcmp(tiku_model_display_name(&special), "Trash") == 0 &&
              strcmp(special.name, ".Trash") == 0, "MA-006: Trash has a "
              "human label while its real name remains usable");
        snprintf(special.name, sizeof special.name, "Desktop");
        special.kind = TIKU_KIND_DESKTOP;
        check(strcmp(tiku_model_display_name(&special), "Desktop") == 0,
              "MA-006: Desktop has its special display name");
        snprintf(special.name, sizeof special.name, "Data disk");
        special.kind = TIKU_KIND_VOLUME;
        check(strcmp(tiku_model_display_name(&special), "Data disk") == 0,
              "MA-006: a volume presents its cached volume name");
    }

    {   /* A path leaf alone is not the Desktop: only the user's actual
         * Desktop gets the special kind and its non-file-manager rules. */
        char desk[TIKU_PATH_MAX], old_home[TIKU_PATH_MAX];
        const char *was_home = getenv("HOME");
        int had_home = (was_home != NULL);

        if (had_home) {
            snprintf(old_home, sizeof old_home, "%s", was_home);
        }
        (void)setenv("HOME", dir, 1);
        /* Tracker's own Desktop, wherever the one function says it is:
         * a device's screen is not the login session's desktop. */
        (void)tiku_desktop_dir(desk, sizeof desk);
        (void)tiku_state_mkparents(desk);
        (void)mkdir(desk, 0755);
        check(b->ops->stat(b, desk, &m) == 0 &&
              m.kind == TIKU_KIND_DESKTOP &&
              strcmp(tiku_model_display_name(&m), "Desktop") == 0,
              "MA-005: the user's Desktop is a special model kind");
        {
            char icon[16];
            tiku_store_t *store = b->ops->state_store(b);
            int icon_len = store != NULL ? tiku_state_read(
                store, desk, TIKU_TRK_ATTR_ICON, icon, sizeof icon) : -1;

            check(store != NULL && icon_len == 8 &&
                  memcmp(icon, "desktop", 7) == 0,
                  "FS-082: resolving Desktop seeds its stable icon descriptor");
        }
        if (had_home) {
            (void)setenv("HOME", old_home, 1);
        } else {
            (void)unsetenv("HOME");
        }
        /* And the tree it needed: the very next check lists this
         * directory, and a stray .config would be an entry in it. */
        {
            char up[TIKU_PATH_MAX];
            char *slash;

            (void)rmdir(desk);
            snprintf(up, sizeof up, "%s", desk);
            for (slash = strrchr(up, '/');
                 slash != NULL && strcmp(up, dir) != 0;
                 slash = strrchr(up, '/')) {
                *slash = '\0';
                if (strcmp(up, dir) == 0) {
                    break;
                }
                (void)rmdir(up);
            }
        }
    }

    check(b->ops->stat(b, f1, &m) == 0, "stat a file");
    check(m.kind == TIKU_KIND_FILE && m.facts.size == 5,
          "kind and size are right");
    check(strcmp(tiku_model_kind_string(&m), "Text file") == 0,
          "the Kind column says what the file IS: the host carries no type "
          "attribute, so the NAME is the only source there is, and a .txt "
          "is a text file rather than a bare File (MA-002)");
    {
        tiku_model_t o;
        char other[64];

        snprintf(other, sizeof other, "%s/lump", dir);
        {
            FILE *f = fopen(other, "w");

            if (f != NULL) { (void)fputs("x", f); (void)fclose(f); }
        }
        check(b->ops->stat(b, other, &o) == 0 &&
              strcmp(tiku_model_kind_string(&o), "File") == 0,
              "and a name that says nothing leaves it a bare File, so "
              "'unknown' stays distinguishable from 'known to be data'");
        (void)remove(other);
    }

    memset(&t, 0, sizeof t);
    check(b->ops->list(b, dir, count_entry, &t) == 2, "lists both entries");
    check(t.files == 1 && t.dirs == 1, "and classifies each correctly");

    {
        char buf[16];
        int n = b->ops->read(b, f1, buf, sizeof buf);
        check(n == 5 && memcmp(buf, "hello", 5) == 0, "reads a file's bytes");
    }
    {
        tiku_store_t *st = b->ops->state_store(b);
        char got[8];
        int ok;

        ok = (tiku_state_write(st, dir, TIKU_ATTR_VIEW_STATE,
                                   "icon", 4) == 0);
        if (ok) {
            ok = (tiku_state_read(st, dir, TIKU_ATTR_VIEW_STATE,
                                      got, sizeof got) == 4);
        }
        /* /tmp may be mounted without user xattrs; that is a legitimate
         * "cannot persist here", not a failure of the code. */
        printf("  [%s] view state on a local dir via xattr%s\n",
               ok ? "PASS" : "SKIP", ok ? "" : " (no xattr support on /tmp)");
    }
    (void)unlink(f1);
    (void)rmdir(sub);
    (void)rmdir(dir);
    tiku_backend_close(b);
}

static void
test_tiku(const char *port, int baud)
{
    tiku_backend_t *b;
    tiku_model_t m;
    struct tally t;
    char buf[128];
    int n;

    printf("tiku backend (%s)\n", port ? port : "auto");
    b = tiku_backend_tiku_open(port, baud);
    if (b == NULL) {
        printf("  [SKIP] no device answered\n");
        return;
    }
    check(b->devid[0] != '\0', "bound a device identity");
    printf("         devid=%s\n", b->devid);

    /* The root is no manifest line, and it is the one path the router
     * asks of a mount when listing the folder the mount sits in: a
     * root that cannot be stat'ed is a board with no row. */
    check(b->ops->stat(b, "/", &m) == 0, "stat the root");
    check(m.kind == TIKU_KIND_VOLUME, "the root is the device, a volume");
    check(m.name[0] != '\0', "and it has a name to show");
    check(b->ops->stat(b, "/sys", &m) == 0, "stat /sys");
    check(m.kind == TIKU_KIND_VIRTUAL_DIR, "/sys is a namespace folder");
    check(tiku_model_is_container(&m), "it contains");
    check(!tiku_model_is_mutable_container(&m),
          "but nothing can be created in it -- New Folder stays disabled");

    memset(&t, 0, sizeof t);
    n = b->ops->list(b, "/sys", count_entry, &t);
    check(n > 3, "lists /sys children");
    check(t.virt > 0 && t.nodes > 0, "both folders and value nodes appear");

    check(b->ops->stat(b, "/sys/uptime", &m) == 0, "stat a value node");
    check(m.kind == TIKU_KIND_DEVICE_NODE, "it is a device node");
    check(!tiku_model_is_writable(&m), "uptime is read-only");
    n = b->ops->read(b, "/sys/uptime", buf, sizeof buf - 1);
    if (n > 0) { buf[n] = '\0'; }
    check(n > 0, "reads its live value");
    printf("         /sys/uptime = %.*s\n", n > 0 ? n : 0, buf);

    if (b->ops->stat(b, "/dev/led0", &m) == 0) {
        char err[160];
        check(tiku_model_is_writable(&m), "led0 is writable");
        check(b->ops->write(b, "/dev/led0", "1", 1, err, sizeof err) == 0,
              "writing it succeeds");
        n = b->ops->read(b, "/dev/led0", buf, sizeof buf - 1);
        if (n > 0) { buf[n] = '\0'; }
        check(n > 0 && buf[0] == '1', "and the value took");
        (void)b->ops->write(b, "/dev/led0", "0", 1, err, sizeof err);
    }
    {
        char err[160];
        int rc = b->ops->write(b, "/sys/uptime", "5", 1, err, sizeof err);
        check(rc != 0, "writing a read-only node is refused");
        check(strlen(err) > 0, "and the device's own reason is reported");
        printf("         refusal: %s\n", err);
    }
    /* a dynamic store behaves like a real directory, unlike /sys */
    if (b->ops->stat(b, "/data", &m) == 0) {
        check(m.kind == TIKU_KIND_DIRECTORY,
              "/data is a real directory, not a namespace folder");
        check(tiku_model_is_mutable_container(&m),
              "so files can be created in it");
        memset(&t, 0, sizeof t);
        (void)b->ops->list(b, "/data", count_entry, &t);
        printf("         /data holds %d files, %d dirs, %d value-nodes\n",
               t.files, t.dirs, t.nodes);
        check(t.nodes == 0,
              "a store's contents are files, not live value nodes");
    }
    {
        tiku_store_t *st = b->ops->state_store(b);
        check(st != NULL, "the device gets a sidecar state store");
        if (st != NULL) {
            check(tiku_state_write(st, "/sys", TIKU_ATTR_VIEW_STATE,
                                       "list", 4) == 0,
                  "view state for a device folder persists host-side");
        }
    }
    tiku_backend_close(b);
}

int
main(int argc, char **argv)
{
    const char *port = NULL;
    int baud = 115200, i;

    /* Sidecar state stores resolve under $HOME, and this test writes
     * real ones (uid-TESTBOARD0001, the local device).  Left on the
     * inherited HOME they land in the PERSON'S home -- which is exactly
     * what happened: the suite quietly seeded ~/.local/share/tracker
     * with fixture state on every run. */
    (void)mkdir("/tmp/trk_model_home", 0755);
    (void)setenv("HOME", "/tmp/trk_model_home", 1);

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-p") == 0 && i + 1 < argc)      { port = argv[++i]; }
        else if (strcmp(argv[i], "-b") == 0 && i + 1 < argc) { baud = atoi(argv[++i]); }
    }
    test_ident();
    test_state();
    test_posix();
    test_tiku(port, baud);
    printf("\n%s\n", fails ? "FAILURES" : "all N1 checks passed");
    return fails ? 1 : 0;
}
