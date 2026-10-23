/* TikuArchive driven without a desktop: an archive opened on a worker,
 * its folders entered and left, rows found and sorted, extracted whole,
 * in part, to a folder named, and one file opened in its program; Stop,
 * hostile names, a gzip's one file, files that are not archives, the
 * toolbar and closing.  SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <unistd.h>
#include "tiku_work.h"
static int audit_post(const tiku_work_job_t *job);
#define tiku_work_post audit_post
#include "../../apps/archive/tiku_arcview.c"
#undef tiku_work_post
#include "tiku_icons.h"
#include "tiku_text_input.h"
#include "../fixtures/archive.inc"

static tiku_workspace_t *desk;
static char scratch[PATH_MAX];
static int picks, saves;            /* the questions put to the person   */
static char opened[PATH_MAX];       /* the last path handed to open_path */
static int gate_extraction;
static atomic_int extraction_written;

/* Hold the worker after writing files, before its result reaches the UI.
 * This makes cancellation during RUNNING deterministic, without a huge
 * fixture. */
static void gated_extract(void *ctx)
{
    av_extract_t *j = ctx;
    av_extract_run(ctx);
    atomic_store(&extraction_written, 1);
    while (!atomic_load(&j->progress->stop)) usleep(1000);
    j->err = TIKU_ARCHIVE_CANCELLED;
}

static int audit_post(const tiku_work_job_t *job)
{
    tiku_work_job_t copy = *job;
    if (gate_extraction && copy.run == av_extract_run) copy.run = gated_extract;
    return tiku_work_post(&copy);
}

/** @brief The Open and Save panels, as counted questions. */
static void host_pick(void *ctx, int save, const char *start,
                      const char *name)
{
    (void)ctx;
    (void)start;
    (void)name;
    if (save) saves++;
    else picks++;
}

/** @brief open_path, remembered: the file the person would see. */
static int host_open(void *ctx, const char *path)
{
    (void)ctx;
    snprintf(opened, sizeof opened, "%s", path);
    return 1;
}

/** @brief Every job run and landed, the window drawn each turn. */
static void settle(tiku_arcview_t *c)
{
    for (int i = 0; i < 5000; i++) {
        (void)tiku_work_collect();
        if (c) tiku_arcview_tick(c);
        if (desk) tiku_workspace_draw(desk);
        if (!tiku_work_busy() && (!c || (!c->loading && !c->busy))) return;
        usleep(1000);
    }
    assert(!"a job never finished");
}

/** @brief A fixture's bytes, decoded from its base64, written to
 *         @p name in the scratch folder; its path into @p out. */
