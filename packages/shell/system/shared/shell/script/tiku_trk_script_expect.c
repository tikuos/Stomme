/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_script_expect.c - the checks a script makes, parsed and run:
 * files on disk and what they hold, the focused window, the Desktop, what
 * the shell says, and what was painted.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_state.h"
#include "tiku_trk_script_private.h"

/**
 * @brief Run a file step: expect-perm, remove, write, expect-file and
 *        expect-no-file, and also expect-alert.
 *
 * @return 1 for a step of this group, 0 for another group's.
 */
int
sc_pump_file_state(tiku_trk_script_t *s, const tiku_trk_script_state_t *st,
                   tiku_trk_script_step_t *out, sc_step_t *cur)
{
    (void)out;
    switch (cur->op) {

    case OP_EXPECT_PERM: {
        struct stat sb;
        char got[4];

        got[0] = got[1] = got[2] = '-';
        got[3] = '\0';
        if (stat(cur->text, &sb) == 0) {
            if (sb.st_mode & S_IRUSR) { got[0] = 'r'; }
            if (sb.st_mode & S_IWUSR) { got[1] = 'w'; }
            if (sb.st_mode & S_IXUSR) { got[2] = 'x'; }
        }
        sc_verdict(s, strcmp(got, cur->text2) == 0, "owner bits:", got);
        return 1;
    }
    case OP_REMOVE:
        if (remove(cur->text) != 0) {
            fprintf(stderr, "script: cannot remove '%s'\n", cur->text);
        }
        return 1;
    case OP_EXPECT_ALERT:
        /* "exists" asks only that one is up; anything else must appear in
         * its words. */
        sc_verdict(s, st != NULL && st->alert != NULL &&
                   (strcmp(cur->text, "exists") == 0 ||
                    strstr(st->alert, cur->text) != NULL),
                "an alert says", cur->text);
        return 1;
    case OP_EXPECT_FILE:
        sc_verdict(s, sc_there(cur->text), "exists:", cur->text);
        return 1;
    case OP_EXPECT_NO_FILE:
        sc_verdict(s, !sc_there(cur->text), "gone:", cur->text);
        return 1;
    case OP_WRITE: {
        FILE *f;

        (void)tiku_state_mkparents(cur->text);
        f = fopen(cur->text, "w");

        if (f != NULL) {
            (void)fputs(cur->text2, f);
            (void)fputc('\n', f);
            (void)fclose(f);
        }
        return 1;
    }
    default:
        return 0;
    }
}

/**
 * @brief Run a Desktop check: expect-desk-selected, its -name form, and
 *        expect-desk-icon and expect-no-desk-icon.
 *
 * The icon checks look up the name with pose_desk_at, the Desktop's poses
 * only, or with pose_at when the shell sets no pose_desk_at.
 *
 * @return 1 for a step of this group, 0 for another group's.
 */
int
sc_pump_desktop(tiku_trk_script_t *s, const tiku_trk_script_state_t *st,
                tiku_trk_script_step_t *out, sc_step_t *cur)
{
    (void)out;
    switch (cur->op) {

    case OP_EXPECT_DESKSEL: {
        char n[32];

        snprintf(n, sizeof n, "%d", (st != NULL) ? st->desk_selected : -1);
        snprintf(n, sizeof n, "%d", (st != NULL) ? st->desk_selected : -1);
        sc_verdict(s, st != NULL && st->desk_selected == cur->count,
                "desktop selected:", n);
        return 1;
    }
    case OP_EXPECT_DESKSELNAME:
        sc_verdict(s, st != NULL && st->desk_sel_name != NULL &&
                strcmp(st->desk_sel_name, cur->text) == 0,
                "desktop selected:",
                (st != NULL && st->desk_sel_name != NULL)
                    ? st->desk_sel_name : "nothing");
        return 1;
    case OP_EXPECT_NO_DESKICON: {
        int px = 0, py = 0, got = 0;
        int (*at)(void *, const char *, int *, int *) =
            (st != NULL && st->pose_desk_at != NULL) ? st->pose_desk_at
            : (st != NULL) ? st->pose_at : NULL;

        if (at != NULL) {
            got = at(st->pose_ctx, cur->text, &px, &py);
        }
        sc_verdict(s, !got, "gone from the Desktop:", cur->text);
        return 1;
    }
    case OP_EXPECT_DESKICON: {
        int px = 0, py = 0, got = 0;
        int (*at)(void *, const char *, int *, int *) =
            (st != NULL && st->pose_desk_at != NULL) ? st->pose_desk_at
            : (st != NULL) ? st->pose_at : NULL;
        char said[64];

        if (at != NULL) {
            got = at(st->pose_ctx, cur->text, &px, &py);
        }
        snprintf(said, sizeof said, "%s %s", cur->text,
                 got ? "is on the Desktop" : "is not there");
        sc_verdict(s, got, "desktop:", said);
        return 1;
    }
    default:
        return 0;
    }
}

/**
 * @brief Run expect-said, expect-under or expect-over: a noun's whole
 *        answer, or its number against a bound.
 *
 * @return 1 for one of these, 0 for another step.
 */
