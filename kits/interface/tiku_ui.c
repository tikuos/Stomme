/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_ui.c - the R5 control set, drawn.
 *
 * Raised and sunken controls are bevel pairs around a flat face, in tints
 * named as the BeOS constants (TIKU_LIGHTEN_*, TIKU_DARKEN_*); push
 * buttons, pills and the scrollbar thumb are rounded fills.  A control
 * drawn while recording is recorded as one command.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_dl.h"
#include "tiku_ui.h"

#include <string.h>

/*---------------------------------------------------------------------------*/
/* Control sizes.  At the default 12 px they are a 22-pixel row and a        */
/* 26-pixel button, which the shell suite's pixel tests depend on.           */
/*---------------------------------------------------------------------------*/

int
tiku_ui_row_h(void)
{
    return tiku_font_plain()->height + 7;      /* 22 at 12 px */
}

int
tiku_ui_button_h(void)
{
    return tiku_font_plain()->height + 11;     /* 26 at 12 px */
}

int
tiku_ui_button_w(const char *label, int least)
{
    int want = tiku_text_width(tiku_font_plain(), label) +
               2 * tiku_font_plain()->height;

    return (want > least) ? want : least;
}

int
tiku_ui_step_w(void)
{
    return tiku_ui_button_h();                 /* square: 26 at 12 px */
}

#define PANEL   TIKU_C_PANEL

/* The shades the controls are built from, all tints of the panel. */
#define WHITE()   tiku_tint(PANEL, TIKU_LIGHTEN_MAX)
#define LIGHT()   tiku_tint(PANEL, TIKU_LIGHTEN_1)
#define SHADOW()  tiku_tint(PANEL, TIKU_DARKEN_2)
#define FRAME()   tiku_tint(PANEL, TIKU_DARKEN_4)
#define DARK()    tiku_tint(PANEL, 1.40f)

void
tiku_ui_panel(tiku_surface_t *s, tiku_rect_t r)
{
    int rec = tiku_gfx_rec_enter(s);

    if (rec) {
        (void)tiku_dl_panel(s->record, r);
    }
    tiku_fill(s, r, PANEL);
    tiku_gfx_rec_leave(s, rec);
}

void
tiku_ui_raised(tiku_surface_t *s, tiku_rect_t r)
{
    int rec = tiku_gfx_rec_enter(s);

    if (rec) {
        (void)tiku_dl_raised(s->record, r);
    }
    tiku_fill(s, r, PANEL);
    tiku_bevel(s, r, WHITE(), SHADOW());
    tiku_gfx_rec_leave(s, rec);
}

void
tiku_ui_popup_border(tiku_surface_t *s, tiku_rect_t r)
{
    /* Three parts text to one part panel: dark in the light and no-hue
     * themes, light in the dark one. */
    tiku_rgb_t edge = tiku_dim(TIKU_C_TEXT, tiku_dim(TIKU_C_TEXT, PANEL));

    tiku_frame(s, r, edge);
    tiku_frame(s, tiku_inset(r, 1), edge);
}

void
tiku_ui_sunken(tiku_surface_t *s, tiku_rect_t r,
                    tiku_rgb_t face)
{
    int rec = tiku_gfx_rec_enter(s);

    if (rec) {
        (void)tiku_dl_sunken(s->record, r, face);
    }
    tiku_fill(s, r, face);
    /* Sunken is the raised bevel inverted: shadow above, light below. */
    tiku_bevel(s, r, tiku_tint(PANEL, TIKU_DARKEN_3), WHITE());
    tiku_bevel(s, tiku_inset(r, 1), DARK(), PANEL);
    tiku_gfx_rec_leave(s, rec);
}

/** @brief Paint the four corner pixels of @p r in @p bg: a square
 *         control's corners, clipped by one pixel. */
static void
clip_corners(tiku_surface_t *s, tiku_rect_t r, tiku_rgb_t bg)
{
    tiku_pixel(s, r.x, r.y, bg);
    tiku_pixel(s, r.x + r.w - 1, r.y, bg);
    tiku_pixel(s, r.x, r.y + r.h - 1, bg);
    tiku_pixel(s, r.x + r.w - 1, r.y + r.h - 1, bg);
}

