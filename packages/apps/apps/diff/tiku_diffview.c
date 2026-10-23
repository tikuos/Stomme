/* tiku_diffview.c -- TikuDiff's viewer: what changed between two files,
 * two folders, or what two archives hold; its commands, keys, menus and
 * the API.  Every read runs on a worker, one job at a time per window.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_diffview.h"
#include "tiku_atomic_file.h"
#include "tiku_archive.h"
#include "tiku_attr.h"
#include "tiku_clipboard.h"
#include "tiku_dialog.h"
#include "tiku_diff.h"
#include "tiku_font.h"
#include "tiku_iconpaint.h"
#include "tiku_app_product.h"
#include "tiku_measure.h"
#include "tiku_scroll.h"
#include "tiku_text_layout_paint.h"
#include "tiku_ui.h"
#include "tiku_work.h"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define DV_PATH       4096          /* a path, one under the temporary     */
                                    /* folder included                     */
#define DV_BAR        15            /* the scrollbar's width               */
#define DV_MAP        10            /* the change map beside it            */
#define DV_DOUBLE_US  450000        /* two clicks this close open a row    */
#define DV_TAB        4             /* the columns a tab stands for        */
#define DV_CLIP_MAX   (16L << 20)   /* what the desk's clipboard holds     */

enum { DV_COMPARE = 1, DV_COMPARE_DIRS, DV_OPEN_LEFT, DV_OPEN_RIGHT, DV_SAVE,
       DV_CLOSE, DV_COPY, DV_SPACE, DV_SHOW_SAME, DV_SWAP, DV_NEXT, DV_PREV,
       DV_BACK, DV_OPEN_ROW, DV_CANCEL, DV_BODY };
enum { DV_ROW_SAME, DV_ROW_DEL, DV_ROW_ADD, DV_ROW_CHANGE };
enum { DV_ENT_SAME, DV_ENT_CHANGED, DV_ENT_LEFT, DV_ENT_RIGHT };
enum { DV_EMPTY, DV_FILES, DV_FOLDERS };
/* What the question put to the person was for. */
enum { DV_ASK_NONE, DV_ASK_LEFT, DV_ASK_RIGHT, DV_ASK_NEW_LEFT,
       DV_ASK_NEW_RIGHT, DV_ASK_SAVE };

/** @brief A row of the file view: a line of each side, or of one. */
typedef struct {
    long a, b;                      /* the line on each side, -1 none    */
    int kind;                       /* DV_ROW_*                          */
} dv_row_t;

/** @brief Two files compared. */
typedef struct {
    char *text[2];                  /* each file's bytes                 */
    size_t len[2];
    int64_t size[2];
    tiku_diff_line_t *line[2];
    long nline[2];
    tiku_diff_t d;
    dv_row_t *row;
    long nrow;
    long *change, *end, nchange;    /* each change's first row, and the  */
                                    /* row after its last                */
    int binary, same, endings;      /* bytes only; alike; CRLF and LF    */
} dv_files_t;

/** @brief A path under both folders, or under one. */
typedef struct {
    char *rel;
    int status;                     /* DV_ENT_*                          */
    int64_t size[2];                /* -1 where it is not                */
} dv_entry_t;

/** @brief Two folders compared: every path under either. */
typedef struct {
    dv_entry_t *e;
    long n;
    long count[4];                  /* the entries, by status            */
} dv_folders_t;

/** @brief How far a job has got: its worker writes, the loop reads. */
typedef struct {
    atomic_llong done, total;
    atomic_int stop;                /* Stop pressed                      */
    int refs;                       /* the viewer and the job; loop only */
} dv_progress_t;

/** @brief One side of a comparison. */
typedef struct {
    char path[DV_PATH];             /* as chosen                         */
    char root[DV_PATH];             /* compared: it, or what it held     */
    char name[256];                 /* shown                             */
    int dir, archive;
} dv_side_t;

/** @brief An entry of the folder list, as a patch job takes it. */
typedef struct {
    char *rel;
    int status;
} dv_rel_t;

/** @brief A job: compare two paths, open an entry of the folder list, or
 *         write every difference as one patch. */
typedef struct {
    struct tiku_diffview *owner;
    int generation, space, landed, patch, status, sub;
    dv_progress_t *progress;
    char path[2][DV_PATH];          /* in: what was chosen               */
    char under[DV_PATH];            /* in: where archives go             */
    char rel[DV_PATH];              /* in: an entry of the folder list   */
    char save[DV_PATH];             /* in: where a patch goes, "" clip   */
    dv_side_t side[2];              /* in for entries and patches, out   */
    dv_rel_t *rels;                 /* in: the entries a patch covers    */
    long nrel;
    dv_files_t *files;              /* out                               */
    dv_folders_t *folders;
    char *patch_text;
    size_t patch_len, patch_cap;
    char said[320];
} dv_job_t;

/** @brief Where the window's parts go, from one description. */
typedef struct {
    tiku_rect_t names, body, list, left, right, map, vbar, status, gauge;
    tiku_rect_t cancel;
    int gutter, rh, cell, rows;
} dv_layout_t;