static int
sc_said_noun(tiku_trk_script_t *s, const tiku_trk_script_state_t *st,
             sc_step_t *cur)
{
    switch (cur->op) {
    case OP_EXPECT_SAID: {
        char text[2048];

        text[0] = '\0';
        if (st != NULL && st->say != NULL) {
            (void)st->say(st->say_ctx, cur->text2, text, sizeof text);
        }
        sc_verdict(s, strcmp(text, cur->text) == 0, cur->text2, cur->text);
        if (s->fails > 0 && strcmp(text, cur->text) != 0) {
            printf("         (said: \"%s\")\n", text);
        }
        return 1;
    }
    case OP_EXPECT_UNDER:
    case OP_EXPECT_OVER: {
        char text[64], said[96];
        long got;
        int under = cur->op == OP_EXPECT_UNDER;

        text[0] = '\0';
        if (st != NULL && st->say != NULL) {
            (void)st->say(st->say_ctx, cur->text2, text, sizeof text);
        }
        got = atol(text);
        snprintf(said, sizeof said, "%s is %ld, %s %d", cur->text2, got,
                 under ? "under" : "over", cur->count);
        sc_verdict(s, text[0] != '\0' &&
                   (under ? got < cur->count : got > cur->count), said, "");
        return 1;
    }
    default:
        return 0;
    }
}

/**
 * @brief Run a check of what the shell says: the count, the menu, the
 *        sounds and the volume, the mounts and controls, a namespace read,
 *        the operation, and the narration.
 *
 * @return 1 for a step of this group, 0 for another group's.
 */
int
sc_pump_said(tiku_trk_script_t *s, const tiku_trk_script_state_t *st,
             tiku_trk_script_step_t *out, sc_step_t *cur)
{
    (void)out;
    if (sc_said_noun(s, st, cur)) {
        return 1;
    }
    switch (cur->op) {

    case OP_EXPECT_COUNT:
        sc_verdict(s, st != NULL && st->count != NULL &&
                strstr(st->count, cur->text) != NULL, "count says:",
                (st != NULL && st->count != NULL) ? st->count : "nothing");
        return 1;
    case OP_EXPECT_MENU:
        sc_verdict(s, st != NULL && st->menu != NULL &&
                strstr(st->menu, cur->text) != NULL, "menu:",
                (st != NULL && st->menu != NULL) ? st->menu : "nothing");
        return 1;
    case OP_EXPECT_NO_MENU:
        sc_verdict(s, st != NULL && st->menu != NULL && st->menu[0] == '\0',
                "menu:", "nothing");
        return 1;
    case OP_EXPECT_SOUND:
    case OP_EXPECT_NO_SOUND:
    case OP_EXPECT_SOUND_ON:
    case OP_EXPECT_SOUND_OFF: {
        /* The "sounds" noun answers name:count:on|off for each moment.  A
         * scripted run is mute: the count says a moment sounded, the flag
         * whether it is enabled. */
        char text[2048], key[80];
        const char *hit, *tail = "";
        unsigned count = 0;
        int ok;

        text[0] = '\0';
        if (st != NULL && st->say != NULL) {
            (void)st->say(st->say_ctx, "sounds", text, sizeof text);
        }
        snprintf(key, sizeof key, "%s:", cur->text);
        hit = strstr(text, key);
        if (hit != NULL) {
            char *end;

            count = (unsigned)strtoul(hit + strlen(key), &end, 10);
            tail = (*end == ':') ? end + 1 : "";
        }
        ok = (cur->op == OP_EXPECT_SOUND)     ? count > 0
           : (cur->op == OP_EXPECT_NO_SOUND)  ? count == 0
           : (cur->op == OP_EXPECT_SOUND_ON)  ? strncmp(tail, "on", 2) == 0
                                              : strncmp(tail, "off", 3) == 0;
        sc_verdict(s, ok,
                   (cur->op == OP_EXPECT_SOUND)    ? "sounded:"
                   : (cur->op == OP_EXPECT_NO_SOUND) ? "silent:"
                   : (cur->op == OP_EXPECT_SOUND_ON) ? "on:" : "off:",
                   cur->text);
        return 1;
    }
    case OP_EXPECT_VOLUME: {
        char text[2048], want[32];
        const char *hit;
        int got = -1;

        text[0] = '\0';
        if (st != NULL && st->say != NULL) {
            (void)st->say(st->say_ctx, "sounds", text, sizeof text);
        }
        hit = strstr(text, "volume:");
        if (hit != NULL) {
            got = atoi(hit + strlen("volume:"));
        }
        snprintf(want, sizeof want, "%d, got %d", cur->count, got);
        sc_verdict(s, got == cur->count, "volume:", want);
        return 1;
    }
    case OP_EXPECT_MOUNTS:
    case OP_EXPECT_CONTROL: {
        /* Asked through the shell's noun table, which the conductor's
         * QUERY also answers from. */
        const char *noun =
            (cur->op == OP_EXPECT_MOUNTS) ? "mounts" : "controls";
        char text[2048];

        text[0] = '\0';
        if (st != NULL && st->say != NULL) {
            (void)st->say(st->say_ctx, noun, text, sizeof text);
        }
        sc_verdict(s, strstr(text, cur->text) != NULL, noun, cur->text);
        if (s->fails > 0 && text[0] != '\0') {
            printf("         (%s: \"%s\")\n", noun, text);
        }
        return 1;
    }
    case OP_EXPECT_READ: {
        /* A path is a noun as well: say() answers it with the node's value
         * or the container's children. */
        char text[2048];

        text[0] = '\0';
        if (st != NULL && st->say != NULL) {
            (void)st->say(st->say_ctx, cur->text2, text, sizeof text);
        }
        sc_verdict(s, strstr(text, cur->text) != NULL, cur->text2, cur->text);
        if (s->fails > 0 && text[0] != '\0') {
            printf("         (read: \"%s\")\n", text);
        }
        return 1;
    }
    case OP_EXPECT_OPERATION: {
        /* Asked again each turn, within the turn budget in count, until
         * the text appears: a job takes turns to reach a state. */
        char text[2048];

        text[0] = '\0';
        if (st != NULL && st->say != NULL) {
            (void)st->say(st->say_ctx, "operation", text, sizeof text);
        }
        if (strstr(text, cur->text) != NULL) {
            sc_verdict(s, 1, "operation says", cur->text);
            return 1;
        }
        if (cur->count-- > 0) {
            s->cursor--;
            s->waiting = 1;
            return 1;
        }
        sc_verdict(s, 0, "operation says", cur->text);
        if (text[0] != '\0') {
            printf("         (operation: \"%s\")\n", text);
        }
        return 1;
    }
    case OP_EXPECT_TOLD: {
        /* The narration is read again each turn, within the turn budget
         * in count, until it holds the text.  tests/tools/trk_conduct.c
         * runs the same line by waiting for the TOLD frame the shell
         * pushes when the narration changes. */
        char lines[2048];

        lines[0] = '\0';
        if (st != NULL && st->narrate != NULL) {
            (void)st->narrate(st->pose_ctx, lines, sizeof lines);
        }
        if (strstr(lines, cur->text) != NULL) {
            sc_verdict(s, 1, "the window comes to say", cur->text);
            return 1;
        }
        if (cur->count-- > 0) {
            s->cursor--;        /* the same line again, next turn */
            s->waiting = 1;
            return 1;
        }
        sc_verdict(s, 0, "the window comes to say", cur->text);
        if (lines[0] != '\0') {
            printf("         (narration: \"%s\")\n", lines);
        }
        return 1;
    }
    case OP_EXPECT_NARRATE:
    case OP_EXPECT_NO_NARRATE: {
        /* The focused window as its display list narrates it, the answer
         * the conductor's "narrate" noun also gives. */
        char lines[2048];
        int said;

        lines[0] = '\0';
        if (st != NULL && st->narrate != NULL) {
            (void)st->narrate(st->pose_ctx, lines, sizeof lines);
        }
        said = strstr(lines, cur->text) != NULL;
        if (cur->op == OP_EXPECT_NO_NARRATE) {
            sc_verdict(s, !said, "narration does not say", cur->text);
        } else {
            sc_verdict(s, said, "narration says", cur->text);
        }
        if (s->fails > 0 && lines[0] != '\0') {
            printf("         (narration: \"%s\")\n", lines);
        }
        return 1;
    }
    default:
        return 0;
    }
}