void
tiku_ui_button(tiku_surface_t *s, tiku_rect_t r,
                    const char *label, unsigned state)
{
    int rec = tiku_gfx_rec_enter(s);

    if (rec) {
        (void)tiku_dl_button(s->record, r, label, state);
    }
    if (state & TIKU_S_ICON) {      /* the face only; the caller's icon */
        if (state & TIKU_S_PRESSED) {
            tiku_ui_sunken(s, r, PANEL);
        } else if ((state & TIKU_S_HOT) && !(state & TIKU_S_DISABLED)) {
            tiku_ui_raised(s, r);
        }
        if (state & TIKU_S_FOCUS) {
            tiku_frame(s, tiku_inset(r, 1), TIKU_C_FOCUS);
        }
        tiku_gfx_rec_leave(s, rec);
        return;
    }
    tiku_rect_t f = r;
    tiku_rgb_t text = (state & TIKU_S_DISABLED)
                           ? tiku_tint(PANEL, 1.30f)
                           : TIKU_C_TEXT;
    const tiku_font_t *font = tiku_font_plain();

    tiku_rgb_t face = PANEL;

    if (state & TIKU_S_DEFAULT) {
        /* The default button, the one Return presses, is filled with
         * the selection colour. */
        face = TIKU_C_SELECT;
        if (!(state & TIKU_S_DISABLED)) { text = TIKU_C_SELTEXT; }
    }
    if (state & TIKU_S_PRESSED) {
        /* Pressed darkens the face, and the label moves a pixel down and
         * right. */
        face = tiku_tint(face, 1.16f);
    }
    tiku_fill_round(s, f, 4, TIKU_CORNER_ALL, face);
    tiku_frame_round(s, f, 4, TIKU_CORNER_ALL,
                     (state & TIKU_S_DEFAULT) ? tiku_tint(face, 1.25f)
                                              : DARK());
    if (state & TIKU_S_FOCUS) {
        tiku_frame_round(s, tiku_inset(f, 2), 3, TIKU_CORNER_ALL,
                         TIKU_C_FOCUS);
    }
    {
        tiku_rect_t t = f;
        if (state & TIKU_S_PRESSED) {
            t.x += 1;
            t.y += 1;
        }
        (void)tiku_text_centered(s, font, t, label, text);
    }
    tiku_gfx_rec_leave(s, rec);
}

/** @brief The 13x13 well shared by checkbox and radio. */
static tiku_rect_t
box_rect(tiku_rect_t r)
{
    tiku_rect_t b;

    b.w = 13;
    b.h = 13;
    b.x = r.x;
    b.y = r.y + (r.h - 13) / 2;
    return b;
}

/**
 * @brief One square of a tab widget: a gradient from its top-left corner
 *        to its bottom-right inside a darker outline, light to @p face, or
 *        darker to lighter while @p down.
 */
static void
blended_rect(tiku_surface_t *s, tiku_rect_t r, tiku_rgb_t face,
             int down)
{
    tiku_rgb_t start = down ? tiku_tint(face, TIKU_DARKEN_1)
                                 : tiku_tint(face,
                                                  TIKU_LIGHTEN_MAX);
    tiku_rgb_t end = down ? tiku_tint(face, TIKU_LIGHTEN_1)
                               : face;
    tiku_rect_t fill = tiku_inset(r, 1);
    int x, y, span;

    if (r.w < 2 || r.h < 2) {
        return;
    }
    span = (fill.w - 1) + (fill.h - 1);
    if (span <= 0) {
        span = 1;
    }
    for (y = 0; y < fill.h; y++) {
        for (x = 0; x < fill.w; x++) {
            int t = ((x + y) * 255) / span;
            unsigned cr = (((start >> 16) & 0xFFu) * (255 - t) +
                           ((end >> 16) & 0xFFu) * t) / 255u;
            unsigned cg = (((start >> 8) & 0xFFu) * (255 - t) +
                           ((end >> 8) & 0xFFu) * t) / 255u;
            unsigned cb = ((start & 0xFFu) * (255 - t) +
                           (end & 0xFFu) * t) / 255u;

            tiku_pixel(s, fill.x + x, fill.y + y,
                            TIKU_RGB(cr, cg, cb));
        }
    }
    tiku_frame(s, r, tiku_tint(face, TIKU_DARKEN_2));
}

void
tiku_ui_tab_widget(tiku_surface_t *s, tiku_rect_t r,
                        int which, tiku_rgb_t face, int down)
{
    if (which == TIKU_TAB_ZOOM) {
        /* Two squares from the same painter: the big one pushed down and
         * right by a quarter, the small one cut back by nearly a half. */
        int inset = r.w / 4;
        tiku_rect_t big = { r.x + inset, r.y + inset, r.w - inset,
                                 r.h - inset };
        int cut = (int)((float)r.w / 2.1f);
        tiku_rect_t small = { r.x, r.y, r.w - cut, r.h - cut };

        blended_rect(s, big, face, down);
        blended_rect(s, small, face, down);
        return;
    }
    blended_rect(s, r, face, down);
}