struct tiku_diffview {
    tiku_workspace_t *workspace;
    tiku_window_t *window;
    int *dirty, refs, generation, busy, mode, from_folder, asking, folder_ask;
    tiku_diffview_host_t host;
    char status[320], hint[320], doing[64];
    char pending[DV_PATH];          /* the left, while the right is asked */
    char rel[DV_PATH];              /* the folder entry shown, or ""     */
    int rel_status;
    dv_side_t side[2];
    dv_files_t *files;
    dv_folders_t *folders;
    long *shown, nshown;            /* the folder list's rows            */
    long sel, cur, last_row;        /* its chosen row; the change shown  */
    int64_t last_click;
    int hoff, space, show_same;     /* columns scrolled; the options     */
    tiku_scroll_t vbar;
    int vdrag, hover_tool;
    tiku_dialog_t dialog;
    dv_progress_t *progress;
    char temp[DV_PATH];             /* archives extracted under here     */
    int temps, live;                /* folders made there; the one in use */
};

static void dv_menus(tiku_diffview_t *c);
static void dv_done(void *ctx);
static const char *dv_label(tiku_diffview_t *c, int id);
static int dv_enabled(tiku_diffview_t *c, int id);
static void dv_action(tiku_diffview_t *c, int id);
static void dv_scroll(tiku_diffview_t *c, long top);
static void dv_hscroll(tiku_diffview_t *c, int by);
static void dv_pick_change(tiku_diffview_t *c, long row);
static void dv_patch_run(dv_job_t *j);
static void dv_rm(int at, const char *name, int depth);

/** @brief Ask for a redraw. */
static void dv_changed(tiku_diffview_t *c)
{
    if (c->dirty) *c->dirty = 1;
}

/** @brief Put @p text in the status line. */
static void dv_say(tiku_diffview_t *c, const char *text)
{
    snprintf(c->status, sizeof c->status, "%s", text);
    dv_changed(c);
}

/** @brief Whether @p x, @p y is inside @p r. */
static int dv_in(tiku_rect_t r, int x, int y)
{
    return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}

#include "tiku_diffview_model.inl"
#include "tiku_diffview_view.inl"

/* ------------------------------------------------------------ places  */

/** @brief @p name in folder @p at removed, a folder with all it holds to
 *         @p depth levels down; a link is removed, never followed. */
static void dv_rm(int at, const char *name, int depth)
{
    int fd = openat(at, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW |
                    O_CLOEXEC);
    DIR *d = fd >= 0 ? fdopendir(fd) : NULL;
    struct dirent *e;
    int pass, left = 1;

    if (!d) {                           /* a file, a link, or gone */
        if (fd >= 0) close(fd);
        else (void)unlinkat(at, name, 0);
        return;
    }
    for (pass = 0; pass < 3 && left; pass++) {  /* a folder read while */
        rewinddir(d);                           /* it shrinks may skip */
        left = 0;
        while ((e = readdir(d)) != NULL) {
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
            left = 1;
            if (depth > 0) dv_rm(dirfd(d), e->d_name, depth - 1);
        }
    }
    closedir(d);
    (void)unlinkat(at, name, AT_REMOVEDIR);
}

/** @brief The viewer's private folder, made the first time it is needed.
 *  @return 0, -1 when none could be. */
static int dv_temp(tiku_diffview_t *c)
{
    const char *tmp = getenv("TMPDIR");

    if (c->temp[0]) return 0;
    snprintf(c->temp, sizeof c->temp, TIKU_PRODUCT_DIFF_TEMP_PATH,
             tmp && *tmp ? tmp : "/tmp");
    if (mkdtemp(c->temp)) return 0;
    c->temp[0] = '\0';
    return -1;
}

/** @brief What the comparison numbered @p sub extracted, removed. */
static void dv_drop_sub(tiku_diffview_t *c, int sub)
{
    char name[32];
    int fd;

    if (sub < 0 || !c->temp[0]) return;
    snprintf(name, sizeof name, "c%d", sub);
    if ((fd = open(c->temp, O_RDONLY | O_DIRECTORY | O_CLOEXEC)) < 0) return;
    dv_rm(fd, name, DV_DEPTH + 4);
    close(fd);
}

/* ----------------------------------------------------------- landing  */

/** @brief The folder list's rows: every entry that differs, and the same
 *         ones too when asked. */
static void dv_shown(tiku_diffview_t *c)
{
    long n = 0;

    free(c->shown);
    c->shown = c->folders ? malloc((size_t)(c->folders->n + 1) *
                                   sizeof *c->shown) : NULL;
    for (long i = 0; c->shown && i < c->folders->n; i++)
        if (c->show_same || c->folders->e[i].status != DV_ENT_SAME)
            c->shown[n++] = i;
    c->nshown = n;
    if (c->sel >= n) c->sel = n - 1;
    if (c->sel < 0 && n > 0) c->sel = 0;
}

/** @brief The window's title: the two sides compared. */
static void dv_title(tiku_diffview_t *c)
{
    if (!c->window) return;
    if (c->mode == DV_EMPTY)
        snprintf(c->window->title, sizeof c->window->title, TIKU_PRODUCT_DIFF_NAME);
    else if (c->from_folder)
        snprintf(c->window->title, sizeof c->window->title, TIKU_PRODUCT_DIFF_TITLE_FORMAT,
                 dv_leaf(c->rel));
    else
        snprintf(c->window->title, sizeof c->window->title,
                 TIKU_PRODUCT_DIFF_COMPARE_TITLE_FORMAT, c->side[0].name, c->side[1].name);
}