/**
 * @brief Run expect-file-has, expect-file-lacks or expect-link: what a file
 *        holds and what a link says.
 *
 * @return 1 for a step of this group, 0 for another group's.
 */
int
sc_pump_file_contents(tiku_trk_script_t *s, const tiku_trk_script_state_t *st,
                      tiku_trk_script_step_t *out, sc_step_t *cur)
{
    (void)st;
    (void)out;
    switch (cur->op) {

    case OP_EXPECT_FILEHAS:
    case OP_EXPECT_FILELACKS: {
        char path[sizeof cur->text + sizeof cur->text2];
        char needle[sizeof cur->text2];
        FILE *f;
        /* Room for a crash report, where the text asked about can lie
         * well past the first few hundred bytes. */
        char got[8192] = "";
        int ok = 0;

        /* A path may hold spaces: the path is the longest prefix of the
         * line that names an existing file, and the needle is the rest.
         * Longest, so "New Query 2" is not shadowed by a "New Query"
         * beside it. */
        {
            char line[sizeof path];
            size_t k, len, best = 0;
            struct stat pst;

            snprintf(line, sizeof line, "%s%s%s", cur->text,
                     cur->text2[0] != '\0' ? " " : "", cur->text2);
            len = strlen(line);
            for (k = strlen(cur->text); k <= len; k++) {
                if (line[k] != '\0' && line[k] != ' ') {
                    continue;
                }
                snprintf(path, sizeof path, "%.*s", (int)k, line);
                if (stat(path, &pst) == 0) {
                    best = k;
                }
            }
            if (best == 0) {
                best = strlen(cur->text);
            }
            snprintf(path, sizeof path, "%.*s", (int)best, line);
            snprintf(needle, sizeof needle, "%s",
                     (best < len) ? line + best + 1 : "");
        }
        f = fopen(path, "r");
        if (f != NULL) {
            size_t rd = fread(got, 1u, sizeof got - 1u, f);

            got[rd] = '\0';
            (void)fclose(f);
            ok = (needle[0] != '\0' && strstr(got, needle) != NULL);
        }
        /* The lacks form asserts that the needle is absent; a file that
         * cannot be read lacks it too.  A setting back at its default
         * leaves the settings file, so this is how it is checked. */
        if (cur->op == OP_EXPECT_FILELACKS) {
            ok = (needle[0] != '\0' && strstr(got, needle) == NULL);
            sc_verdict(s, ok, "absent from file:", cur->text2);
        } else {
            sc_verdict(s, ok, "in file:", cur->text2);
        }
        return 1;
    }
    case OP_EXPECT_LINK: {
        char target[512];
        ssize_t got = readlink(cur->text, target, sizeof target - 1u);

        /* The link's own text, as readlink returns it: a relative and an
         * absolute link to one file differ only there. */
        target[(got > 0) ? (size_t)got : 0u] = '\0';
        sc_verdict(s, got > 0 && strcmp(target, cur->text2) == 0,
                "link says:", (got > 0) ? target : "nothing");
        return 1;
    }
    default:
        return 0;
    }
}