void
tiku_ui_menufield(tiku_surface_t *s, tiku_rect_t r,
                       const char *label, unsigned state)
{
    int rec = tiku_gfx_rec_enter(s);
    const tiku_font_t *font = tiku_font_plain();
    tiku_rgb_t text = (state & TIKU_S_DISABLED)
                           ? tiku_tint(PANEL, 1.30f)
                           : TIKU_C_TEXT;
    int size, ax, ay, hin, vin, pen, i, y;

    if (rec) {
        (void)tiku_dl_menufield(s->record, r, label, state);
    }
    tiku_fill(s, r, PANEL);
    tiku_frame(s, r, DARK());
    clip_corners(s, r, PANEL);
    if (state & TIKU_S_PRESSED) {
        tiku_bevel(s, tiku_inset(r, 1), SHADOW(), WHITE());
    } else {
        tiku_bevel(s, tiku_inset(r, 1), WHITE(), SHADOW());
        tiku_bevel(s, tiku_inset(r, 2), LIGHT(), PANEL);
    }
    if (state & TIKU_S_FOCUS) {
        tiku_frame(s, tiku_inset(r, 3), TIKU_C_FOCUS);
    }

    /* The label is clipped short of the marker. */
    size = (r.h * 2) / 3;
    ax = r.x + r.w - size - 5;
    tiku_clip_set(s, (tiku_rect_t){ r.x + 3, r.y, ax - r.x - 6,
                                              r.h });
    tiku_text(s, font, r.x + 8,
                   r.y + (r.h - font->height) / 2 + font->ascent,
                   (label != NULL) ? label : "", text);
    tiku_clip_reset(s);

    /* The marker: a V of two strokes, pen pixels thick. */
    ay = r.y + (r.h - size) / 2;
    hin = size / 3;
    vin = size / 3;
    pen = (vin / 2 > 0) ? vin / 2 : 1;
    for (i = 0; i < pen; i++) {
        for (y = 0; y <= (size - 2 * hin) / 2; y++) {
            tiku_pixel(s, ax + hin + y, ay + vin + y + i, DARK());
            tiku_pixel(s, ax + size - hin - y, ay + vin + y + i, DARK());
        }
    }
    tiku_gfx_rec_leave(s, rec);
}

void
tiku_ui_checkbox(tiku_surface_t *s, tiku_rect_t r,
                      const char *label, unsigned state)
{
    int rec = tiku_gfx_rec_enter(s);

    if (rec) {
        (void)tiku_dl_checkbox(s->record, r, label, state);
    }
    tiku_rect_t b = box_rect(r);
    tiku_rgb_t text = (state & TIKU_S_DISABLED)
                           ? tiku_tint(PANEL, 1.30f) : TIKU_C_TEXT;

    tiku_fill(s, b, (state & TIKU_S_PRESSED)
                         ? tiku_tint(PANEL, TIKU_DARKEN_1)
                         : TIKU_C_DOC);
    tiku_bevel(s, b, DARK(), WHITE());
    tiku_bevel(s, tiku_inset(b, 1), SHADOW(), PANEL);

    if (state & TIKU_S_ON) {
        /* The mark is a solid square. */
        tiku_rect_t m = tiku_inset(b, 3);
        tiku_fill(s, m, (state & TIKU_S_DISABLED)
                             ? tiku_tint(PANEL, 1.30f)
                             : TIKU_C_TEXT);
    }
    if (state & TIKU_S_FOCUS) {
        tiku_frame(s, (tiku_rect_t){ b.x - 2, b.y - 2, b.w + 4,
                                               b.h + 4 }, TIKU_C_FOCUS);
    }
    if (label != NULL) {
        const tiku_font_t *f = tiku_font_plain();
        tiku_text(s, f, b.x + b.w + 4,
                       r.y + (r.h - f->height) / 2 + f->ascent, label, text);
    }
    tiku_gfx_rec_leave(s, rec);
}

void
tiku_ui_radio(tiku_surface_t *s, tiku_rect_t r,
                   const char *label, unsigned state)
{
    int rec = tiku_gfx_rec_enter(s);

    if (rec) {
        (void)tiku_dl_radio(s->record, r, label, state);
    }
    tiku_rect_t b = box_rect(r);
    tiku_rgb_t text = (state & TIKU_S_DISABLED)
                           ? tiku_tint(PANEL, 1.30f) : TIKU_C_TEXT;
    int cx = b.x + b.w / 2, cy = b.y + b.h / 2, rad = b.w / 2;
    int y;

    /* A 13 px circle drawn as horizontal spans, each with a shaded pixel
     * at either end for the bevel. */
    for (y = -rad; y <= rad; y++) {
        int half = (y * y >= rad * rad) ? 0
                 : (int)((rad * rad - y * y) / (rad * 0.9f) + 0.5f);
        int len = 2 * half + 1;
        tiku_hline(s, cx - half, cy + y, len,
                        (state & TIKU_S_PRESSED)
                        ? tiku_tint(PANEL, 1.16f)
                        : ((state & TIKU_S_DISABLED)
                           ? tiku_tint(PANEL, TIKU_LIGHTEN_1)
                           : TIKU_C_DOC));
        if (y == -rad || y == rad) {
            tiku_hline(s, cx - half, cy + y, len, DARK());
        } else {
            tiku_pixel(s, cx - half - 1, cy + y, (y < 0) ? DARK()
                                                              : SHADOW());
            tiku_pixel(s, cx + half + 1, cy + y, (y < 0) ? SHADOW()
                                                              : WHITE());
        }
    }
    if (state & TIKU_S_ON) {
        for (y = -2; y <= 2; y++) {
            int half = (y * y >= 4) ? 1 : 2;
            tiku_hline(s, cx - half, cy + y, 2 * half + 1,
                            (state & TIKU_S_DISABLED)
                            ? tiku_tint(PANEL, 1.30f)
                            : TIKU_C_TEXT);
        }
    }
    if (state & TIKU_S_FOCUS) {
        tiku_frame(s, (tiku_rect_t){ b.x - 2, b.y - 2, b.w + 4,
                                               b.h + 4 }, TIKU_C_FOCUS);
    }
    if (label != NULL) {
        const tiku_font_t *f = tiku_font_plain();
        tiku_text(s, f, b.x + b.w + 4,
                       r.y + (r.h - f->height) / 2 + f->ascent, label, text);
    }
    tiku_gfx_rec_leave(s, rec);
}

