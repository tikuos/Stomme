/* tiku_grabview.c -- TikuGrab's window: the desk or one of its windows is
 * chosen, the delay waited out, and the desktop asked; it takes the
 * picture into Desktop/Screenshots, and the window shows it.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_grabview.h"
#include "tiku_app.h"
#include "tiku_atomic_file.h"
#include "tiku_attr.h"
#include "tiku_dialog.h"
#include "tiku_font.h"
#include "tiku_iconpaint.h"
#include "tiku_app_product.h"
#include "tiku_image.h"
#include "tiku_measure.h"
#include "tiku_msg.h"
#include "tiku_remote.h"
#include "tiku_scroll.h"
#include "tiku_text_layout_paint.h"
#include "tiku_ui.h"
#include "tiku_work.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define GB_TARGETS    64            /* windows the list holds             */
#define GB_LIST_W     230           /* its width                          */
#define GB_REFRESH_US 1500000       /* the list asked for again this often */
#define GB_CHECK      8             /* the checkerboard behind a picture  */
#define GB_TIMEOUT_US 30000000     /* how long an answer is waited for   */

enum { GB_GRAB = 1, GB_DELAY, GB_OPEN, GB_SHOW, GB_SAVE, GB_CLOSE,
       GB_DELAY_NONE, GB_DELAY_3, GB_DELAY_5, GB_DELAY_10, GB_LIST, GB_CANCEL };

/** @brief A grabbed picture, shared with preview workers by count; its
 *         pixels do not change while a worker holds it, and the count
 *         changes only on the loop. */
typedef struct { int refs; tiku_image_t image; } gb_picture_t;

/** @brief Something to grab: the desk (id 0), or a window by its id. */
typedef struct {
    char title[128];
    int64_t id;
} gb_target_t;

/** @brief Where the window's parts go. */
typedef struct {
    tiku_rect_t list, view, status, bar;
    int rh;
} gb_layout_t;

/** @brief The window's state; only the loop touches it. */
struct tiku_grabview {
    tiku_workspace_t *workspace;
    tiku_window_t *window;
    int *dirty;
    tiku_grabview_host_t host;
    gb_target_t target[GB_TARGETS + 1];
    int ntarget, sel, top, hover_tool;
    int refs, alive, generation, saving;
    tiku_scroll_t scroll;
    int dragging;
    int delay, waiting;             /* seconds to wait; an answer owed    */
    int tray_left;                  /* the seconds the tray shows, or 0   */
    /* the clock; when the delayed grab is taken, or 0; when the window
     * list was last asked for */
    int64_t now, due, asked;
    /* the number of the last grab asked, which its answer carries; when
     * that answer is overdue */
    int64_t request, deadline;
    /* the target chosen when Grab was pressed */
    gb_target_t pending;
    char path[TIKU_PATH_MAX];       /* the last picture's file, or ""     */
    char save_source[TIKU_PATH_MAX]; /* the picture Save As... copies    */
    long long size;
    gb_picture_t *picture;
    uint32_t *view;                 /* it, fitted to the preview          */
    int view_w, view_h, fit_w, fit_h;
    char status[320];
    tiku_dialog_t dialog;
};

/** @brief A worker job: the picture fitted to the preview, or a copy
 *         saved for Save As.... */
typedef struct {
    tiku_grabview_t *owner;
    int generation, save, ok;
    char path[TIKU_PATH_MAX], to[TIKU_PATH_MAX];
    gb_picture_t *picture;
    uint32_t *view;
    int view_w, view_h, fit_w, fit_h;
} gb_job_t;

static void gb_menus(tiku_grabview_t *c);
static void gb_action(tiku_grabview_t *c, int id);
static void gb_preview(tiku_grabview_t *c, int w, int h);
static gb_layout_t gb_layout(tiku_grabview_t *c, tiku_rect_t r);

/** @brief Ask for a redraw. */
static void gb_changed(tiku_grabview_t *c)
{
    if (c->dirty) *c->dirty = 1;
}

/** @brief Put @p text in the status line. */
static void gb_say(tiku_grabview_t *c, const char *text)
{
    snprintf(c->status, sizeof c->status, "%s", text);
    gb_changed(c);
}

/** @brief Whether @p x, @p y is inside @p r. */
static int gb_in(tiku_rect_t r, int x, int y)
{
    return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}

/** @brief The last part of @p path. */
static const char *gb_leaf(const char *path)
{
    const char *slash = strrchr(path, '/');

    return slash ? slash + 1 : path;
}

/* ------------------------------------------------------------ asking  */

/** @brief A request to the desktop: the list (@p what TIKU_GRAB_LIST), or
 *         a picture of the desk or of window @p id. */
static void gb_ask(tiku_grabview_t *c, int what, int64_t id)
{
    tiku_msg_t *m = tiku_msg_new(TIKU_MSG_GRAB);
    int sent;

    if (!m) {
        if (what != TIKU_GRAB_LIST) {
            c->waiting = 0;
            gb_say(c, "Not enough memory to request a screenshot.");
        }
        return;
    }
    (void)tiku_msg_add_int32(m, "what", what);
    if (what == TIKU_GRAB_WINDOW) (void)tiku_msg_add_int64(m, "id", id);
    if (what != TIKU_GRAB_LIST)
        (void)tiku_msg_add_int64(m, "request", c->request);
    sent = c->host.send && c->host.send(c->host.ctx, m);
    tiku_msg_free(m);
    if (what == TIKU_GRAB_LIST) {
        c->asked = c->now;
    } else if (!sent) {
        c->waiting = 0;
        gb_say(c, "The desktop could not be asked for a screenshot.");
    }
}