/** @brief What the comparison came to, as a sentence. */
static void dv_summary(tiku_diffview_t *c)
{
    const dv_files_t *f = c->files;
    const long *n = c->folders ? c->folders->count : NULL;

    if (c->mode == DV_FOLDERS && n) {
        snprintf(c->status, sizeof c->status, "%ld changed, %ld only on "
                 "the left, %ld only on the right, %ld the same.",
                 n[DV_ENT_CHANGED], n[DV_ENT_LEFT], n[DV_ENT_RIGHT],
                 n[DV_ENT_SAME]);
    } else if (f && f->binary) {
        snprintf(c->status, sizeof c->status, f->same ? "The files are the "
                 "same." : "Binary files differ: %lld and %lld bytes.",
                 (long long)f->size[0], (long long)f->size[1]);
    } else if (f && f->same) {
        snprintf(c->status, sizeof c->status, "The files are the same%s%s.",
                 c->space ? ", whitespace aside" : "",
                 f->endings ? ", but for their line endings" : "");
    } else if (f) {
        snprintf(c->status, sizeof c->status, "%ld line%s removed, %ld "
                 "added%s%s.", f->d.removed, f->d.removed == 1 ? "" : "s",
                 f->d.added, c->space ? "; whitespace ignored" : "",
                 f->endings ? "; line endings differ" : "");
    }
    dv_changed(c);
}

/** @brief What a job made, taken into the window: an entry's files, or
 *         a new comparison in place of the old, whose extracted folder
 *         is removed. */
static void dv_land(tiku_diffview_t *c, dv_job_t *j)
{
    if (j->rel[0]) {                    /* a file of the folder list */
        dv_files_free(c->files);
        c->files = j->files;
        snprintf(c->rel, sizeof c->rel, "%s", j->rel);
        c->rel_status = j->status;
        c->from_folder = 1;
    } else {
        if (c->live >= 0 && c->live != j->sub) dv_drop_sub(c, c->live);
        c->live = j->sub;
        memcpy(c->side, j->side, sizeof c->side);
        dv_files_free(c->files);
        dv_folders_free(c->folders);
        c->files = j->files;
        c->folders = j->folders;
        c->rel[0] = '\0';
        c->from_folder = 0;
        c->sel = 0;
    }
    j->files = NULL;
    j->folders = NULL;
    c->mode = c->files ? DV_FILES : DV_FOLDERS;
    dv_shown(c);
    c->cur = -1;
    c->hoff = 0;
    dv_scroll(c, 0);
    if (c->mode == DV_FILES && !c->files->binary && c->files->nchange)
        dv_pick_change(c, c->files->change[0]);
}

/** @brief A patch job's text: into the file asked for, or onto the
 *         clipboard. */
static void dv_patch_land(tiku_diffview_t *c, const dv_job_t *j)
{
    if (j->said[0]) {
        dv_say(c, j->said);
    } else if (!j->patch_text || !j->patch_len) {
        dv_say(c, "There are no differences to write.");
    } else if (j->save[0]) {
        snprintf(c->status, sizeof c->status, "The differences are in %s.",
                 dv_leaf(j->save));
    } else if (j->patch_len > DV_CLIP_MAX) {
        dv_say(c, "Too much for the clipboard: use Save Differences.");
    } else {
        dv_say(c, tiku_clipboard_import(j->patch_text, j->patch_len)
               ? "The differences are on the clipboard, as a unified diff."
               : "Could not copy the differences; the clipboard is unchanged.");
    }
    dv_changed(c);
}

/** @brief On the loop: what a job made, applied if the window it was
 *         made for is still the one open. */
static void dv_done(void *ctx)
{
    dv_job_t *j = ctx;
    tiku_diffview_t *c = j->owner;

    j->landed = 1;
    if (j->generation != c->generation || !c->workspace) return;
    if (j->patch) {
        if (!j->save[0] && dv_stopped(j->progress)) {
            dv_say(c, "Stopped. No patch was copied.");
            return;
        }
        dv_patch_land(c, j);
        return;
    }
    if (dv_stopped(j->progress)) {
        dv_say(c, "Stopped. The previous comparison is unchanged.");
        if (j->sub >= 0 && j->sub != c->live) dv_drop_sub(c, j->sub);
        return;
    }
    if (!j->files && !j->folders) {     /* refused: what was, stays */
        dv_say(c, j->said[0] ? j->said : "They could not be compared.");
        if (j->sub >= 0 && j->sub != c->live) dv_drop_sub(c, j->sub);
        return;
    }
    dv_land(c, j);
    dv_title(c);
    dv_summary(c);
    dv_menus(c);
}

/** @brief The folder list's chosen entry, compared as files. */
static void dv_open_entry(tiku_diffview_t *c)
{
    const dv_entry_t *e;
    dv_job_t *j;

    if (c->mode != DV_FOLDERS || c->sel < 0 || c->sel >= c->nshown ||
        !(j = calloc(1, sizeof *j))) return;
    e = &c->folders->e[c->shown[c->sel]];
    memcpy(j->side, c->side, sizeof j->side);
    snprintf(j->rel, sizeof j->rel, "%s", e->rel);
    j->status = e->status;
    j->sub = -1;
    (void)dv_post(c, j, "Comparing");
}

