/* tiku_editor_preferences.c -- Editor defaults and the Preferences window.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_app_product.h"
#include "tiku_editor_private.h"
#include "tiku_atomic_file.h"
#include "tiku_preferences_layout.h"
#include <ctype.h>

/* The file's first line, and the most a file read back may hold. */
#define PREF_MAGIC TIKU_PRODUCT_EDITOR_PREFERENCES_MAGIC
#define PREF_FILE_MAX 512

/* The window's controls in focus order: four steppers, seven
 * checkboxes, the colour choice, then Defaults, Cancel and Save. */
#define PREF_STEPPERS 4
#define PREF_COLOURS 11
#define PREF_DEFAULTS 12
#define PREF_CANCEL 13
#define PREF_SAVE 14
#define PREF_CONTROLS 15

/* The colour choices: the theme's code inks, then the two derived
 * from them. */
#define PREF_MUTED 1
#define PREF_STRONG 2
#define PREF_PRESETS 3

/* How a save ended. */
enum { SAVE_DONE, SAVE_UNSYNCED, SAVE_FAILED };

/* The steppers' ranges, in control order: tab, indent, column, zoom. */
static const int step_low[PREF_STEPPERS] = {1, 0, 1, EDIT_ZOOM_MIN};
static const int step_high[PREF_STEPPERS] = {32, 32, 240, EDIT_ZOOM_MAX};

/** The defaults of a first run: tab stops every four columns, the
 *  ruler at column 80 when it is shown, and line numbers on. */
static editor_preferences_t defaults(void)
{
    return (editor_preferences_t){.tab = 4, .column = 80, .numbers = 1};
}

/** The field stepper @p i sets in @p p. */
static int *stepper_value(editor_preferences_t *p, int i)
{
    int *values[PREF_STEPPERS] = {&p->tab, &p->indent, &p->column,
                                  &p->zoom};

    return values[i];
}

/** The View switch checkbox @p i sets in @p p. */
static int *check_value(editor_preferences_t *p, int i)
{
    int *values[PREF_COLOURS - PREF_STEPPERS] = {
        &p->numbers, &p->wrap, &p->guides, &p->ruler, &p->invisibles,
        &p->caret_line, &p->block_caret};

    return values[i - PREF_STEPPERS];
}

/** Whether every value of @p p is in its range. */
static int valid(const editor_preferences_t *p)
{
    editor_preferences_t copy = *p;

    for (int i = 0; i < PREF_STEPPERS; i++) {
        int value = *stepper_value(&copy, i);

        if (value < step_low[i] || value > step_high[i]) {
            return 0;
        }
    }
    for (int i = PREF_STEPPERS; i < PREF_COLOURS; i++) {
        if ((unsigned)*check_value(&copy, i) > 1u) {
            return 0;
        }
    }
    return p->colors >= 0 && p->colors < PREF_PRESETS;
}

/**
 * @brief Where the preferences file is: TIKU_EDITOR_PREFERENCES when it
 *        is set, an empty value turning preferences off, otherwise
 *        TikuEditorPreferences in XDG_CONFIG_HOME, or in ~/.config when
 *        that variable is unset, empty or not an absolute path.
 *
 * @param create  Make the configuration folder when it is missing
 * @return 1 with @p path filled in, 0 when there is no usable place
 */
static int path_for(char *path, size_t cap, int create)
{
    const char *override = getenv(TIKU_PRODUCT_EDITOR_TIKU_EDITOR_PREFERENCES);
    const char *xdg = getenv("XDG_CONFIG_HOME"), *home = getenv("HOME");
    char dir[PATH_MAX];
    int n;

    if (override != NULL) {
        return override[0] != '\0' &&
               snprintf(path, cap, "%s", override) < (int)cap;
    }
    if (xdg != NULL && xdg[0] == '/') {
        n = snprintf(dir, sizeof dir, "%s", xdg);
    } else if (home != NULL && home[0] == '/') {
        n = snprintf(dir, sizeof dir, "%s/.config", home);
    } else {
        return 0;
    }
    if (n < 0 || n >= (int)sizeof dir) {
        return 0;
    }
    if (create && mkdir(dir, 0700) != 0 && errno != EEXIST) {
        return 0;
    }
    return snprintf(path, cap, TIKU_PRODUCT_EDITOR_PREFERENCES_PATH, dir) <
           (int)cap;
}

/** The colour preset @p preset gives @p role on the live theme: the
 *  theme's ink pulled halfway to the grey of its own lightness for
 *  Muted, or pushed half as far again from that grey for Strong. */