/** @brief The countdown beside the Deskbar's clock: the timer and the
 *         seconds @p left, or taken away at 0; sent only when it changes.
 *         A click on it comes back as TIKU_MSG_TRAYHIT, and cancels. */
static void gb_tray(tiku_grabview_t *c, int left)
{
    char text[8], tip[48];
    tiku_msg_t *m;

    if (left == c->tray_left) return;
    c->tray_left = left;
    snprintf(text, sizeof text, "%d", left);
    snprintf(tip, sizeof tip, "Grabbing in %d: click to cancel", left);
    m = tiku_app_tray_message("delay", left ? "grab_timer" : "",
                              left ? text : "", left ? tip : "");
    if (m && c->host.send) (void)c->host.send(c->host.ctx, m);
    tiku_msg_free(m);
}

/** @brief The grab itself, of the target chosen when Grab was pressed. */
static void gb_take(tiku_grabview_t *c)
{
    const gb_target_t *t = &c->pending;

    c->due = 0;
    gb_tray(c, 0);
    c->waiting = 1;
    c->request++;
    c->deadline = c->now + GB_TIMEOUT_US;
    snprintf(c->status, sizeof c->status, "Grabbing %s...", t->title);
    gb_changed(c);
    gb_ask(c, t->id ? TIKU_GRAB_WINDOW : TIKU_GRAB_DESK, t->id);
    gb_menus(c);
}

/** @brief The list from the desktop's answer, the chosen one kept chosen
 *         when it is still there. */
static void gb_targets(tiku_grabview_t *c, const tiku_msg_t *m)
{
    int64_t chosen = c->target[c->sel].id, id;
    const char *title;

    c->ntarget = 1;
    c->sel = 0;
    for (int i = 0; c->ntarget <= GB_TARGETS &&
         (title = tiku_msg_find_string(m, "title", i)) != NULL; i++) {
        gb_target_t *t = &c->target[c->ntarget];

        if (tiku_msg_find_int64(m, "id", i, &id) != 1) continue;
        snprintf(t->title, sizeof t->title, "%s", *title ? title : "Untitled");
        t->id = id;
        if (id == chosen) c->sel = c->ntarget;
        c->ntarget++;
    }
    gb_changed(c);
}

/* ----------------------------------------------------------- picture  */

/** @brief The mean of the picture's pixels in the box from (@p x0, @p y0)
 *         to (@p x1, @p y1), laid over @p back where it lets it through.
 *  @return that pixel, opaque. */
static uint32_t gb_mean(const tiku_image_t *im, int x0, int y0, int x1,
                        int y1, uint32_t back)
{
    uint64_t sum[4] = { 0, 0, 0, 0 }, n = 0;
    uint32_t out = 0xff000000u;

    for (int y = y0; y < y1 && y < im->h; y++)
        for (int x = x0; x < x1 && x < im->w; x++, n++) {
            uint32_t p = im->px[(size_t)y * im->w + x];

            sum[3] += p >> 24;
            for (int ch = 0; ch < 3; ch++)
                sum[ch] += ((p >> (16 - 8 * ch)) & 255u) * (p >> 24);
        }
    for (int ch = 0; n && ch < 3; ch++) {
        uint64_t a = sum[3] / n, b = (back >> (16 - 8 * ch)) & 255u;
        uint64_t v = (sum[ch] / n + b * (255 - a)) / 255;

        out |= (uint32_t)(v > 255 ? 255 : v) << (16 - 8 * ch);
    }
    return out;
}

/** @brief The last picture fitted into @p w by @p h: shrunk, never grown,
 *         each pixel the mean of those it covers, over a checkerboard
 *         where the picture lets it through. */
static void gb_fit(gb_job_t *c, int w, int h)
{
    const tiku_image_t *im = &c->picture->image;
    double k = 1.0;

    free(c->view);
    c->view = NULL;
    c->fit_w = w;
    c->fit_h = h;
    if (!im->px || w < 8 || h < 8) return;
    if (im->w > w) k = (double)w / im->w;
    if (im->h * k > h) k = (double)h / im->h;
    c->view_w = (int)(im->w * k) > 0 ? (int)(im->w * k) : 1;
    c->view_h = (int)(im->h * k) > 0 ? (int)(im->h * k) : 1;
    if (!(c->view = malloc((size_t)c->view_w * c->view_h * sizeof *c->view)))
        return;
    for (int y = 0; y < c->view_h; y++) {
        if (tiku_work_cancelled(c)) return;
        int y0 = (int)(y / k), y1 = (int)((y + 1) / k);

        for (int x = 0; x < c->view_w; x++) {
            int x0 = (int)(x / k), x1 = (int)((x + 1) / k);

            c->view[(size_t)y * c->view_w + x] = gb_mean(im, x0, y0,
                x1 > x0 ? x1 : x0 + 1, y1 > y0 ? y1 : y0 + 1,
                ((x / GB_CHECK + y / GB_CHECK) & 1) ? 0xe4e4e4u : 0xffffffu);
        }
    }
}

/** @brief A width, for fitting a size into the status line. */
static int gb_measure(const char *text, void *ctx)
{
    (void)ctx;
    return tiku_measure_width(TIKU_MEASURE_PLAIN, text);
}