/** @brief Every difference of the folder list, as one patch, written on
 *         a worker: to @p save, or onto the clipboard when NULL. */
static void dv_patch(tiku_diffview_t *c, const char *save)
{
    dv_job_t *j = calloc(1, sizeof *j);
    const dv_folders_t *d = c->folders;

    if (!j || !d || !(j->rels = calloc((size_t)d->n + 1, sizeof *j->rels))) {
        free(j);
        dv_say(c, "Not enough memory to prepare the patch. Nothing was saved.");
        return;
    }
    j->patch = 1;
    j->sub = -1;
    memcpy(j->side, c->side, sizeof j->side);
    if (save) snprintf(j->save, sizeof j->save, "%s", save);
    for (long i = 0; i < d->n; i++) {
        if (d->e[i].status == DV_ENT_SAME) continue;
        j->rels[j->nrel].status = d->e[i].status;
        if (!(j->rels[j->nrel].rel = strdup(d->e[i].rel))) {
            for (long k = 0; k < j->nrel; k++) free(j->rels[k].rel);
            free(j->rels);
            free(j);
            dv_say(c, "Not enough memory to prepare the patch. "
                      "Nothing was saved.");
            return;
        }
        j->nrel++;
    }
    (void)dv_post(c, j, "Writing the differences");
}

/** @brief The two files shown as unified text, named as diff names them:
 *         the paths chosen, or a/ and b/ before an entry of a folder. */
static char *dv_unified(tiku_diffview_t *c, size_t *len)
{
    char na[DV_PATH], nb[DV_PATH];

    if (!c->files || c->files->binary) return NULL;
    if (c->from_folder) {
        snprintf(na, sizeof na, c->rel_status == DV_ENT_RIGHT ? "/dev/null"
                                                             : "a/%s", c->rel);
        snprintf(nb, sizeof nb, c->rel_status == DV_ENT_LEFT ? "/dev/null"
                                                            : "b/%s", c->rel);
    } else {
        snprintf(na, sizeof na, "%s", c->side[0].path);
        snprintf(nb, sizeof nb, "%s", c->side[1].path);
    }
    return tiku_diff_unified(&c->files->d, c->files->line[0],
                             c->files->line[1], na, nb, 3, len);
}

/** @brief The difference onto the clipboard: the files shown, or every
 *         file of the folder list. */
static void dv_copy(tiku_diffview_t *c)
{
    size_t len = 0;
    char *u;

    if (c->mode == DV_FOLDERS) {
        dv_patch(c, NULL);
        return;
    }
    if (!(u = dv_unified(c, &len))) {
        dv_say(c, "Not enough memory to write the difference.");
    } else if (len > (size_t)DV_CLIP_MAX) {
        dv_say(c, "Too much for the clipboard: use Save Differences.");
    } else {
        dv_say(c, tiku_clipboard_import(u, len)
               ? "The difference is on the clipboard, as a unified diff."
               : "Could not copy the difference; the clipboard is unchanged.");
    }
    free(u);
}

/** @brief The difference written to @p path on a worker: the files
 *         shown, their text made now; or the folder list's, made on the
 *         worker too. */
static void dv_save_to(tiku_diffview_t *c, const char *path)
{
    dv_job_t *j;

    if (c->busy) {
        dv_say(c, "Wait for the current operation, then save the differences.");
        return;
    }
    if (c->mode == DV_FOLDERS) {
        dv_patch(c, path);
        return;
    }
    j = calloc(1, sizeof *j);
    if (!j || !(j->patch_text = dv_unified(c, &j->patch_len))) {
        free(j);
        dv_say(c, "Not enough memory to prepare the patch. Nothing was saved.");
        return;
    }
    j->patch = 2;                 /* owned snapshot, only the write remains */
    j->sub = -1;
    snprintf(j->save, sizeof j->save, "%s", path);
    (void)dv_post(c, j, "Saving differences");
}

/* ----------------------------------------------------------- moving  */

/** @brief The first row shown set to @p top, kept inside the rows. */
static void dv_scroll(tiku_diffview_t *c, long top)
{
    long max = dv_total(c) - c->vbar.visible;

    if (top > max) top = max;
    if (top < 0) top = 0;
    tiku_scroll_to(&c->vbar, (int)top);
    dv_changed(c);
}

/** @brief The columns scrolled across by @p by, kept between the first
 *         and the 4000th. */
static void dv_hscroll(tiku_diffview_t *c, int by)
{
    c->hoff += by;
    if (c->hoff < 0) c->hoff = 0;
    if (c->hoff > 4000) c->hoff = 4000;
    dv_changed(c);
}

/** @brief The change holding @p row, or the next one after it, made the
 *         one shown; when it is not wholly in sight it is scrolled to,
 *         a few rows below the top. */
static void dv_pick_change(tiku_diffview_t *c, long row)
{
    const dv_files_t *f = c->files;
    long k;

    if (!f || f->binary || !f->nchange) return;
    for (k = 0; k < f->nchange - 1 && f->end[k] <= row; k++) continue;
    c->cur = k;
    if (f->change[k] < c->vbar.value ||
        f->end[k] > c->vbar.value + c->vbar.visible)
        dv_scroll(c, f->change[k] - 3);
    dv_menus(c);
    dv_changed(c);
}

