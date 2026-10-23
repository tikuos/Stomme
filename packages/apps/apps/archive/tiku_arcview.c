/* tiku_arcview.c -- TikuArchive's viewer: the folder tree an archive's
 * paths make, listed as Tracker lists a folder, its commands, keys and
 * menus.  Every read of the archive runs on a worker.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_arcview.h"
#include "tiku_archive.h"
#include "tiku_attr.h"
#include "tiku_clipboard.h"
#include "tiku_dialog.h"
#include "tiku_dl.h"
#include "tiku_field_event.h"
#include "tiku_font.h"
#include "tiku_iconpaint.h"
#include "tiku_app_product.h"
#include "tiku_list.h"
#include "tiku_measure.h"
#include "tiku_scroll.h"
#include "tiku_text_layout_paint.h"
#include "tiku_ui.h"
#include "tiku_work.h"
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define AV_BAR          15          /* the scrollbar's width             */
#define AV_DOUBLE_US    450000      /* two clicks this close open a row  */
#define AV_REPORTED     4           /* names a report lists              */

enum { AV_OPEN = 1, AV_CLOSE, AV_UP, AV_EXTRACT_ALL, AV_EXTRACT,
       AV_EXTRACT_TO, AV_OPEN_ENTRY, AV_SHOW, AV_CANCEL, AV_SELECT_ALL,
       AV_COPY, AV_FIND, AV_METADATA, AV_LIST, AV_FIND_FIELD, AV_REPORT };
enum { AV_BY_NAME, AV_BY_SIZE, AV_BY_PACKED, AV_BY_DATE };

/** @brief A folder or file of the tree an archive's paths make. */
typedef struct {
    char    *name;                  /* this part of the path             */
    int      parent;                /* its folder's node, -1 the root's  */
    long     entry;                 /* its entry; -1 for a folder that   */
                                    /* only the paths under it imply     */
    int      dir;
    int      first, next;           /* its first child, its next sibling */
    int      count;                 /* a folder: what is directly in it  */
    int64_t  size, packed, mtime;   /* a folder's size and packed: its   */
                                    /* contents' summed, -1 if unknown   */
    unsigned flags;                 /* the entry's, TIKU_ARC_*           */
} av_node_t;

/** @brief The archive as its jobs share it. */
typedef struct {
    tiku_archive_t *a;              /* read by one job at a time: the key */
    int refs;                       /* the viewer and each job; loop only */
    char key[TIKU_WORK_KEY];
} av_arc_t;

/** @brief How far a job has got: its worker writes, the loop reads. */
typedef struct {
    atomic_llong done, total;
    atomic_int stop;                /* Stop pressed                      */
    int refs;                       /* the viewer and the job; loop only */
} av_progress_t;

/** @brief Where the window's parts go, from one description. */
typedef struct {
    tiku_rect_t tools, find, crumbs, head, body, vbar, status, gauge, cancel;
} av_layout_t;

struct tiku_arcview {
    tiku_workspace_t *workspace;
    tiku_window_t *window;
    int *dirty, refs, generation, loading, busy;
    tiku_arcview_host_t host;
    char path[TIKU_PATH_MAX], title[256], status[320], hint[320];
    char format[64];                /* what the archive was found to be  */
    av_arc_t *arc;
    av_node_t *node;
    int nnode, damaged, gz;         /* gz: one file, not a folder's worth */
    int cwd;                        /* the folder shown                  */
    int *rows, nrows, all_rows;     /* shown; how many the folder holds  */
    int sort, desc, meta;           /* column, descending, Mac metadata  */
    tiku_list_t list;
    tiku_scroll_t vbar;
    int vdrag;
    tiku_dialog_t dialog;
    tiku_field_t find;
    tiku_field_input_t find_in;
    int hover_tool;                 /* the toolbar button pointed at      */
    int last_row;                   /* the row last clicked, for doubles  */
    int64_t last_click;
    av_progress_t *progress;        /* the running job's, or NULL        */
    char doing[64];                 /* what that job is: "Extracting"    */
    long long drawn;                /* the progress last drawn            */
    char made[TIKU_PATH_MAX];       /* the last extraction's folder       */
    char *report;                  /* last extraction's detailed report */
};