static tiku_rgb_t preset_ink(int preset, tiku_ink_t role)
{
    tiku_rgb_t ink = tiku_ink(role), grey = tiku_grey(ink), out = 0;

    if (preset == PREF_MUTED) {
        return tiku_dim(ink, grey);
    }
    for (int shift = 0; shift <= 16; shift += 8) {
        int c = (int)((ink >> shift) & 0xFFu);
        int g = (int)((grey >> shift) & 0xFFu);
        int v = c + (c - g) / 2;

        v = v < 0 ? 0 : v > 255 ? 255 : v;
        out |= (tiku_rgb_t)v << shift;
    }
    return out;
}

/** Colour @p view's reserved words, strings, numbers and remarks as
 *  preset @p preset derives them from the live theme, or in the theme's
 *  own inks with 0.  A style that already matches is not set again, so
 *  a call per frame restyles only after the theme or preset changes. */
void tiku_editor_preferences_colours(tiku_codeview_t *view, int preset)
{
    static const tiku_ink_t roles[] = {TIKU_INK_WORD, TIKU_INK_STR,
                                       TIKU_INK_NUM, TIKU_INK_REMARK};

    for (size_t k = 0; k < sizeof roles / sizeof roles[0]; k++) {
        tiku_codeview_style_t have, want;

        if (tiku_codeview_get_style(view, roles[k], &have) != TIKU_EDIT_OK) {
            continue;
        }
        want = have;
        want.colors &= ~TIKU_CODEVIEW_STYLE_FORE;
        want.foreground = 0;
        if (preset == PREF_MUTED || preset == PREF_STRONG) {
            want.colors |= TIKU_CODEVIEW_STYLE_FORE;
            want.foreground = preset_ink(preset, roles[k]);
        }
        if (want.colors != have.colors || want.foreground != have.foreground) {
            (void)tiku_codeview_set_style(view, roles[k], &want);
        }
    }
}

/** Give @p view the tab width, indentation, zoom and code colours @p p
 *  names. */
void tiku_editor_preferences_pane(tiku_codeview_t *view,
                                  const editor_preferences_t *p)
{
    (void)tiku_codeview_set_tab_width(view, p->tab);
    (void)tiku_codeview_set_indent(view, p->indent);
    (void)tiku_codeview_set_zoom(view, p->zoom);
    tiku_editor_preferences_colours(view, p->colors);
}

/** Make @p p this window's defaults: its View switches and every pane
 *  take them. */
void tiku_editor_preferences_apply(edit_state_t *st,
                                   const editor_preferences_t *p)
{
    st->preferences = *p;
    st->numbers = p->numbers;
    st->wrap = p->wrap;
    st->indent_guides = p->guides;
    st->ruler = p->ruler;
    st->show_invisibles = p->invisibles;
    st->caret_line = p->caret_line;
    st->block_caret = p->block_caret;
    for (int i = 0; i < 2 && st->panes[i]; i++) {
        tiku_editor_preferences_pane(st->panes[i], p);
    }
}

/** Read the file's @p text into @p out.  @return 1 when it is whole and
 *  every value is in range; 0 leaves @p out as it was. */
static int parse(const char *text, editor_preferences_t *out)
{
    editor_preferences_t got = {0};
    int *fields[] = {&got.tab, &got.indent, &got.column, &got.zoom,
                     &got.colors, &got.numbers, &got.wrap, &got.guides,
                     &got.ruler, &got.invisibles, &got.caret_line,
                     &got.block_caret};
    const char *at;

    if (strncmp(text, PREF_MAGIC, strlen(PREF_MAGIC)) != 0) {
        return 0;
    }
    at = text + strlen(PREF_MAGIC);
    for (size_t i = 0; i < sizeof fields / sizeof fields[0]; i++) {
        char *end;
        long value;

        if (!isspace((unsigned char)*at)) {
            return 0;
        }
        errno = 0;
        value = strtol(at, &end, 10);
        if (end == at || errno != 0 || value < INT_MIN || value > INT_MAX) {
            return 0;
        }
        *fields[i] = (int)value;
        at = end;
    }
    while (isspace((unsigned char)*at)) {
        at++;
    }
    if (*at != '\0' || !valid(&got)) {
        return 0;
    }
    *out = got;
    return 1;
}

/** Apply the saved defaults to this window, or the first-run ones when
 *  there is no file or it is not whole and valid.  Nothing is written. */