/** @brief The next change (@p dir 1) or the one before; in the folder
 *         list, the next or previous row. */
static void dv_go(tiku_diffview_t *c, int dir)
{
    if (c->mode == DV_FOLDERS) {
        c->sel += dir;
        if (c->sel >= c->nshown) c->sel = c->nshown - 1;
        if (c->sel < 0) c->sel = c->nshown ? 0 : -1;
        if (c->sel >= 0 && (c->sel < c->vbar.value ||
                            c->sel >= c->vbar.value + c->vbar.visible))
            dv_scroll(c, c->sel - c->vbar.visible / 2);
        dv_menus(c);
        return;
    }
    if (!c->files || !c->files->nchange) return;
    c->cur += dir;
    if (c->cur >= c->files->nchange) c->cur = c->files->nchange - 1;
    if (c->cur < 0) c->cur = 0;
    dv_pick_change(c, c->files->change[c->cur]);
}

/** @brief From an entry's lines back to the folder list, the entry still
 *         chosen. */
static void dv_back(tiku_diffview_t *c)
{
    dv_files_free(c->files);
    c->files = NULL;
    c->from_folder = 0;
    c->rel[0] = '\0';
    c->mode = DV_FOLDERS;
    c->cur = -1;
    dv_scroll(c, c->sel - 3);
    dv_title(c);
    dv_summary(c);
}

/* ----------------------------------------------------------- actions  */

/** @brief A command's label, for its menu item and its button's
 *         narration. */
static const char *dv_label(tiku_diffview_t *c, int id)
{
    (void)c;
    switch (id) {
    case DV_COMPARE: return "Compare Files...";
    case DV_COMPARE_DIRS: return "Compare Folders...";
    case DV_OPEN_LEFT: return "Open Left...";
    case DV_OPEN_RIGHT: return "Open Right...";
    case DV_SAVE: return "Save Differences...";
    case DV_CLOSE: return "Close";
    case DV_COPY: return "Copy Differences";
    case DV_SPACE: return "Ignore Whitespace";
    case DV_SHOW_SAME: return "Show Identical Files";
    case DV_SWAP: return "Swap Sides";
    case DV_NEXT: return "Next Change";
    case DV_PREV: return "Previous Change";
    case DV_BACK: return "Back to Folders";
    case DV_OPEN_ROW: return "Compare Selected";
    case DV_CANCEL: return "Stop";
    }
    return "";
}

/** @brief Whether there is anything to write as a diff. */
static int dv_differs(const tiku_diffview_t *c)
{
    const long *n = c->folders ? c->folders->count : NULL;

    if (c->mode == DV_FILES)
        return c->files && !c->files->binary && !c->files->same;
    return c->mode == DV_FOLDERS && n &&
           n[DV_ENT_CHANGED] + n[DV_ENT_LEFT] + n[DV_ENT_RIGHT] > 0;
}

/** @brief Whether command @p id can do anything now. */
static int dv_enabled(tiku_diffview_t *c, int id)
{
    int files = c->mode == DV_FILES && c->files && !c->files->binary;

    switch (id) {
    case DV_COMPARE:
    case DV_COMPARE_DIRS: return !c->busy;
    case DV_OPEN_LEFT:
    case DV_OPEN_RIGHT:
    case DV_SWAP: return c->mode != DV_EMPTY && !c->busy;
    case DV_SAVE:
    case DV_COPY: return dv_differs(c) && !c->busy;
    case DV_SPACE: return !c->busy;
    case DV_SHOW_SAME: return c->mode == DV_FOLDERS;
    case DV_NEXT:
        return (files && c->cur < c->files->nchange - 1) ||
               (c->mode == DV_FOLDERS && c->sel < c->nshown - 1);
    case DV_PREV:
        return (files && c->cur > 0) ||
               (c->mode == DV_FOLDERS && c->sel > 0);
    case DV_BACK: return c->mode == DV_FILES && c->from_folder;
    case DV_OPEN_ROW:
        return c->mode == DV_FOLDERS && c->sel >= 0 && c->sel < c->nshown &&
               !c->busy;
    case DV_CANCEL: return c->progress != NULL;
    case DV_CLOSE: return 1;
    }
    return 0;
}

/** @brief Put a question to the person, remembering what it was for. */
static void dv_ask(tiku_diffview_t *c, int asking, int what, const char *start,
                   const char *name)
{
    const char *home = getenv("HOME");
    int given = start && *start;
    char dir[DV_PATH];
    char *slash;

    c->asking = asking;
    snprintf(dir, sizeof dir, "%s", given ? start : home ? home : "/");
    if (given && (slash = strrchr(dir, '/')) && slash > dir &&
        what != TIKU_DIFFVIEW_ASK_FOLDER)
        *slash = '\0';                  /* a file's folder, to start in */
    if (c->host.pick) c->host.pick(c->host.ctx, what, dir, name);
}

/** @brief The same two sides compared again as the options now say,
 *         or swapped with @p swap. */
static void dv_again(tiku_diffview_t *c, int swap)
{
    char left[DV_PATH], right[DV_PATH];

    if (c->mode == DV_EMPTY) return;
    if (c->from_folder && !swap) {
        dv_open_entry(c);
        return;
    }
    snprintf(left, sizeof left, "%s", c->side[swap].path);
    snprintf(right, sizeof right, "%s", c->side[!swap].path);
    (void)tiku_diffview_compare(c, left, right);
}