static void av_menus(tiku_arcview_t *c);
static const char *av_label(tiku_arcview_t *c, int id);
static int av_enabled(tiku_arcview_t *c, int id);
static void av_action(tiku_arcview_t *c, int id);
static void av_rows(tiku_arcview_t *c);
static void av_open_row(tiku_arcview_t *c);
static int av_field_event(tiku_arcview_t *c, const tiku_event_t *ev,
                          const av_layout_t *l);

/** @brief Ask for a redraw. */
static void av_changed(tiku_arcview_t *c)
{
    if (c->dirty) *c->dirty = 1;
}

/** @brief Put @p text in the status line. */
static void av_say(tiku_arcview_t *c, const char *text)
{
    snprintf(c->status, sizeof c->status, "%s", text);
    av_changed(c);
}

/** @brief Whether @p x, @p y is inside @p r. */
static int av_in(tiku_rect_t r, int x, int y)
{
    return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}

/* ------------------------------------------------------------- tree  */

/** @brief Free @p n nodes and their names. */
static void av_tree_free(av_node_t *node, int n)
{
    for (int i = 0; node && i < n; i++) free(node[i].name);
    free(node);
}

/** @brief Node @p i's path inside the archive, "" for the root. */
static void av_node_path(const av_node_t *node, int i, char *out, size_t max)
{
    char tmp[TIKU_PATH_MAX];

    out[0] = '\0';
    for (; i > 0 && node[i].parent >= 0; i = node[i].parent) {
        snprintf(tmp, sizeof tmp, "%s%s%s", node[i].name,
                 out[0] ? "/" : "", out);
        snprintf(out, max, "%s", tmp);
    }
}

/** @brief Whether node @p i is a Mac's metadata: flagged by the kit, or
 *         the __MACOSX folder at the top that holds them. */
static int av_metadata(const av_node_t *node, int i)
{
    return (node[i].flags & TIKU_ARC_METADATA) ||
           (node[i].parent == 0 && !strcmp(node[i].name, "__MACOSX"));
}

/* The viewer qsort sorts for: its comparison has no context argument. */
static const tiku_arcview_t *av_sorting;

/** @brief qsort's order for two rows: folders first, then the sorted
 *         column with ties broken by name, reversed when descending. */
static int av_row_cmp(const void *x, const void *y)
{
    const tiku_arcview_t *c = av_sorting;
    const av_node_t *a = &c->node[*(const int *)x];
    const av_node_t *b = &c->node[*(const int *)y];
    int64_t va = 0, vb = 0;
    int r;

    if (a->dir != b->dir) return a->dir ? -1 : 1;
    if (c->sort == AV_BY_SIZE) va = a->size, vb = b->size;
    else if (c->sort == AV_BY_PACKED) va = a->packed, vb = b->packed;
    else if (c->sort == AV_BY_DATE) va = a->mtime, vb = b->mtime;
    r = va < vb ? -1 : va > vb ? 1 : strcasecmp(a->name, b->name);
    return c->desc ? -r : r;
}

/** @brief Whether @p name holds the find field's text, ignoring case. */
static int av_matches(const tiku_arcview_t *c, const char *name)
{
    const char *want = c->find.text;
    size_t n = strlen(want);

    if (!n) return 1;
    for (; *name; name++)
        if (!strncasecmp(name, want, n)) return 1;
    return 0;
}

/** @brief The rows of the folder shown: its children, narrowed by the
 *         find field, folders first, in the sorted column's order. */
