/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_nodeicon.c - which icon an entry gets, and what invalidates it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "tiku_trk_nodeicon.h"
#include "tiku_trk_types.h"

static int fails;

static void
check(int cond, const char *row, const char *what)
{
    printf("  [%s] %-9s %s\n", cond ? "PASS" : "FAIL", row, what);
    if (!cond) { fails++; }
}

static tiku_model_t
entry(const char *path, tiku_kind_t k)
{
    tiku_model_t m;
    const char *slash;

    memset(&m, 0, sizeof m);
    snprintf(m.path, sizeof m.path, "%s", path);
    slash = strrchr(path, '/');
    snprintf(m.name, sizeof m.name, "%s", slash ? slash + 1 : path);
    m.kind = k;
    return m;
}

/** @brief A real 2x2 BMP, so the image sniff has something to sniff. */
static void
write_bmp(const char *path)
{
    static const unsigned char hdr[54] = {
        'B','M', 70,0,0,0, 0,0,0,0, 54,0,0,0, 40,0,0,0,
        2,0,0,0, 2,0,0,0, 1,0, 24,0, 0,0,0,0, 16,0,0,0,
        0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0
    };
    unsigned char px[16] = { 0 };
    FILE *f = fopen(path, "wb");

    if (f != NULL) {
        (void)fwrite(hdr, 1, sizeof hdr, f);
        (void)fwrite(px, 1, sizeof px, f);
        (void)fclose(f);
    }
}