void
tiku_ui_textfield(tiku_surface_t *s, tiku_rect_t r,
                       const char *text, int caret, unsigned state)
{
    tiku_ui_textfield_sel(s, r, text, caret, 0, 0, state);
}

/** @brief Width of the first @p at bytes of @p text, at most 255. */
static int
text_offset(const tiku_font_t *f, const char *text, int at)
{
    char head[256];
    int n = (at < (int)sizeof head - 1) ? at : (int)sizeof head - 1;

    if (n <= 0) {
        return 0;
    }
    memcpy(head, text, (size_t)n);
    head[n] = '\0';
    return tiku_text_width(f, head);
}

void
tiku_ui_textfield_sel(tiku_surface_t *s, tiku_rect_t r,
                           const char *text, int caret, int sel_a, int sel_b,
                           unsigned state)
{
    tiku_ui_textfield_scroll(s, r, text, caret, sel_a, sel_b, state,
                                  0);
}

void
tiku_ui_textfield_scroll(tiku_surface_t *s, tiku_rect_t r,
                              const char *text, int caret, int sel_a,
                              int sel_b, unsigned state, int scroll_px)
{
    int rec = tiku_gfx_rec_enter(s);
    tiku_rect_t saved_clip = s->clip;

    if (rec) {
        /* Recorded with its text and state; the caret, the selection
         * and the slide are not recorded. */
        (void)tiku_dl_textfield(s->record, r, text, state);
    }
    const tiku_font_t *f = tiku_font_plain();
    tiku_rgb_t face = (state & TIKU_S_DISABLED)
                           ? PANEL : TIKU_C_DOC;
    int ty = r.y + (r.h - f->height) / 2 + f->ascent;

    tiku_ui_sunken(s, r, face);
    if (state & TIKU_S_FOCUS) {
        tiku_frame(s, r, TIKU_C_FOCUS);
    }
    /* The text is clipped to the field's inside, within whatever clip the
     * caller set; that clip is put back when the field is drawn. */
    tiku_rect_t inner = tiku_inset(r, 2);
    int right = inner.x + inner.w < saved_clip.x + saved_clip.w
                    ? inner.x + inner.w : saved_clip.x + saved_clip.w;
    int bottom = inner.y + inner.h < saved_clip.y + saved_clip.h
                    ? inner.y + inner.h : saved_clip.y + saved_clip.h;

    if (inner.x < saved_clip.x) {
        inner.x = saved_clip.x;
    }
    if (inner.y < saved_clip.y) {
        inner.y = saved_clip.y;
    }
    inner.w = right > inner.x ? right - inner.x : 0;
    inner.h = bottom > inner.y ? bottom - inner.y : 0;
    tiku_clip_set(s, inner);
    if (text != NULL) {
        int lo = (sel_a < sel_b) ? sel_a : sel_b;
        int hi = (sel_a < sel_b) ? sel_b : sel_a;

        tiku_text(s, f, r.x + 3 - scroll_px, ty, text,
                  (state & TIKU_S_DISABLED)
                  ? tiku_tint(PANEL, 1.30f) : TIKU_C_TEXT);
        if (hi > lo) {
            tiku_rect_t hl;

            hl.x = r.x + 3 - scroll_px + text_offset(f, text, lo);
            hl.y = r.y + 2;
            hl.w = text_offset(f, text, hi) - text_offset(f, text, lo);
            hl.h = r.h - 4;
            tiku_fill(s, hl, TIKU_C_SELECT);
            right = hl.x + hl.w < inner.x + inner.w
                      ? hl.x + hl.w : inner.x + inner.w;
            hl.x = hl.x > inner.x ? hl.x : inner.x;
            hl.w = right > hl.x ? right - hl.x : 0;
            hl.y = inner.y;
            hl.h = inner.h;
            tiku_clip_set(s, hl);
            tiku_text(s, f, r.x + 3 - scroll_px, ty, text, TIKU_C_SELTEXT);
            tiku_clip_set(s, inner);
        }
        if (caret >= 0) {
            tiku_vline(s, r.x + 3 - scroll_px + text_offset(f, text, caret),
                            r.y + 3, r.h - 6, TIKU_C_TEXT);
        }
    }
    tiku_clip_set(s, saved_clip);
    tiku_gfx_rec_leave(s, rec);
}

/**
 * @brief A scrollbar arrow button with its triangle, dimmed unless @p can;
 *        a held one (@p down) has a darker face.
 */
static void
arrow_button(tiku_surface_t *s, tiku_rect_t r, int dir, int can, int down)
{
    tiku_fill(s, r, down ? tiku_tint(PANEL, 1.16f) : tiku_tint(PANEL, 1.10f));
    tiku_frame(s, r, FRAME());
    tiku_ui_arrow(s, r, dir, can);
}