/** @brief The picture the desktop wrote, read back to be shown. */
static void gb_landed(tiku_grabview_t *c, const tiku_msg_t *m)
{
    const char *path = tiku_msg_find_string(m, "path", 0);
    int64_t size = 0;
    char said[96];

    if (!path || !*path || strlen(path) >= sizeof c->path) {
        gb_say(c, "The desktop replied without a usable screenshot path.");
        return;
    }
    (void)tiku_msg_find_int64(m, "size", 0, &size);
    snprintf(c->path, sizeof c->path, "%s", path);
    c->size = (long long)size;
    gb_layout_t l = gb_layout(c, tiku_window_content(c->window));
    gb_picture_t *old = c->picture;
    c->picture = NULL;
    if (old && --old->refs == 0) { tiku_image_free(&old->image); free(old); }
    free(c->view);
    c->view = NULL;
    gb_preview(c, l.view.w - 16, l.view.h - 16);
    (void)tiku_attr_size_fit(size, 90, gb_measure, NULL, said, sizeof said);
    snprintf(c->status, sizeof c->status, "Saved as %.200s, %s.",
             gb_leaf(c->path), said);
    gb_ask(c, TIKU_GRAB_LIST, 0);           /* titles may have moved on */
    gb_menus(c);
    gb_changed(c);
}

/** @brief The atomic copy's producer: @p ctx's bytes into @p out. */
static int gb_copy_bytes(FILE *out, void *ctx)
{
    FILE *in = ctx;
    char buf[65536];
    size_t n;
    int bad = 0;

    while (!bad && (n = fread(buf, 1, sizeof buf, in)) > 0)
        bad = fwrite(buf, 1, n, out) != n;
    if (ferror(in)) bad = 1;
    return bad ? -1 : 0;
}

/** @brief The picture's file copied to @p to.  @return 0, -1 when it
 *         could not be. */
static int gb_copy(const char *from, const char *to)
{
    struct stat a, b;
    FILE *in = fopen(from, "rb");
    int result;
    if (!in) return -1;
    if (fstat(fileno(in), &a) != 0 || !S_ISREG(a.st_mode)) {
        fclose(in); return -1;
    }
    if (stat(to, &b) == 0 && a.st_dev == b.st_dev && a.st_ino == b.st_ino) {
        fclose(in); return 0;       /* same path, hard link or symbolic alias */
    }
    result = tiku_atomic_file(to, gb_copy_bytes, in);
    fclose(in);
    return result;
}

/** @brief Drop a hold on the window's state; the last frees it and the
 *         picture. */
static void gb_unref(tiku_grabview_t *c)
{
    if (--c->refs) return;
    if (c->picture && --c->picture->refs == 0) {
        tiku_image_free(&c->picture->image);
        free(c->picture);
    }
    free(c->view);
    free(c);
}

/** @brief On a worker: copy the picture's file for Save As..., or load
 *         the picture and fit it to the preview. */
static void gb_run(void *ctx)
{
    gb_job_t *j = ctx;
    if (j->save) { j->ok = gb_copy(j->path, j->to) == 0; return; }
    if (!j->picture) {
        j->picture = calloc(1, sizeof *j->picture);
        if (!j->picture) return;
        j->picture->refs = 1;
        if (tiku_image_load(j->path, &j->picture->image) != TIKU_IMG_OK) return;
    }
    gb_fit(j, j->fit_w, j->fit_h);
    j->ok = j->picture->image.px != NULL &&
            (j->view || j->fit_w < 8 || j->fit_h < 8);
}

/** @brief On the loop: say where the copy went, or show the fitted
 *         preview unless a newer one was asked for. */
static void gb_done(void *ctx)
{
    gb_job_t *j = ctx;
    tiku_grabview_t *c = j->owner;
    if (!c->alive || !c->window) return;
    if (j->save) {
        snprintf(c->status, sizeof c->status, j->ok ? "A copy is in %s."
                 : "Could not write %s. The destination is unchanged.",
                 gb_leaf(j->to));
    } else if (j->generation == c->generation) {
        if (j->ok) {
            if (c->picture && --c->picture->refs == 0) {
                tiku_image_free(&c->picture->image); free(c->picture);
            }
            c->picture = j->picture;
            j->picture = NULL;
            free(c->view);
            c->view = j->view; j->view = NULL;
            c->view_w = j->view_w; c->view_h = j->view_h;
        } else gb_say(c, "The screenshot was saved, but its preview "
                         "could not be loaded.");
    }
    gb_changed(c);
}

/** @brief After done, or instead of it: free the job and drop its hold. */
static void gb_free(void *ctx)
{
    gb_job_t *j = ctx;
    if (j->save) j->owner->saving = 0;
    if (j->owner->alive) { gb_menus(j->owner); gb_changed(j->owner); }
    if (j->picture && --j->picture->refs == 0) {
        tiku_image_free(&j->picture->image); free(j->picture);
    }
    free(j->view);
    gb_unref(j->owner);
    free(j);
}

/** @brief Post @p j; a new preview cancels the one queued before it.
 *  @return 1 when queued. */
static int gb_post(tiku_grabview_t *c, gb_job_t *j)
{
    tiku_work_job_t work = {.run = gb_run, .done = gb_done, .free = gb_free,
                            .ctx = j};
    j->owner = c; c->refs++;
    snprintf(work.key, sizeof work.key, "grab-%s:%p",
             j->save ? "save" : "preview", (void *)c);
    if (!j->save) (void)tiku_work_cancel(work.key);
    if (tiku_work_post(&work) == 0) return 1;
    gb_free(j);
    gb_say(c, "The workers are busy. Try again in a moment.");
    return 0;
}

