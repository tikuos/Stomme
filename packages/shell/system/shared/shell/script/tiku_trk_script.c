/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_script.c - script reader, event source and check tally.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_script_private.h"

/** @brief Whether @p path names something that stats; NULL and "" do not. */
int
sc_there(const char *path)
{
    struct stat st;

    return (path != NULL && path[0] != '\0' && stat(path, &st) == 0);
}

/** @brief Split @p line into words in place.  @return how many. */
int
sc_words(char *line, char **out, int max)
{
    int n = 0;
    char *p = line;

    while (*p != '\0' && n < max) {
        /* The line's own newline is a separator like any other. */
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
            p++;
        }
        if (*p == '\0') {
            break;
        }
        out[n++] = p;
        while (*p != '\0' && *p != ' ' && *p != '\t' && *p != '\n' &&
               *p != '\r') {
            p++;
        }
        if (*p != '\0') {
            *p++ = '\0';
        }
    }
    return n;
}

/** @brief Everything from word @p from onward, with single spaces. */
void
sc_rest(char **w, int n, int from, char *out, size_t max)
{
    int i;
    size_t k = 0;

    out[0] = '\0';
    for (i = from; i < n; i++) {
        size_t l = strlen(w[i]);

        if (k + l + 2u >= max) {
            break;
        }
        if (k > 0u) {
            out[k++] = ' ';
        }
        memcpy(out + k, w[i], l);
        k += l;
        out[k] = '\0';
    }
}

/** @brief The modifier words from word @p from on: ctrl (TIKU_MOD_CMD),
 *         shift and alt. */
unsigned
sc_mods_of(char **w, int n, int from)
{
    unsigned m = 0;
    int i;

    for (i = from; i < n; i++) {
        if (strcmp(w[i], "ctrl") == 0) {
            m |= TIKU_MOD_CMD;
        } else if (strcmp(w[i], "shift") == 0) {
            m |= TIKU_MOD_SHIFT;
        } else if (strcmp(w[i], "alt") == 0) {
            m |= TIKU_MOD_OPTION;
        }
    }
    return m;
}

/** @brief Resolve a key name of the script grammar; one character is itself.
 *         @return the key, or 0 for an unknown name. */
unsigned
sc_key_of(const char *name)
{
    static const struct {
        const char *name;
        unsigned key;
    } named[] = {
        { "Up", TIKU_KEY_UP },
        { "Down", TIKU_KEY_DOWN },
        { "Left", TIKU_KEY_LEFT },
        { "Right", TIKU_KEY_RIGHT },
        { "Home", TIKU_KEY_HOME },
        { "End", TIKU_KEY_END },
        { "Prior", TIKU_KEY_PAGE_UP },
        { "Next", TIKU_KEY_PAGE_DOWN },
        { "BackSpace", TIKU_KEY_BACKSPACE },
        { "Delete", TIKU_KEY_DELETE },
        { "Escape", TIKU_KEY_ESCAPE },
        { "Return", TIKU_KEY_RETURN },
        { "KP_Enter", TIKU_KEY_RETURN },
        { "Tab", TIKU_KEY_TAB },
        { "Menu", TIKU_KEY_MENU },
        { "F2", TIKU_KEY_F2 },
        { "F1", TIKU_KEY_F1 },
        { "space", ' ' },
        { "comma", ',' },
        { "numbersign", '#' },
        { "plus", '+' },
        { "equal", '=' },
        { "minus", '-' }
    };
    size_t i;

    if (name == NULL || name[0] == '\0') {
        return 0u;
    }
    if (name[1] == '\0') {
        return (unsigned char)name[0];
    }
    for (i = 0; i < sizeof named / sizeof named[0]; i++) {
        if (strcmp(name, named[i].name) == 0) {
            return named[i].key;
        }
    }
    return 0u;
}

tiku_trk_script_t *
tiku_trk_script_open(const char *path)
{
    tiku_trk_script_t *s;
    FILE *f;
    char line[SC_TEXT_MAX * 2];
    int lineno = 0;

    if (path == NULL) {
        return NULL;
    }
    f = fopen(path, "r");
    if (f == NULL) {
        fprintf(stderr, "script: cannot read '%s'\n", path);
        return NULL;
    }
    s = calloc(1u, sizeof *s);
    if (s == NULL) {
        (void)fclose(f);
        return NULL;
    }
    while (fgets(line, sizeof line, f) != NULL) {
        char *w[16];
        char *hash;
        int n, r;
        sc_step_t *st;

        lineno++;
        hash = strchr(line, '#');
        if (hash != NULL) {
            *hash = '\0';
        }
        n = sc_words(line, w, 16);
        if (n == 0) {
            continue;
        }
        if (s->count >= SC_MAX_STEPS) {
            fprintf(stderr, "script:%d: more than %d steps; this line and "
                    "the rest are not run\n", lineno, SC_MAX_STEPS);
            s->unread = lineno;
            break;
        }
        st = &s->step[s->count];
        memset(st, 0, sizeof *st);
        st->line = lineno;
        st->button = 1u;

        r = sc_parse_gesture(st, w, n, lineno);
        if (r == 0) { r = sc_parse_window(st, w, n); }
        if (r == 0) { r = sc_parse_file_contents(st, w, n); }
        if (r == 0) { r = sc_parse_said(st, w, n); }
        if (r == 0) { r = sc_parse_desktop(st, w, n); }
        if (r == 0) { r = sc_parse_file_state(st, w, n); }
        if (r < 0) {
            continue;           /* a bad line, already reported */
        }
        if (r == 0 && strcmp(w[0], "note") == 0 && n >= 2) {
            st->op = OP_NOTE;
            sc_rest(w, n, 1, st->text, sizeof st->text);
        } else if (r == 0 && strcmp(w[0], "quit") == 0) {
            st->op = OP_QUIT;
        } else if (r == 0) {
            fprintf(stderr, "script:%d: cannot parse '%s'\n", lineno, w[0]);
            continue;
        }
        s->count++;
    }
    (void)fclose(f);
    return s;
}