tiku_rect_t
tiku_ui_tip_size(const char *text)
{
    const tiku_font_t *f = tiku_font_plain();
    tiku_rect_t r = { 0, 0, 0, 0 };

    if (text == NULL || text[0] == '\0') {
        return r;
    }
    r.w = tiku_text_width(f, text) + 12;
    r.h = f->height + 6;
    return r;
}

void
tiku_ui_tip(tiku_surface_t *s, tiku_rect_t r, const char *text)
{
    const tiku_font_t *f = tiku_font_plain();
    int rec;

    if (s == NULL || text == NULL || text[0] == '\0' ||
        r.w <= 0 || r.h <= 0) {
        return;
    }
    rec = tiku_gfx_rec_enter(s);
    if (rec) {
        (void)tiku_dl_tip(s->record, r, text);
    }
    tiku_fill(s, r, TIKU_C_NOTE);
    tiku_frame(s, r, TIKU_C_TEXT);
    tiku_text(s, f, r.x + 6, r.y + 3 + f->ascent, text, TIKU_C_TEXT);
    tiku_gfx_rec_leave(s, rec);
}

void
tiku_ui_arrow(tiku_surface_t *s, tiku_rect_t r, int dir, int enabled)
{
    int cx = r.x + r.w / 2, cy = r.y + r.h / 2, i;
    /* A disabled arrow is drawn lighter. */
    tiku_rgb_t ink = enabled ? DARK()
                             : tiku_tint(DARK(), TIKU_LIGHTEN_2);

    if (s == NULL) {
        return;
    }
    for (i = 0; i < 4; i++) {
        int len = 2 * i + 1;

        switch (dir) {
        case 0: tiku_hline(s, cx - i, cy - 2 + i, len, ink); break;
        case 1: tiku_hline(s, cx - i, cy + 2 - i, len, ink); break;
        case 2: tiku_vline(s, cx - 2 + i, cy - i, len, ink); break;
        default: tiku_vline(s, cx + 2 - i, cy - i, len, ink); break;
        }
    }
}

void
tiku_ui_gauge(tiku_surface_t *s, tiku_rect_t r, float fraction)
{
    int rec = tiku_gfx_rec_enter(s);
    tiku_rect_t fill;

    if (!(fraction >= 0.0f)) { fraction = 0.0f; }
    if (fraction > 1.0f) { fraction = 1.0f; }
    if (rec) {
        /* Recorded as its value in thousandths; the frame and the bar
         * below are drawn without being recorded. */
        (void)tiku_dl_gauge(s->record, r, (int)(fraction * 1000.0f));
    }

    tiku_fill(s, r, TIKU_C_DOC);
    tiku_frame(s, r, tiku_tint(PANEL, 1.40f));
    fill = (tiku_rect_t){ r.x + 1, r.y + 1,
                          (int)((float)(r.w - 2) * fraction), r.h - 2 };
    if (fill.w > 0 && fill.h > 0) {
        tiku_fill(s, fill, TIKU_C_SELECT);
    }
    tiku_gfx_rec_leave(s, rec);
}

int
tiku_ui_pill_w(const char *label)
{
    return tiku_text_width(tiku_font_bold(), label) + 20;
}

void
tiku_ui_pill(tiku_surface_t *s, tiku_rect_t r, const char *label, int tone)
{
    const tiku_font_t *f = tiku_font_bold();
    tiku_rgb_t ground;
    int tw;

    if (s == NULL || r.w <= 0 || r.h <= 0) {
        return;
    }
    switch (tone) {
    case TIKU_TONE_PASS: ground = TIKU_C_SUCCESS; break;
    case TIKU_TONE_FAIL: ground = TIKU_C_DANGER; break;
    case TIKU_TONE_WARN: ground = TIKU_C_WARN; break;
    default:             ground = tiku_tint(PANEL, 1.60f); break;
    }
    tiku_fill_round(s, r, r.h / 2, TIKU_CORNER_ALL, ground);
    if (label == NULL || label[0] == '\0') {
        return;
    }
    tw = tiku_text_width(f, label);
    tiku_text(s, f, r.x + (r.w - tw) / 2,
              r.y + (r.h - f->height) / 2 + f->ascent, label,
              TIKU_C_SELTEXT);
}

/* A switch's track, in pixels; tiku_ui_switch() lowers it to fit a
 * shorter rectangle. */
#define SWITCH_H  18
#define SWITCH_W  34

int
tiku_ui_switch_w(const char *label)
{
    return tiku_text_width(tiku_font_plain(), label != NULL ? label : "") +
           8 + SWITCH_W;
}