static void av_rows(tiku_arcview_t *c)
{
    int k = 0, all = 0;

    c->nrows = 0;
    if (!c->node) {
        (void)tiku_list_set_count(&c->list, 0);
        return;
    }
    for (int i = c->node[c->cwd].first; i >= 0; i = c->node[i].next) {
        if (!c->meta && av_metadata(c->node, i)) continue;
        all++;
        if (av_matches(c, c->node[i].name) && k < c->nnode) c->rows[k++] = i;
    }
    av_sorting = c;
    qsort(c->rows, (size_t)k, sizeof *c->rows, av_row_cmp);
    c->nrows = tiku_list_set_count(&c->list, k);
    c->all_rows = all;
}

/** @brief Go into folder @p node, with nothing chosen and the list
 *         scrolled to its top; a @p node that is not a folder is ignored. */
static void av_enter(tiku_arcview_t *c, int node)
{
    if (!c->node || node < 0 || node >= c->nnode || !c->node[node].dir)
        return;
    c->cwd = node;
    tiku_list_select_none(&c->list);
    av_rows(c);
    tiku_scroll_to(&c->vbar, 0);
    (void)tiku_list_scroll_to(&c->list, 0, (tiku_rect_t){0, 0, 1, 1});
    av_menus(c);
    av_changed(c);
}

/** @brief Mark @p node's entry, and everything under it, in @p which. */
static void av_mark(const tiku_arcview_t *c, int node, unsigned char *which)
{
    if (c->node[node].entry >= 0) which[c->node[node].entry] = 1;
    for (int i = c->node[node].first; i >= 0; i = c->node[i].next)
        if (c->meta || !av_metadata(c->node, i)) av_mark(c, i, which);
}

/** @brief The entries the chosen rows stand for, a byte an entry; with
 *         @p all, every entry.  @return it, or NULL out of memory. */
static unsigned char *av_which(tiku_arcview_t *c, int all, long count)
{
    unsigned char *which = calloc((size_t)(count ? count : 1), 1);

    if (!which) return NULL;
    if (all) {
        av_mark(c, 0, which);
        return which;
    }
    for (int r = 0; r < c->nrows; r++)
        if (tiku_list_selected(&c->list, r)) av_mark(c, c->rows[r], which);
    return which;
}

#include "tiku_arcview_jobs.inl"
#include "tiku_arcview_view.inl"

/* ----------------------------------------------------------- actions  */

/** @brief A command's label, for its menu item and its button's
 *         narration. */
static const char *av_label(tiku_arcview_t *c, int id)
{
    int some = c->node && tiku_list_selection_count(&c->list) > 0;

    switch (id) {
    case AV_OPEN: return "Open...";
    case AV_CLOSE: return "Close";
    case AV_UP: return "Enclosing Folder";
    case AV_EXTRACT_ALL: return "Extract All";
    case AV_EXTRACT: return "Extract Selected";
    case AV_EXTRACT_TO: return some ? "Extract Selected To..."
                                    : "Extract All To...";
    case AV_OPEN_ENTRY: return "Open Selected";
    case AV_SHOW: return "Show Extracted in Tracker";
    case AV_CANCEL: return "Stop";
    case AV_SELECT_ALL: return "Select All";
    case AV_COPY: return "Copy Names";
    case AV_FIND: return "Find";
    case AV_METADATA: return "Show Mac Metadata";
    case AV_REPORT: return "Copy Extraction Report";
    }
    return "";
}

/** @brief Whether command @p id can do anything now. */
static int av_enabled(tiku_arcview_t *c, int id)
{
    int open = c->node != NULL, some = open && c->nrows > 0 &&
               tiku_list_selection_count(&c->list) > 0;

    switch (id) {
    case AV_OPEN: return !c->loading && !c->busy;
    case AV_CLOSE: return 1;
    case AV_UP: return open && c->cwd != 0;
    case AV_EXTRACT_ALL:
    case AV_EXTRACT_TO: return open && !c->busy;
    case AV_EXTRACT: return some && !c->busy && !c->gz;
    case AV_OPEN_ENTRY: return some && !c->busy;
    case AV_SHOW: return c->made[0] != '\0';
    case AV_REPORT: return c->report != NULL;
    case AV_CANCEL: return c->progress != NULL;
    case AV_SELECT_ALL:
    case AV_COPY: return open && c->nrows > 0;
    case AV_FIND:
    case AV_METADATA: return open;
    }
    return 0;
}