/**
 * @brief Run a check of the focused window: status, title, scroll, rows,
 *        windows, selection, renaming, Find, location, navigation, type
 *        narrowing and columns.
 *
 * @return 1 for a step of this group, 0 for another group's.
 */
int
sc_pump_window(tiku_trk_script_t *s, const tiku_trk_script_state_t *st,
               tiku_trk_script_step_t *out, sc_step_t *cur)
{
    (void)out;
    switch (cur->op) {

    case OP_EXPECT_STATUS:
        sc_verdict(s, st != NULL && st->status != NULL &&
                   strstr(st->status, cur->text) != NULL,
                "status says", cur->text);
        if (s->fails > 0 && st != NULL && st->status != NULL) {
            /* On a failure, print what the status line did say. */
            printf("         (status: \"%s\")\n", st->status);
        }
        return 1;
    case OP_EXPECT_TITLE:
        /* The verdict prints the window's title, not the expectation. */
        sc_verdict(s, st != NULL && st->title != NULL &&
                   strstr(st->title, cur->text) != NULL,
                "title says",
                (st != NULL && st->title != NULL) ? st->title : "nothing");
        return 1;
    case OP_EXPECT_NO_TITLE:
        sc_verdict(s, st == NULL || st->title == NULL ||
                   strstr(st->title, cur->text) == NULL,
                "title does not contain",
                (st != NULL && st->title != NULL) ? st->title : "nothing");
        return 1;
    case OP_EXPECT_SCROLL: {
        char n[32];

        snprintf(n, sizeof n, "%d", (st != NULL) ? st->scroll : -1);
        if (cur->x != 0) {
            /* The "at most" form, from expect-scroll-at-most. */
            sc_verdict(s, st != NULL && st->scroll <= cur->count,
                    "scrolled at most, and is at", n);
        } else {
            sc_verdict(s, st != NULL && st->scroll >= cur->count,
                    "scrolled at least, and is at", n);
        }
        return 1;
    }
    case OP_EXPECT_ROWS: {
        char n[32];

        snprintf(n, sizeof n, "%d", (st != NULL) ? st->rows : -1);
        sc_verdict(s, st != NULL && (cur->x ? st->rows >= cur->count
                                         : st->rows == cur->count),
                "rows:", n);
        return 1;
    }
    case OP_EXPECT_WINDOWS: {
        char n[32];

        snprintf(n, sizeof n, "%d", (st != NULL) ? st->windows : -1);
        sc_verdict(s, st != NULL && st->windows == cur->count, "windows:", n);
        return 1;
    }
    case OP_EXPECT_SEL: {
        char n[32];

        /* The verdict prints the count, not the expectation. */
        snprintf(n, sizeof n, "%d", (st != NULL) ? st->selected : -1);
        sc_verdict(s, st != NULL && st->selected == cur->count, "selected:", n);
        return 1;
    }
    case OP_EXPECT_SELNAME:
        sc_verdict(s, st != NULL && st->sel_name != NULL &&
                strcmp(st->sel_name, cur->text) == 0, "selected:",
                (st != NULL && st->sel_name != NULL) ? st->sel_name
                                                     : "nothing");
        return 1;
    case OP_EXPECT_RENAMING: {
        char n[16];

        snprintf(n, sizeof n, "%d", (st != NULL) ? st->renaming : -1);
        snprintf(n, sizeof n, "%d", (st != NULL) ? st->renaming : -1);
        sc_verdict(s, st != NULL && st->renaming == cur->count, "renaming:", n);
        return 1;
    }
    case OP_EXPECT_FINDTEXT:
        sc_verdict(s, st != NULL && st->find_text != NULL &&
                strcmp(st->find_text, cur->text) == 0, "find text:",
                cur->text);
        return 1;
    case OP_EXPECT_FINDTYPE:
        sc_verdict(s, st != NULL && st->find_type != NULL &&
                strcmp(st->find_type, cur->text) == 0, "of type:",
                cur->text);
        return 1;
    case OP_EXPECT_LOCATION:
        sc_verdict(s, st != NULL && st->location != NULL &&
                strcmp(st->location, cur->text) == 0, "location:",
                (st != NULL && st->location != NULL) ? st->location
                                                     : "nothing");
        return 1;
    case OP_EXPECT_NAV_BACK:
    case OP_EXPECT_NAV_FORWARD:
    case OP_EXPECT_NAV_UP:
    case OP_EXPECT_NAV_VISIBLE:
    case OP_EXPECT_VIEWPORT_ROWS: {
        int actual = (st == NULL) ? -1
                   : (cur->op == OP_EXPECT_NAV_BACK) ? st->nav_back
                   : (cur->op == OP_EXPECT_NAV_FORWARD) ? st->nav_forward
                   : (cur->op == OP_EXPECT_NAV_UP) ? st->nav_up
                   : (cur->op == OP_EXPECT_NAV_VISIBLE) ? st->nav_visible
                                                       : st->viewport_rows;
        char value[16];

        snprintf(value, sizeof value, "%d", actual);
        sc_verdict(s, actual == cur->count,
                (cur->op == OP_EXPECT_NAV_BACK) ? "nav back:"
                : (cur->op == OP_EXPECT_NAV_FORWARD) ? "nav forward:"
                : (cur->op == OP_EXPECT_NAV_UP) ? "nav up:"
                : (cur->op == OP_EXPECT_NAV_VISIBLE) ? "navigator visible:"
                                                     : "visible rows:", value);
        return 1;
    }
    case OP_EXPECT_FOLDERS:
        sc_verdict(s, st != NULL && (st->kinds & 2) != 0 &&
                (st->kinds & 1) == 0, "every row is of the asked type", "");
        return 1;
    case OP_EXPECT_COLS: {
        char n[32];

        snprintf(n, sizeof n, "%d", cur->count);
        snprintf(n, sizeof n, "%d", (st != NULL) ? st->columns : -1);
        sc_verdict(s, st != NULL && st->columns == cur->count, "columns:", n);
        return 1;
    }
    default:
        return 0;
    }
}