/** @brief Ask a worker for the picture fitted into @p w by @p h, loading
 *         it the first time. */
static void gb_preview(tiku_grabview_t *c, int w, int h)
{
    gb_job_t *j = calloc(1, sizeof *j);
    if (!j) { gb_say(c, "Not enough memory for the preview."); return; }
    j->generation = ++c->generation;
    j->picture = c->picture;
    if (j->picture) j->picture->refs++;
    snprintf(j->path, sizeof j->path, "%s", c->path);
    j->fit_w = c->fit_w = w;
    j->fit_h = c->fit_h = h;
    (void)gb_post(c, j);
}

/* -------------------------------------------------------------- view  */

/** @brief A toolbar button's icon, from the desk's set. */
static const char *gb_tool_icon(int id)
{
    switch (id) {
    case GB_GRAB: return "screen_grab";
    case GB_DELAY: return "grab_timer";
    case GB_OPEN: return "image_viewer";
    case GB_SHOW: return "folder";
    case GB_CANCEL: return "force_quit";
    }
    return "file";
}

/** @brief What a toolbar button does, as its tip says it. */
static const char *gb_tool_tip(const tiku_grabview_t *c, int id)
{
    static char delay[48];

    switch (id) {
    case GB_GRAB: return "Grab (Ctrl+G)";
    case GB_DELAY:
        snprintf(delay, sizeof delay, c->delay ? "Delay: %d seconds"
                 : "No delay", c->delay);
        return delay;
    case GB_OPEN: return TIKU_PRODUCT_GRAB_OPEN_IMAGE_HINT;
    case GB_SHOW: return "Show in Tracker";
    case GB_CANCEL: return "Cancel capture (Escape)";
    }
    return "";
}

/** @brief Whether command @p id can do anything now. */
static int gb_enabled(const tiku_grabview_t *c, int id)
{
    switch (id) {
    case GB_GRAB: return !c->waiting && !c->due;
    case GB_CANCEL: return c->waiting || c->due;
    case GB_OPEN:
    case GB_SHOW: return c->path[0] != '\0';
    case GB_SAVE: return c->path[0] && !c->saving;
    }
    return 1;
}

/** @brief A command's label, for its menu item and its button's
 *         narration. */
static const char *gb_label(int id)
{
    switch (id) {
    case GB_GRAB: return "Grab";
    case GB_DELAY: return "Delay";
    case GB_OPEN: return TIKU_PRODUCT_GRAB_OPEN_IMAGE_LABEL;
    case GB_SHOW: return "Show in Tracker";
    case GB_SAVE: return "Save As...";
    case GB_CLOSE: return "Close";
    case GB_CANCEL: return "Cancel Capture";
    case GB_DELAY_NONE: return "No Delay";
    case GB_DELAY_3: return "3 Seconds";
    case GB_DELAY_5: return "5 Seconds";
    case GB_DELAY_10: return "10 Seconds";
    }
    return "";
}

/** @brief Where everything goes in content @p r; the toolbar's buttons
 *         into the dialog. */
static gb_layout_t gb_layout(tiku_grabview_t *c, tiku_rect_t r)
{
    static const int ids[] = { GB_GRAB, GB_DELAY, GB_CANCEL, GB_OPEN, GB_SHOW };
    int bh = tiku_ui_button_h(), bw = bh > 30 ? bh : 30, x = r.x + 6;
    int fh = tiku_font_plain()->height + 6, y = r.y + 4, below;
    gb_layout_t l;

    tiku_dialog_begin(&c->dialog, -1, -1);
    for (size_t i = 0; i < sizeof ids / sizeof ids[0]; i++) {
        int w = ids[i] == GB_DELAY ? bw + 26 : bw;

        if (ids[i] == GB_OPEN) x += 10;
        if (x + w > r.x + r.w - 6 && x > r.x + 6) {
            x = r.x + 6; y += bw + 4;
        }
        (void)tiku_dialog_add(&c->dialog, ids[i],
                              (tiku_rect_t){x, y, w, bw},
                              TIKU_DIALOG_BUTTON, gb_enabled(c, ids[i]));
        x += w + 2;
    }
    below = y + bw + 4;
    l.rh = tiku_measure_row_h() + 8;
    l.status = (tiku_rect_t){r.x + 8, r.y + r.h - (bh > fh ? bh : fh) - 3,
                             r.w - 16, bh > fh ? bh : fh};
    l.list = (tiku_rect_t){r.x + 6, below, GB_LIST_W,
                           l.status.y - 4 - below};
    l.view = (tiku_rect_t){l.list.x + l.list.w + 6, below,
                           r.x + r.w - 6 - (l.list.x + l.list.w + 6),
                           l.list.h};
    if (r.w < 480) {               /* stacked panes in a narrow window */
        l.list.w = r.w > 12 ? r.w - 12 : 0;
        l.list.h = l.list.h > 0 ? l.list.h / 2 : 0;
        l.view = (tiku_rect_t){l.list.x, below + l.list.h + 6, l.list.w,
                               l.status.y - below - l.list.h - 10};
    }
    if (l.list.h < 0) l.list.h = 0;
    if (l.view.h < 0) l.view.h = 0;
    if (l.view.w < 0) l.view.w = 0;
    if (l.status.w < 0) l.status.w = 0;
    int visible = (l.list.h - 4) / l.rh;
    if (visible < 1) visible = 1;
    if (c->top > c->ntarget - visible) c->top = c->ntarget - visible;
    if (c->top < 0) c->top = 0;
    l.bar = (tiku_rect_t){l.list.x + l.list.w - 15, l.list.y, 15, l.list.h};
    if (l.list.w < 15) l.bar.w = 0;
    tiku_scroll_set_range(&c->scroll, c->ntarget, visible);
    tiku_scroll_to(&c->scroll, c->top);
    (void)tiku_dialog_add(&c->dialog, GB_LIST, l.list, TIKU_DIALOG_INPUT, 1);
    tiku_dialog_end(&c->dialog);
    if (c->dialog.focus == TIKU_DIALOG_NONE)
        (void)tiku_dialog_focus(&c->dialog, GB_LIST);
    return l;
}