/** @brief Open the chosen row: a folder goes in, a file is extracted
 *         to a folder that is kept and opened in its program. */
static void av_open_row(tiku_arcview_t *c)
{
    int r = tiku_list_chosen(&c->list), n;

    if (!c->node || r < 0 || r >= c->nrows) return;
    n = c->rows[r];
    if (c->node[n].dir) av_enter(c, n);
    else if (c->node[n].entry >= 0) (void)av_open_entry(c, n);
}

/** @brief The chosen rows' paths, a line each, to the clipboard. */
static void av_copy_names(tiku_arcview_t *c)
{
    size_t cap = 4096, used = 0;
    char *text = malloc(cap), path[TIKU_PATH_MAX];

    if (!text) return;
    text[0] = '\0';
    for (int r = 0; r < c->nrows; r++) {
        size_t n;

        if (!tiku_list_selected(&c->list, r)) continue;
        av_node_path(c->node, c->rows[r], path, sizeof path);
        n = strlen(path);
        if (used + n + 2 > cap) {
            char *more = realloc(text, cap = (used + n + 2) * 2);
            if (!more) break;
            text = more;
        }
        memcpy(text + used, path, n);
        used += n;
        text[used++] = '\n';
        text[used] = '\0';
    }
    tiku_clipboard_set(text);
    av_say(c, "The chosen names are on the clipboard.");
    free(text);
}

/** @brief Carry out command @p id. */
static void av_action(tiku_arcview_t *c, int id)
{
    if (!av_enabled(c, id)) return;
    switch (id) {
    case AV_OPEN:
        if (c->host.pick) c->host.pick(c->host.ctx, 0, getenv("HOME"), NULL);
        break;
    case AV_CLOSE: tiku_arcview_close(c); return;
    case AV_UP: av_enter(c, c->node[c->cwd].parent); break;
    case AV_EXTRACT_ALL: (void)av_extract(c, 1, NULL); break;
    case AV_EXTRACT: (void)av_extract(c, 0, NULL); break;
    case AV_EXTRACT_TO: av_ask_where(c); break;
    case AV_OPEN_ENTRY: av_open_row(c); break;
    case AV_SHOW:
        if (c->host.open_path) (void)c->host.open_path(c->host.ctx, c->made);
        break;
    case AV_CANCEL:
        atomic_store(&c->progress->stop, 1);
        av_say(c, "Stopping...");
        break;
    case AV_REPORT:
        if (tiku_clipboard_import(c->report, strlen(c->report)))
            av_say(c, "Extraction report copied.");
        else av_say(c, "Could not copy the extraction report.");
        break;
    case AV_SELECT_ALL: tiku_list_select_all(&c->list); break;
    case AV_COPY: av_copy_names(c); break;
    case AV_FIND: tiku_dialog_focus(&c->dialog, AV_FIND_FIELD); break;
    case AV_METADATA:
        c->meta = !c->meta;
        av_rows(c);
        break;
    }
    av_menus(c);
    av_changed(c);
}

/* ------------------------------------------------------------- keys  */

/** @brief Command keys, whatever has the focus, unless the find field
 *         wants its own cut, copy, paste, undo and select all. */