void tiku_editor_preferences_load(edit_state_t *st)
{
    editor_preferences_t p = defaults();
    char path[PATH_MAX], text[PREF_FILE_MAX];
    struct stat info;
    FILE *f = NULL;
    int fd = -1;

    if (path_for(path, sizeof path, 0)) {
        fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    }
    if (fd >= 0 && (fstat(fd, &info) != 0 || !S_ISREG(info.st_mode) ||
                    info.st_size >= PREF_FILE_MAX)) {
        close(fd);
        fd = -1;
    }
    if (fd >= 0) {
        f = fdopen(fd, "r");
        if (f == NULL) {
            close(fd);
        }
    }
    if (f != NULL) {
        size_t n = fread(text, 1, sizeof text - 1, f);
        int whole = !ferror(f) && feof(f) && memchr(text, 0, n) == NULL;

        text[n] = '\0';
        if (whole) {
            (void)parse(text, &p);
        }
        fclose(f);
    }
    tiku_editor_preferences_apply(st, &p);
}

/** Write @p ctx, an editor_preferences_t, as the file's three lines. */
static int write_preferences(FILE *file, void *ctx)
{
    const editor_preferences_t *p = ctx;

    return fprintf(file, "%s\n%d %d %d %d %d\n%d %d %d %d %d %d %d\n",
                   PREF_MAGIC, p->tab, p->indent, p->column, p->zoom,
                   p->colors, p->numbers, p->wrap, p->guides, p->ruler,
                   p->invisibles, p->caret_line, p->block_caret) < 0 ? -1 : 0;
}

/**
 * @brief Write @p p to the preferences file, replacing it whole.  A
 *        symbolic link is saved through: the file it names is replaced
 *        in its own folder and the link is kept.
 *
 * @return SAVE_DONE; SAVE_UNSYNCED when the file was replaced but its
 *         folder could not be synced; SAVE_FAILED with the file as it was
 */
static int save(editor_preferences_t *p)
{
    char path[PATH_MAX], target[PATH_MAX];
    struct stat before, after;
    int existed;

    if (!valid(p) || !path_for(path, sizeof path, 1)) {
        return SAVE_FAILED;
    }
    if (realpath(path, target) != NULL) {
        memcpy(path, target, sizeof path);
    }
    existed = lstat(path, &before) == 0;
    if (tiku_atomic_file(path, write_preferences, p) == 0) {
        return SAVE_DONE;
    }
    /* The write also fails when only syncing the folder after the rename
     * does; a different file in place means the rename happened. */
    if (lstat(path, &after) == 0 && S_ISREG(after.st_mode) &&
        (!existed || after.st_dev != before.st_dev ||
         after.st_ino != before.st_ino)) {
        return SAVE_UNSYNCED;
    }
    return SAVE_FAILED;
}

int tiku_editor_preferences_replace(edit_state_t *st,
                                    const editor_preferences_t *prefs)
{
    editor_preferences_t next=*prefs;int result=save(&next);
    if(result==SAVE_FAILED)return -1;
    tiku_editor_preferences_apply(st,&next);return result==SAVE_UNSYNCED;
}

/** Show the Preferences window with the saved defaults to edit, or
 *  bring it to the front when it is open. */
void tiku_editor_preferences_open(edit_state_t *st)
{
    if (st->prefs_id) {
        if (st->services->raise_window) {
            st->services->raise_window(st->services->ctx, st->prefs_id);
        }
        return;
    }
    st->pref_draft = st->preferences;
    st->pref_message[0] = '\0';
    st->pref_focus = 0;
    if (!st->prefs_surface) {
        st->prefs_surface = tiku_surface_new(EDIT_PREF_W, EDIT_PREF_H,
                                             TIKU_C_PANEL);
    }
    if (!st->prefs_surface) {
        return;
    }
    st->prefs_id = st->services->open(st->services->ctx,
                                      "Editor Preferences", EDIT_PREF_W,
                                      EDIT_PREF_H);
    tiku_editor_preferences_paint(st);
}

/** Close the Preferences window; what it held unsaved is dropped. */
void tiku_editor_preferences_close(edit_state_t *st)
{
    uint32_t id = st->prefs_id;

    st->prefs_id = 0;
    if (id && st->services->close) {
        st->services->close(st->services->ctx, id);
    }
}

/** Where control @p i is: a stepper's row, a checkbox, the colour
 *  choice, or one of the three buttons along the bottom. */