/** @brief A toolbar button: its icon, raised under the pointer, sunk
 *         while pressed; the delay's seconds beside the timer. */
static void gb_button(tiku_grabview_t *c, tiku_surface_t *s,
                      const tiku_dialog_control_t *d)
{
    unsigned flags = d->enabled ? 0 : TIKU_S_DISABLED;
    int held = tiku_dialog_pressed(&c->dialog, d->id), size = d->rect.h - 6;
    char secs[8];

    if (tiku_dialog_focused(&c->dialog, d->id)) flags |= TIKU_S_FOCUS;
    if (held) flags |= TIKU_S_PRESSED;
    if (c->hover_tool == d->id) flags |= TIKU_S_HOT;
    tiku_ui_button(s, d->rect, gb_label(d->id), flags | TIKU_S_ICON);
    if (size > 24) size = 24;
    (void)tiku_icon_paint_dim(s, gb_tool_icon(d->id),
                              d->rect.x + (d->rect.h - size) / 2 + held,
                              d->rect.y + (d->rect.h - size) / 2 + held, size,
                              d->enabled ? 0.0f : 0.72f, TIKU_C_PANEL);
    if (d->id != GB_DELAY) return;
    snprintf(secs, sizeof secs, c->delay ? "%ds" : "off", c->delay);
    tiku_text_line_draw(s, (tiku_rect_t){d->rect.x + d->rect.h - 2, d->rect.y,
                        d->rect.w - d->rect.h, d->rect.h}, TIKU_MEASURE_PLAIN,
                        secs, TIKU_C_TEXT);
}

/** @brief The list: the desk, then each window, the chosen one lit. */
static void gb_list_draw(tiku_grabview_t *c, tiku_surface_t *s,
                         const gb_layout_t *l)
{
    tiku_rect_t clip = s->clip;

    tiku_ui_sunken(s, l->list, TIKU_C_DOC);
    tiku_clip_set(s, (tiku_rect_t){l->list.x + 2, l->list.y + 2,
                  l->list.w > 19 ? l->list.w - 19 : 0,
                  l->list.h > 4 ? l->list.h - 4 : 0});
    for (int i = c->top;
         i < c->ntarget && (i - c->top) * l->rh < l->list.h; i++) {
        tiku_rect_t r = { l->list.x + 2, l->list.y + 2 + (i - c->top) * l->rh,
                          l->list.w - 19, l->rh };
        int sel = i == c->sel;

        if (sel) tiku_fill(s, r, TIKU_C_SELECT);
        (void)tiku_icon_paint(s, i ? "grab_window" : "grab_desk", r.x + 6,
                              r.y + (r.h - 20) / 2, 20);
        tiku_text_line_draw(s, (tiku_rect_t){r.x + 32, r.y, r.w - 38, r.h},
                            TIKU_MEASURE_PLAIN, c->target[i].title,
                            sel ? TIKU_C_SELTEXT : TIKU_C_TEXT);
    }
    tiku_clip_set(s, clip);
    int max = tiku_scroll_max(&c->scroll);
    tiku_ui_scrollbar_held(s, l->bar, max ? (float)c->top / max : 0.0f,
                          max ? (float)c->scroll.visible / c->ntarget : 1.0f,
                          0, c->scroll.arrow_dir);
}

/** @brief The preview: the last picture, fitted and centred; blank until
 *         there is one. */
static void gb_view_draw(tiku_grabview_t *c, tiku_surface_t *s,
                         const gb_layout_t *l)
{
    tiku_rect_t in = { l->view.x + 8, l->view.y + 8, l->view.w - 16,
                       l->view.h - 16 };

    tiku_ui_sunken(s, l->view, TIKU_C_DOC);
    if (c->path[0] && (c->fit_w != in.w || c->fit_h != in.h))
        gb_preview(c, in.w, in.h);
    tiku_rect_t clip = s->clip;
    if (in.w < 1 || in.h < 1) return;
    tiku_clip_set(s, in);
    if (c->view)
        tiku_picture(s, in.x + (in.w - c->view_w) / 2,
                     in.y + (in.h - c->view_h) / 2, c->view_w, c->view_h,
                     c->view);
    tiku_clip_set(s, clip);
}