static int av_command_key(tiku_arcview_t *c, const tiku_event_t *ev)
{
    int field = c->dialog.focus == AV_FIND_FIELD;
    unsigned key = ev->key;

    if (!(ev->modifiers & TIKU_MOD_CMD)) return 0;
    if (field && (key == 'c' || key == 'x' || key == 'v' || key == 'a' ||
                  key == 'z')) return 0;
    if (key == 'o') av_action(c, AV_OPEN);
    else if (key == 'w' || key == 'q') av_action(c, AV_CLOSE);
    else if (key == 'e') av_action(c, ev->modifiers & TIKU_MOD_SHIFT
                                      ? AV_EXTRACT : AV_EXTRACT_ALL);
    else if (key == 'a') av_action(c, AV_SELECT_ALL);
    else if (key == 'c') av_action(c, AV_COPY);
    else if (key == 'f') av_action(c, AV_FIND);
    else if (key == TIKU_KEY_UP) av_action(c, AV_UP);
    else if (key == TIKU_KEY_DOWN) av_action(c, AV_OPEN_ENTRY);
    else if (key == '.') av_action(c, AV_CANCEL);
    else return 0;
    return 1;
}

/** @brief A key or typed text for the find field: the rows narrow as it
 *         is typed; Return goes to the list, Escape empties it. */
static int av_field_event(tiku_arcview_t *c, const tiku_event_t *ev,
                          const av_layout_t *l)
{
    tiku_field_result_t r;
    char before[TIKU_FIELD_MAX];

    if (c->dialog.focus != AV_FIND_FIELD) return 0;
    if (ev->type == TIKU_EVENT_KEY_DOWN && ev->key == TIKU_KEY_TAB) return 0;
    snprintf(before, sizeof before, "%s", c->find.text);
    r = tiku_field_event(&c->find, &c->find_in, ev, l->find, 1);
    if (r == TIKU_FIELD_COMMITTED) {
        tiku_dialog_focus(&c->dialog, AV_LIST);
    } else if (r == TIKU_FIELD_CANCELLED) {
        tiku_field_start(&c->find, "");
        tiku_dialog_focus(&c->dialog, AV_LIST);
    }
    if (strcmp(before, c->find.text)) {
        av_rows(c);
        if (c->nrows) tiku_list_select_only(&c->list, 0);
    }
    av_menus(c);
    av_changed(c);
    return ev->type == TIKU_EVENT_KEY_DOWN ||
           ev->type == TIKU_EVENT_TEXT_INPUT || r != TIKU_FIELD_IGNORED;
}

/** @brief The name of row @p row, for type-ahead selection. */
static const char *av_row_name(void *ctx, int row)
{
    tiku_arcview_t *c = ctx;

    return row >= 0 && row < c->nrows ? c->node[c->rows[row]].name : "";
}

/** @brief Keys for the list: moving, opening, going up, type-ahead;
 *         Escape empties the find field. */
static int av_list_key(tiku_arcview_t *c, const tiku_event_t *ev,
                       const av_layout_t *l)
{
    if (!c->node) return 0;
    if (ev->type == TIKU_EVENT_TEXT_INPUT) {
        return (unsigned char)ev->text[0] > ' ' &&
               tiku_list_typeahead(&c->list, ev->text[0], ev->time_us,
                                   l->body, av_row_name, c) >= 0;
    }
    if (ev->key == TIKU_KEY_RETURN) av_open_row(c);
    else if (ev->key == TIKU_KEY_BACKSPACE) av_action(c, AV_UP);
    else if (ev->key == TIKU_KEY_ESCAPE && c->find.text[0]) {
        tiku_field_start(&c->find, "");
        av_rows(c);
    } else if (!tiku_list_key(&c->list, ev->key, ev->modifiers, l->body)) {
        return 0;
    }
    av_menus(c);
    return 1;
}

/** @brief Keys: commands first, then the find field, then the list. */
static int av_key(tiku_arcview_t *c, const tiku_event_t *ev,
                  const av_layout_t *l)
{
    if (ev->type == TIKU_EVENT_KEY_DOWN && av_command_key(c, ev)) return 1;
    if (av_field_event(c, ev, l)) return 1;
    if (c->dialog.focus == AV_LIST || c->dialog.focus == TIKU_DIALOG_NONE)
        return av_list_key(c, ev, l);
    return 0;
}

/** @brief Every window event.  Keys come first; a press goes to the
 *         dialog's buttons, then to the find field, path bar, headers,
 *         scrollbar and rows; the dialog takes the rest and carries out
 *         a button's command when it is released. */