void
tiku_ui_switch(tiku_surface_t *s, tiku_rect_t r, const char *label,
               unsigned state)
{
    const tiku_font_t *f = tiku_font_plain();
    int h = r.h < SWITCH_H ? r.h : SWITCH_H;
    tiku_rect_t t = { r.x + r.w - SWITCH_W, r.y + (r.h - h) / 2, SWITCH_W,
                      h };
    tiku_rect_t k = { 0, t.y + 2, h - 4, h - 4 };
    int dim = (state & TIKU_S_DISABLED) != 0;
    tiku_rgb_t ground = (state & TIKU_S_ON) ? TIKU_C_SELECT : DARK();

    if (s == NULL || r.w <= 0 || r.h <= 0) {
        return;
    }
    if (state & TIKU_S_PRESSED) {
        ground = tiku_tint(ground, TIKU_DARKEN_4);
    }
    if (dim) {
        ground = tiku_tint(PANEL, 1.20f);
    }
    tiku_fill_round(s, t, h / 2, TIKU_CORNER_ALL, ground);
    k.x = (state & TIKU_S_ON) ? t.x + t.w - 2 - k.w : t.x + 2;
    tiku_fill_round(s, k, k.w / 2, TIKU_CORNER_ALL,
                    dim ? LIGHT() : WHITE());
    if (state & TIKU_S_FOCUS) {
        tiku_frame_round(s, (tiku_rect_t){ t.x - 2, t.y - 2, t.w + 4,
                                           t.h + 4 }, h / 2 + 2,
                         TIKU_CORNER_ALL, TIKU_C_FOCUS);
    }
    if (label != NULL && label[0] != '\0') {
        tiku_text(s, f, r.x, r.y + (r.h - f->height) / 2 + f->ascent, label,
                  dim ? tiku_tint(PANEL, 1.30f) : TIKU_C_TEXT);
    }
}

void
tiku_ui_scrollbar_held(tiku_surface_t *s, tiku_rect_t r,
                       float pos, float frac, int horiz, int held)
{
    int rec = tiku_gfx_rec_enter(s);
    int btn = horiz ? r.h : r.w;
    tiku_rect_t thumb;

    /* The track and its frame, darker than the panel. */
    tiku_fill(s, r, tiku_tint(PANEL, 1.10f));
    tiku_frame(s, r, tiku_tint(PANEL, 1.22f));

    {
        /* An arrow dims at its end of the track, and both dim when
         * everything fits. */
        int can_back = frac < 1.0f && pos > 0.0f;
        int can_on = frac < 1.0f && pos < 1.0f;

        if (horiz) {
            arrow_button(s, (tiku_rect_t){ r.x, r.y, btn, btn }, 2,
                         can_back, held < 0);
            arrow_button(s, (tiku_rect_t){ r.x + r.w - btn, r.y, btn,
                                                btn }, 3, can_on, held > 0);
        } else {
            arrow_button(s, (tiku_rect_t){ r.x, r.y, btn, btn }, 0,
                         can_back, held < 0);
            arrow_button(s, (tiku_rect_t){ r.x, r.y + r.h - btn, btn,
                                                btn }, 1, can_on, held > 0);
        }
    }
    if (frac > 1.0f) { frac = 1.0f; }
    if (frac < 0.05f) { frac = 0.05f; }
    if (rec) {
        /* Recorded as the position and length in thousandths, rounded,
         * so thousandths handed in come back exactly. */
        (void)tiku_dl_scrollbar(s->record, r, (int)(pos * 1000.0f + 0.5f),
                                (int)(frac * 1000.0f + 0.5f), horiz);
    }
    thumb = tiku_ui_scrollbar_thumb(r, pos, frac, horiz);

    {
        /* A pill, a pixel in from the track on each side. */
        tiku_rect_t pill = horiz
            ? (tiku_rect_t){ thumb.x + 1, thumb.y + 2, thumb.w - 2,
                             thumb.h - 4 }
            : (tiku_rect_t){ thumb.x + 2, thumb.y + 1, thumb.w - 4,
                             thumb.h - 2 };
        int radius = ((horiz ? pill.h : pill.w) - 1) / 2;

        tiku_fill_round(s, pill, radius, TIKU_CORNER_ALL,
                        tiku_tint(PANEL, 1.42f));
    }

    tiku_gfx_rec_leave(s, rec);
}

tiku_rect_t
tiku_ui_scrollbar_thumb(tiku_rect_t r, float pos, float frac, int horiz)
{
    int btn = horiz ? r.h : r.w;
    int span = horiz ? r.w - 2 * btn : r.h - 2 * btn;
    int tlen, toff;

    if (frac > 1.0f) { frac = 1.0f; }
    if (frac < 0.05f) { frac = 0.05f; }
    tlen = (int)((float)span * frac);
    toff = (int)((float)(span - tlen) * pos);
    return horiz ? (tiku_rect_t){ r.x + btn + toff, r.y, tlen, r.h }
                 : (tiku_rect_t){ r.x, r.y + btn + toff, r.w, tlen };
}

void
tiku_ui_scrollbar(tiku_surface_t *s, tiku_rect_t r,
                  float pos, float frac, int horiz)
{
    tiku_ui_scrollbar_held(s, r, pos, frac, horiz, 0);
}

void
tiku_ui_menubar(tiku_surface_t *s, tiku_rect_t r,
                     const char *const *items, int n, int active)
{
    const tiku_font_t *f = tiku_font_plain();
    int x = r.x + 6, i;

    tiku_fill(s, r, PANEL);
    tiku_hline(s, r.x, r.y + r.h - 1, r.w, SHADOW());
    for (i = 0; i < n; i++) {
        int w = tiku_text_width(f, items[i]) + 16;
        tiku_rect_t it = { x, r.y, w, r.h - 1 };
        int on = (i == active);

        if (on) {
            tiku_fill(s, it, TIKU_C_SELECT);
        }
        (void)tiku_text_centered(s, f, it, items[i],
                                      on ? TIKU_C_SELTEXT
                                         : TIKU_C_TEXT);
        x += w;
    }
}