/** @brief The tip of the toolbar button under the pointer, below it. */
static void gb_tip_draw(tiku_grabview_t *c, tiku_surface_t *s, tiku_rect_t r)
{
    for (int i = 0; i < c->dialog.count && c->hover_tool; i++) {
        const tiku_dialog_control_t *d = &c->dialog.control[i];
        const char *text = gb_tool_tip(c, d->id);
        tiku_rect_t tip;

        if (d->id != c->hover_tool || d->kind != TIKU_DIALOG_BUTTON) continue;
        tip = tiku_ui_tip_size(text);
        tip.x = d->rect.x;
        tip.y = d->rect.y + d->rect.h + 4;
        if (tip.x + tip.w > r.x + r.w - 4) tip.x = r.x + r.w - 4 - tip.w;
        tiku_ui_tip(s, tip, text);
    }
}

/** @brief The whole window, laid out afresh each time. */
static void gb_draw(tiku_window_t *w, tiku_surface_t *s, tiku_rect_t r,
                    void *ctx)
{
    tiku_grabview_t *c = ctx;
    tiku_rect_t clip = s->clip;
    gb_layout_t l;

    (void)w;
    tiku_clip_set(s, r);
    tiku_ui_panel(s, r);
    l = gb_layout(c, r);
    for (int i = 0; i < c->dialog.count; i++)
        if (c->dialog.control[i].kind == TIKU_DIALOG_BUTTON)
            gb_button(c, s, &c->dialog.control[i]);
    gb_list_draw(c, s, &l);
    gb_view_draw(c, s, &l);
    tiku_text_line_draw(s, l.status, TIKU_MEASURE_PLAIN, c->status,
                        tiku_ink(TIKU_INK_FAINT));
    gb_tip_draw(c, s, r);           /* last: nothing paints over it */
    s->clip = clip;
}

/* ----------------------------------------------------------- actions  */

/** @brief Carry out command @p id. */
static void gb_action(tiku_grabview_t *c, int id)
{
    char dir[TIKU_PATH_MAX], *slash;
    static const int delays[] = { 0, 3, 5, 10 };

    if (!gb_enabled(c, id)) return;
    switch (id) {
    case GB_GRAB:
        c->pending = c->target[c->sel];
        if (!c->delay) {
            gb_take(c);
            break;
        }
        c->due = c->now + (int64_t)c->delay * 1000000;
        snprintf(c->status, sizeof c->status, "Grabbing in %d... (%s)",
                 c->delay, c->pending.title);
        gb_tray(c, c->delay);
        break;
    case GB_CANCEL:
        c->due = 0;
        c->waiting = 0;
        gb_tray(c, 0);
        gb_say(c, "Capture cancelled. An already submitted screenshot "
                  "may still be saved by the desktop.");
        break;
    case GB_DELAY:
        c->delay = c->delay == 0 ? 3 : c->delay == 3 ? 5 : c->delay == 5 ? 10
                                                                          : 0;
        break;
    case GB_DELAY_NONE: case GB_DELAY_3: case GB_DELAY_5: case GB_DELAY_10:
        c->delay = delays[id - GB_DELAY_NONE];
        break;
    case GB_OPEN:
        if (c->host.open_path) (void)c->host.open_path(c->host.ctx, c->path);
        break;
    case GB_SHOW:
        snprintf(dir, sizeof dir, "%s", c->path);
        if ((slash = strrchr(dir, '/'))) *slash = '\0';
        if (c->host.open_path) (void)c->host.open_path(c->host.ctx, dir);
        break;
    case GB_SAVE:
        snprintf(c->save_source, sizeof c->save_source, "%s", c->path);
        if (c->host.ask_save)
            c->host.ask_save(c->host.ctx, getenv("HOME"), gb_leaf(c->path));
        break;
    case GB_CLOSE: tiku_grabview_close(c); return;
    }
    gb_menus(c);
    gb_changed(c);
}

/** @brief Keys: the command keys, and the list's Up, Down and Return. */
static int gb_key(tiku_grabview_t *c, const tiku_event_t *ev)
{
    unsigned key = ev->key;

    if (key == TIKU_KEY_ESCAPE) {
        if (!gb_enabled(c, GB_CANCEL)) return 0;
        gb_action(c, GB_CANCEL);
        return 1;
    }

    if (ev->modifiers & TIKU_MOD_CMD) {
        if (key == 'g') gb_action(c, GB_GRAB);
        else if (key == 's') gb_action(c, GB_SAVE);
        else if (key == 'o') gb_action(c, GB_OPEN);
        else if (key == 'w' || key == 'q') gb_action(c, GB_CLOSE);
        else return 0;
        return 1;
    }
    if (key == TIKU_KEY_UP && c->sel > 0) c->sel--;
    else if (key == TIKU_KEY_DOWN && c->sel < c->ntarget - 1) c->sel++;
    else if (key == TIKU_KEY_RETURN) gb_action(c, GB_GRAB);
    else if (key != TIKU_KEY_UP && key != TIKU_KEY_DOWN) return 0;
    gb_layout_t l = gb_layout(c, tiku_window_content(c->window));
    int visible = (l.list.h - 4) / l.rh;
    if (visible < 1) visible = 1;
    if (c->sel < c->top) c->top = c->sel;
    if (c->sel >= c->top + visible) c->top = c->sel - visible + 1;
    gb_changed(c);
    return 1;
}