/** @brief Carry out command @p id. */
static void dv_action(tiku_diffview_t *c, int id)
{
    char name[300];

    if (!dv_enabled(c, id)) return;
    switch (id) {
    case DV_COMPARE:
    case DV_COMPARE_DIRS:
        c->folder_ask = id == DV_COMPARE_DIRS;
        dv_ask(c, DV_ASK_LEFT, c->folder_ask ? TIKU_DIFFVIEW_ASK_FOLDER
                                             : TIKU_DIFFVIEW_ASK_FILE,
               c->side[0].path, NULL);
        break;
    case DV_OPEN_LEFT:
    case DV_OPEN_RIGHT:
        c->folder_ask = c->mode == DV_FOLDERS || c->from_folder;
        dv_ask(c, id == DV_OPEN_LEFT ? DV_ASK_NEW_LEFT : DV_ASK_NEW_RIGHT,
               c->folder_ask ? TIKU_DIFFVIEW_ASK_FOLDER
                             : TIKU_DIFFVIEW_ASK_FILE,
               c->side[id == DV_OPEN_RIGHT].path, NULL);
        break;
    case DV_SAVE:
        snprintf(name, sizeof name, "%s.diff", c->from_folder ?
                 dv_leaf(c->rel) : c->side[0].name);
        dv_ask(c, DV_ASK_SAVE, TIKU_DIFFVIEW_ASK_SAVE, c->side[0].path, name);
        break;
    case DV_CLOSE: tiku_diffview_close(c); return;
    case DV_COPY: dv_copy(c); break;
    case DV_SPACE:
        c->space = !c->space;
        dv_again(c, 0);
        break;
    case DV_SHOW_SAME:
        c->show_same = !c->show_same;
        dv_shown(c);
        dv_scroll(c, c->vbar.value);
        break;
    case DV_SWAP: dv_again(c, 1); break;
    case DV_NEXT: dv_go(c, 1); break;
    case DV_PREV: dv_go(c, -1); break;
    case DV_BACK: dv_back(c); break;
    case DV_OPEN_ROW: dv_open_entry(c); break;
    case DV_CANCEL:
        atomic_store(&c->progress->stop, 1);
        dv_say(c, "Stopping...");
        break;
    }
    dv_menus(c);
    dv_changed(c);
}

/* ------------------------------------------------------------- keys  */

/** @brief Command keys, whatever has the focus. */
static int dv_command_key(tiku_diffview_t *c, const tiku_event_t *ev)
{
    unsigned key = ev->key;
    int shift = (ev->modifiers & TIKU_MOD_SHIFT) != 0;

    if (!(ev->modifiers & TIKU_MOD_CMD)) return 0;
    if (key == 'o') dv_action(c, shift ? DV_COMPARE_DIRS : DV_COMPARE);
    else if (key == 'w' || key == 'q') dv_action(c, DV_CLOSE);
    else if (key == 's') dv_action(c, DV_SAVE);
    else if (key == 'c') dv_action(c, DV_COPY);
    else if (key == ']') dv_action(c, DV_NEXT);
    else if (key == '[') dv_action(c, DV_PREV);
    else if (key == '.') dv_action(c, DV_CANCEL);
    else if (key == TIKU_KEY_UP) dv_action(c, DV_BACK);
    else if (key == TIKU_KEY_DOWN) dv_action(c, DV_OPEN_ROW);
    else return 0;
    return 1;
}

/** @brief Keys for the rows: moving through the lines or the list, the
 *         next and previous change, opening an entry, going back. */
static int dv_body_key(tiku_diffview_t *c, const tiku_event_t *ev)
{
    long page = c->vbar.visible > 1 ? c->vbar.visible - 1 : 1;
    int list = c->mode == DV_FOLDERS;
    unsigned key = ev->key;

    if (c->mode == DV_EMPTY) return 0;
    if (ev->type == TIKU_EVENT_TEXT_INPUT) {
        if (ev->text[0] == 'n' || ev->text[0] == 'p')
            dv_go(c, ev->text[0] == 'n' ? 1 : -1);
        return ev->text[0] == 'n' || ev->text[0] == 'p';
    }
    if (key == TIKU_KEY_UP || key == TIKU_KEY_DOWN) {
        if (list) dv_go(c, key == TIKU_KEY_UP ? -1 : 1);
        else dv_scroll(c, c->vbar.value + (key == TIKU_KEY_UP ? -1 : 1));
    } else if (key == TIKU_KEY_PAGE_UP || key == TIKU_KEY_PAGE_DOWN) {
        dv_scroll(c, c->vbar.value + (key == TIKU_KEY_PAGE_UP ? -page : page));
        if (list) c->sel = c->vbar.value;
    } else if (key == TIKU_KEY_HOME || key == TIKU_KEY_END) {
        dv_scroll(c, key == TIKU_KEY_HOME ? 0 : dv_total(c));
        if (list) c->sel = key == TIKU_KEY_HOME ? 0 : c->nshown - 1;
    } else if ((key == TIKU_KEY_LEFT || key == TIKU_KEY_RIGHT) && !list) {
        dv_hscroll(c, key == TIKU_KEY_LEFT ? -4 : 4);
    } else if (key == TIKU_KEY_RETURN && list) {
        dv_action(c, DV_OPEN_ROW);
    } else if ((key == TIKU_KEY_ESCAPE || key == TIKU_KEY_BACKSPACE) &&
               c->from_folder) {
        dv_action(c, DV_BACK);
    } else {
        return 0;
    }
    dv_menus(c);
    return 1;
}