int
main(void)
{
    tiku_store_t *store = tiku_store_memory_open();
    tiku_thumb_q_t q;
    tiku_trk_nodeicon_t c;
    tiku_model_t file, dir, img;
    char got[TIKU_NAME_MAX];
    char dirpath[256], imgpath[512];

    snprintf(dirpath, sizeof dirpath, "/tmp/trk-nodeicon-%d", (int)getpid());
    {
        char cmd[512];

        snprintf(cmd, sizeof cmd, "mkdir -p %s", dirpath);
        if (system(cmd) != 0) { return 77; }
    }
    snprintf(imgpath, sizeof imgpath, "%s/photo.bmp", dirpath);
    write_bmp(imgpath);

    tiku_thumb_init(&q);
    file = entry("/tmp/x/report.txt", TIKU_KIND_FILE);
    dir  = entry(dirpath, TIKU_KIND_DIRECTORY);
    img  = entry(imgpath, TIKU_KIND_FILE);

    printf("the one probe, and what it finds (IV-005, IV-037)\n");
    check(tiku_trk_nodeicon_hint(store, &q, &file) ==
          TIKU_ICONHINT_NONE, "IV-005",
          "a file with nothing on it has no picture of its own");
    check(tiku_trk_nodeicon_hint(store, &q, &img) ==
          TIKU_ICONHINT_THUMB, "IV-037",
          "an image is known to have one up front -- before the thumbnail "
          "exists, or the first draw of every image would take the type "
          "icon and nothing would ever ask again");
    check(tiku_trk_nodeicon_hint(store, NULL, &img) ==
          TIKU_ICONHINT_THUMB, "IV-037",
          "and having been asked once it is not asked again");
    {
        tiku_model_t off = entry(imgpath, TIKU_KIND_FILE);

        q.enabled = 0;
        check(tiku_trk_nodeicon_hint(store, &q, &off) ==
              TIKU_ICONHINT_NONE, "IV-037",
              "with thumbnails turned off it is just a file again");
        q.enabled = 1;
    }
    (void)tiku_state_write(store, file.path, TIKU_TRK_ATTR_ICON,
                               "special", 8);
    tiku_trk_nodeicon_unhint(&file);
    check(tiku_trk_nodeicon_hint(store, &q, &file) ==
          TIKU_ICONHINT_ATTR, "IV-005",
          "one that carries an icon attribute says so");
    check(!tiku_trk_nodeicon_possible(&dir), "IV-005",
          "a model nobody has probed answers NO rather than probing from "
          "the draw path");

    printf("\nthe lookup order (IV-001)\n");
    tiku_trk_nodeicon_init(&c);
    check(tiku_trk_nodeicon_resolve(&c, store, &file, got, sizeof got) ==
          TIKU_TRK_ICON_FROM_NODE && strcmp(got, "special") == 0,
          "IV-001",
          "a file that carries its own picture is drawn with it, not as a "
          "generic anything");
    {   /* The type must not win over the node. */
        tiku_model_t typed = file;

        snprintf(typed.type, sizeof typed.type, "text/plain");
        tiku_trk_nodeicon_init(&c);
        check(tiku_trk_nodeicon_resolve(&c, store, &typed, got,
                                        sizeof got) ==
              TIKU_TRK_ICON_FROM_NODE, "IV-001",
              "even when its type has an icon of its own -- the node "
              "always wins");
    }
    {
        tiku_model_t other = entry("/tmp/x/notes.md",
                                       TIKU_KIND_FILE);

        snprintf(other.type, sizeof other.type, "text/plain");
        (void)tiku_trk_nodeicon_hint(store, &q, &other);
        tiku_trk_nodeicon_init(&c);
        check(tiku_trk_nodeicon_resolve(&c, store, &other, got,
                                        sizeof got) ==
              TIKU_TRK_ICON_FROM_TYPE, "IV-001",
              "with no picture of its own, a known type supplies one");
        snprintf(other.type, sizeof other.type, "x-vnd/nothing-knows-this");
        tiku_trk_nodeicon_init(&c);
        check(tiku_trk_nodeicon_resolve(&c, store, &other, got,
                                        sizeof got) ==
              TIKU_TRK_ICON_FROM_GENERIC, "IV-001",
              "a type nothing is registered for does NOT count as a type "
              "answer -- it lands on the same document icon an untyped "
              "file gets, and one cached icon must not be shared across "
              "every unregistered type in a listing");
    }
    tiku_trk_nodeicon_init(&c);
    check(tiku_trk_nodeicon_resolve(&c, store, &img, got, sizeof got) ==
          TIKU_TRK_ICON_FROM_THUMB && got[0] != '\0', "IV-001",
          "and an image's own picture is its icon, which the caller draws "
          "rather than names -- though a name comes back anyway, because a "
          "thumbnail not yet made still has to draw as something");

    printf("\nnot touching the store from the draw path (IV-005)\n");
    tiku_trk_nodeicon_init(&c);
    (void)tiku_trk_nodeicon_resolve(&c, store, &dir, got, sizeof got);
    (void)tiku_trk_nodeicon_resolve(&c, store, &img, got, sizeof got);
    check(c.reads == 0, "IV-005",
          "an entry the hint said has none costs no read at all, and "
          "neither does one whose picture is its thumbnail");
    (void)tiku_trk_nodeicon_resolve(&c, store, &file, got, sizeof got);
    check(c.reads == 1, "IV-005", "one that might have one costs a read");
    (void)tiku_trk_nodeicon_resolve(&c, store, &file, got, sizeof got);
    (void)tiku_trk_nodeicon_resolve(&c, store, &file, got, sizeof got);
    check(c.reads == 1, "IV-005",
          "and asking again costs none: the answer is remembered, which is "
          "what stops a listing being a read per row per frame");

    printf("\nwhen the remembered answer is wrong (IV-038)\n");
    check(tiku_trk_nodeicon_attr_matters(TIKU_TRK_ATTR_ICON), "IV-038",
          "the icon attribute changes an icon");
    check(tiku_trk_nodeicon_attr_matters(TIKU_ATTR_THUMB), "IV-038",
          "so does the thumbnail, which IS an image's own icon");
    check(!tiku_trk_nodeicon_attr_matters("_trk/pinfo"), "IV-038",
          "while a pose position does not");
    check(tiku_trk_nodeicon_changed(&c, file.path), "IV-038",
          "a change drops what was remembered");
    (void)tiku_trk_nodeicon_resolve(&c, store, &file, got, sizeof got);
    check(c.reads == 2, "IV-038",
          "so the next draw reads it again -- an icon edited on disk shows "
          "at once rather than after a restart");
    (void)tiku_state_forget(store, file.path);
    tiku_trk_nodeicon_unhint(&file);
    (void)tiku_trk_nodeicon_changed(&c, file.path);
    check(tiku_trk_nodeicon_resolve(&c, store, &file, got, sizeof got) ==
              TIKU_TRK_ICON_FROM_GENERIC,
          "PVN-033", "deleting a custom icon falls back to the generic icon");
    (void)tiku_state_write(store, file.path, TIKU_TRK_ATTR_ICON,
                               "special", 8);
    tiku_trk_nodeicon_unhint(&file);
    {   /* The hint has to come back too. */
        tiku_model_t fresh = entry("/tmp/x/plain.txt",
                                       TIKU_KIND_FILE);

        (void)tiku_trk_nodeicon_hint(store, &q, &fresh);
        (void)tiku_state_write(store, fresh.path, TIKU_TRK_ATTR_ICON,
                                   "given", 6);
        (void)tiku_trk_nodeicon_changed(&c, fresh.path);
        tiku_trk_nodeicon_unhint(&fresh);
        tiku_trk_nodeicon_init(&c);
        check(tiku_trk_nodeicon_hint(store, &q, &fresh) ==
              TIKU_ICONHINT_ATTR &&
              tiku_trk_nodeicon_resolve(&c, store, &fresh, got,
                                        sizeof got) ==
              TIKU_TRK_ICON_FROM_NODE, "IV-038",
              "a file GIVEN an icon shows it: dropping the cached art "
              "alone would leave the model still believing it has none, "
              "and the new one would never be looked for");
    }

    (void)tiku_trk_nodeicon_hint(store, &q, &file);

    printf("\nwhen the entry goes (IV-041)\n");
    check(tiku_trk_nodeicon_deleted(&c, file.path) == 0, "IV-041",
          "there is nothing to drop for an entry nothing remembers");
    (void)tiku_trk_nodeicon_resolve(&c, store, &file, got, sizeof got);
    check(tiku_trk_nodeicon_deleted(&c, file.path), "IV-041",
          "deleting an entry drops its cached icon");
    tiku_trk_nodeicon_preload(&c, "/apps/mail", "application");
    {
        tiku_model_t app = entry("/apps/mail", TIKU_KIND_FILE);

        check(tiku_trk_nodeicon_resolve(&c, store, &app, got, sizeof got) ==
              TIKU_TRK_ICON_FROM_NODE &&
              strcmp(got, "application") == 0 && c.reads == 2, "IV-041",
              "a preloaded icon answers without a read");
    }
    check(!tiku_trk_nodeicon_deleted(&c, "/apps/mail"), "IV-041",
          "and a delete does NOT drop it: it was cached because it is "
          "wanted often, and the file going does not make the next ask "
          "less likely");
    {
        uint64_t before_generation = tiku_trk_nodeicon_generation(&c);

        check(tiku_trk_nodeicon_changed(&c, "/apps/mail") &&
              tiku_trk_nodeicon_generation(&c) > before_generation, "PVN-036",
          "though a CHANGE does -- being wanted often is no reason to keep "
          "showing the wrong picture and invalidates the title-bar icon");
    }
    tiku_trk_nodeicon_preload(&c, "/apps/mail", "application");
    check(tiku_trk_nodeicon_removing(&c, "/apps/mail"), "IV-041",
          "and whoever preloaded it can say the file behind it has gone, "
          "which is the one removal a permanent entry does not survive");

    tiku_store_free(store);
    {
        char cmd[512];

        snprintf(cmd, sizeof cmd, "rm -rf %s", dirpath);
        (void)system(cmd);
    }
    printf("\nthe art a TYPE wears (IV-011, AW-013)\n");
    {
        tiku_trk_types_t db;
        tiku_model_t m;
        char art[TIKU_NAME_MAX];

        tiku_trk_types_init(&db);
        (void)tiku_trk_types_add(&db, "text/", "Text file", "my_text_art",
                                 NULL);
        memset(&m, 0, sizeof m);
        m.kind = TIKU_KIND_FILE;
        snprintf(m.name, sizeof m.name, "notes.txt");
        snprintf(m.path, sizeof m.path, "/tmp/notes.txt");
        snprintf(m.type, sizeof m.type, "text/plain");
        (void)tiku_trk_nodeicon_resolve(&c, NULL, &m, art, sizeof art);
        check(strcmp(art, "my_text_art") != 0, "IV-011",
              "with no database the compiled art is what a type wears");
        tiku_trk_nodeicon_set_types(&c, &db);
        tiku_trk_nodeicon_deleted(&c, m.path);
        check(tiku_trk_nodeicon_resolve(&c, NULL, &m, art, sizeof art) ==
              TIKU_TRK_ICON_FROM_TYPE &&
              strcmp(art, "my_text_art") == 0, "IV-011",
              "and with one, the art the database declares for the type -- "
              "which is the icon an installation can change");
        check(tiku_trk_nodeicon_misses(&c) == 0, "IV-011",
              "nothing is remembered as missing until a lookup has "
              "actually failed");
        (void)tiku_trk_types_add(&db, "audio/", "Audio", "", NULL);
        snprintf(m.type, sizeof m.type, "audio/mp3");
        tiku_trk_nodeicon_deleted(&c, m.path);
        (void)tiku_trk_nodeicon_resolve(&c, NULL, &m, art, sizeof art);
        check(art[0] != '\0' && strcmp(art, "my_text_art") != 0, "IV-011",
              "a record that declares no art of its own leaves the "
              "compiled answer standing, rather than handing over an "
              "empty name");
        check(tiku_trk_nodeicon_misses(&c) == 1, "IV-011",
              "and a type with no art of its own is REMEMBERED as such, "
              "so the walk that proved it is paid once rather than per "
              "row of a folder full of them");
        tiku_trk_nodeicon_deleted(&c, m.path);
        (void)tiku_trk_nodeicon_resolve(&c, NULL, &m, art, sizeof art);
        check(tiku_trk_nodeicon_misses(&c) == 1 && art[0] != '\0',
              "IV-011",
              "asking again answers the same thing and records nothing "
              "new");
        tiku_trk_nodeicon_set_types(&c, &db);
        check(tiku_trk_nodeicon_misses(&c) == 0, "IV-011",
              "a different database makes every remembered miss an "
              "unasked question again -- what is missing is a question "
              "about the database");
    }

    printf("\n%s\n", fails ? "FAILURES" : "the icon tiers hold");
    return fails ? 1 : 0;
}