static tiku_rect_t control(int i)
{
    static const tiku_preferences_action_t left = {"Defaults", 90};
    static const tiku_preferences_action_t right[] = {{"Cancel", 90},
                                                      {"Save", 90}};
    tiku_rect_t a, b[2];

    if (i < PREF_STEPPERS) {
        return (tiku_rect_t){365, 53 + 32 * i, 160, 25};
    }
    if (i < PREF_COLOURS) {
        int k = i - PREF_STEPPERS;

        return (tiku_rect_t){28 + k % 2 * 260, 238 + k / 2 * 31, 246, 25};
    }
    if (i == PREF_COLOURS) {
        return (tiku_rect_t){365, 181, 160, 25};
    }
    (void)tiku_preferences_actions((tiku_rect_t){16, 392, 528, 32}, &left,
                                   right, 2, 10, &a, b);
    return i == PREF_DEFAULTS ? a : b[i - PREF_CANCEL];
}

/** Draw the four steppers: a label, then -, the value and +. */
static void paint_steppers(edit_state_t *st, tiku_surface_t *s)
{
    static const char *labels[PREF_STEPPERS] = {
        "Tab width", "Indent with (0 = tabs)", "Ruler column",
        "Default zoom"};
    const tiku_font_t *f = tiku_font_plain();

    for (int i = 0; i < PREF_STEPPERS; i++) {
        tiku_rect_t r = control(i);
        unsigned focus = st->pref_focus == i ? TIKU_S_FOCUS : 0;
        char text[32];

        snprintf(text, sizeof text, "%d",
                 *stepper_value(&st->pref_draft, i));
        tiku_text(s, f, 29, r.y + f->ascent + 3, labels[i], TIKU_C_TEXT);
        tiku_ui_button(s, (tiku_rect_t){r.x, r.y, 30, r.h}, "-", focus);
        tiku_ui_textfield(s, (tiku_rect_t){r.x + 34, r.y, 92, r.h}, text,
                          -1, 0);
        tiku_ui_button(s, (tiku_rect_t){r.x + 130, r.y, 30, r.h}, "+",
                       focus);
    }
}

/** Draw the colour choice and the seven View switch checkboxes. */
static void paint_display(edit_state_t *st, tiku_surface_t *s)
{
    static const char *colours[PREF_PRESETS] = {"Desktop theme", "Muted",
                                                "Strong"};
    static const char *labels[PREF_COLOURS - PREF_STEPPERS] = {
        "Line numbers", "Wrap lines", "Indentation guides", "Column ruler",
        "Show whitespace", "Highlight caret line", "Block caret"};
    const tiku_font_t *f = tiku_font_plain();

    tiku_text(s, f, 29, 184 + f->ascent, "Syntax colours", TIKU_C_TEXT);
    tiku_ui_button(s, control(PREF_COLOURS), colours[st->pref_draft.colors],
                   st->pref_focus == PREF_COLOURS ? TIKU_S_FOCUS : 0);
    tiku_ui_group(s, (tiku_rect_t){16, 212, 528, 148}, "Display");
    for (int i = PREF_STEPPERS; i < PREF_COLOURS; i++) {
        unsigned state = *check_value(&st->pref_draft, i) ? TIKU_S_ON : 0;

        if (st->pref_focus == i) {
            state |= TIKU_S_FOCUS;
        }
        tiku_ui_checkbox(s, control(i), labels[i - PREF_STEPPERS], state);
    }
}

/** Paint the Preferences window from the values it is editing. */
void tiku_editor_preferences_paint(edit_state_t *st)
{
    static const char *actions[] = {"Defaults", "Cancel", "Save"};
    tiku_surface_t *s = st->prefs_surface;

    if (!st->prefs_id || !s) {
        return;
    }
    tiku_fill(s, (tiku_rect_t){0, 0, EDIT_PREF_W, EDIT_PREF_H},
              TIKU_C_PANEL);
    tiku_text(s, tiku_font_at(12), 18, 23,
              "Defaults for new windows; Save also updates this window.",
              TIKU_C_TEXT);
    tiku_ui_group(s, (tiku_rect_t){16, 32, 528, 181}, "Text");
    paint_steppers(st, s);
    paint_display(st, s);
    tiku_text(s, tiku_font_at(10), 20, 379,
              st->pref_message[0] ? st->pref_message
              : "Tab: next control   Arrows: adjust   Space: toggle   "
                "Enter: save", TIKU_C_TEXT);
    for (int i = PREF_DEFAULTS; i < PREF_CONTROLS; i++) {
        unsigned state = i == PREF_SAVE ? TIKU_S_DEFAULT : 0;

        if (st->pref_focus == i) {
            state |= TIKU_S_FOCUS;
        }
        tiku_ui_button(s, control(i), actions[i - PREF_DEFAULTS], state);
    }
    st->services->frame(st->services->ctx, st->prefs_id, s->px, EDIT_PREF_W,
                        EDIT_PREF_H);
}