/** @brief Every window event.  Keys come first; a press goes to the
 *         dialog's buttons, then to the scrollbar, map and rows; the dialog
 *         takes the rest and carries out a button's command when it is
 *         released. */
static int dv_event(tiku_window_t *w, const tiku_event_t *ev, void *ctx)
{
    tiku_diffview_t *c = ctx;
    dv_layout_t l = dv_layout(c, tiku_window_content(w));
    int pointer = ev->type == TIKU_EVENT_POINTER_DOWN ||
                  ev->type == TIKU_EVENT_POINTER_MOVE ||
                  ev->type == TIKU_EVENT_POINTER_UP ||
                  ev->type == TIKU_EVENT_WHEEL;
    tiku_dialog_result_t r;

    if (pointer || ev->type == TIKU_EVENT_KEY_DOWN ||
        ev->type == TIKU_EVENT_DEACTIVATED) dv_tool_hover(c, ev);
    if (ev->type == TIKU_EVENT_DEACTIVATED) {
        c->vdrag = 0;
        tiku_scroll_release(&c->vbar);
    }
    if ((ev->type == TIKU_EVENT_KEY_DOWN && dv_command_key(c, ev)) ||
        ((ev->type == TIKU_EVENT_KEY_DOWN ||
          ev->type == TIKU_EVENT_TEXT_INPUT) && dv_body_key(c, ev))) {
        dv_changed(c);
        return 1;
    }
    if (pointer && c->dialog.capture == TIKU_DIALOG_NONE) {
        if (ev->type == TIKU_EVENT_POINTER_DOWN) {
            r = tiku_dialog_event(&c->dialog, ev);
            if (r.taken) {
                dv_changed(c);
                return 1;
            }
        }
        if (dv_bar_pointer(c, ev, &l) || dv_body_pointer(c, ev, &l)) {
            dv_changed(c);
            return 1;
        }
        if (ev->type == TIKU_EVENT_POINTER_DOWN) return 0;
    }
    r = tiku_dialog_event(&c->dialog, ev);
    if (r.activated >= 0) dv_action(c, r.activated);
    if (r.taken) dv_changed(c);
    return r.taken;
}

/** @brief The native window went away: nothing may point at it. */
static void dv_window_gone(tiku_window_t *w, void *ctx)
{
    tiku_diffview_t *c = ctx;

    (void)w;
    c->window = NULL;
    c->generation++;
    dv_changed(c);
}

/** @brief A menu pick, carried out as its command. */
static void dv_menu_pick(tiku_window_t *w, int id, void *ctx)
{
    (void)w;
    dv_action(ctx, id);
}

/** @brief Add command @p id to menu @p m, with its shortcut. */
static void dv_menu_add(tiku_diffview_t *c, tiku_menu_list_t *m, int id,
                        char sc, unsigned mods, int marked)
{
    tiku_menu_entry_t *e;

    if (m->nitem >= TIKU_MENUSET_ITEMS) return;
    e = &m->item[m->nitem++];
    memset(e, 0, sizeof *e);
    snprintf(e->label, sizeof e->label, "%s", dv_label(c, id));
    e->command = id;
    e->enabled = (unsigned char)dv_enabled(c, id);
    e->sc = sc;
    e->mods = sc ? mods : 0;
    e->marked = (unsigned char)marked;
}

/** @brief Publish the File, Edit, View and Go menus, each item enabled
 *         as its command is now. */
static void dv_menus(tiku_diffview_t *c)
{
    static const char *const titles[] = { "File", "Edit", "View", "Go" };
    tiku_menuset_t m;

    if (!c->window) return;
    memset(&m, 0, sizeof m);
    m.nmenu = 4;
    for (int i = 0; i < 4; i++)
        snprintf(m.menu[i].title, sizeof m.menu[i].title, "%s", titles[i]);
    dv_menu_add(c, &m.menu[0], DV_COMPARE, 'o', TIKU_MOD_CMD, 0);
    dv_menu_add(c, &m.menu[0], DV_COMPARE_DIRS, 'o',
                TIKU_MOD_CMD | TIKU_MOD_SHIFT, 0);
    dv_menu_add(c, &m.menu[0], DV_OPEN_LEFT, 0, 0, 0);
    dv_menu_add(c, &m.menu[0], DV_OPEN_RIGHT, 0, 0, 0);
    dv_menu_add(c, &m.menu[0], DV_SAVE, 's', TIKU_MOD_CMD, 0);
    dv_menu_add(c, &m.menu[0], DV_CANCEL, '.', TIKU_MOD_CMD, 0);
    dv_menu_add(c, &m.menu[0], DV_CLOSE, 'w', TIKU_MOD_CMD, 0);
    dv_menu_add(c, &m.menu[1], DV_COPY, 'c', TIKU_MOD_CMD, 0);
    dv_menu_add(c, &m.menu[2], DV_SPACE, 0, 0, c->space);
    dv_menu_add(c, &m.menu[2], DV_SHOW_SAME, 0, 0, c->show_same);
    dv_menu_add(c, &m.menu[2], DV_SWAP, 0, 0, 0);
    dv_menu_add(c, &m.menu[3], DV_NEXT, ']', TIKU_MOD_CMD, 0);
    dv_menu_add(c, &m.menu[3], DV_PREV, '[', TIKU_MOD_CMD, 0);
    dv_menu_add(c, &m.menu[3], DV_OPEN_ROW, 0, 0, 0);
    dv_menu_add(c, &m.menu[3], DV_BACK, 0, 0, 0);
    tiku_window_publish_menus(c->window, &m, dv_menu_pick, c);
}