void
tiku_ui_menu_icons(tiku_surface_t *s, tiku_rect_t r,
                        const char *const *items, int n, int hot,
                        tiku_ui_menu_icon_fn icon, void *context)
{
    const tiku_font_t *f = tiku_font_plain();
    int rowh = f->height + 6;
    int indent = (icon != NULL) ? 20 : 0;
    int i;

    tiku_fill(s, r, PANEL);
    tiku_ui_popup_border(s, r);
    for (i = 0; i < n; i++) {
        tiku_rect_t row = { r.x + 2, r.y + 2 + i * rowh, r.w - 4, rowh };
        int on = (i == hot);

        if (items[i][0] == '-') {          /* separator */
            int y = row.y + rowh / 2;
            tiku_hline(s, row.x + 1, y, row.w - 2, DARK());
            tiku_hline(s, row.x + 1, y + 1, row.w - 2, WHITE());
            continue;
        }
        if (on) {
            tiku_fill(s, row, TIKU_C_SELECT);
        }
        if (icon != NULL) {
            icon(s, i, row.x + 4, row.y + (rowh - 16) / 2, 16, context);
        }
        /* A long label is clipped to its row. */
        tiku_clip_set(s, row);
        tiku_text(s, f, row.x + 8 + indent,
                       row.y + (rowh - f->height) / 2 + f->ascent, items[i],
                       on ? TIKU_C_SELTEXT : TIKU_C_TEXT);
        tiku_clip_reset(s);
    }
}

void
tiku_ui_menu(tiku_surface_t *s, tiku_rect_t r,
                  const char *const *items, int n, int hot)
{
    tiku_ui_menu_icons(s, r, items, n, hot, NULL, NULL);
}

void
tiku_ui_submenu_arrow(tiku_surface_t *s, tiku_rect_t item, tiku_rgb_t c)
{
    int th, tw, mid, x, y, i;

    if (s == NULL || item.h <= 0) {
        return;
    }
    /* Half the row's height, made odd so the point is a single pixel
     * row, and at least 5. */
    th = item.h / 2;
    if ((th & 1) == 0) {
        th--;
    }
    if (th < 5) {
        th = 5;
    }
    tw = th / 2 + 1;
    mid = th / 2;

    /* Set 8 pixels in from the right edge. */
    x = item.x + item.w - 8 - tw;
    y = item.y + (item.h - th) / 2;

    for (i = 0; i < th; i++) {
        int d = (i > mid) ? i - mid : mid - i;
        int len = tw - (d * (tw - 1)) / mid;

        tiku_hline(s, x, y + i, len, c);
    }
}

void
tiku_ui_list_row(tiku_surface_t *s, tiku_rect_t r,
                      const char *text, int selected)
{
    int rec = tiku_gfx_rec_enter(s);

    if (rec) {
        (void)tiku_dl_list_row(s->record, r, text, selected);
    }
    const tiku_font_t *f = tiku_font_plain();

    tiku_fill(s, r, selected ? TIKU_C_SELECT : TIKU_C_DOC);
    tiku_text(s, f, r.x + 4, r.y + (r.h - f->height) / 2 + f->ascent,
                   text, selected ? TIKU_C_SELTEXT : TIKU_C_TEXT);
    tiku_gfx_rec_leave(s, rec);
}

void
tiku_ui_list_header(tiku_surface_t *s, tiku_rect_t r,
                         const char *const *cols, const int *widths, int n,
                         int sort_col)
{
    const tiku_font_t *f = tiku_font_plain();
    int x = r.x, i;

    for (i = 0; i < n; i++) {
        tiku_rect_t c = { x, r.y, widths[i], r.h };

        tiku_fill(s, c, PANEL);
        tiku_bevel(s, c, WHITE(), SHADOW());
        tiku_text(s, f, c.x + 5, c.y + (c.h - f->height) / 2 + f->ascent,
                       cols[i], TIKU_C_TEXT);
        if (i == sort_col) {
            /* Sort marker: a small downward triangle. */
            int cx = c.x + c.w - 10, cy = c.y + c.h / 2, k;
            for (k = 0; k < 3; k++) {
                tiku_hline(s, cx - k, cy + 1 - k, 2 * k + 1, DARK());
            }
        }
        x += widths[i];
    }
}

