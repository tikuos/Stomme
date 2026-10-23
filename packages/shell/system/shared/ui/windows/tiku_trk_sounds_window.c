/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_sounds_window.c - the Sounds preferences window.
 *
 * The volume is a slider: the knob is taken on a press and follows the
 * pointer until the release, which the shell routes here while the
 * knob is held (tiku_trk_sounds_window_tracking).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_font.h"
#include "tiku_gfx.h"
#include "tiku_state.h"
#include "tiku_strings.h"
#include "tiku_trk_sounds_window.h"
#include "tiku_ui.h"
#include <stdio.h>
#include <string.h>

#define PAD 16
#define BUTTON_H 24
#define TEST_W 64
#define LABEL_W 150
#define CHOOSE_W 28
#define KNOB_W 12

/** @brief A table row's height: the label's face plus room around it. */
static int
row_h(void)
{
    return tiku_font_plain()->height + 10;
}

/** @brief Where the first row begins, under the heading. */
static int
list_y(void)
{
    return 10 + tiku_font_bold()->height + 8;
}

/** @brief Where the volume slider sits, under the last row. */
static int
volume_y(void)
{
    return list_y() + TIKU_SOUND_EVENTS * row_h() + 12;
}

/** @brief Where the Defaults and Revert buttons sit. */
static int
button_y(void)
{
    return volume_y() + BUTTON_H + 16;
}

/** @brief The frame's height: the content, its margin, and the tab. */
static int
window_height(void)
{
    return button_y() + BUTTON_H + PAD + tiku_window_tab_h() +
           TIKU_WINDOW_BORDER;
}

/** @brief The Test button of row @p i. */
static tiku_rect_t
test_rect(tiku_rect_t content, int i)
{
    tiku_rect_t r;

    r.w = tiku_ui_button_w(tiku_str("button.test"), TEST_W);
    r.h = row_h() - 4;
    r.x = content.x + content.w - PAD - r.w;
    r.y = content.y + list_y() + i * row_h() + 2;
    return r;
}

/** @brief The checkbox of row @p i, label included. */
static tiku_rect_t
check_rect(tiku_rect_t content, int i)
{
    tiku_rect_t r;

    r.x = content.x + PAD;
    r.y = content.y + list_y() + i * row_h();
    r.w = LABEL_W;
    r.h = row_h();
    return r;
}

/** @brief The button that asks for a file from anywhere, before Test. */
static tiku_rect_t
choose_rect(tiku_rect_t content, int i)
{
    tiku_rect_t r = test_rect(content, i);

    r.x -= 8 + CHOOSE_W;
    r.w = CHOOSE_W;
    return r;
}

/** @brief The button naming what row @p i plays, between label and "…". */
static tiku_rect_t
clip_rect(tiku_rect_t content, int i)
{
    tiku_rect_t r;
    tiku_rect_t c = choose_rect(content, i);

    r.x = content.x + PAD + LABEL_W + 8;
    r.y = c.y;
    r.w = c.x - 8 - r.x;
    r.h = c.h;
    return r;
}

/** @brief The slider's track, from the label to the number. */
static tiku_rect_t
track_rect(tiku_rect_t content)
{
    tiku_rect_t r;
    int label_w = tiku_text_width(tiku_font_bold(),
                                  tiku_str("prefs.sounds.volume"));

    r.x = content.x + PAD + label_w + 12;
    r.y = content.y + volume_y() + (BUTTON_H - 8) / 2;
    r.w = content.x + content.w - PAD - 48 - r.x;
    r.h = 8;
    return r;
}

/** @brief The knob at @p volume on the track. */
static tiku_rect_t
knob_rect(tiku_rect_t content, int volume)
{
    tiku_rect_t t = track_rect(content);
    tiku_rect_t r;

    r.x = t.x + (t.w - KNOB_W) * volume / 100;
    r.y = content.y + volume_y();
    r.w = KNOB_W;
    r.h = BUTTON_H;
    return r;
}