/* -------------------------------------------------------------- API  */

tiku_diffview_t *tiku_diffview_new(tiku_workspace_t *workspace, int *dirty,
                                   const tiku_diffview_host_t *host)
{
    tiku_diffview_t *c = calloc(1, sizeof *c);

    if (!c) return NULL;
    c->workspace = workspace;
    c->dirty = dirty;
    c->refs = 1;
    if (host) c->host = *host;
    c->last_row = c->cur = -1;
    c->live = -1;
    (void)tiku_app_product_register_icons();   /* the toolbar's icons */
    tiku_dialog_init(&c->dialog);
    tiku_scroll_init(&c->vbar, 1);
    return c;
}

void tiku_diffview_open(tiku_diffview_t *c)
{
    if (!c || !c->workspace) return;
    if (!c->window) {
        c->window = tiku_workspace_open(c->workspace, TIKU_PRODUCT_DIFF_NAME,
            (tiku_rect_t){40, 40, 920, 600}, dv_draw, c, c);
        if (!c->window) return;
        tiku_window_set_handlers(c->window, dv_event, NULL, dv_window_gone);
        tiku_window_set_size_controls(c->window, 1, 1);
        dv_say(c, "");              /* an empty window says nothing */
    }
    tiku_workspace_activate(c->workspace, c->window);
    dv_menus(c);
    dv_changed(c);
}

void tiku_diffview_close(tiku_diffview_t *c)
{
    if (c && c->window) tiku_workspace_close(c->workspace, c->window);
}

void tiku_diffview_destroy(tiku_diffview_t *c)
{
    if (!c) return;
    tiku_diffview_close(c);
    c->generation++;
    if (c->progress) atomic_store(&c->progress->stop, 1);
    c->workspace = NULL;
    c->dirty = NULL;
    memset(&c->host, 0, sizeof c->host);
    dv_unref(c);
}

tiku_window_t *tiku_diffview_handle(tiku_diffview_t *c)
{
    return c ? c->window : NULL;
}

void tiku_diffview_picked(tiku_diffview_t *c, const char *path)
{
    int asking;

    if (!c || !path) return;
    asking = c->asking;
    c->asking = DV_ASK_NONE;
    if (asking == DV_ASK_LEFT) {
        snprintf(c->pending, sizeof c->pending, "%s", path);
        dv_ask(c, DV_ASK_RIGHT, c->folder_ask ? TIKU_DIFFVIEW_ASK_FOLDER
                                              : TIKU_DIFFVIEW_ASK_FILE,
               path, NULL);
    } else if (asking == DV_ASK_RIGHT) {
        (void)tiku_diffview_compare(c, c->pending, path);
    } else if (asking == DV_ASK_NEW_LEFT || asking == DV_ASK_NEW_RIGHT) {
        char keep[DV_PATH];

        snprintf(keep, sizeof keep, "%s",
                 c->side[asking == DV_ASK_NEW_LEFT].path);
        (void)tiku_diffview_compare(c, asking == DV_ASK_NEW_LEFT ? path : keep,
                                    asking == DV_ASK_NEW_LEFT ? keep : path);
    } else if (asking == DV_ASK_SAVE) {
        dv_save_to(c, path);
    }
}

int tiku_diffview_compare(tiku_diffview_t *c, const char *left,
                          const char *right)
{
    dv_job_t *j;

    if (!c || !left || !right) return 0;
    if (c->busy) {
        dv_say(c, "A comparison is under way.");
        return 0;
    }
    if (!(j = calloc(1, sizeof *j))) return 0;
    snprintf(j->path[0], sizeof j->path[0], "%s", left);
    snprintf(j->path[1], sizeof j->path[1], "%s", right);
    j->sub = c->temps++;
    if (dv_temp(c) == 0)
        snprintf(j->under, sizeof j->under, "%s/c%d", c->temp, j->sub);
    return dv_post(c, j, "Comparing");
}

void tiku_diffview_start_with(tiku_diffview_t *c, const char *left)
{
    struct stat st;

    if (!c || !left) return;
    snprintf(c->pending, sizeof c->pending, "%s", left);
    c->folder_ask = stat(left, &st) == 0 && S_ISDIR(st.st_mode);
    dv_ask(c, DV_ASK_RIGHT, c->folder_ask ? TIKU_DIFFVIEW_ASK_FOLDER
                                          : TIKU_DIFFVIEW_ASK_FILE, left, NULL);
}

void tiku_diffview_tick(tiku_diffview_t *c)
{
    if (c && c->progress) dv_changed(c);    /* the gauge moves */
}