/** Save the window's values; once they are in the file, apply them to
 *  this window and close, else keep the window open and say why. */
static void save_and_apply(edit_state_t *st)
{
    int saved = save(&st->pref_draft);

    if (saved == SAVE_FAILED) {
        snprintf(st->pref_message, sizeof st->pref_message, "%s",
                 "Could not save preferences. Settings have not been "
                 "applied.");
        return;
    }
    tiku_editor_preferences_apply(st, &st->pref_draft);
    tiku_editor_preferences_close(st);
    if (saved == SAVE_UNSYNCED) {
        snprintf(st->note, sizeof st->note, "%s",
                 "Preferences saved and applied; their folder could not "
                 "be synced to disk.");
    }
    tiku_editor_reveal(st);
    tiku_editor_publish(st);
    tiku_editor_paint(st);
}

/** Act on control @p i: a stepper moves by @p delta within its range,
 *  a checkbox toggles, the colour choice steps by @p delta, and the
 *  buttons reset the values, cancel or save. */
static void activate(edit_state_t *st, int i, int delta)
{
    if (i < PREF_STEPPERS) {
        int *value = stepper_value(&st->pref_draft, i);
        int next = *value + delta;

        if (next >= step_low[i] && next <= step_high[i]) {
            *value = next;
        }
    } else if (i < PREF_COLOURS) {
        int *value = check_value(&st->pref_draft, i);

        *value = !*value;
    } else if (i == PREF_COLOURS) {
        st->pref_draft.colors =
            (st->pref_draft.colors + PREF_PRESETS + delta) % PREF_PRESETS;
    } else if (i == PREF_DEFAULTS) {
        st->pref_draft = defaults();
    } else if (i == PREF_CANCEL) {
        tiku_editor_preferences_close(st);
    } else {
        save_and_apply(st);
    }
}

/** A key in the Preferences window: Tab moves the focus, Return saves
 *  or presses the focused button, Space presses the focused control, and
 *  Left and Right or - and + adjust a stepper or the colour choice. */
static void preferences_key(edit_state_t *st, const tiku_event_t *e)
{
    int focus = st->pref_focus;

    if (e->key == TIKU_KEY_TAB) {
        int step = (e->modifiers & TIKU_MOD_SHIFT) ? PREF_CONTROLS - 1 : 1;

        st->pref_focus = (focus + step) % PREF_CONTROLS;
    } else if (e->key == TIKU_KEY_RETURN) {
        activate(st, focus >= PREF_DEFAULTS ? focus : PREF_SAVE, 1);
    } else if (e->key == ' ') {
        activate(st, focus, 1);
    } else if (focus < PREF_STEPPERS || focus == PREF_COLOURS) {
        if (e->key == TIKU_KEY_LEFT || e->key == '-') {
            activate(st, focus, -1);
        } else if (e->key == TIKU_KEY_RIGHT || e->key == '+') {
            activate(st, focus, 1);
        }
    }
}

/** A press on control @p i at @p x: a stepper only on its - or + button,
 *  any other control anywhere; the colour choice steps back on its left
 *  end. */
static void preferences_press(edit_state_t *st, int i, int x)
{
    tiku_rect_t r = control(i);

    st->pref_focus = i;
    if (i >= PREF_STEPPERS || x < r.x + 30 || x >= r.x + 130) {
        activate(st, i, x < r.x + 30 ? -1 : 1);
    }
}

/** Route an event to the Preferences window; closing it, Escape and
 *  Cmd+W close it without saving.  @return 0. */
int tiku_editor_preferences_event(edit_state_t *st, const tiku_event_t *e)
{
    if (e->type == TIKU_EVENT_CLOSE) {
        tiku_editor_preferences_close(st);
        return 0;
    }
    if (e->type == TIKU_EVENT_KEY_DOWN) {
        if (e->key == TIKU_KEY_ESCAPE || ((e->modifiers & TIKU_MOD_CMD) &&
                                          (e->key == 'w' || e->key == 'W'))) {
            tiku_editor_preferences_close(st);
            return 0;
        }
        preferences_key(st, e);
    }
    if (e->type == TIKU_EVENT_POINTER_DOWN && e->button <= 1) {
        for (int i = 0; i < PREF_CONTROLS; i++) {
            tiku_rect_t r = control(i);

            if (e->x >= r.x && e->x < r.x + r.w &&
                e->y >= r.y && e->y < r.y + r.h) {
                preferences_press(st, i, e->x);
                break;
            }
        }
    }
    tiku_editor_preferences_paint(st);
    return 0;
}