/**
 * @brief Parse expect-perm, write, remove, expect-file or expect-no-file.
 *
 * @return 1 for one of these, 0 for another verb.
 */
int
sc_parse_file_state(sc_step_t *st, char **w, int n)
{
    if (strcmp(w[0], "expect-perm") == 0 && n >= 3) {
        st->op = OP_EXPECT_PERM;
        snprintf(st->text, sizeof st->text, "%s", w[1]);
        snprintf(st->text2, sizeof st->text2, "%s", w[2]);
    } else if (strcmp(w[0], "write") == 0 && n >= 3) {
        /* Writes a file behind the shell's back, to raise a change
         * notice. */
        st->op = OP_WRITE;
        snprintf(st->text, sizeof st->text, "%s", w[1]);
        sc_rest(w, n, 2, st->text2, sizeof st->text2);
    } else if (strcmp(w[0], "remove") == 0 && n >= 2) {
        /* Removes a file behind the shell's back. */
        st->op = OP_REMOVE;
        sc_rest(w, n, 1, st->text, sizeof st->text);
    } else if (strcmp(w[0], "expect-file") == 0 && n >= 2) {
        st->op = OP_EXPECT_FILE;
        sc_rest(w, n, 1, st->text, sizeof st->text);
    } else if (strcmp(w[0], "expect-no-file") == 0 && n >= 2) {
        st->op = OP_EXPECT_NO_FILE;
        sc_rest(w, n, 1, st->text, sizeof st->text);
    } else {
        return 0;
    }
    return 1;
}

/**
 * @brief Parse the Desktop verbs: click-desk by icon name (a trailing
 *        number is the button), and the expectations on what the Desktop
 *        shows or has selected.
 *
 * @return 1 for a verb of this group, 0 for another verb.
 */
int
sc_parse_desktop(sc_step_t *st, char **w, int n)
{
    if (strcmp(w[0], "click-desk") == 0 && n >= 2) {
        /* By name: where a Desktop icon lands depends on when its source
         * reported it. */
        st->op = OP_CLICK_DESK;
        st->button = 1;
        if (n >= 3 && atoi(w[n - 1]) > 0) {
            st->button = atoi(w[n - 1]);
            sc_rest(w, n - 1, 1, st->text, sizeof st->text);
        } else {
            sc_rest(w, n, 1, st->text, sizeof st->text);
        }
    } else if (strcmp(w[0], "expect-desk-selected") == 0 && n >= 2) {
        st->op = OP_EXPECT_DESKSEL;
        st->count = atoi(w[1]);
    } else if (strcmp(w[0], "expect-desk-selected-name") == 0 &&
               n >= 2) {
        st->op = OP_EXPECT_DESKSELNAME;
        sc_rest(w, n, 1, st->text, sizeof st->text);
    } else if (strcmp(w[0], "expect-no-desk-icon") == 0 && n >= 2) {
        st->op = OP_EXPECT_NO_DESKICON;
        sc_rest(w, n, 1, st->text, sizeof st->text);
    } else if (strcmp(w[0], "expect-desk-icon") == 0 && n >= 2) {
        st->op = OP_EXPECT_DESKICON;
        sc_rest(w, n, 1, st->text, sizeof st->text);
    } else {
        return 0;
    }
    return 1;
}

/**
 * @brief Parse a check of what the shell says (expect-count, -menu,
 *        -no-menu, -narrate, -no-narrate, the sound checks, -volume,
 *        -mounts, -control, -read, -under, -over, -said, -operation and
 *        -told), and the press-desk, move-desk and release-desk gestures.
 *
 * @return 1 for one of these, 0 for another verb.
 */
