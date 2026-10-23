/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_alert.h - the question put to the user.
 *
 * A text, a kind and a row of buttons, one of which the user presses:
 * the alert's state, layout, hit-testing, keys and look.  An application
 * that asks (save these changes?  replace that file?) holds one over its
 * own content.  The alert reports which button was pressed; what that
 * means is the asker's.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_ALERT_H_
#define TIKU_ALERT_H_

#include "tiku_gfx.h"

#include "tiku_press.h"

/** @brief How many buttons an alert can carry. */
#define TIKU_ALERT_BUTTONS 4

/** @brief What an alert is about; it picks the icon. */
typedef enum {
    TIKU_ALERT_INFO = 0,
    TIKU_ALERT_WARN,
    TIKU_ALERT_STOP
} tiku_alert_kind_t;

/** @brief One alert, open or not. */
typedef struct {
    int               open;
    tiku_alert_kind_t kind;
    char              text[512];
    char              button[TIKU_ALERT_BUTTONS][40];
    int               nbutton;
    int               cancel;   /* which button Escape presses       */
    /* Which button Return presses, drawn with the default ring: the
     * rightmost, stepped left past a Delete, Replace, Replace all or Empty
     * Trash label, stopping at the first button. */
    int               dflt;
    /* The width of every button, measured from the longest label by
     * tiku_alert_draw; 0 until then, which means a fixed width. */
    int               btn_w;
    int               chosen;   /* -1 until one is pressed           */
    /* The asker's own value from tiku_alert_open(), kept unchanged. */
    int               tag;
    /* The button a pointer press holds down, numbered from 1, drawn
     * pressed until the release; a release on it answers. */
    tiku_press_t      press;
} tiku_alert_t;

/** @brief Clear @p a: closed, no text or buttons, nothing chosen. */
void tiku_alert_reset(tiku_alert_t *a);

/**
 * @brief A press at (@p x, @p y) inside a @p w by @p h frame: arm the
 *        button under it, if any.
 * @return nonzero when a button took the press and wants a repaint.
 */
int tiku_alert_press(tiku_alert_t *a, int w, int h, int x, int y);

/**
 * @brief The pointer moved while pressing.
 * @return nonzero when the held button's look changed.
 */
int tiku_alert_track(tiku_alert_t *a, int w, int h, int x, int y);

/**
 * @brief The pointer let go at (@p x, @p y).
 * @return the button released on, now chosen, when it is the one
 *         pressed; otherwise -1, and the press is taken back.
 */
int tiku_alert_release(tiku_alert_t *a, int w, int h, int x, int y);

/**
 * @brief Raise an alert of @p kind showing @p text, keeping @p tag.
 *
 * @p buttons is a "|"-separated list of at most TIKU_ALERT_BUTTONS
 * labels, laid out left to right.  Escape presses the first; Return presses
 * dflt, which starts at the last button and steps left past the English
 * destructive labels.  A caller with translated labels sets dflt after
 * opening.  Text and labels are truncated to their array lengths.
 */
void tiku_alert_open(tiku_alert_t *a, tiku_alert_kind_t kind, int tag,
                     const char *text, const char *buttons);

/** @brief The panel's fixed size, for a caller placing one. */
void tiku_alert_size(int *w, int *h);

/** @brief The rectangle of button @p i inside a @p w by @p h alert. */
void tiku_alert_button_rect(const tiku_alert_t *a, int i, int w, int h,
                            int *bx, int *by, int *bw, int *bh);

/** @brief Which button a point is over, or -1. */
int tiku_alert_button_at(const tiku_alert_t *a, int w, int h, int x, int y);

/**
 * @brief Press the button a key stands for: Return the default, Escape
 *        the cancel button.
 * @return the button pressed, recorded in @p a->chosen as a click would
 *         be, or -1 when the key was not the alert's to take.
 */
int tiku_alert_key(tiku_alert_t *a, unsigned key);

/**
 * @brief Draw the whole panel into @p frame: chrome, stripe, icon, the
 * two-tier text, and the buttons with the default ringed.
 *
 * Also stores the button width for this alert's labels in @p a->btn_w,
 * which tiku_alert_button_rect() and the hit-test read.
 */
void tiku_alert_draw(tiku_alert_t *a, tiku_surface_t *s,
                     tiku_rect_t frame);

/**
 * @brief The kind's icon alone, centred on @p cx,@p cy: a ringed disc
 *        with its mark, in the selection colour for info, the tab colour
 *        for a warning and the danger colour for a stop.
 *
 * tiku_dl_play() draws an alert-icon command with it.
 */
void tiku_alert_icon_draw(tiku_surface_t *s, int cx, int cy,
                          tiku_alert_kind_t kind);

#endif /* TIKU_ALERT_H_ */