/** @brief The toolbar button under the pointer, lit and tipped. */
static void gb_hover(tiku_grabview_t *c, const tiku_event_t *ev)
{
    int tool = 0;

    if (ev->type == TIKU_EVENT_POINTER_MOVE &&
        c->dialog.capture == TIKU_DIALOG_NONE)
        for (int i = 0; i < c->dialog.count; i++)
            if (c->dialog.control[i].kind == TIKU_DIALOG_BUTTON &&
                gb_in(c->dialog.control[i].rect, ev->x, ev->y))
                tool = c->dialog.control[i].id;
    if (tool != c->hover_tool) {
        c->hover_tool = tool;
        gb_changed(c);
    }
}

/** @brief Every window event: activation asks for the list again; then
 *         keys, the list's scrollbar, wheel and presses, and the toolbar's
 *         buttons. */
static int gb_event(tiku_window_t *w, const tiku_event_t *ev, void *ctx)
{
    tiku_grabview_t *c = ctx;
    gb_layout_t l = gb_layout(c, tiku_window_content(w));
    tiku_dialog_result_t r;

    gb_hover(c, ev);
    if (ev->type == TIKU_EVENT_ACTIVATED) gb_ask(c, TIKU_GRAB_LIST, 0);
    if (ev->type == TIKU_EVENT_KEY_DOWN && gb_key(c, ev)) return 1;
    if (ev->type == TIKU_EVENT_DEACTIVATED) {
        c->dragging = 0; tiku_scroll_release(&c->scroll);
    }
    if (ev->type == TIKU_EVENT_POINTER_DOWN && ev->button == 1 &&
        gb_in(l.bar, ev->x, ev->y)) {
        int at = ev->y - l.bar.y;
        if (at < l.bar.w || at >= l.bar.h - l.bar.w) {
            int dir = at < l.bar.w ? -1 : 1;
            tiku_scroll_arrow_press(&c->scroll, dir);
            tiku_scroll_to(&c->scroll, c->top + dir);
        } else {
            (void)tiku_scroll_press(&c->scroll, l.bar.h - 2 * l.bar.w,
                                    at - l.bar.w);
        }
        c->dragging = 1;
    } else if (ev->type == TIKU_EVENT_POINTER_MOVE && c->dragging) {
        if (!c->scroll.arrow_dir)
            (void)tiku_scroll_drag(&c->scroll, l.bar.h - 2 * l.bar.w,
                                   ev->y - l.bar.y - l.bar.w);
    } else if (ev->type == TIKU_EVENT_POINTER_UP && c->dragging) {
        c->dragging = 0;
        tiku_scroll_release(&c->scroll);
    } else goto list_input;
    c->top = c->scroll.value;
    gb_changed(c);
    return 1;
list_input:
    if (ev->type == TIKU_EVENT_WHEEL && gb_in(l.list, ev->x, ev->y)) {
        c->top += ev->dy > 0 ? -3 : ev->dy < 0 ? 3 : 0;
        (void)gb_layout(c, tiku_window_content(w));
        gb_changed(c);
        return 1;
    }
    if (ev->type == TIKU_EVENT_POINTER_DOWN && ev->button == 1 &&
        gb_in(l.list, ev->x, ev->y) && c->dialog.capture == TIKU_DIALOG_NONE) {
        int row = c->top + (ev->y - l.list.y - 2) / l.rh;

        if (row >= 0 && row < c->ntarget) c->sel = row;
        (void)tiku_dialog_focus(&c->dialog, GB_LIST);
        gb_changed(c);
        return 1;
    }
    r = tiku_dialog_event(&c->dialog, ev);
    if (r.activated >= 0) gb_action(c, r.activated);
    if (r.taken) gb_changed(c);
    return r.taken;
}

/** @brief The window went away: forget it, and any grab due or awaited. */
static void gb_window_gone(tiku_window_t *w, void *ctx)
{
    tiku_grabview_t *c = ctx;

    (void)w;
    c->window = NULL;
    c->due = 0;
    c->waiting = 0;
    gb_changed(c);
}

/** @brief A menu pick, carried out as its command. */
static void gb_menu_pick(tiku_window_t *w, int id, void *ctx)
{
    (void)w;
    gb_action(ctx, id);
}

/** @brief Add command @p id to menu @p m, with its shortcut. */
static void gb_menu_add(tiku_grabview_t *c, tiku_menu_list_t *m, int id,
                        char sc, int marked)
{
    tiku_menu_entry_t *e;

    if (m->nitem >= TIKU_MENUSET_ITEMS) return;
    e = &m->item[m->nitem++];
    memset(e, 0, sizeof *e);
    snprintf(e->label, sizeof e->label, "%s", gb_label(id));
    e->command = id;
    e->enabled = (unsigned char)gb_enabled(c, id);
    e->sc = sc;
    e->mods = sc ? TIKU_MOD_CMD : 0;
    e->marked = (unsigned char)marked;
}

/** @brief Publish the File and Delay menus, each item enabled and marked
 *         as the window now stands. */
static void gb_menus(tiku_grabview_t *c)
{
    tiku_menuset_t m;

    if (!c->window) return;
    memset(&m, 0, sizeof m);
    m.nmenu = 2;
    snprintf(m.menu[0].title, sizeof m.menu[0].title, "File");
    snprintf(m.menu[1].title, sizeof m.menu[1].title, "Delay");
    gb_menu_add(c, &m.menu[0], GB_GRAB, 'g', 0);
    gb_menu_add(c, &m.menu[0], GB_CANCEL, 0, 0);
    gb_menu_add(c, &m.menu[0], GB_SAVE, 's', 0);
    gb_menu_add(c, &m.menu[0], GB_OPEN, 'o', 0);
    gb_menu_add(c, &m.menu[0], GB_SHOW, 0, 0);
    gb_menu_add(c, &m.menu[0], GB_CLOSE, 'w', 0);
    gb_menu_add(c, &m.menu[1], GB_DELAY_NONE, 0, c->delay == 0);
    gb_menu_add(c, &m.menu[1], GB_DELAY_3, 0, c->delay == 3);
    gb_menu_add(c, &m.menu[1], GB_DELAY_5, 0, c->delay == 5);
    gb_menu_add(c, &m.menu[1], GB_DELAY_10, 0, c->delay == 10);
    tiku_window_publish_menus(c->window, &m, gb_menu_pick, c);
}