static void fixture(const char *which, const char *name, char *out)
{
    static const char abc[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const char *b64 = NULL;
    unsigned v = 0;
    int bits = 0;
    FILE *f;

    for (size_t k = 0; k < sizeof fixtures / sizeof fixtures[0]; k++)
        if (!strcmp(fixtures[k].name, which)) b64 = fixtures[k].data;
    assert(b64);
    snprintf(out, PATH_MAX, "%s/%s", scratch, name);
    f = fopen(out, "wb");
    assert(f);
    for (; *b64 && *b64 != '='; b64++) {
        v = v << 6 | (unsigned)(strchr(abc, *b64) - abc);
        if ((bits += 6) >= 8) {
            bits -= 8;
            fputc((int)(v >> bits) & 0xff, f);
        }
    }
    fclose(f);
}

/** @brief A key pressed in the viewer. */
static void press(tiku_arcview_t *c, unsigned key, unsigned mods)
{
    tiku_event_t ev = {.type = TIKU_EVENT_KEY_DOWN, .key = key,
                       .modifiers = mods};

    av_event(c->window, &ev, c);
}

/** @brief Text typed into whatever has the focus, a character at a time,
 *         each as the host commits typed text. */
static void type(tiku_arcview_t *c, const char *text)
{
    for (; *text; text++) {
        tiku_text_input_t in;
        tiku_event_t ev;
        char one[2] = { *text, 0 };

        (void)tiku_text_input_set(&in, one, 1);
        while (tiku_text_input_next(&in, &ev)) av_event(c->window, &ev, c);
    }
}

/** @brief A press and release at @p x, @p y, at time @p us. */
static void click_at(tiku_arcview_t *c, int x, int y, int64_t us)
{
    tiku_event_t ev = {.type = TIKU_EVENT_POINTER_DOWN, .button = 1,
                       .x = x, .y = y, .time_us = us};

    av_event(c->window, &ev, c);
    ev.type = TIKU_EVENT_POINTER_UP;
    av_event(c->window, &ev, c);
}

/** @brief The layout input and drawing see. */
static av_layout_t layout(tiku_arcview_t *c)
{
    return av_layout(c, tiku_window_content(c->window));
}

/** @brief The row showing @p name, or -1. */
static int row_of(tiku_arcview_t *c, const char *name)
{
    for (int r = 0; r < c->nrows; r++)
        if (!strcmp(c->node[c->rows[r]].name, name)) return r;
    return -1;
}

/** @brief A double click on row @p row. */
static void open_row(tiku_arcview_t *c, int row)
{
    av_layout_t l = layout(c);
    tiku_rect_t r = tiku_list_row_rect(&c->list, l.body, row);

    click_at(c, r.x + 40, r.y + r.h / 2, 1000000);
    click_at(c, r.x + 40, r.y + r.h / 2, 1100000);
    settle(c);
}

/** @brief Whether @p path is there (a file, folder or link). */
static int there(const char *path)
{
    struct stat st;

    return lstat(path, &st) == 0;
}

/** @brief Before an archive is open: the window blank, on @p s, and its
 *         status line saying nothing. */
static void empty_tests(tiku_arcview_t *c, const tiku_surface_t *s)
{
    av_layout_t l = layout(c);
    tiku_rect_t r = {l.head.x, l.head.y, l.head.w, l.head.h + l.body.h};

    tiku_workspace_draw(desk);
    assert(!c->node && !c->status[0]);
    for (int y = r.y + 1; y < r.y + r.h - 1; y++)    /* inside its frame */
        for (int x = r.x + 1; x < r.x + r.w - 1; x++)
            assert((s->px[(size_t)y * s->w + x] & 0xffffffu) ==
                   (TIKU_C_DOC & 0xffffffu));
}

/** @brief Opening, the rows of the top folder, going in and out. */
static void browse_tests(tiku_arcview_t *c, const char *zip)
{
    assert(tiku_arcview_file(c, zip));
    settle(c);
    assert(c->node && !strcmp(c->format, "ZIP archive"));
    assert(strstr(c->status, "ZIP archive") && strstr(c->status, "entries"));
    /* Folders first, then files; the Mac's metadata out of sight. */
    assert(c->nrows == 8 && c->all_rows == 8);
    assert(!strcmp(c->node[c->rows[0]].name, "bin") &&
           !strcmp(c->node[c->rows[3]].name, "na\xc3\xafve") &&
           !strcmp(c->node[c->rows[4]].name, "link"));
    assert(row_of(c, "__MACOSX") < 0);
    open_row(c, row_of(c, "docs"));
    assert(c->nrows == 1 && !strcmp(c->node[c->rows[0]].name, "readme.txt"));
    press(c, TIKU_KEY_BACKSPACE, 0);
    assert(c->cwd == 0 && c->nrows == 8);
    open_row(c, row_of(c, "data"));
    assert(c->nrows == 1);
    av_action(c, AV_UP);
    assert(c->cwd == 0);
}

/** @brief Finding narrows the rows as it is typed; sorting by a column,
 *         and the other way on a second press; the Mac's metadata shown. */
static void find_sort_tests(tiku_arcview_t *c)
{
    av_layout_t l;
    int x;

    av_action(c, AV_FIND);
    type(c, "txt");
    assert(c->nrows == 3 && c->all_rows == 8);
    press(c, TIKU_KEY_ESCAPE, 0);
    assert(!c->find.text[0] && c->nrows == 8);
    l = layout(c);
    x = l.head.x + av_name_w(l.body) + 10;      /* the Size column */
    click_at(c, x, l.head.y + 4, 5000000);
    assert(c->sort == AV_BY_SIZE && !c->desc);
    click_at(c, x, l.head.y + 4, 6000000);
    assert(c->sort == AV_BY_SIZE && c->desc);
    click_at(c, l.head.x + 10, l.head.y + 4, 7000000);
    assert(c->sort == AV_BY_NAME && !c->desc);
    av_action(c, AV_METADATA);
    assert(c->nrows == 9 && row_of(c, "__MACOSX") >= 0);
    av_action(c, AV_METADATA);
    assert(c->nrows == 8);
}

/** @brief Extract All beside the archive, twice; the chosen rows; to a
 *         folder the Save panel named. */
static void extract_tests(tiku_arcview_t *c)
{
    char p[PATH_MAX];

    av_action(c, AV_EXTRACT_ALL);
    settle(c);
    snprintf(p, sizeof p, "%s/features/docs/readme.txt", scratch);
    assert(there(p) && strstr(c->status, "Extracted into \"features\""));
    assert(av_enabled(c, AV_SHOW) && strstr(c->made, "/features"));
    snprintf(p, sizeof p, "%s/features/__MACOSX", scratch);
    assert(!there(p));
    av_action(c, AV_EXTRACT_ALL);
    settle(c);
    snprintf(p, sizeof p, "%s/features 2/bin/tool", scratch);
    assert(there(p));
    tiku_list_select_none(&c->list);
    tiku_list_click(&c->list, row_of(c, "docs"), 0);
    tiku_list_click(&c->list, row_of(c, "stamped.txt"), TIKU_MOD_CMD);
    assert(av_enabled(c, AV_EXTRACT));
    av_action(c, AV_EXTRACT);
    settle(c);
    snprintf(p, sizeof p, "%s/features 3/docs/readme.txt", scratch);
    assert(there(p));
    snprintf(p, sizeof p, "%s/features 3/stamped.txt", scratch);
    assert(there(p));
    snprintf(p, sizeof p, "%s/features 3/bin", scratch);
    assert(!there(p));
    av_action(c, AV_EXTRACT_TO);
    assert(saves == 1);
    tiku_list_select_none(&c->list);
    snprintf(p, sizeof p, "%s/Chosen", scratch);
    assert(tiku_arcview_extract_to(c, p));
    settle(c);
    snprintf(p, sizeof p, "%s/Chosen/data/big.bin", scratch);
    assert(there(p));
    av_action(c, AV_SHOW);
    assert(!strcmp(opened + strlen(opened) - 6, "Chosen"));
}

/** @brief A file opened from inside is a retained copy, safe to edit. */
static void open_entry_tests(tiku_arcview_t *c)
{
    FILE *f;
    char line[32];

    tiku_list_select_only(&c->list, row_of(c, "stamped.txt"));
    press(c, TIKU_KEY_RETURN, 0);
    settle(c);
    assert(!strncmp(opened, scratch, strlen(scratch)) &&
           strstr(opened, "/stamped.txt"));
    assert(strstr(c->status, "Opened stamped.txt"));
    f = fopen(opened, "r");
    assert(f && fgets(line, sizeof line, f) && !strcmp(line, "stamped\n"));
    fclose(f);
    f = fopen(opened, "w");
    assert(f && fputs("edited copy\n", f) >= 0 && fclose(f) == 0);
}

/** @brief Stop before a job starts: nothing made, said so. */
static void stop_tests(tiku_arcview_t *c, const char *tgz)
{
    char p[PATH_MAX];

    assert(tiku_arcview_file(c, tgz));
    settle(c);
    assert(!strcmp(c->format, "gzip-compressed tar archive"));
    assert(av_extract(c, 1, NULL) && c->progress && c->busy);
    assert(av_enabled(c, AV_CANCEL) && !av_enabled(c, AV_EXTRACT_ALL));
    av_action(c, AV_CANCEL);
    settle(c);
    assert(!c->busy && !c->progress);
    assert(strstr(c->status, "Stopped"));
    snprintf(p, sizeof p, "%s/pkg/pkg/src.c", scratch);
    assert(!there(p));
}

/** @brief Names that climb out: listed at the top whole, not made, and
 *         said so after an extraction. */
static void hostile_tests(tiku_arcview_t *c, const char *zip)
{
    int r;

    assert(tiku_arcview_file(c, zip));
    settle(c);
    r = row_of(c, "../climb.txt");
    assert(r >= 0 && (c->node[c->rows[r]].flags & TIKU_ARC_REFUSED));
    assert(row_of(c, "..") < 0);
    av_action(c, AV_EXTRACT_ALL);
    settle(c);
    assert(strstr(c->status, "but 3 not") &&
           strstr(c->status, "reach outside"));
    assert(c->report && strstr(c->report, "../climb.txt"));
    av_action(c, AV_REPORT);
    assert(!strcmp(tiku_clipboard_text(), c->report));
    for (long i = 0; i < tiku_archive_count(c->arc->a); i++) {
        const tiku_archive_entry_t *e = tiku_archive_entry(c->arc->a, i);
        if (e->flags & TIKU_ARC_REFUSED) assert(strstr(c->report, e->path));
    }
    av_extract_t bounded = {.report_len = 1u << 20};
    av_report_append(&bounded, "file", "failure");
    assert(bounded.report_incomplete && !bounded.report);
}

static void running_cancel_tests(tiku_arcview_t *c, const char *zip)
{
    char dest[PATH_MAX], file[PATH_MAX];
    assert(tiku_arcview_file(c, zip));
    settle(c);
    snprintf(dest, sizeof dest, "%s/cancelled", scratch);
    snprintf(file, sizeof file, "%s/cancelled/stamped.txt", scratch);
    atomic_store(&extraction_written, 0);
    gate_extraction = 1;
    assert(av_extract(c, 1, dest));
    for (int i = 0; i < 5000 && !atomic_load(&extraction_written); i++)
        usleep(1000);
    assert(atomic_load(&extraction_written) && there(file));
    assert(!tiku_arcview_file(c, zip)); /* cannot orphan an operation */
    av_action(c, AV_CANCEL);
    settle(c);
    gate_extraction = 0;
    assert(strstr(c->status, "What was extracted so far"));
    assert(!strcmp(c->made, dest) && av_enabled(c, AV_SHOW));
    assert(c->report && strstr(c->report, dest));
}

static void narrow_tests(tiku_arcview_t *c)
{
    for (int width = 180; width <= 760; width += 20) {
        av_layout_t l = av_layout(c, (tiku_rect_t){0, 0, width, 500});
        for (int i = 0; i < c->dialog.count; i++) {
            const tiku_dialog_control_t *a = &c->dialog.control[i];
            if (a->kind != TIKU_DIALOG_BUTTON) continue;
            assert(a->rect.x >= 0 && a->rect.x + a->rect.w <= width);
            assert(a->rect.y + a->rect.h <= l.find.y ||
                   l.find.y + l.find.h <= a->rect.y ||
                   a->rect.x + a->rect.w <= l.find.x ||
                   l.find.x + l.find.w <= a->rect.x);
        }
        assert(av_name_w(l.body) > 0);
        assert(l.find.x >= 0 && l.find.x + l.find.w <= width);
    }
    (void)layout(c);
}

/** @brief A gzip's one file beside the archive, under a new name the
 *         second time; files that are not archives, or not yet read. */
static void gzip_and_other_tests(tiku_arcview_t *c)
{
    char gz[PATH_MAX], p[PATH_MAX];
    FILE *f;

    fixture("gz_fields", "notes.txt.gz", gz);
    assert(tiku_arcview_file(c, gz));
    settle(c);
    assert(c->gz && c->nrows == 1 && row_of(c, "notes.txt") == 0);
    assert(!av_enabled(c, AV_EXTRACT));
    av_action(c, AV_EXTRACT_ALL);
    settle(c);
    snprintf(p, sizeof p, "%s/notes.txt", scratch);
    assert(there(p));
    av_action(c, AV_EXTRACT_ALL);
    settle(c);
    snprintf(p, sizeof p, "%s/notes 2.txt", scratch);
    assert(there(p));
    snprintf(p, sizeof p, "%s/plain.txt", scratch);
    f = fopen(p, "w");
    assert(f && fputs("not an archive\n", f) >= 0);
    fclose(f);
    assert(tiku_arcview_file(c, p));
    settle(c);
    assert(strstr(c->status, "not an archive") && c->gz);
    snprintf(p, sizeof p, "%s/later.xz", scratch);
    f = fopen(p, "wb");
    assert(f && fwrite("\xfd" "7zXZ\0 later", 1, 12, f) == 12);
    fclose(f);
    assert(tiku_arcview_file(c, p));
    settle(c);
    assert(strstr(c->status, "xz-compressed file") &&
           strstr(c->status, "not read those yet"));
}

/** @brief Every toolbar button: an icon from the set, a tip, a square. */
static void toolbar_tests(tiku_arcview_t *c)
{
    int buttons = 0;

    (void)layout(c);
    for (int i = 0; i < c->dialog.count; i++) {
        const tiku_dialog_control_t *d = &c->dialog.control[i];

        if (d->kind != TIKU_DIALOG_BUTTON || d->id == AV_CANCEL) continue;
        buttons++;
        assert(tiku_icons_find(av_tool_icon(d->id)) != NULL);
        assert(*av_tool_tip(d->id) && *av_label(c, d->id));
        assert(d->rect.w == d->rect.h && d->rect.w >= 30);
    }
    assert(buttons == 5);
    assert(tiku_icons_find("archive_viewer") &&
           tiku_icons_find("archive_file"));
}

/** @brief Every group, over fixtures in a scratch folder removed at the
 *         end; with an argument, a picture of the window to it. */
int main(int argc, char **argv)
{
    tiku_arcview_host_t host = { host_pick, host_open, NULL };
    char zip[PATH_MAX], tgz[PATH_MAX], hostile[PATH_MAX];
    char temp[PATH_MAX], cmd[PATH_MAX + 16];
    const char *tmp = getenv("TMPDIR");
    int dirty = 0;

    snprintf(scratch, sizeof scratch, "%s/tikuarchive-XXXXXX",
             tmp ? tmp : "/tmp");
    assert(mkdtemp(scratch));
    fixture("zip_features", "features.zip", zip);
    fixture("tgz", "pkg.tar.gz", tgz);
    fixture("hostile_zip", "hostile.zip", hostile);

    tiku_surface_t *s = tiku_surface_new(1000, 680, TIKU_C_BACKDROP);
    desk = tiku_workspace_new(s);
    tiku_arcview_t *c = tiku_arcview_new(desk, &dirty, &host);
    assert(c);
    tiku_arcview_open(c);
    assert(c->window && !c->node && !av_enabled(c, AV_EXTRACT_ALL));
    empty_tests(c, s);
    av_action(c, AV_OPEN);
    assert(picks == 1);
    browse_tests(c, zip);
    find_sort_tests(c);
    extract_tests(c);
    narrow_tests(c);
    open_entry_tests(c);
    snprintf(temp, sizeof temp, "%s", opened);
    toolbar_tests(c);
    tiku_workspace_draw(desk);
    if (argc > 1) {
        snprintf(cmd, sizeof cmd, "%s", argv[1]);
        assert(tiku_surface_png(s, cmd) == 0);
    }
    stop_tests(c, tgz);
    running_cancel_tests(c, zip);
    hostile_tests(c, hostile);
    gzip_and_other_tests(c);

    tiku_arcview_close(c);
    settle(c);
    assert(!c->window && !c->node);
    tiku_arcview_destroy(c);
    tiku_workspace_free(desk);
    desk = NULL;
    settle(NULL);
    assert(there(temp));            /* another application's file survives */
    FILE *saved = fopen(temp, "r");
    char saved_text[32];
    assert(saved && fgets(saved_text, sizeof saved_text, saved));
    assert(!strcmp(saved_text, "edited copy\n"));
    fclose(saved);
    tiku_workspace_free(tiku_workspace_new(s));
    tiku_surface_free(s);
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", scratch);
    (void)system(cmd);
    printf("TikuArchive: open, browse, find, sort, extract all, some and "
           "to a folder, open a file, stop, hostile names, gzip PASS\n");
    return 0;
}
