/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_help_window.c - the help window, and where a link goes.
 *
 * The policy is the part worth pinning: which hrefs reach a page, which
 * reach a place in the page, which are handed to the desktop, and which
 * are refused out loud.  After that, the reading: Tab walks the links
 * and hands the Tab on when it comes round, Back returns to the
 * paragraph it left, and a page laid out again keeps the reader's place.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>

#include "../../apps/reader/tiku_reader_window.h"
#include "tiku_dl.h"
#include "tiku_font.h"
#include "tiku_measure.h"

static int fails;
static tiku_reader_help_window_t help;
static char opened[TIKU_PATH_MAX];
static int opens;

static void
check(int cond, const char *what)
{
    printf("  [%s] help   %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond) {
        fails++;
    }
}

static int
opener(void *ctx, const char *path)
{
    (void)ctx;
    snprintf(opened, sizeof opened, "%s", path);
    opens++;
    /* Everything but this one is somewhere this machine has. */
    return strcmp(path, "/devices/gone") != 0;
}

static void
write_page(const char *root, const char *name, const char *text)
{
    char path[TIKU_PATH_MAX];
    FILE *f;

    snprintf(path, sizeof path, "%s/%s", root, name);
    f = fopen(path, "w");
    if (f) {
        fputs(text, f);
        fclose(f);
    }
}

static void
key(tiku_window_t *w, unsigned k, unsigned mods)
{
    tiku_event_t ev = {0};

    ev.type = TIKU_EVENT_KEY_DOWN;
    ev.key = k;
    ev.modifiers = mods;
    (void)tiku_window_send(w, &ev);
}