/* -------------------------------------------------------------- API  */

tiku_grabview_t *tiku_grabview_new(tiku_workspace_t *workspace, int *dirty,
                                   const tiku_grabview_host_t *host)
{
    tiku_grabview_t *c = calloc(1, sizeof *c);

    if (!c) return NULL;
    c->refs = c->alive = 1;
    c->workspace = workspace;
    c->dirty = dirty;
    if (host) c->host = *host;
    snprintf(c->target[0].title, sizeof c->target[0].title, "Whole desk");
    c->ntarget = 1;
    c->fit_w = c->fit_h = -1;
    (void)tiku_app_product_register_icons();
    tiku_dialog_init(&c->dialog);
    tiku_scroll_init(&c->scroll, 1);
    return c;
}

void tiku_grabview_open(tiku_grabview_t *c)
{
    if (!c || !c->workspace) return;
    if (!c->window) {
        c->window = tiku_workspace_open(c->workspace, TIKU_PRODUCT_GRAB_NAME,
            (tiku_rect_t){120, 90, 720, 460}, gb_draw, c, c);
        if (!c->window) return;
        tiku_window_set_handlers(c->window, gb_event, NULL, gb_window_gone);
        tiku_window_set_size_controls(c->window, 1, 1);
        gb_say(c, "");              /* an empty window says nothing */
    }
    tiku_workspace_activate(c->workspace, c->window);
    gb_ask(c, TIKU_GRAB_LIST, 0);
    gb_menus(c);
    gb_changed(c);
}

void tiku_grabview_close(tiku_grabview_t *c)
{
    if (c && c->window) tiku_workspace_close(c->workspace, c->window);
}

void tiku_grabview_destroy(tiku_grabview_t *c)
{
    if (!c) return;
    tiku_grabview_close(c);
    c->alive = 0;
    c->dirty = NULL;
    gb_unref(c);
}

tiku_window_t *tiku_grabview_handle(tiku_grabview_t *c)
{
    return c ? c->window : NULL;
}

void tiku_grabview_message(tiku_grabview_t *c, const struct tiku_msg *m)
{
    int32_t list = 0;
    int64_t request = 0;
    const char *why;

    if (c && m && tiku_msg_what(m) == TIKU_MSG_TRAYHIT && c->due &&
        (why = tiku_msg_find_string(m, "id", 0)) && !strcmp(why, "delay")) {
        gb_action(c, GB_CANCEL);    /* the countdown's icon, clicked */
        return;
    }
    if (!c || !m || tiku_msg_what(m) != TIKU_MSG_GRABBED) return;
    if (tiku_msg_find_int32(m, "list", 0, &list) == 1 && list) {
        gb_targets(c, m);
        return;
    }
    if (!c->waiting || !c->window ||
        tiku_msg_find_int64(m, "request", 0, &request) != 1 ||
        request != c->request)
        return;
    c->waiting = 0;
    if ((why = tiku_msg_find_string(m, "error", 0)) != NULL) gb_say(c, why);
    else gb_landed(c, m);
    gb_menus(c);
}

void tiku_grabview_picked(tiku_grabview_t *c, const char *path)
{
    gb_job_t *j;
    if (!c || !path || !*path || !c->save_source[0] || c->saving) return;
    j = calloc(1, sizeof *j);
    if (!j) { gb_say(c, "Not enough memory to save a copy."); return; }
    j->save = 1;
    snprintf(j->path, sizeof j->path, "%s", c->save_source);
    snprintf(j->to, sizeof j->to, "%s", path);
    c->save_source[0] = '\0';
    c->saving = 1;
    if (gb_post(c, j)) gb_say(c, "Saving a copy...");
    gb_menus(c);
}

void tiku_grabview_tick(tiku_grabview_t *c, int64_t now_us)
{
    int left;

    if (!c) return;
    c->now = now_us;
    int step = tiku_scroll_arrow_repeat(&c->scroll);
    if (step) {
        tiku_scroll_to(&c->scroll, c->top + step);
        c->top = c->scroll.value;
        gb_changed(c);
    }
    if (c->waiting && now_us >= c->deadline) {
        c->waiting = 0;
        gb_say(c, "The desktop did not reply within 30 seconds. "
                  "You can try again; check Screenshots for a late save.");
        gb_menus(c);
    }
    if (c->due && now_us >= c->due) {
        gb_take(c);
    } else if (c->due) {
        left = (int)((c->due - now_us + 999999) / 1000000);
        snprintf(c->status, sizeof c->status, "Grabbing in %d... (%s)",
                 left, c->pending.title);
        gb_tray(c, left);
        gb_changed(c);
    } else if (!c->waiting && now_us - c->asked >= GB_REFRESH_US) {
        gb_ask(c, TIKU_GRAB_LIST, 0);
    }
}