int
sc_parse_said(sc_step_t *st, char **w, int n)
{
    if (strcmp(w[0], "expect-count") == 0 && n >= 2) {
        st->op = OP_EXPECT_COUNT;
        sc_rest(w, n, 1, st->text, sizeof st->text);
    } else if (strcmp(w[0], "expect-menu") == 0 && n >= 2) {
        st->op = OP_EXPECT_MENU;
        sc_rest(w, n, 1, st->text, sizeof st->text);
    } else if (strcmp(w[0], "expect-narrate") == 0 && n >= 2) {
        st->op = OP_EXPECT_NARRATE;
        sc_rest(w, n, 1, st->text, sizeof st->text);
    } else if (strcmp(w[0], "expect-no-narrate") == 0 && n >= 2) {
        st->op = OP_EXPECT_NO_NARRATE;
        sc_rest(w, n, 1, st->text, sizeof st->text);
    } else if (strcmp(w[0], "expect-sound") == 0 && n >= 2) {
        st->op = OP_EXPECT_SOUND;
        snprintf(st->text, sizeof st->text, "%s", w[1]);
    } else if (strcmp(w[0], "expect-no-sound") == 0 && n >= 2) {
        st->op = OP_EXPECT_NO_SOUND;
        snprintf(st->text, sizeof st->text, "%s", w[1]);
    } else if (strcmp(w[0], "expect-sound-on") == 0 && n >= 2) {
        st->op = OP_EXPECT_SOUND_ON;
        snprintf(st->text, sizeof st->text, "%s", w[1]);
    } else if (strcmp(w[0], "expect-sound-off") == 0 && n >= 2) {
        st->op = OP_EXPECT_SOUND_OFF;
        snprintf(st->text, sizeof st->text, "%s", w[1]);
    } else if (strcmp(w[0], "expect-volume") == 0 && n >= 2) {
        st->op = OP_EXPECT_VOLUME;
        st->count = atoi(w[1]);
    } else if (strcmp(w[0], "expect-mounts") == 0 && n >= 2) {
        st->op = OP_EXPECT_MOUNTS;
        sc_rest(w, n, 1, st->text, sizeof st->text);
    } else if (strcmp(w[0], "expect-control") == 0 && n >= 2) {
        st->op = OP_EXPECT_CONTROL;
        sc_rest(w, n, 1, st->text, sizeof st->text);
    } else if (strcmp(w[0], "expect-read") == 0 && n >= 3) {
        /* The path first, then the substring: a path is one word, a value
         * may be several. */
        st->op = OP_EXPECT_READ;
        snprintf(st->text2, sizeof st->text2, "%s", w[1]);
        sc_rest(w, n, 2, st->text, sizeof st->text);
    } else if ((strcmp(w[0], "expect-under") == 0 ||
                strcmp(w[0], "expect-over") == 0) && n >= 3) {
        /* A noun that answers a number, and the bound it must stay under
         * or over. */
        st->op = (w[0][7] == 'u') ? OP_EXPECT_UNDER : OP_EXPECT_OVER;
        snprintf(st->text2, sizeof st->text2, "%s", w[1]);
        st->count = atoi(w[2]);
    } else if (strcmp(w[0], "expect-said") == 0 && n >= 2) {
        /* A noun and its whole answer, which may be empty. */
        st->op = OP_EXPECT_SAID;
        snprintf(st->text2, sizeof st->text2, "%s", w[1]);
        sc_rest(w, n, 2, st->text, sizeof st->text);
    } else if (strcmp(w[0], "expect-operation") == 0 && n >= 2) {
        st->op = OP_EXPECT_OPERATION;
        sc_rest(w, n, 1, st->text, sizeof st->text);
        /* A smaller turn budget than expect-told's: the engine is asked
         * directly, with no notice to wait for. */
        st->count = 400;
    } else if (strcmp(w[0], "expect-told") == 0 && n >= 2) {
        st->op = OP_EXPECT_TOLD;
        sc_rest(w, n, 1, st->text, sizeof st->text);
        st->count = 2000;    /* the turn budget */
    } else if (strcmp(w[0], "expect-no-menu") == 0) {
        st->op = OP_EXPECT_NO_MENU;
    } else if ((strcmp(w[0], "press-desk") == 0 ||
                strcmp(w[0], "move-desk") == 0 ||
                strcmp(w[0], "release-desk") == 0) && n >= 2) {
        /* The pieces of a drag, aimed by name like click-desk. */
        st->op = (w[0][0] == 'p') ? OP_PRESS_DESK
               : (w[0][0] == 'm') ? OP_MOVE_DESK : OP_RELEASE_DESK;
        st->button = 1;
        sc_rest(w, n, 1, st->text, sizeof st->text);
    } else {
        return 0;
    }
    return 1;
}

/**
 * @brief Parse expect-file-has, expect-file-lacks or expect-link.
 *
 * @return 1 for one of these, 0 for another verb.
 */
int
sc_parse_file_contents(sc_step_t *st, char **w, int n)
{
    if (strcmp(w[0], "expect-file-has") == 0 && n >= 3) {
        st->op = OP_EXPECT_FILEHAS;
        snprintf(st->text, sizeof st->text, "%s", w[1]);
        sc_rest(w, n, 2, st->text2, sizeof st->text2);
    } else if (strcmp(w[0], "expect-file-lacks") == 0 && n >= 3) {
        st->op = OP_EXPECT_FILELACKS;
        snprintf(st->text, sizeof st->text, "%s", w[1]);
        sc_rest(w, n, 2, st->text2, sizeof st->text2);
    } else if (strcmp(w[0], "expect-link") == 0 && n >= 3) {
        /* The target is the last word, so a link whose own name has a
         * space in it ("a.txt link") can still be named. */
        st->op = OP_EXPECT_LINK;
        sc_rest(w, n - 1, 1, st->text, sizeof st->text);
        snprintf(st->text2, sizeof st->text2, "%s", w[n - 1]);
    } else {
        return 0;
    }
    return 1;
}

/**
 * @brief Parse a check of the window or of what was painted: the font,
 *        status, title, alert, scroll, rows, windows, selection, renaming,
 *        Find, location, navigation, type narrowing, columns, positions,
 *        rows drawn, ink, pixels and a full redraw.
 *
 * @return 1 for one of these, 0 for another verb.
 */