void
tiku_trk_script_free(tiku_trk_script_t *s)
{
    free(s);
}

/** @brief Print one check's PASS or FAIL, with the last note under it, and
 *         count it. */
void
sc_verdict(tiku_trk_script_t *s, int ok, const char *what, const char *arg)
{
    printf("  [%s] %s%s%s\n", ok ? "PASS" : "FAIL", what,
           (arg != NULL && arg[0] != '\0') ? " " : "",
           (arg != NULL) ? arg : "");
    if (s->note[0] != '\0') {
        printf("         %s\n", s->note);
        s->note[0] = '\0';
    }
    if (ok) {
        s->passes++;
    } else {
        s->fails++;
    }
}

int
tiku_trk_script_pump(tiku_trk_script_t *s, const tiku_trk_script_state_t *st,
                     tiku_trk_script_step_t *out)
{
    sc_step_t *cur;

    if (s == NULL || out == NULL) {
        return 0;
    }
    memset(out, 0, sizeof *out);
    out->what = TIKU_TRK_SC_IDLE;

    /* A wait owes the loop turns before anything else happens, so deferred
     * work (a stepped copy, an armed rename) gets as far as it would on a
     * real clock. */
    if (s->waiting > 0) {
        s->waiting--;
        return 1;
    }
    if (s->dragging_window == 1) {
        /* Halfway: the move that carries it. */
        s->dragging_window = 2;
        out->what = TIKU_TRK_SC_MOVE;
        out->x = s->drag_to_x;
        out->y = s->drag_to_y;
        out->button = 1u;
        return 1;
    }
    if (s->dragging_window == 2) {
        s->dragging_window = 0;
        out->what = TIKU_TRK_SC_RELEASE;
        out->x = s->drag_to_x;
        out->y = s->drag_to_y;
        out->button = 1u;
        return 1;
    }
    if (s->pending_release) {
        cur = &s->step[s->cursor - 1];
        s->pending_release = 0;
        out->what = TIKU_TRK_SC_RELEASE;
        out->x = cur->x;
        out->y = cur->y;
        out->button = cur->button;
        out->modifiers = cur->modifiers;
        return 1;
    }
    if (s->cursor >= s->count) {
        s->finished = 1;        /* every step ran: the end is the quit */
        return 0;
    }
    cur = &s->step[s->cursor++];
    if (sc_pump_gesture(s, st, out, cur)) {
        return 1;
    }
    if (sc_pump_window(s, st, out, cur)) {
        return 1;
    }
    if (sc_pump_pixels(s, st, out, cur)) {
        return 1;
    }
    if (sc_pump_file_contents(s, st, out, cur)) {
        return 1;
    }
    if (sc_pump_said(s, st, out, cur)) {
        return 1;
    }
    if (sc_pump_desktop(s, st, out, cur)) {
        return 1;
    }
    if (sc_pump_file_state(s, st, out, cur)) {
        return 1;
    }
    switch (cur->op) {
    case OP_QUIT:
    default:
        s->finished = 1;
        out->what = TIKU_TRK_SC_QUIT;
        return 1;
    }
}

int
tiku_trk_script_failures(const tiku_trk_script_t *s)
{
    return (s != NULL) ? s->fails : 0;
}

int
tiku_trk_script_report(const tiku_trk_script_t *s, const char *who)
{
    if (s == NULL) {
        return 0;
    }
    /* A script that made no check fails. */
    if (s->passes + s->fails == 0) {
        printf("\n%s: no checks were made\n", (who != NULL) ? who : "script");
        return 1;
    }
    printf("\n%s: %d passed, %d failed\n", (who != NULL) ? who : "script",
           s->passes, s->fails);
    /* A script with steps unreached fails: the run was cut short.  One that
     * ran every step without a quit line has finished. */
    if (s->unread > 0) {
        printf("%s: lines from %d on were not read, steps unreached\n",
               (who != NULL) ? who : "script", s->unread);
        return 1;
    }
    if (!s->finished) {
        int at = (s->cursor > 0) ? s->step[s->cursor - 1].line : 0;

        printf("%s: stopped at line %d, step %d of %d, steps unreached\n",
               (who != NULL) ? who : "script", at, s->cursor, s->count);
        return 1;
    }
    return (s->fails > 0) ? 1 : 0;
}

int
tiku_trk_script_waiting(const tiku_trk_script_t *s)
{
    return (s != NULL && s->waiting > 0) ? s->waiting : 0;
}