static int av_event(tiku_window_t *w, const tiku_event_t *ev, void *ctx)
{
    tiku_arcview_t *c = ctx;
    av_layout_t l = av_layout(c, tiku_window_content(w));
    int pointer = ev->type == TIKU_EVENT_POINTER_DOWN ||
                  ev->type == TIKU_EVENT_POINTER_MOVE ||
                  ev->type == TIKU_EVENT_POINTER_UP ||
                  ev->type == TIKU_EVENT_WHEEL;
    tiku_dialog_result_t r;

    if (pointer || ev->type == TIKU_EVENT_KEY_DOWN ||
        ev->type == TIKU_EVENT_DEACTIVATED) av_tool_hover(c, ev);
    if (ev->type == TIKU_EVENT_DEACTIVATED) {
        c->vdrag = 0;
        tiku_scroll_release(&c->vbar);
    }
    if ((ev->type == TIKU_EVENT_KEY_DOWN ||
         ev->type == TIKU_EVENT_TEXT_INPUT) && av_key(c, ev, &l)) {
        av_changed(c);
        return 1;
    }
    if (pointer && c->dialog.capture == TIKU_DIALOG_NONE) {
        if (ev->type == TIKU_EVENT_POINTER_DOWN) {
            r = tiku_dialog_event(&c->dialog, ev);
            if (r.taken) { av_changed(c); return 1; }
        }
        if (av_pointer(c, ev, &l)) { av_changed(c); return 1; }
        if (ev->type == TIKU_EVENT_POINTER_DOWN) return 0;
    }
    r = tiku_dialog_event(&c->dialog, ev);
    if (r.activated >= 0) av_action(c, r.activated);
    if (r.taken) av_changed(c);
    return r.taken;
}

/** @brief The native window went away: nothing may point at it. */
static void av_window_gone(tiku_window_t *w, void *ctx)
{
    tiku_arcview_t *c = ctx;

    (void)w;
    c->window = NULL;
    c->generation++;
    av_forget(c);
    av_changed(c);
}

/** @brief A menu pick, carried out as its command. */
static void av_menu_pick(tiku_window_t *w, int id, void *ctx)
{
    (void)w;
    av_action(ctx, id);
}

/** @brief Add command @p id to menu @p m, with its shortcut. */
static void av_menu_add(tiku_arcview_t *c, tiku_menu_list_t *m, int id,
                        char sc, unsigned mods, int marked)
{
    tiku_menu_entry_t *e;

    if (m->nitem >= TIKU_MENUSET_ITEMS) return;
    e = &m->item[m->nitem++];
    memset(e, 0, sizeof *e);
    snprintf(e->label, sizeof e->label, "%s", av_label(c, id));
    e->command = id;
    e->enabled = (unsigned char)av_enabled(c, id);
    e->sc = sc;
    e->mods = sc ? mods : 0;
    e->marked = (unsigned char)marked;
}

/** @brief Publish the File, Edit, View and Go menus, each item enabled
 *         as its command is now. */