int
sc_parse_window(sc_step_t *st, char **w, int n)
{
    if (strcmp(w[0], "expect-font") == 0 && n >= 2) {
        st->op = OP_EXPECT_FONT;
        sc_rest(w, n, 1, st->text, sizeof st->text);
    } else if (strcmp(w[0], "expect-status") == 0 && n >= 2) {
        st->op = OP_EXPECT_STATUS;
        sc_rest(w, n, 1, st->text, sizeof st->text);
    } else if (strcmp(w[0], "expect-title") == 0 && n >= 2) {
        st->op = OP_EXPECT_TITLE;
        sc_rest(w, n, 1, st->text, sizeof st->text);
    } else if (strcmp(w[0], "expect-no-title") == 0 && n >= 2) {
        st->op = OP_EXPECT_NO_TITLE;
        sc_rest(w, n, 1, st->text, sizeof st->text);
    } else if (strcmp(w[0], "expect-alert") == 0 && n >= 2) {
        st->op = OP_EXPECT_ALERT;
        sc_rest(w, n, 1, st->text, sizeof st->text);
    } else if (strcmp(w[0], "expect-scroll") == 0 && n >= 2) {
        st->op = OP_EXPECT_SCROLL;
        st->count = atoi(w[1]);
    } else if (strcmp(w[0], "expect-scroll-at-most") == 0 && n >= 2) {
        st->op = OP_EXPECT_SCROLL;
        st->count = atoi(w[1]);
        st->x = 1;
    } else if (strcmp(w[0], "expect-rows") == 0 && n >= 2) {
        st->op = OP_EXPECT_ROWS;
        st->count = atoi(w[1]);
    } else if (strcmp(w[0], "expect-rows-at-least") == 0 && n >= 2) {
        st->op = OP_EXPECT_ROWS;
        st->count = atoi(w[1]);
        st->x = 1;
    } else if (strcmp(w[0], "expect-windows") == 0 && n >= 2) {
        st->op = OP_EXPECT_WINDOWS;
        st->count = atoi(w[1]);
    } else if (strcmp(w[0], "expect-selected") == 0 && n >= 2) {
        st->op = OP_EXPECT_SEL;
        st->count = atoi(w[1]);
    } else if (strcmp(w[0], "expect-selected-name") == 0 && n >= 2) {
        st->op = OP_EXPECT_SELNAME;
        sc_rest(w, n, 1, st->text, sizeof st->text);
    } else if (strcmp(w[0], "expect-renaming") == 0 && n >= 2) {
        st->op = OP_EXPECT_RENAMING;
        st->count = atoi(w[1]);
    } else if (strcmp(w[0], "expect-find-text") == 0 && n >= 2) {
        st->op = OP_EXPECT_FINDTEXT;
        sc_rest(w, n, 1, st->text, sizeof st->text);
    } else if (strcmp(w[0], "expect-find-type") == 0 && n >= 2) {
        st->op = OP_EXPECT_FINDTYPE;
        sc_rest(w, n, 1, st->text, sizeof st->text);
    } else if (strcmp(w[0], "expect-location") == 0 && n >= 2) {
        st->op = OP_EXPECT_LOCATION;
        sc_rest(w, n, 1, st->text, sizeof st->text);
    } else if (strcmp(w[0], "expect-nav-back") == 0 && n >= 2) {
        st->op = OP_EXPECT_NAV_BACK;
        st->count = atoi(w[1]);
    } else if (strcmp(w[0], "expect-nav-forward") == 0 && n >= 2) {
        st->op = OP_EXPECT_NAV_FORWARD;
        st->count = atoi(w[1]);
    } else if (strcmp(w[0], "expect-nav-up") == 0 && n >= 2) {
        st->op = OP_EXPECT_NAV_UP;
        st->count = atoi(w[1]);
    } else if (strcmp(w[0], "expect-navigator") == 0 && n >= 2) {
        st->op = OP_EXPECT_NAV_VISIBLE;
        st->count = atoi(w[1]);
    } else if (strcmp(w[0], "expect-visible-rows") == 0 && n >= 2) {
        st->op = OP_EXPECT_VIEWPORT_ROWS;
        st->count = atoi(w[1]);
    } else if (strcmp(w[0], "expect-type-narrowed") == 0) {
        st->op = OP_EXPECT_FOLDERS;
    } else if (strcmp(w[0], "expect-columns") == 0 && n >= 2) {
        st->op = OP_EXPECT_COLS;
        st->count = atoi(w[1]);
    } else if (strcmp(w[0], "expect-deskbar-at") == 0 && n >= 3) {
        st->op = OP_EXPECT_BARAT;
        st->x = atoi(w[1]);
        st->y = atoi(w[2]);
    } else if (strcmp(w[0], "expect-window-at") == 0 && n >= 3) {
        st->op = OP_EXPECT_WINAT;
        st->x = atoi(w[1]);
        st->y = atoi(w[2]);
    } else if ((strcmp(w[0], "expect-icon-screen") == 0 ||
                strcmp(w[0], "expect-icon-at") == 0) && n >= 4) {
        /* The coordinates are the last two words, so a name with a space
         * in it ("New folder") can still be named. */
        st->op = (w[0][12] == 's') ? OP_EXPECT_ICONSCREEN
                                   : OP_EXPECT_ICONAT;
        sc_rest(w, n - 2, 1, st->text, sizeof st->text);
        st->x = atoi(w[n - 2]);
        st->y = atoi(w[n - 1]);
    } else if (strcmp(w[0], "draw-reset") == 0) {
        st->op = OP_DRAW_RESET;
    } else if (strcmp(w[0], "expect-drawn") == 0 && n >= 2) {
        /* At most <n> by default; at least with "at-least <n>". */
        st->op = OP_EXPECT_DRAWN;
        if (n >= 3 && strcmp(w[1], "at-least") == 0) {
            st->x = 1;
            st->count = atoi(w[2]);
        } else {
            st->count = atoi(w[1]);
        }
    } else if (strcmp(w[0], "expect-ink") == 0 && n >= 3) {
        /* Anything but white at the point, whatever shade the font
         * renderer gives its edges. */
        st->op = OP_EXPECT_INK;
        st->x = atoi(w[1]);
        st->y = atoi(w[2]);
    } else if (strcmp(w[0], "expect-same-as-redraw") == 0) {
        st->op = OP_SAME_AS_REDRAW;
    } else if (strcmp(w[0], "expect-pixel") == 0 && n >= 4) {
        st->op = OP_EXPECT_PIXEL;
        st->x = atoi(w[1]);
        st->y = atoi(w[2]);
        snprintf(st->text, sizeof st->text, "%s", w[3]);
    } else {
        return 0;
    }
    return 1;
}