tiku_rect_t
tiku_ui_window(tiku_surface_t *s, tiku_rect_t r,
                    const char *title, int active)
{
    const tiku_font_t *f = tiku_font_bold();
    const int tabh = 21, border = 5;
    int tabw = tiku_text_width(f, title) + 24;
    tiku_rect_t tab = { r.x + 4, r.y, tabw, tabh };
    tiku_rect_t body = { r.x, r.y + tabh - 1, r.w, r.h - tabh + 1 };
    tiku_rect_t content;
    tiku_rgb_t tabc = active ? TIKU_C_TAB : TIKU_C_TAB_IDLE;

    if (tabw > r.w - 8) {
        tabw = r.w - 8;
        tab.w = tabw;
    }

    /* The body frame first, so the tab overlaps its top edge. */
    tiku_fill(s, body, PANEL);
    tiku_frame(s, body, DARK());
    tiku_bevel(s, tiku_inset(body, 1), WHITE(), SHADOW());

    tiku_fill(s, tab, tabc);
    tiku_frame(s, tab, DARK());
    tiku_bevel(s, tiku_inset(tab, 1),
                    tiku_tint(tabc, TIKU_LIGHTEN_1),
                    tiku_tint(tabc, TIKU_DARKEN_2));
    /* The tab's bottom edge is the body's top: erase the line between. */
    tiku_hline(s, tab.x + 1, tab.y + tab.h - 1, tab.w - 2, tabc);
    tiku_hline(s, tab.x + 1, tab.y + tab.h, tab.w - 2, PANEL);
    tiku_text(s, f, tab.x + 12,
                   tab.y + (tabh - f->height) / 2 + f->ascent, title,
                   TIKU_C_TEXT);

    content.x = body.x + border;
    content.y = body.y + border;
    content.w = body.w - 2 * border;
    content.h = body.h - 2 * border;
    return content;
}

int
tiku_ui_text_spans(tiku_surface_t *s, const tiku_font_t *f, int x, int y,
                        const char *text, const tiku_span_t *span, int n)
{
    /* Each run is copied out and terminated, since tiku_text() draws a
     * whole string; a run longer than the buffer is drawn in pieces. */
    char run[256];
    int at = 0, start = x;
    int i = 0;

    if (s == NULL || f == NULL || text == NULL) {
        return 0;
    }
    for (;;) {
        tiku_ink_t ink = TIKU_INK_PLAIN;
        int len;

        if (text[at] == '\0') {
            break;
        }
        if (span != NULL && i < n) {
            ink = span[i].ink;
            len = span[i].len;
            i++;
        } else {
            /* Past the table, the rest of the line is plain. */
            len = (int)strlen(text + at);
        }
        if (len <= 0) {
            continue;
        }
        while (len > 0 && text[at] != '\0') {
            int take = len;
            int k;

            if (take > (int)sizeof run - 1) {
                int back = (int)sizeof run - 1;

                /* A piece ends on a code point boundary: the halves of a
                 * split UTF-8 sequence draw as two fallback glyphs of the
                 * wrong width. */
                while (back > 0 &&
                       ((unsigned char)text[at + back] & 0xC0u) == 0x80u) {
                    back--;
                }
                take = (back > 0) ? back : (int)sizeof run - 1;
            }
            for (k = 0; k < take && text[at + k] != '\0'; k++) {
                run[k] = text[at + k];
            }
            run[k] = '\0';
            if (k == 0) {
                break;
            }
            tiku_text(s, f, x, y, run, tiku_ink(ink));
            x += tiku_text_width(f, run);
            at += k;
            len -= k;
        }
    }
    return x - start;
}

tiku_rect_t
tiku_ui_group(tiku_surface_t *s, tiku_rect_t r, const char *label)
{
    const tiku_font_t *f = tiku_font_plain();
    tiku_rect_t in = r;
    int top, gap = 0, lx;

    if (s == NULL) {
        return in;
    }
    /* The top line sits at the label's mid-height, so the label
     * straddles it. */
    top = r.y + f->height / 2;
    lx = r.x + 10;
    if (label != NULL && label[0] != '\0') {
        gap = tiku_text_width(f, label) + 8;
    }
    /*
     * Each side is a dark line with a light line beside it, drawn one
     * side at a time because the top has a gap for the label.
     */
    if (gap > 0) {
        tiku_hline(s, r.x, top, lx - r.x - 4, DARK());
        tiku_hline(s, r.x, top + 1, lx - r.x - 4, WHITE());
        tiku_hline(s, lx + gap - 4, top, r.x + r.w - (lx + gap - 4),
                        DARK());
        tiku_hline(s, lx + gap - 4, top + 1,
                        r.x + r.w - (lx + gap - 4), WHITE());
    } else {
        tiku_hline(s, r.x, top, r.w, DARK());
        tiku_hline(s, r.x, top + 1, r.w, WHITE());
    }
    tiku_hline(s, r.x, r.y + r.h - 2, r.w, DARK());
    tiku_hline(s, r.x, r.y + r.h - 1, r.w, WHITE());
    tiku_vline(s, r.x, top, r.y + r.h - top - 1, DARK());
    tiku_vline(s, r.x + 1, top, r.y + r.h - top - 1, WHITE());
    tiku_vline(s, r.x + r.w - 2, top, r.y + r.h - top - 1, DARK());
    tiku_vline(s, r.x + r.w - 1, top, r.y + r.h - top - 1, WHITE());
    if (gap > 0) {
        tiku_text(s, f, lx, r.y + f->ascent, label, TIKU_C_TEXT);
    }
    in.x = r.x + 8;
    in.y = r.y + f->height + 4;
    in.w = r.w - 16;
    in.h = r.h - (in.y - r.y) - 8;
    if (in.w < 0) {
        in.w = 0;
    }
    if (in.h < 0) {
        in.h = 0;
    }
    return in;
}