/** @brief The volume a pointer at @p x on the track asks for. */
static int
volume_at(tiku_rect_t content, int x)
{
    tiku_rect_t t = track_rect(content);
    int v = (x - t.x - KNOB_W / 2) * 100 / (t.w - KNOB_W);

    return (v < 0) ? 0 : (v > 100) ? 100 : v;
}

/** @brief The Defaults button. */
static tiku_rect_t
defaults_rect(tiku_rect_t content)
{
    tiku_rect_t r;

    r.w = tiku_ui_button_w(tiku_str("button.defaults"), 112);
    r.h = BUTTON_H;
    r.x = content.x + PAD;
    r.y = content.y + button_y();
    return r;
}

/** @brief The Revert button, beside Defaults. */
static tiku_rect_t
revert_rect(tiku_rect_t content)
{
    tiku_rect_t r = defaults_rect(content);

    r.x += r.w + 12;
    r.w = tiku_ui_button_w(tiku_str("button.revert"), 100);
    return r;
}

/** @brief Whether (@p x, @p y) lies in @p r. */
static int
inside(tiku_rect_t r, int x, int y)
{
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

/** @brief Whether anything differs from what the window opened on. */
static int
revertable(const tiku_trk_sounds_window_t *controller)
{
    int i;

    for (i = 0; i < TIKU_SOUND_EVENTS; i++) {
        if (tiku_trk_soundprefs_enabled(controller->prefs, i) !=
                controller->opened[i] ||
            strcmp(tiku_trk_soundprefs_clip(controller->prefs, i),
                   controller->opened_clip[i]) != 0) {
            return 1;
        }
    }
    return tiku_trk_soundprefs_volume(controller->prefs) !=
           controller->opened_volume;
}

/** @brief Put every setting back to what the window opened on. */
static void
revert(tiku_trk_sounds_window_t *controller)
{
    int i;

    for (i = 0; i < TIKU_SOUND_EVENTS; i++) {
        tiku_trk_soundprefs_set_enabled(controller->prefs, i,
                                        controller->opened[i]);
        tiku_trk_soundprefs_set_clip(controller->prefs, i,
                                     controller->opened_clip[i]);
    }
    tiku_trk_prefs_set(controller->prefs, "sound.volume",
                       controller->opened_volume);
}

/** @brief Save the settings, push them into the kit, and redraw. */
static void
apply(tiku_trk_sounds_window_t *controller)
{
    if (controller->path[0] != '\0') {
        (void)tiku_trk_prefs_save(controller->prefs, controller->path);
    }
    tiku_trk_soundprefs_apply(controller->prefs);
    if (controller->dirty != NULL) {
        *controller->dirty = 1;
    }
}

/**
 * @brief Play @p event even when its row is off, leaving its on/off as it
 *        was.
 */
static void
test(tiku_sound_event_t event)
{
    int on = tiku_sound_enabled(event);

    tiku_sound_set_enabled(event, 1);
    tiku_sound_play(event);
    tiku_sound_set_enabled(event, on);
}

/**
 * @brief Draw the table of moments, the volume slider and the buttons.
 */
static void
draw(tiku_window_t *window, tiku_surface_t *surface, tiku_rect_t content,
     void *context)
{
    tiku_trk_sounds_window_t *controller = context;
    const tiku_font_t *font = tiku_font_plain();
    const tiku_font_t *bold = tiku_font_bold();
    char number[32], key[48];
    int volume = tiku_trk_soundprefs_volume(controller->prefs);
    int i;

    (void)window;
    tiku_fill(surface, content, TIKU_C_PANEL);
    tiku_text(surface, bold, content.x + PAD, content.y + bold->ascent + 10,
              tiku_str("prefs.sounds.event"), TIKU_C_TEXT);
    for (i = 0; i < TIKU_SOUND_EVENTS; i++) {
        const char *clip = tiku_trk_soundprefs_clip(controller->prefs, i);

        snprintf(key, sizeof key, "sound.%s", tiku_sound_name(i));
        tiku_ui_checkbox(surface, check_rect(content, i), tiku_str(key),
                         tiku_trk_soundprefs_enabled(controller->prefs, i)
                             ? TIKU_S_ON : 0u);
        tiku_ui_button(surface, clip_rect(content, i),
                       clip[0] != '\0' ? clip : tiku_str("sound.builtin"),
                       (controller->offer.count > 0 ? 0u : TIKU_S_DISABLED) |
                       (tiku_press_held(&controller->press, 3 * i + 1)
                            ? TIKU_S_PRESSED : 0u));
        tiku_ui_button(surface, choose_rect(content, i),
                       tiku_str("sound.choose"),
                       (controller->choose != NULL ? 0u : TIKU_S_DISABLED) |
                       (tiku_press_held(&controller->press, 3 * i + 2)
                            ? TIKU_S_PRESSED : 0u));
        tiku_ui_button(surface, test_rect(content, i),
                       tiku_str("button.test"),
                       tiku_press_held(&controller->press, 3 * i + 3)
                           ? TIKU_S_PRESSED : 0u);
    }
    {
        tiku_rect_t track = track_rect(content);
        tiku_rect_t knob = knob_rect(content, volume);
        tiku_rect_t shown = { track.x + track.w + 4, knob.y, 44, knob.h };

        tiku_text(surface, bold, content.x + PAD,
                  knob.y + (knob.h - bold->height) / 2 + bold->ascent,
                  tiku_str("prefs.sounds.volume"), TIKU_C_TEXT);
        tiku_ui_sunken(surface, track, TIKU_C_DOC);
        tiku_ui_raised(surface, knob);
        snprintf(number, sizeof number, "%d %%", volume);
        (void)tiku_text_centered(surface, font, shown, number, TIKU_C_TEXT);
    }
    tiku_ui_button(surface, defaults_rect(content), tiku_str("button.defaults"),
                   (tiku_trk_soundprefs_defaultable(controller->prefs)
                       ? 0u : TIKU_S_DISABLED) |
                   (tiku_press_held(&controller->press, 100)
                        ? TIKU_S_PRESSED : 0u));
    tiku_ui_button(surface, revert_rect(content), tiku_str("button.revert"),
                   (revertable(controller) ? 0u : TIKU_S_DISABLED) |
                   (tiku_press_held(&controller->press, 101)
                        ? TIKU_S_PRESSED : 0u));
}

/**
 * @brief A press toggles a moment or takes the volume knob; the clip,
 *        choose and Test buttons, Defaults and Revert act on a release
 *        over them.  What changed is applied.
 * @return 1 when taken.
 */
static int
event(tiku_window_t *window, const tiku_event_t *ev, void *context)
{
    tiku_trk_sounds_window_t *controller = context;
    tiku_rect_t content = tiku_window_content(window);
    int i;

    if (controller->tracking) {
        /* The knob follows the pointer, and the settings file is written
         * once, on the release. */
        tiku_trk_prefs_set(controller->prefs, "sound.volume",
                           volume_at(content, ev->x));
        tiku_trk_soundprefs_apply(controller->prefs);
        if (ev->type == TIKU_EVENT_POINTER_UP) {
            controller->tracking = 0;
            apply(controller);
        }
        if (controller->dirty != NULL) {
            *controller->dirty = 1;
        }
        return 1;
    }
    {
        /* Every button shows pressed while held and acts on a release
         * over it; the checkboxes and the knob act on the press. */
        int under = 0, k;

        for (k = 0; k < TIKU_SOUND_EVENTS && under == 0; k++) {
            if (controller->offer.count > 0 &&
                inside(clip_rect(content, k), ev->x, ev->y)) {
                under = 3 * k + 1;
            } else if (controller->choose != NULL &&
                       inside(choose_rect(content, k), ev->x, ev->y)) {
                under = 3 * k + 2;
            } else if (inside(test_rect(content, k), ev->x, ev->y)) {
                under = 3 * k + 3;
            }
        }
        if (under == 0 && inside(defaults_rect(content), ev->x, ev->y) &&
            tiku_trk_soundprefs_defaultable(controller->prefs)) { under = 100; }
        if (under == 0 && inside(revert_rect(content), ev->x, ev->y) &&
            revertable(controller)) { under = 101; }
        if (ev->type == TIKU_EVENT_POINTER_MOVE) {
            if (tiku_press_track(&controller->press, under)) {
                if (controller->dirty != NULL) { *controller->dirty = 1; }
            }
            return controller->press.id != 0;
        }
        if (ev->type == TIKU_EVENT_POINTER_UP) {
            int id = tiku_press_release(&controller->press, under);

            if (controller->dirty != NULL) { *controller->dirty = 1; }
            if (id == 100) {
                tiku_trk_soundprefs_defaults(controller->prefs);
                apply(controller);
            } else if (id == 101) {
                revert(controller);
                apply(controller);
            } else if (id > 0) {
                k = (id - 1) / 3;
                switch ((id - 1) % 3) {
                case 0:
                    /* The new clip is played as well as named. */
                    if (tiku_trk_soundprefs_cycle_clip(controller->prefs,
                                                       &controller->offer, k)) {
                        apply(controller);
                        test(k);
                    }
                    break;
                case 1:
                    if (controller->choose != NULL) {
                        controller->choose(controller->choose_ctx, k);
                    }
                    break;
                default:
                    test(k);
                    break;
                }
            }
            return 1;
        }
        if (ev->type == TIKU_EVENT_POINTER_DOWN && under != 0) {
            (void)tiku_press_arm(&controller->press, under);
            if (controller->dirty != NULL) { *controller->dirty = 1; }
            return 1;
        }
    }
    if (ev->type != TIKU_EVENT_POINTER_DOWN ||
        !inside(content, ev->x, ev->y)) {
        return 0;
    }
    for (i = 0; i < TIKU_SOUND_EVENTS; i++) {
        if (inside(check_rect(content, i), ev->x, ev->y)) {
            tiku_trk_soundprefs_set_enabled(
                controller->prefs, i,
                !tiku_trk_soundprefs_enabled(controller->prefs, i));
            apply(controller);
            return 1;
        }
    }
    {
        tiku_rect_t grip = knob_rect(content, 0);

        grip.x = track_rect(content).x - KNOB_W / 2;
        grip.w = track_rect(content).w + KNOB_W;
        if (inside(grip, ev->x, ev->y)) {
            controller->tracking = 1;
            tiku_trk_prefs_set(controller->prefs, "sound.volume",
                               volume_at(content, ev->x));
            tiku_trk_soundprefs_apply(controller->prefs);
            if (controller->dirty != NULL) {
                *controller->dirty = 1;
            }
            return 1;
        }
    }
    return 0;
}

/** @brief Copy the file at @p from to @p to.  @return 0 when done. */
static int
copy_file(const char *from, const char *to)
{
    FILE *in = fopen(from, "rb"), *out = NULL;
    char buf[4096];
    size_t n;
    int bad = (in == NULL);

    if (in != NULL) {
        (void)tiku_state_mkparents(to);
        out = fopen(to, "wb");
        bad = (out == NULL);
    }
    while (!bad && (n = fread(buf, 1, sizeof buf, in)) > 0) {
        bad = fwrite(buf, 1, n, out) != n;
    }
    if (in != NULL) {
        fclose(in);
    }
    if (out != NULL) {
        fclose(out);
    }
    return bad ? -1 : 0;
}

void
tiku_trk_sounds_window_chosen(tiku_trk_sounds_window_t *controller,
                              tiku_sound_event_t event, const char *path)
{
    const char *base;
    char dir[1024], to[1200];

    if (path == NULL || event < 0 || event >= TIKU_SOUND_EVENTS) {
        return;
    }
    base = strrchr(path, '/');
    base = (base != NULL) ? base + 1 : path;
    if (strlen(base) >= TIKU_TRK_PREF_TEXT_MAX) {
        return;
    }
    /* Copied into the sounds folder, where every row is offered it and
     * it no longer depends on the original file. */
    (void)tiku_sounds_dir(dir, sizeof dir);
    snprintf(to, sizeof to, "%s/%s", dir, base);
    if (strcmp(to, path) != 0 && copy_file(path, to) != 0) {
        return;
    }
    tiku_trk_soundprefs_set_clip(controller->prefs, event, base);
    (void)tiku_trk_soundprefs_offer(&controller->offer);
    apply(controller);
    test(event);
}

int
tiku_trk_sounds_window_tracking(const tiku_trk_sounds_window_t *controller)
{
    return controller->tracking;
}

void
tiku_trk_sounds_window_init(tiku_trk_sounds_window_t *controller,
                            tiku_workspace_t *workspace,
                            tiku_trk_prefs_t *prefs, const char *path,
                            int screen_w, int screen_h, int *dirty,
                            tiku_trk_sounds_choose_fn choose,
                            void *choose_ctx)
{
    memset(controller, 0, sizeof *controller);
    controller->workspace = workspace;
    controller->prefs = prefs;
    controller->choose = choose;
    controller->choose_ctx = choose_ctx;
    if (path != NULL) {
        snprintf(controller->path, sizeof controller->path, "%s", path);
    }
    controller->screen_w = screen_w;
    controller->screen_h = screen_h;
    controller->dirty = dirty;
}

void
tiku_trk_sounds_window_close(tiku_trk_sounds_window_t *controller)
{
    if (controller->window != NULL && controller->window->open) {
        tiku_workspace_close(controller->workspace, controller->window);
    }
    controller->window = NULL;
    if (controller->dirty != NULL) {
        *controller->dirty = 1;
    }
}

void
tiku_trk_sounds_window_destroy(tiku_trk_sounds_window_t *controller)
{
    tiku_trk_sounds_window_close(controller);
}

void
tiku_trk_sounds_window_open(tiku_trk_sounds_window_t *controller)
{
    tiku_rect_t frame;
    int i;

    if (controller->window != NULL && controller->window->open) {
        tiku_workspace_activate(controller->workspace, controller->window);
        return;
    }
    for (i = 0; i < TIKU_SOUND_EVENTS; i++) {
        controller->opened[i] =
            tiku_trk_soundprefs_enabled(controller->prefs, i);
        snprintf(controller->opened_clip[i], sizeof controller->opened_clip[i],
                 "%s", tiku_trk_soundprefs_clip(controller->prefs, i));
    }
    /* The sounds folder is read on every open, so a file dropped in while
     * the window was shut is offered. */
    (void)tiku_trk_soundprefs_offer(&controller->offer);
    controller->opened_volume = tiku_trk_soundprefs_volume(controller->prefs);
    frame.w = TIKU_TRK_SOUNDS_W;
    frame.h = window_height();
    frame.x = (controller->screen_w - frame.w) / 2;
    frame.y = (controller->screen_h - frame.h) / 3;
    controller->window = tiku_workspace_open(controller->workspace,
        tiku_str("title.sounds"), frame, draw, controller, NULL);
    if (controller->window != NULL) {
        tiku_window_set_size_controls(controller->window, 0, 0);
        tiku_window_set_handlers(controller->window, event, NULL, NULL);
    }
    if (controller->dirty != NULL) {
        *controller->dirty = 1;
    }
}

tiku_window_t *
tiku_trk_sounds_window_handle(tiku_trk_sounds_window_t *controller)
{
    return controller->window;
}