/**
 * @brief Run a check of where things are and what was painted: the
 *        Deskbar, the focused window, an icon, the rows drawn, the font,
 *        ink, a pixel, and a full redraw compared.
 *
 * @return 1 for a step of this group, 0 for another group's.
 */
int
sc_pump_pixels(tiku_trk_script_t *s, const tiku_trk_script_state_t *st,
               tiku_trk_script_step_t *out, sc_step_t *cur)
{
    (void)out;
    switch (cur->op) {

    case OP_EXPECT_BARAT: {
        char at[48];

        snprintf(at, sizeof at, "%d,%d", (st != NULL) ? st->bar_x : -1,
                 (st != NULL) ? st->bar_y : -1);
        sc_verdict(s, st != NULL && st->bar_x == cur->x && st->bar_y == cur->y,
                "deskbar at:", at);
        return 1;
    }
    case OP_EXPECT_WINAT: {
        char at[48];

        /* The verdict prints where the window is. */
        snprintf(at, sizeof at, "%d,%d", (st != NULL) ? st->win_x : -1,
                 (st != NULL) ? st->win_y : -1);
        sc_verdict(s, st != NULL && st->win_x == cur->x && st->win_y == cur->y,
                "window at:", at);
        return 1;
    }
    case OP_EXPECT_ICONAT:
    case OP_EXPECT_ICONSCREEN: {
        char at[64];
        int px = 0, py = 0, got = 0;

        if (st != NULL && cur->op == OP_EXPECT_ICONSCREEN) {
            got = st->pose_screen != NULL &&
                  st->pose_screen(st->pose_ctx, cur->text, &px, &py);
        } else if (st != NULL && st->pose_at != NULL) {
            got = st->pose_at(st->pose_ctx, cur->text, &px, &py);
        }
        if (got) {
            snprintf(at, sizeof at, "%s at %d,%d", cur->text, px, py);
        } else {
            snprintf(at, sizeof at, "%s is not there", cur->text);
        }
        /* Within two pixels each way: the position is stored unscaled and
         * scaled back for the view, which rounds. */
        sc_verdict(s, got && px >= cur->x - 2 && px <= cur->x + 2 &&
                       py >= cur->y - 2 && py <= cur->y + 2, "icon:", at);
        return 1;
    }
    case OP_DRAW_RESET:
        if (st != NULL && st->rows_drawn != NULL) {
            (void)st->rows_drawn(st->pose_ctx, 1);
        }
        return 1;
    case OP_EXPECT_DRAWN: {
        char n[32];
        int got = (st != NULL && st->rows_drawn != NULL)
                      ? st->rows_drawn(st->pose_ctx, 0) : -1;

        snprintf(n, sizeof n, "%d", got);
        sc_verdict(s, got >= 0 && (cur->x ? got >= cur->count
                                       : got <= cur->count),
                "rows painted:", n);
        return 1;
    }
    case OP_EXPECT_FONT: {
        /* Asked of the toolkit, where every label in the interface gets
         * its metrics. */
        const char *name = tiku_font_current_family();
        char got[64];

        snprintf(got, sizeof got, "%s %d px",
                 (name != NULL) ? name : "nothing",
                 tiku_font_size());
        sc_verdict(s, strstr(got, cur->text) != NULL,
                "the interface is drawn in", got);
        return 1;
    }
    case OP_EXPECT_INK: {
        char got[16];
        unsigned rgb = 0u;

        if (st != NULL && st->pixel_at != NULL) {
            rgb = st->pixel_at(st->pose_ctx, cur->x, cur->y);
        }
        snprintf(got, sizeof got, "%06x", rgb & 0xffffffu);
        sc_verdict(s, (rgb & 0xffffffu) != 0xffffffu, "ink at:", got);
        return 1;
    }
    case OP_SAME_AS_REDRAW: {
        char got[32];
        long differ = -1;

        if (st != NULL && st->redraw_diff != NULL) {
            differ = st->redraw_diff(st->redraw_ctx);
        }
        snprintf(got, sizeof got, "%ld pixels", differ);
        sc_verdict(s, differ == 0, "against a full redraw:", got);
        return 1;
    }
    case OP_EXPECT_PIXEL: {
        char got[16];
        unsigned rgb = 0u;

        if (st != NULL && st->pixel_at != NULL) {
            rgb = st->pixel_at(st->pose_ctx, cur->x, cur->y);
        }
        snprintf(got, sizeof got, "%06x", rgb & 0xffffffu);
        sc_verdict(s, strcasecmp(got, cur->text) == 0, "pixel:", got);
        return 1;
    }
    default:
        return 0;
    }
    return 1;
}