int
main(void)
{
    const char *root = "/tmp/trk_help_fixture";
    tiku_surface_t *s;
    tiku_workspace_t *ws;
    int dirty = 0;

    tiku_font_set_size(12);
    (void)mkdir(root, 0700);
    write_page(root, "index.html",
        "<h1>Contents</h1>"
        "<p>Read about <a href=\"devices.html\">devices</a>, jump to "
        "<a href=\"#far\">the end</a>, open "
        "<a href=\"tiku:/devices/board1\">a board</a>, or try "
        "<a href=\"https://example.com/\">the web</a> and "
        "<a href=\"nosuch.html\">a page that is not there</a> and "
        "<a href=\"#nowhere\">a place that is not there</a>.</p>"
        "<h2 id=\"far\">The end</h2><p>the last words</p>");
    {
        /* Enough page to have somewhere to scroll to. */
        char path[TIKU_PATH_MAX];
        FILE *f;

        snprintf(path, sizeof path, "%s/index.html", root);
        f = fopen(path, "r+");
        if (f) {
            char all[TIKU_DOC_TEXT_MAX];
            size_t n = fread(all, 1, sizeof all - 1, f);
            char tail[4096];
            int at = 0;

            all[n] = 0;
            for (int i = 0; i < 40 && at < (int)sizeof tail - 40; i++) {
                at += snprintf(tail + at, sizeof tail - (size_t)at,
                               "<p>a filler paragraph numbered %d</p>", i);
            }
            fclose(f);
            f = fopen(path, "w");
            if (f) {
                /* The end heading stays last, so #far is the foot. */
                char *cut = strstr(all, "<h2 id=");

                if (cut) *cut = 0;
                fprintf(f, "%s%s<h2 id=\"far\">The end</h2>"
                           "<p>the last words</p>", all, tail);
                fclose(f);
            }
        }
    }
    write_page(root, "devices.html",
        "<h1>Devices</h1><p>a page of its own with "
        "<a href=\"index.html\">a way back</a>.</p>");

    s = tiku_surface_new(1000, 700, TIKU_C_PANEL);
    ws = tiku_workspace_new(s);
    tiku_reader_help_window_init(&help, ws, root, 1000, 700, &dirty);
    tiku_reader_help_window_opener(&help, opener, NULL);

    printf("\nthe window opens on the folder's own first page\n");
    {
        tiku_reader_help_window_open(&help);
        check(tiku_reader_help_window_handle(&help) != NULL, "there is a window");
        check(strcmp(tiku_reader_help_window_page(&help), "index.html") == 0,
              "and it is showing the contents");
        tiku_workspace_draw(ws);
        check(help.doc && tiku_doc_link_count(help.doc) == 6,
              "with every link of the page in it");
    }

    printf("\nwhere a link goes is decided here, not in the kit\n");
    {
        check(tiku_reader_help_window_go(&help, "#far") == TIKU_READER_HELP_ANCHOR,
              "a name is a place in the page now shown");
        check(help.bar.value > 0, "and the page moves to it");
        check(tiku_reader_help_window_go(&help, "#nowhere") ==
              TIKU_READER_HELP_NOWHERE, "a name it has not got is nowhere");
        check(tiku_reader_help_window_status(&help)[0] != 0,
              "and the window says so rather than moving");
        opens = 0;
        check(tiku_reader_help_window_go(&help, "tiku:/devices/board1") ==
              TIKU_READER_HELP_NAMESPACE, "a device path is the desktop's");
        check(opens == 1 && strcmp(opened, "/devices/board1") == 0,
              "and it is handed over whole");
        char web[256], missing[256];

        check(tiku_reader_help_window_go(&help, "https://example.com/") ==
              TIKU_READER_HELP_NOWHERE, "a web address is not followed");
        check(opens == 1, "and nothing is handed to the desktop for it");
        snprintf(web, sizeof web, "%s", tiku_reader_help_window_status(&help));
        (void)tiku_reader_help_window_go(&help, "nosuch.html");
        snprintf(missing, sizeof missing,
                 "%s", tiku_reader_help_window_status(&help));
        check(web[0] && strcmp(web, missing) != 0,
              "and it is refused as a web address, not as a missing page");
        check(tiku_reader_help_window_go(&help, "../../etc/passwd") ==
              TIKU_READER_HELP_NOWHERE, "nor is a name that climbs out");
        check(tiku_reader_help_window_go(&help, "nosuch.html") ==
              TIKU_READER_HELP_NOWHERE, "a page that is not there is refused");
        check(strcmp(tiku_reader_help_window_page(&help), "index.html") == 0,
              "and the page shown is the one that was there");
    }

    printf("\na page that is not there leaves the history alone\n");
    {
        tiku_reader_help_window_t fresh;

        /* Nothing has been read, so there is nothing to step back
         * from, and a refusal must leave the way back empty rather
         * than one below it. */
        tiku_reader_help_window_init(&fresh, ws, root, 800, 600, &dirty);
        check(tiku_reader_help_window_go(&fresh, "nosuch.html") ==
              TIKU_READER_HELP_NOWHERE, "a page that is not there is refused");
        check(fresh.nback == 0, "and the way back is still empty");
        check(tiku_reader_help_window_back(&fresh) == 0,
              "so back from it does nothing rather than reading past the end");
        tiku_reader_help_window_destroy(&fresh);
    }

    printf("\na page named with a place in it reaches the place\n");
    {
        /*
         * The href comes out of the page's own link table, which is
         * where every href the reader can follow comes from: it points
         * into the words the next page is about to overwrite.  A
         * literal here would pin a road the window never travels.
         */
        write_page(root, "deep.html",
            "<h1>Deep</h1><h2 id=\"top\">Top</h2>"
            "<p>one</p><p>two</p><p>three</p><p>four</p><p>five</p>"
            "<p>six</p><p>seven</p><p>eight</p><p>nine</p><p>ten</p>"
            "<p>eleven</p><p>twelve</p><p>thirteen</p><p>fourteen</p>"
            "<p>fifteen</p><p>sixteen</p><p>seventeen</p><p>eighteen</p>"
            "<h2 id=\"foot\">The foot</h2><p>the last of it</p>");
        write_page(root, "jump.html",
            "<h1>Jump</h1><p><a href=\"deep.html#foot\">the foot</a></p>");
        (void)tiku_reader_help_window_go(&help, "jump.html");
        tiku_workspace_draw(ws);
        check(help.doc && tiku_doc_link_count(help.doc) == 1,
              "a page whose one link names a place in another page");
        (void)tiku_reader_help_window_go(&help,
            tiku_doc_link_href(help.doc, 0));
        tiku_workspace_draw(ws);
        check(strcmp(tiku_reader_help_window_page(&help), "deep.html") == 0,
              "the page it named opens");
        {
            int foot = tiku_doc_anchor_y(help.doc, "foot");
            int top = tiku_doc_anchor_y(help.doc, "top");

            check(foot > top && top >= 0, "which has both places in it");
            check(help.bar.value > top,
                  "and the reader is at the place, not at the one whose "
                  "name happened to lie at that offset");
        }
        check(tiku_reader_help_window_wanted(&help)[0] != '#',
              "and a place is not mistaken for a path to hand over");
    }

    printf("\nback returns to the paragraph it left\n");
    {
        int was;

        /* From the contents, whatever the block before was reading. */
        (void)tiku_reader_help_window_go(&help, "index.html");
        tiku_workspace_draw(ws);
        tiku_scroll_to(&help.bar, 120);
        was = help.bar.value;
        check(tiku_reader_help_window_go(&help, "devices.html") ==
              TIKU_READER_HELP_PAGE, "another page of the folder opens");
        check(strcmp(tiku_reader_help_window_page(&help), "devices.html") == 0,
              "and it is the one that was named");
        check(help.bar.value == 0, "a new page starts at its top");
        tiku_workspace_draw(ws);
        check(tiku_reader_help_window_back(&help) == 1, "back steps back");
        check(strcmp(tiku_reader_help_window_page(&help), "index.html") == 0 &&
              help.bar.value == was,
              "to the page and the paragraph it was left on");
        check(tiku_reader_help_window_forward(&help) == 1 &&
              strcmp(tiku_reader_help_window_page(&help), "devices.html") == 0,
              "and forward returns to where back came from");
        check(tiku_reader_help_window_back(&help) == 1, "back once more");
        while (tiku_reader_help_window_back(&help)) { /* to the beginning */ }
        check(tiku_reader_help_window_back(&help) == 0,
              "and back from the first page does nothing at all");
    }

    printf("\nthe keyboard reaches every link, then hands the Tab on\n");
    {
        tiku_window_t *w = tiku_reader_help_window_handle(&help);
        int walked = 0;

        (void)tiku_reader_help_window_go(&help, "index.html");
        tiku_workspace_draw(ws);
        tiku_dialog_focus(&help.dialog, TIKU_READER_HELP_CONTROL_PAGE);
        for (int i = 0; i < 8; i++) {
            int before = help.focus_link;

            key(w, TIKU_KEY_TAB, 0);
            if (help.focus_link != before && help.focus_link != -1) walked++;
        }
        check(walked >= 6, "Tab reaches every link of the page");
        check(help.focus_link == -1,
              "and the Tab after the last one is the panel's again");
    }

    printf("\na link is followed where a reader sees it\n");
    {
        /* Where to click is read off the drawing rather than worked out
         * again here: the page is followed at the pixels it was drawn
         * at, which is the whole claim the piece table makes. */
        tiku_window_t *w = tiku_reader_help_window_handle(&help);
        tiku_dl_t *dl = tiku_dl_new();
        char wire[65536];
        size_t len = 0, at = 0;
        tiku_dl_fact_t f;
        int hit_x = -1, hit_y = -1;

        (void)tiku_reader_help_window_go(&help, "index.html");
        /* Not at the top: a hit that forgets the scroll lands on the
         * line above, and at the top there is no line above to catch it. */
        tiku_workspace_draw(ws);
        tiku_scroll_to(&help.bar, 20);
        s->record = dl;
        tiku_workspace_draw(ws);
        s->record = NULL;
        (void)tiku_dl_flatten(dl, wire, sizeof wire, &len);
        {
            /* The line under the first link is as wide as its own
             * words; a button's bevel is a hairline too. */
            int piece = 0, n = 0, want = 0;

            (void)tiku_doc_link_pieces(help.doc, 0, &piece, &n);
            want = tiku_doc_piece_rect(help.doc, piece).w;
            while (tiku_dl_read(wire, len, &at, &f)) {
                if (f.kind == TIKU_DL_FACT_PAINT && f.rect.h == 1 &&
                    f.rect.w == want && hit_x < 0) {
                    hit_x = f.rect.x + f.rect.w / 2;
                    hit_y = f.rect.y - 3;
                }
            }
        }
        check(hit_x > 0, "the first link was drawn with a line under it");
        if (hit_x > 0) {
            tiku_event_t press = {0};

            press.type = TIKU_EVENT_POINTER_DOWN;
            press.button = 1;
            press.x = hit_x;
            press.y = hit_y;
            (void)tiku_window_send(w, &press);
            check(strcmp(tiku_reader_help_window_page(&help),
                         "devices.html") == 0,
                  "and pressing it there opens the page it names");
        }
        tiku_dl_free(dl);
        (void)tiku_reader_help_window_go(&help, "index.html");
        tiku_workspace_draw(ws);
    }

    printf("\na narrower window lays the page out again\n");
    {
        tiku_window_t *w = tiku_reader_help_window_handle(&help);
        int tall, taller;
        int original_width = w->frame.w;

        tiku_workspace_draw(ws);
        tall = tiku_doc_height(help.doc);
        w->frame.w = 360;
        tiku_workspace_draw(ws);
        taller = tiku_doc_height(help.doc);
        check(taller > tall, "the same page in less width is more page");
        check(tiku_doc_width(help.doc) < 360 &&
              tiku_doc_width(help.doc) > 0,
              "laid out for the room the narrower window leaves");
        w->frame.w = original_width;
        tiku_workspace_draw(ws);
        check(tiku_doc_height(help.doc) == tall,
              "and back again when it grows");
    }

    printf("\nthe page moves under the keys that move a page\n");
    {
        tiku_window_t *w = tiku_reader_help_window_handle(&help);
        int top, down, paged;

        tiku_scroll_to(&help.bar, 0);
        tiku_workspace_draw(ws);
        top = help.bar.value;
        key(w, TIKU_KEY_DOWN, 0);
        down = help.bar.value;
        key(w, TIKU_KEY_PAGE_DOWN, 0);
        paged = help.bar.value;
        check(down - top == tiku_scroll_line(&help.bar), "a line,");
        check(paged - down == tiku_scroll_page(&help.bar) &&
              paged - down > down - top, "then a page, which is further");
        key(w, TIKU_KEY_END, 0);
        check(help.bar.value >= paged, "End reaches the foot");
        key(w, TIKU_KEY_HOME, 0);
        check(help.bar.value == 0, "and Home the top");
    }

    printf("\nDone closes the window from inside its own handler\n");
    {
        tiku_window_t *w = tiku_reader_help_window_handle(&help);
        tiku_dl_t *dl = tiku_dl_new();
        char wire[65536];
        size_t len = 0, at = 0;
        tiku_dl_fact_t f;
        tiku_rect_t done = {0, 0, 0, 0};
        tiku_event_t ev = {0};

        s->record = dl;
        tiku_workspace_draw(ws);
        s->record = NULL;
        (void)tiku_dl_flatten(dl, wire, sizeof wire, &len);
        while (tiku_dl_read(wire, len, &at, &f)) {
            if (f.kind == TIKU_DL_FACT_BUTTON && f.text &&
                !strcmp(f.text, "Done")) done = f.rect;
        }
        tiku_dl_free(dl);
        check(done.w > 0, "the Done button was drawn, and says where");
        ev.type = TIKU_EVENT_POINTER_DOWN;
        ev.button = 1;
        ev.x = done.x + done.w / 2;
        ev.y = done.y + done.h / 2;
        (void)tiku_window_send(w, &ev);
        ev.type = TIKU_EVENT_POINTER_UP;
        (void)tiku_window_send(w, &ev);
        check(tiku_reader_help_window_handle(&help) == NULL,
              "and pressing it closes the window it is in");
        tiku_reader_help_window_open(&help);
        check(tiku_reader_help_window_handle(&help) != NULL,
              "which opens again after");
        tiku_workspace_draw(ws);
    }

    printf("\na device path that is not one is refused here\n");
    {
        int before = opens;

        check(tiku_reader_help_window_go(&help, "tiku:devices/board1") ==
              TIKU_READER_HELP_NOWHERE, "a device path must start at the root");
        check(opens == before, "and nothing is handed over for it to drop");
        check(tiku_reader_help_window_go(&help, "tiku:/devices/gone") ==
              TIKU_READER_HELP_NOWHERE,
              "a place the machine has not got is a link that says so");
        check(tiku_reader_help_window_status(&help)[0] != 0,
              "rather than one that looks followed and did nothing");
    }

    printf("\na window with no folder still opens, and says why\n");
    {
        tiku_reader_help_window_t lost;

        tiku_reader_help_window_init(&lost, ws, "/tmp/trk_help_none", 800, 600,
                                  &dirty);
        tiku_reader_help_window_open(&lost);
        check(tiku_reader_help_window_handle(&lost) != NULL,
              "there is a window to read the refusal in");
        check(tiku_reader_help_window_status(&lost)[0] != 0,
              "and it says there are no pages");
        tiku_workspace_draw(ws);
        tiku_reader_help_window_destroy(&lost);
    }

    printf("\na change of text size lays the page out again\n");
    {
        int sizes[] = {10, 12, 14, 16};
        int height[4], ok = 1;

        (void)tiku_reader_help_window_go(&help, "index.html");
        for (size_t i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
            tiku_font_set_size(sizes[i]);
            tiku_workspace_draw(ws);
            height[i] = tiku_doc_height(help.doc);
            if (!help.doc || !tiku_doc_check(help.doc)) ok = 0;
            if (height[i] <= 0) ok = 0;
        }
        check(ok, "the page holds together at all four sizes");
        /* The window's width never moved, so a page laid out for the
         * width alone would answer the same height four times. */
        check(height[3] > height[0],
              "and bigger letters are a taller page, not the same one");
        /* What is drawn is what is measured: a piece's width is the
         * width of its own words in the size now in force. */
        {
            int wrong = 0;

            tiku_font_set_size(16);
            tiku_workspace_draw(ws);
            for (int i = 0; i < tiku_doc_piece_count(help.doc); i++) {
                const tiku_doc_piece_t *pc = tiku_doc_piece(help.doc, i);
                char word[256];
                int face = tiku_doc_run(help.doc, pc->run)->face;
                int n = pc->len < 255 ? pc->len : 255;

                memcpy(word, tiku_doc_text(help.doc, pc->off), (size_t)n);
                word[n] = 0;
                if (pc->w != tiku_measure_width(face, word)) wrong++;
            }
            check(wrong == 0,
                  "every piece is as wide as its words are at that size");
            tiku_font_set_size(12);
            tiku_workspace_draw(ws);
        }
    }

    printf("\nthe bar is grabbed where it is drawn\n");
    {
        /* Where the bar is comes off the drawing, not out of this file:
         * a coordinate written down here goes stale the moment the
         * window's layout moves. */
        tiku_window_t *w = tiku_reader_help_window_handle(&help);
        tiku_dl_t *dl = tiku_dl_new();
        char wire[65536];
        size_t len = 0, at = 0;
        tiku_dl_fact_t f;
        tiku_rect_t bar = {0, 0, 0, 0};
        tiku_event_t ev = {0};

        (void)tiku_reader_help_window_go(&help, "index.html");
        tiku_scroll_to(&help.bar, 0);
        s->record = dl;
        tiku_workspace_draw(ws);
        s->record = NULL;
        (void)tiku_dl_flatten(dl, wire, sizeof wire, &len);
        while (tiku_dl_read(wire, len, &at, &f)) {
            if (f.kind == TIKU_DL_FACT_SCROLLBAR) bar = f.rect;
        }
        tiku_dl_free(dl);
        check(bar.w > 0 && bar.h > 0, "the bar was drawn, and says where");
        ev.type = TIKU_EVENT_POINTER_DOWN;
        ev.button = 1;
        ev.x = bar.x + bar.w / 2;
        ev.y = bar.y + bar.h - bar.w / 2;       /* the arrow at the foot */
        (void)tiku_window_send(w, &ev);
        check(help.bar.value == tiku_scroll_line(&help.bar),
              "the arrow at its foot steps on by one line");
        ev.type = TIKU_EVENT_POINTER_UP;
        (void)tiku_window_send(w, &ev);
        ev.type = TIKU_EVENT_POINTER_DOWN;
        ev.y = bar.y + bar.w / 2;               /* the arrow at the head */
        (void)tiku_window_send(w, &ev);
        check(help.bar.value == 0, "and the one at its head steps back");
        ev.type = TIKU_EVENT_POINTER_UP;
        (void)tiku_window_send(w, &ev);
        /* And the thumb is taken hold of and dragged, within the track
         * the bar was drawn with rather than its whole height. */
        ev.type = TIKU_EVENT_POINTER_DOWN;
        ev.y = bar.y + bar.w + 2;
        (void)tiku_window_send(w, &ev);
        ev.type = TIKU_EVENT_POINTER_MOVE;
        ev.y = bar.y + bar.h - bar.w - 2;
        (void)tiku_window_send(w, &ev);
        check(help.bar.value > tiku_scroll_max(&help.bar) / 2,
              "a thumb dragged to the foot of the track is near the foot");
        /* And to the head of the track is the top of the page: a drag
         * measured against the whole bar instead of the track between
         * its arrows lands short by an arrow's worth. */
        ev.type = TIKU_EVENT_POINTER_MOVE;
        ev.y = bar.y + bar.w;
        (void)tiku_window_send(w, &ev);
        check(help.bar.value == 0,
              "and dragged back to its head is the top of the page");
        ev.type = TIKU_EVENT_POINTER_UP;
        (void)tiku_window_send(w, &ev);
        check(help.bar.tracking == 0, "and letting go lets go");
    }

    printf("\ndesktop pages grow beyond the old embedded text budget\n");
    {
        char big[TIKU_READER_HELP_PAGE_MAX * 2];
        char path[TIKU_PATH_MAX];
        FILE *f;
        int at = snprintf(big, sizeof big, "<h1>Long</h1>");

        while (at < (int)sizeof big - 48) {
            at += snprintf(big + at, sizeof big - (size_t)at,
                           "<p>a paragraph of the long page</p>");
        }
        snprintf(path, sizeof path, "%s/long.html", root);
        f = fopen(path, "w");
        if (f) { fputs(big, f); fclose(f); }
        check(tiku_reader_help_window_go(&help, "long.html") ==
              TIKU_READER_HELP_PAGE, "a page longer than 16 KiB opens in full");
        check(help.oversize == 0 && help.doc->ntext > TIKU_READER_HELP_PAGE_MAX,
              "and desktop storage reads past the old 16 KiB boundary");
        (void)tiku_reader_help_window_go(&help, "index.html");
        check(help.oversize == 0, "a page that fitted is not said to be cut");
    }

    printf("\nthe contents is a move like any other\n");
    {
        (void)tiku_reader_help_window_go(&help, "devices.html");
        check(tiku_reader_help_window_back(&help) == 1, "back from a page");
        check(help.nforward == 1, "leaves a way forward");
        check(tiku_reader_help_window_home(&help) == 1, "Contents opens");
        check(help.nforward == 0,
              "and Contents clears it: forward would be where the reader "
              "has never been");
    }

    tiku_reader_help_window_destroy(&help);
    tiku_workspace_free(ws);
    tiku_surface_free(s);
    {
        static const char *const wrote[] = {"index.html", "devices.html",
                                            "deep.html", "jump.html",
                                            "long.html"};

        for (size_t i = 0; i < sizeof wrote / sizeof wrote[0]; i++) {
            char path[TIKU_PATH_MAX];

            snprintf(path, sizeof path, "%s/%s", root, wrote[i]);
            (void)remove(path);
        }
        (void)remove(root);
    }
    /*
     * The folder this desktop ships, read as a reader would read it: a
     * page that has outgrown the arena, a link to a page nobody wrote
     * and a name no heading wears are all things nothing else would
     * catch until somebody pressed them.
     */
    printf("\nevery page of the shipped help folder reads and links\n");
    {
        /* The folder is WALKED, not listed: a page added to it is
         * checked without this file being edited, which is how a new
         * page arrives with a link nobody followed. */
        enum { SHIPPED_MAX = 128 };
        char shipped[SHIPPED_MAX][TIKU_PATH_MAX];
        int nshipped = 0;
        static const char *const where[] = {"", "apps", "fr", "fr/apps"};
        const char *folder = "assets/help";
        tiku_doc_t *page = calloc(1, sizeof *page);
        int read_ok = 1, laid = 1, links = 1, anchors = 1, entities = 1;
        int pages = 0;

        if (access("assets/help/index.html", R_OK) != 0) {
            folder = "help";
        }
        for (size_t w = 0; w < sizeof where / sizeof where[0]; w++) {
            char dir[TIKU_PATH_MAX];
            struct dirent *de;
            DIR *d;

            snprintf(dir, sizeof dir, "%s%s%s", folder,
                     *where[w] ? "/" : "", where[w]);
            d = opendir(dir);
            if (d == NULL) continue;
            while ((de = readdir(d)) != NULL && nshipped < SHIPPED_MAX) {
                const char *dot = strrchr(de->d_name, '.');

                if (dot == NULL || strcmp(dot, ".html") != 0) continue;
                snprintf(shipped[nshipped], sizeof shipped[0], "%s%s%s",
                         where[w], *where[w] ? "/" : "", de->d_name);
                nshipped++;
            }
            closedir(d);
        }
        for (int i = 0; page && i < nshipped; i++) {
            char path[TIKU_PATH_MAX];
            char text[TIKU_DOC_TEXT_MAX + 64];
            FILE *f;
            size_t n;

            snprintf(path, sizeof path, "%s/%s", folder, shipped[i]);
            f = fopen(path, "r");
            if (f == NULL) { read_ok = 0; continue; }
            n = fread(text, 1, sizeof text - 1, f);
            fclose(f);
            text[n] = '\0';
            pages++;
            /* Nothing cut: a page over the arena loses its tail, and
             * the tail is where a page puts what it links to. */
            if (!tiku_doc_read(page, text)) read_ok = 0;
            for (int w = 240; w <= 720; w += 240) {
                tiku_doc_layout(page, w);
                if (!tiku_doc_check(page)) laid = 0;
            }
            /* An entity this dialect does not know survives as its own
             * text, which is a page with "&mdash;" printed in it. */
            for (int k = 0; k < tiku_doc_piece_count(page); k++) {
                const tiku_doc_piece_t *pc = tiku_doc_piece(page, k);
                const char *t = tiku_doc_text(page, pc->off);

                for (int c = 0; c + 2 < pc->len; c++) {
                    if (t[c] != '&') continue;
                    for (int e = c + 1; e < pc->len && e < c + 10; e++) {
                        if (t[e] == ';') entities = 0;
                        if (t[e] < 'a' || t[e] > 'z') break;
                    }
                }
            }
            for (int k = 0; k < tiku_doc_link_count(page); k++) {
                const char *href = tiku_doc_link_href(page, k);
                char other[TIKU_PATH_MAX];

                if (href[0] == '/' || !*href) continue;
                if (href[0] == '#') {
                    if (tiku_doc_anchor_y(page, href + 1) < 0) {
                        printf("         (%s: no anchor %s)\n", shipped[i],
                               href);
                        anchors = 0;
                    }
                    continue;
                }
                /* A link may name a place in another page: the reader
                 * splits at the '#', so the check does too, and then
                 * asks that page whether it wears the name. */
                const char *hash = strchr(href, '#');
                size_t nlen = hash ? (size_t)(hash - href) : strlen(href);

                snprintf(other, sizeof other, "%s/%.*s", folder,
                         (int)nlen, href);
                if (access(other, R_OK) != 0) {
                    printf("         (%s: no page %.*s)\n", shipped[i],
                           (int)nlen, href);
                    links = 0;
                } else if (hash != NULL && hash[1] != '\0') {
                    tiku_doc_t *far = calloc(1, sizeof *far);
                    char ftext[TIKU_DOC_TEXT_MAX + 64];
                    FILE *ff = fopen(other, "r");
                    size_t fn = ff ? fread(ftext, 1, sizeof ftext - 1, ff) : 0;

                    if (ff) fclose(ff);
                    ftext[fn] = '\0';
                    if (far != NULL) {
                        tiku_doc_read(far, ftext);
                        tiku_doc_layout(far, 400);
                        if (tiku_doc_anchor_y(far, hash + 1) < 0) {
                            printf("         (%s: %s has no %s)\n",
                                   shipped[i], other, hash + 1);
                            anchors = 0;
                        }
                        free(far);
                    }
                }
            }
        }
        check(nshipped >= 15 && pages == nshipped,
              "every page in the folder was read");
        check(nshipped < SHIPPED_MAX,
              "and the walk reached the end of it rather than its own "
              "ceiling, which would check fewer pages in silence");
        check(read_ok, "and each one fits the reader's arena whole");
        check(laid, "and lays out at three widths with its tables agreeing");
        check(entities, "and spells every entity in the dialect's own set");
        check(links, "every link between pages reaches a page that exists");
        check(anchors, "and every name reaches a heading that wears it");
        free(page);
    }

    printf("\n%s\n", fails ? "FAILURES" : "the pages open where they say");
    return fails ? 1 : 0;
}