static void av_menus(tiku_arcview_t *c)
{
    static const char *const titles[] = { "File", "Edit", "View", "Go" };
    tiku_menuset_t m;

    if (!c->window) return;
    memset(&m, 0, sizeof m);
    m.nmenu = 4;
    for (int i = 0; i < 4; i++)
        snprintf(m.menu[i].title, sizeof m.menu[i].title, "%s", titles[i]);
    av_menu_add(c, &m.menu[0], AV_OPEN, 'o', TIKU_MOD_CMD, 0);
    av_menu_add(c, &m.menu[0], AV_EXTRACT_ALL, 'e', TIKU_MOD_CMD, 0);
    av_menu_add(c, &m.menu[0], AV_EXTRACT, 'e',
                TIKU_MOD_CMD | TIKU_MOD_SHIFT, 0);
    av_menu_add(c, &m.menu[0], AV_EXTRACT_TO, 0, 0, 0);
    av_menu_add(c, &m.menu[0], AV_SHOW, 0, 0, 0);
    av_menu_add(c, &m.menu[0], AV_REPORT, 0, 0, 0);
    av_menu_add(c, &m.menu[0], AV_CANCEL, '.', TIKU_MOD_CMD, 0);
    av_menu_add(c, &m.menu[0], AV_CLOSE, 'w', TIKU_MOD_CMD, 0);
    av_menu_add(c, &m.menu[1], AV_COPY, 'c', TIKU_MOD_CMD, 0);
    av_menu_add(c, &m.menu[1], AV_SELECT_ALL, 'a', TIKU_MOD_CMD, 0);
    av_menu_add(c, &m.menu[1], AV_FIND, 'f', TIKU_MOD_CMD, 0);
    av_menu_add(c, &m.menu[2], AV_METADATA, 0, 0, c->meta);
    av_menu_add(c, &m.menu[3], AV_UP, 0, 0, 0);
    av_menu_add(c, &m.menu[3], AV_OPEN_ENTRY, 0, 0, 0);
    tiku_window_publish_menus(c->window, &m, av_menu_pick, c);
}

/* -------------------------------------------------------------- API  */

tiku_arcview_t *tiku_arcview_new(tiku_workspace_t *workspace, int *dirty,
                                 const tiku_arcview_host_t *host)
{
    tiku_arcview_t *c = calloc(1, sizeof *c);

    if (!c) return NULL;
    c->workspace = workspace;
    c->dirty = dirty;
    c->refs = 1;
    if (host) c->host = *host;
    c->last_row = -1;
    (void)tiku_app_product_register_icons();   /* the toolbar's and list's icons */
    tiku_dialog_init(&c->dialog);
    tiku_list_init(&c->list, 0);
    tiku_scroll_init(&c->vbar, 1);
    tiku_field_start(&c->find, "");
    tiku_field_input_init(&c->find_in);
    return c;
}

void tiku_arcview_open(tiku_arcview_t *c)
{
    if (!c || !c->workspace) return;
    if (!c->window) {
        c->window = tiku_workspace_open(c->workspace, TIKU_PRODUCT_ARCHIVE_NAME,
            (tiku_rect_t){90, 60, 720, 500}, av_draw, c, c);
        if (!c->window) return;
        tiku_window_set_handlers(c->window, av_event, NULL, av_window_gone);
        tiku_window_set_size_controls(c->window, 1, 1);
        av_say(c, "");              /* an empty window says nothing */
    }
    tiku_workspace_activate(c->workspace, c->window);
    av_menus(c);
    av_changed(c);
}

void tiku_arcview_close(tiku_arcview_t *c)
{
    if (c && c->window) tiku_workspace_close(c->workspace, c->window);
}

void tiku_arcview_destroy(tiku_arcview_t *c)
{
    if (!c) return;
    tiku_arcview_close(c);
    c->generation++;
    c->workspace = NULL;
    c->dirty = NULL;
    memset(&c->host, 0, sizeof c->host);
    av_unref(c);
}

tiku_window_t *tiku_arcview_handle(tiku_arcview_t *c)
{
    return c ? c->window : NULL;
}

const char *tiku_arcview_path(tiku_arcview_t *c)
{
    return c && c->node ? c->path : "";
}

int tiku_arcview_file(tiku_arcview_t *c, const char *path)
{
    if (!c || !c->workspace) return 0;
    tiku_arcview_open(c);
    return av_load(c, path);
}

int tiku_arcview_extract_to(tiku_arcview_t *c, const char *path)
{
    if (!c || !c->node || !path || !*path) return 0;
    return av_extract(c, tiku_list_selection_count(&c->list) == 0, path);
}

void tiku_arcview_tick(tiku_arcview_t *c)
{
    if (c && c->progress &&
        atomic_load(&c->progress->done) != c->drawn) {
        c->drawn = atomic_load(&c->progress->done);
        av_changed(c);
    }
}
