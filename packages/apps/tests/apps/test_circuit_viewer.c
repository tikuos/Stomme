/* test_circuit_viewer.c -- TikuCircuit driven without a desktop: imports
 * and reloads that keep the shown drawing when they fail, sheets, layers,
 * Find, Selection Details, the menus, the confined resolver, first views,
 * large windows, resize, cancellation, and the application's descriptor
 * with its file picker.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "../../apps/circuit/tiku_circuit_window_private.h"
#include "../../apps/circuit/tiku_circuit_source.h"
#include "tiku_app.h"
#include "tiku_icons.h"
#include "tiku_manifest.h"
#include "tiku_clipboard.h"
#include <assert.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>

#define SETTLE_TURNS   10000    /* loop turns before a wait fails */
#define SETTLE_WAIT_US 1000     /* between two of them */
#define WINDOW_ID      42       /* the fake desktop's one window */

extern const tiku_app_export_t tiku_app_v1;

static void request_picker(void *ctx);

/** @brief Turn the loop until @p v has no render or import left, or with
 *         NULL until no job at all is left. */
static void settle(tiku_circuitview_t *v)
{
    for (int i = 0; i < SETTLE_TURNS; i++) {
        tiku_work_collect();
        if (v) {
            tiku_circuitview_tick(v);
        }
        if (v ? !v->busy && !v->needs_render && !v->loading &&
                    !v->needs_load && !v->exporting && !v->saving_view :
                !tiku_work_busy()) {
            return;
        }
        usleep(SETTLE_WAIT_US);
    }
    assert(!"circuit worker timeout");
}

/** @brief What the fake desktop saw the application do. */
typedef struct {
    int opened;     /* windows asked for */
    int frames;     /* frames handed over */
    int closed;     /* windows given back */
    int pickers;    /* file panels asked for */
} host_t;

/** @brief The desktop's open(): TikuCircuit's one window. */
static uint32_t open_window(void *ctx, const char *title, int w, int h)
{
    host_t *host = ctx;

    assert(!strcmp(title, "TikuCircuit") && w > 0 && h > 0);
    host->opened++;
    return WINDOW_ID;
}

/** @brief The desktop's frame(): a frame of that window, counted. */
static int frame(void *ctx, uint32_t id, const uint32_t *pixels, int w,
                 int h)
{
    assert(id == WINDOW_ID && pixels && w > 0 && h > 0);
    ((host_t *)ctx)->frames++;
    return 1;
}

/** @brief The desktop's close(). */
static void close_window(void *ctx, uint32_t id)
{
    assert(id == WINDOW_ID);
    ((host_t *)ctx)->closed++;
}

/** @brief The desktop's pick(): the panel is put up and counted. */
static int choose_file(void *ctx, uint32_t id, int kind, const char *folder,
                       const char *name)
{
    (void)folder;
    (void)name;
    assert(id == WINDOW_ID && (kind == TIKU_APP_PICK_LOCAL_OPEN || kind == TIKU_APP_PICK_SAVE));
    if(kind == TIKU_APP_PICK_SAVE) assert(name && !strcmp(name,"circuit-view.png"));
    ((host_t *)ctx)->pickers++;
    return 1;
}

/** @brief Copy a fixture into a test-owned file; an import only reads. */
static void copy_file(const char *from, const char *to)
{
    FILE *a = fopen(from, "rb"), *b = fopen(to, "wb");

    assert(a && b);
    int c;
    while ((c = fgetc(a)) != EOF) {
        assert(fputc(c, b) != EOF);
    }
    assert(!fclose(a) && !fclose(b));
}

/** @brief Press and release the primary button at (@p x, @p y). */
static void click(tiku_circuitview_t *v, int x, int y)
{
    tiku_event_t e = {.type = TIKU_EVENT_POINTER_DOWN, .button = 1, .x = x,
                      .y = y};

    tiku_circuitview_event(v->window, &e, v);
    e.type = TIKU_EVENT_POINTER_UP;
    tiku_circuitview_event(v->window, &e, v);
}

/** @brief A canvas point whose click selects an object outside layer
 *         @p layer, found as the canvas hit-tests a click.
 *  @return 1 with the point in @p px and @p py, or 0 */
static int point_off_layer(tiku_circuitview_t *v, size_t layer, int *px,
                           int *py)
{
    static const double at[][2] = {{.5, .5}, {.2, .2}, {.8, .2}, {.2, .8},
                                   {.8, .8}};

    for (size_t i = 0; i < tiku_circuit_count(v->document); i++) {
        tiku_circuit_box_t b = tiku_circuit_object(v->document, i)->bounds;
        for (size_t k = 0; k < sizeof at / sizeof *at; k++) {
            double wx = b.x0 + (b.x1 - b.x0) * at[k][0];
            double wy = b.y0 + (b.y1 - b.y0) * at[k][1];
            int x = v->canvas.x + (int)lround(wx * v->view.scale + v->view.x);
            int y = v->canvas.y + (int)lround(wy * v->view.scale + v->view.y);
            double hx, hy;
            if (!cv_inside(v->canvas, x, y) ||
                !tiku_circuit_world(&v->view, x - v->canvas.x,
                                    y - v->canvas.y, &hx, &hy)) {
                continue;
            }
            size_t hit = tiku_circuit_hit(v->document, hx, hy,
                                          CV_HIT_SLOP / v->view.scale);
            if (hit != SIZE_MAX &&
                tiku_circuit_object_layer(v->document, hit) != layer) {
                *px = x;
                *py = y;
                return 1;
            }
        }
    }
    return 0;
}

/** @brief Post the import asked for and land it, and go no further: a
 *         render it leaves wanted is not posted. */
static void land_import(tiku_circuitview_t *v)
{
    tiku_circuitview_files_tick(v);
    for (int i = 0; v->loading && i < SETTLE_TURNS; i++) {
        usleep(SETTLE_WAIT_US);
        tiku_work_collect();
    }
    assert(!v->loading);
}

/** @brief Command @p command's entry in menu @p menu, or NULL. */
static const tiku_menu_entry_t *menu_entry(const tiku_menuset_t *set,
                                           int menu, int command)
{
    for (int i = 0; i < set->menu[menu].nitem; i++) {
        if (set->menu[menu].item[i].command == command) {
            return &set->menu[menu].item[i];
        }
    }
    return NULL;
}

/** @brief Open and Reload replace the drawing whole or not at all, and a
 *         newer request replaces an older one; no job holds UI pointers. */
static void imports(tiku_circuitview_t *v)
{
    const char *fixture = "tests/fixtures/circuit/transforms.kicad_sch";
    char directory[] = "/tmp/tiku-circuit-test-XXXXXX";

    assert(mkdtemp(directory));
    char path[256], fifo[256];
    snprintf(path, sizeof path, "%s/source.kicad_sch", directory);
    snprintf(fifo, sizeof fifo, "%s/pipe", directory);
    copy_file(fixture, path);
    struct stat before, after;
    assert(!stat(path, &before));
    tiku_circuitview_file(v, path);
    settle(v);
    assert(!stat(path, &after) && before.st_size == after.st_size &&
           before.st_mtime == after.st_mtime);
    assert(v->document && tiku_circuit_count(v->document) == 4 &&
           !strcmp(v->path, path));
    assert(v->image.px && v->rendered_generation == v->generation);
    tiku_circuit_t *original = v->document;
    uint32_t *image = v->image.px;
    tiku_circuitview_file(v, "/no/such/schematic");
    settle(v);
    assert(v->document == original && v->image.px == image &&
           !strcmp(v->path, path));
    assert(strstr(v->status, "Previous drawing"));
    assert(!mkfifo(fifo, 0600));
    /* A FIFO is opened nonblocking, so reading one does not wait. */
    tiku_circuitview_file(v, fifo);
    settle(v);
    assert(v->document == original && strstr(v->status, "could not be read"));
    tiku_circuitview_file(v, directory);
    settle(v);
    assert(v->document == original);
    FILE *f = fopen(path, "wb");
    assert(f);
    assert(fputs("(kicad_sch (version 20230121) (image))", f) >= 0 &&
           !fclose(f));
    tiku_circuitview_action(v, CIRCUIT_RELOAD);
    settle(v);
    assert(v->document == original && v->image.px == image &&
           strstr(v->status, "Line 1:"));
    assert(strstr(v->status, "Previous drawing") && !strcmp(v->path, path));
    /* An import's error stays said after an unrelated render lands. */
    tiku_circuitview_action(v, CIRCUIT_IN);
    settle(v);
    assert(strstr(v->status, "Previous drawing"));
    copy_file(fixture, path);
    tiku_circuitview_action(v, CIRCUIT_RELOAD);
    settle(v);
    assert(v->document && !strstr(v->status, "Previous drawing"));
    tiku_circuitview_action(v, CIRCUIT_FIND);
    double scale = v->view.scale;
    tiku_event_t key = {.type = TIKU_EVENT_KEY_DOWN, .key = '+',
                        .text = "+"};
    tiku_circuitview_event(v->window, &key, v);
    assert(!strcmp(v->query.text, "+") && scale == v->view.scale);
    tiku_field_start(&v->query, "u3");
    tiku_circuitview_action(v, CIRCUIT_NEXT);
    settle(v);
    assert(v->selected == 2 && !v->fit);
    /* Find Next from the only match wraps round to it: the view is
     * centred on it again and the status says a match was selected. */
    double centre = v->view.x;
    v->view.x += 100;
    tiku_circuitview_note(v, "");
    tiku_circuitview_action(v, CIRCUIT_NEXT);
    assert(v->selected == 2 && v->view.x == centre &&
           strstr(v->status, "Match selected"));
    tiku_circuitview_file(v, path);
    tiku_circuitview_tick(v);
    tiku_circuitview_file(v, "/no/such/older-request");
    tiku_circuitview_file(v, fixture);
    v->window->frame.w = 460;
    tiku_workspace_draw(v->workspace);
    settle(v);
    assert(!strcmp(v->path, fixture));
    assert(v->image.w == v->canvas.w &&
           v->rendered_generation == v->generation);
    original = v->document;
    tiku_circuitview_file(v, path);
    tiku_circuitview_tick(v);
    tiku_circuitview_action(v, CIRCUIT_CANCEL);
    settle(v);
    assert(v->document == original && !strcmp(v->path, fixture));
    tiku_circuitview_file(v, path);
    tiku_circuitview_tick(v);
    tiku_circuitview_action(v, CIRCUIT_EXAMPLE);
    settle(v);
    assert(!v->path[0] && tiku_circuit_count(v->document) == 8);
    assert(!unlink(path) && !unlink(fifo) && !rmdir(directory));
}

/** @brief EAGLE sheets with search, copy and a refused reload; Gerber
 *         macros, repeats and cut-ins open and reload. */
static void extended_formats(tiku_circuitview_t *v)
{
    char directory[] = "/tmp/tiku-circuit-formats-XXXXXX";

    assert(mkdtemp(directory));
    char path[256];
    snprintf(path, sizeof path, "%s/eagle.sch", directory);
    copy_file("tests/fixtures/circuit/eagle.sch", path);
    tiku_circuitview_file(v, path);
    settle(v);
    assert(v->project && tiku_circuit_project_count(v->project) == 2 &&
           v->image.px);
    assert(strstr(tiku_circuit_warning(v->document), "EAGLE XML"));
    tiku_field_start(&v->query, "DATA");
    tiku_circuitview_action(v, CIRCUIT_NEXT);
    settle(v);
    assert(v->sheet == 1 && v->selected != SIZE_MAX);
    tiku_circuitview_action(v, CIRCUIT_RELOAD);
    settle(v);
    assert(v->sheet == 1 &&
           tiku_circuit_find(v->document, "DATA", 0) != SIZE_MAX);
    tiku_circuitview_sheet(v, 0);
    settle(v);
    tiku_field_start(&v->query, "U1");
    tiku_circuitview_action(v, CIRCUIT_NEXT);
    settle(v);
    assert(v->selected != SIZE_MAX);
    for (size_t i = 0; i < tiku_circuit_count(v->document); i++) {
        if (strstr(tiku_circuit_details(v->document, i), "Pads: 4")) {
            v->selected = i;
            break;
        }
    }
    tiku_circuitview_action(v, CIRCUIT_COPY);
    assert(strstr(tiku_clipboard_text(), "Gate: B") &&
           strstr(tiku_clipboard_text(), "Pads: 4"));
    tiku_circuit_project_t *project = v->project;
    tiku_circuit_t *doc = v->document;
    uint32_t *pixels = v->image.px;
    FILE *f = fopen(path, "wb");
    assert(f);
    assert(fputs("EESchema Schematic File Version 4", f) >= 0 && !fclose(f));
    tiku_circuitview_action(v, CIRCUIT_RELOAD);
    settle(v);
    assert(v->project == project && v->document == doc &&
           v->image.px == pixels && strstr(v->status, "not EAGLE XML"));
    copy_file("tests/fixtures/circuit/eagle.sch", path);
    tiku_circuitview_action(v, CIRCUIT_RELOAD);
    settle(v);
    assert(v->project && !strstr(v->status, "Previous drawing"));
    const char *files[] = {"macros.gbr", "repeats.gbr", "cutins.gbr"};
    const size_t counts[] = {7, 12, 1};
    for (size_t i = 0; i < sizeof files / sizeof *files; i++) {
        char source[256];
        snprintf(source, sizeof source, "tests/fixtures/circuit/%s",
                 files[i]);
        tiku_circuitview_file(v, source);
        settle(v);
        assert(v->document && !v->project &&
               tiku_circuit_count(v->document) == counts[i] && v->image.px);
        if (!i) {
            assert(strstr(tiku_circuit_details(v->document, 0), "macro"));
        }
        tiku_circuitview_action(v, CIRCUIT_RELOAD);
        settle(v);
        assert(tiku_circuit_count(v->document) == counts[i] &&
               v->rendered_generation == v->generation);
    }
    assert(!unlink(path) && !rmdir(directory));
}

/** @brief The resolver's cancel(), true once the test sets it. */
static int cancel_source(void *ctx)
{
    return *(int *)ctx;
}

/** @brief Links stay inside the chosen file's directory; the chosen file
 *         itself may be anywhere. */
static void source_policy(void)
{
    char directory[] = "/tmp/tiku-circuit-path-XXXXXX";

    assert(mkdtemp(directory));
    char root[256], sub[256], main[256], child[256], link[256];
    char dirlink[256], hard[256], fifo[256], large[256];
    snprintf(root, sizeof root, "%s/root", directory);
    assert(!mkdir(root, 0700));
    snprintf(sub, sizeof sub, "%s/root/sub", directory);
    assert(!mkdir(sub, 0700));
    snprintf(main, sizeof main, "%s/root/main.kicad_sch", directory);
    snprintf(child, sizeof child, "%s/root/sub/child.kicad_sch", directory);
    snprintf(link, sizeof link, "%s/root/link", directory);
    snprintf(dirlink, sizeof dirlink, "%s/root/dirlink", directory);
    snprintf(hard, sizeof hard, "%s/root/alias", directory);
    snprintf(fifo, sizeof fifo, "%s/root/pipe", directory);
    snprintf(large, sizeof large, "%s/root/large", directory);
    copy_file("tests/fixtures/circuit/transforms.kicad_sch", main);
    copy_file(main, child);
    assert(!symlink(main, link) && !symlink(sub, dirlink) &&
           !mkfifo(fifo, 0600));
    assert(!linkat(AT_FDCWD, main, AT_FDCWD, hard, 0));
    int fd = open(large, O_CREAT | O_WRONLY | O_EXCL, 0600);
    assert(fd >= 0);
    /* One byte over the resolver's 8 MiB limit. */
    assert(!ftruncate(fd, 8u * 1024u * 1024u + 1) && !close(fd));
    int cancel = 0;
    tiku_circuitview_local_t local = {.directory = -1, .cancel = cancel_source,
                                      .ctx = &cancel};
    tiku_circuit_resolver_t resolver = tiku_circuitview_local_resolver(&local);
    tiku_circuit_source_t source = {0};
    tiku_circuit_diagnostic_t diag = {0};
    assert(!resolver.read(resolver.ctx, NULL, main, &source, &diag));
    char key[128];
    snprintf(key, sizeof key, "%s", source.key);
    assert(!strcmp(source.path, "main.kicad_sch"));
    resolver.release(resolver.ctx, &source);
    assert(!resolver.read(resolver.ctx, "main.kicad_sch",
                          "./sub/../sub/child.kicad_sch", &source, &diag));
    assert(!strcmp(source.path, "sub/child.kicad_sch"));
    resolver.release(resolver.ctx, &source);
    assert(!resolver.read(resolver.ctx, "sub/child.kicad_sch", "../alias",
                          &source, &diag));
    assert(!strcmp(source.key, key));
    resolver.release(resolver.ctx, &source);
    const char *bad[] = {"../outside", "sub/../../outside", "/etc/passwd",
        "https://example.org/file", "C:\\schematic", "${KIPRJMOD}/file",
        "$HOME/file", "sub\\file", "link", "dirlink/child.kicad_sch",
        "pipe", "sub", "", "bad\nname"};
    for (size_t i = 0; i < sizeof bad / sizeof *bad; i++) {
        assert(resolver.read(resolver.ctx, "main.kicad_sch", bad[i],
                             &source, &diag));
        assert(!source.data);
        resolver.release(resolver.ctx, &source);
    }
    assert(resolver.read(resolver.ctx, "main.kicad_sch", "large", &source,
                         &diag) == TIKU_CIRCUIT_LIMIT);
    resolver.release(resolver.ctx, &source);
    cancel = 1;
    assert(resolver.read(resolver.ctx, "main.kicad_sch", "alias", &source,
                         &diag) == TIKU_CIRCUIT_CANCELLED);
    resolver.release(resolver.ctx, &source);
    tiku_circuitview_local_close(&local);
    assert(!unlink(main) && !unlink(child) && !unlink(link) &&
           !unlink(dirlink) && !unlink(hard));
    assert(!unlink(fifo) && !unlink(large) && !rmdir(sub) && !rmdir(root) &&
           !rmdir(directory));
}

/** @brief Search, the list's pointer and keys, and a failed reload keep
 *         the sheet instance shown. */
static void hierarchy(tiku_circuitview_t *v)
{
    char directory[] = "/tmp/tiku-circuit-hierarchy-XXXXXX";

    assert(mkdtemp(directory));
    char root[256], child[256];
    snprintf(root, sizeof root, "%s/hierarchy.kicad_sch", directory);
    snprintf(child, sizeof child, "%s/channel.kicad_sch", directory);
    copy_file("tests/fixtures/circuit/hierarchy.kicad_sch", root);
    copy_file("tests/fixtures/circuit/channel.kicad_sch", child);
    v->window->frame.w = 800;
    v->window->frame.h = 550;
    tiku_circuitview_file(v, root);
    settle(v);
    assert(v->project && tiku_circuit_project_count(v->project) == 3 &&
           v->navigator.w);
    assert(!tiku_circuit_project_incomplete(v->project));
    tiku_field_start(&v->query, "R2");
    tiku_circuitview_action(v, CIRCUIT_NEXT);
    settle(v);
    assert(v->sheet == 2 &&
           !strcmp(tiku_circuit_object(v->document, v->selected)->name,
                   "R2"));
    assert(tiku_list_chosen(&v->sheets) == 2 && v->image.px);
    tiku_circuitview_action(v, CIRCUIT_RELOAD);
    settle(v);
    assert(v->sheet == 2 &&
           tiku_circuit_find(v->document, "R2", 0) != SIZE_MAX);
    tiku_rect_t row = tiku_list_row_rect(&v->sheets, v->sheet_rows, 1);
    tiku_event_t e = {.type = TIKU_EVENT_POINTER_DOWN, .button = 1,
                      .x = row.x + 8, .y = row.y + 8};
    tiku_circuitview_event(v->window, &e, v);
    settle(v);
    assert(v->sheet == 1);
    e = (tiku_event_t){.type = TIKU_EVENT_KEY_DOWN, .key = TIKU_KEY_UP};
    tiku_circuitview_event(v->window, &e, v);
    settle(v);
    assert(v->sheet == 0);
    e.key = TIKU_KEY_END;
    tiku_circuitview_event(v->window, &e, v);
    settle(v);
    assert(v->sheet == 2);
    v->window->frame.w = 360;
    tiku_workspace_draw(v->workspace);
    settle(v);
    assert(!v->navigator.w);
    e.key = '[';
    e.modifiers = TIKU_MOD_CMD;
    tiku_circuitview_event(v->window, &e, v);
    settle(v);
    assert(v->sheet == 1);
    tiku_circuitview_action(v, CIRCUIT_SHEETS);
    assert(!v->show_sheets);
    v->window->frame.w = 800;
    tiku_workspace_draw(v->workspace);
    settle(v);
    assert(!v->navigator.w);
    tiku_circuitview_action(v, CIRCUIT_SHEETS);
    settle(v);
    assert(v->navigator.w);
    tiku_circuit_project_t *old = v->project;
    tiku_circuit_t *doc = v->document;
    uint32_t *image = v->image.px;
    assert(!unlink(child));
    tiku_circuitview_action(v, CIRCUIT_RELOAD);
    settle(v);
    assert(v->project == old && v->document == doc && v->sheet == 1 &&
           v->image.px == image);
    assert(strstr(v->status, "Reload failed") &&
           strstr(v->status, "Previous drawing"));
    /* File > Open of the project shown opens it as any file is opened:
     * its unavailable sheet is marked, where Reload refused it. */
    int picks = 0;
    v->choose = request_picker;
    v->choose_ctx = &picks;
    tiku_circuitview_action(v, CIRCUIT_OPEN);
    tiku_circuitview_picked(v, root);
    settle(v);
    assert(picks == 1 && tiku_circuit_project_incomplete(v->project) &&
           v->sheet == 0 && !strstr(v->status, "Reload failed"));
    v->choose = NULL;
    v->choose_ctx = NULL;
    tiku_circuitview_action(v, CIRCUIT_EMPTY);
    tiku_circuitview_file(v, root);
    settle(v);
    assert(v->project && tiku_circuit_project_incomplete(v->project));
    tiku_circuitview_sheet(v, 1);
    settle(v);
    assert(!v->document && !v->image.px &&
           strstr(v->status, "Unavailable:"));
    tiku_workspace_draw(v->workspace);
    assert(tiku_circuitview_enabled(v, CIRCUIT_FIND) &&
           tiku_circuitview_enabled(v, CIRCUIT_EMPTY));
    tiku_field_start(&v->query, "R2");
    tiku_circuitview_action(v, CIRCUIT_NEXT);
    assert(strstr(v->status, "Unavailable sheets"));
    tiku_field_start(&v->query, "Channel A");
    tiku_circuitview_action(v, CIRCUIT_NEXT);
    settle(v);
    assert(v->document && v->sheet == 0);
    copy_file("tests/fixtures/circuit/channel.kicad_sch", child);
    tiku_circuitview_action(v, CIRCUIT_RELOAD);
    settle(v);
    assert(!tiku_circuit_project_incomplete(v->project));
    for (int i = 0; i < 8; i++) {
        tiku_circuitview_sheet(v, (size_t)i % 3);
        tiku_circuitview_tick(v);
    }
    settle(v);
    assert(v->sheet == 1 && v->generation == v->rendered_generation);
    tiku_circuitview_action(v, CIRCUIT_EXAMPLE);
    settle(v);
    assert(!v->project && !v->navigator.w && !v->path[0]);
    assert(!unlink(root) && !unlink(child) && !rmdir(directory));
}

/** @brief A hierarchy longer than the list: its scrollbar, wheel and
 *         keys, not only its drawing. */
static void many_sheets(tiku_circuitview_t *v)
{
    char directory[] = "/tmp/tiku-circuit-list-XXXXXX";

    assert(mkdtemp(directory));
    char root[256], child[256];
    snprintf(root, sizeof root, "%s/root.kicad_sch", directory);
    snprintf(child, sizeof child, "%s/page.kicad_sch", directory);
    FILE *f = fopen(root, "wb");
    assert(f);
    fputs("(kicad_sch (version 20230121) "
          "(uuid 11111111-1111-4111-8111-111111111111) ", f);
    for (int i = 0; i < 63; i++) {
        fprintf(f, "(sheet (at 10 10) (size 5 5) "
                "(uuid %08d-2222-4222-8222-222222222222) "
                "(property \"Sheetname\" \"Child %d\" (effects hide)) "
                "(property \"Sheetfile\" \"page.kicad_sch\" "
                "(effects hide))) ", i + 1, i + 1);
    }
    assert(fputs(")", f) >= 0 && !fclose(f));
    f = fopen(child, "wb");
    assert(f);
    assert(fputs("(kicad_sch (version 20230121))", f) >= 0 && !fclose(f));
    v->window->frame.w = 800;
    v->window->frame.h = 400;
    tiku_circuitview_file(v, root);
    settle(v);
    tiku_workspace_draw(v->workspace);
    assert(tiku_circuit_project_count(v->project) == 64 &&
           v->sheet_bar.w == CV_BAR);
    tiku_event_t e = {.type = TIKU_EVENT_WHEEL, .dy = 1,
                      .x = v->sheet_rows.x + 5, .y = v->sheet_rows.y + 5};
    tiku_circuitview_event(v->window, &e, v);
    assert(v->sheets.top == CV_WHEEL_ROWS);
    e = (tiku_event_t){.type = TIKU_EVENT_POINTER_DOWN, .button = 1,
                       .x = v->sheet_bar.x + 6,
                       .y = v->sheet_bar.y + v->sheet_bar.h - 6};
    tiku_circuitview_event(v->window, &e, v);
    assert(v->sheets.top == CV_WHEEL_ROWS + 1);
    e.type = TIKU_EVENT_POINTER_UP;
    tiku_circuitview_event(v->window, &e, v);
    int track = v->sheet_bar.h - 2 * CV_BAR, pos, thick;
    assert(tiku_scroll_thumb(&v->sheet_scroll, track, &pos, &thick));
    e.type = TIKU_EVENT_POINTER_DOWN;
    e.y = v->sheet_bar.y + CV_BAR + pos + thick / 2;
    tiku_circuitview_event(v->window, &e, v);
    assert(v->sheet_scroll.tracking);
    e.type = TIKU_EVENT_POINTER_MOVE;
    e.y = v->sheet_bar.y + v->sheet_bar.h - CV_BAR;
    tiku_circuitview_event(v->window, &e, v);
    assert(v->sheets.top == tiku_scroll_max(&v->sheet_scroll));
    e.type = TIKU_EVENT_POINTER_UP;
    tiku_circuitview_event(v->window, &e, v);
    e = (tiku_event_t){.type = TIKU_EVENT_KEY_DOWN, .key = TIKU_KEY_END};
    tiku_circuitview_event(v->window, &e, v);
    settle(v);
    assert(v->sheet == 63);
    e.key = TIKU_KEY_HOME;
    tiku_circuitview_event(v->window, &e, v);
    settle(v);
    assert(v->sheet == 0 && v->sheets.top == 0);
    tiku_circuitview_action(v, CIRCUIT_EMPTY);
    assert(!unlink(root) && !unlink(child) && !rmdir(directory));
}

/** @brief Selection Details scrolls and copies without taking Find's keys
 *         or the canvas's wheel. */
static void inspection(tiku_circuitview_t *v)
{
    v->window->frame.w = 800;
    v->window->frame.h = 550;
    tiku_circuitview_file(v, "tests/fixtures/circuit/labels.kicad_sch");
    settle(v);
    v->selected = tiku_circuit_find(v->document, "U1", 0);
    tiku_workspace_draw(v->workspace);
    settle(v);
    assert(v->inspector.w &&
           strstr(v->inspect_text, "Do not populate: Yes"));
    assert(v->inspect_bar.w && tiku_scroll_max(&v->inspect_scroll) > 0);
    assert(tiku_circuitview_enabled(v, CIRCUIT_COPY));
    tiku_circuitview_action(v, CIRCUIT_COPY);
    assert(!strcmp(tiku_clipboard_text(), v->inspect_text));
    assert(!strcmp(v->status, "Selection details copied."));
    assert(strstr(tiku_clipboard_text(),
                  "https://example.invalid/test.pdf"));
    tiku_circuit_view_t view = v->view;
    tiku_event_t e = {.type = TIKU_EVENT_WHEEL, .dy = 1,
                      .x = v->inspect_rows.x + 5,
                      .y = v->inspect_rows.y + 5};
    assert(tiku_circuitview_event(v->window, &e, v));
    assert(v->inspect_scroll.value > 0 &&
           !memcmp(&view, &v->view, sizeof view));
    e.type = TIKU_EVENT_POINTER_DOWN;
    e.button = 1;
    tiku_circuitview_event(v->window, &e, v);
    assert(v->dialog.focus == CIRCUIT_INSPECTOR);
    e = (tiku_event_t){.type = TIKU_EVENT_KEY_DOWN, .key = TIKU_KEY_END};
    tiku_circuitview_event(v->window, &e, v);
    assert(v->inspect_scroll.value == tiku_scroll_max(&v->inspect_scroll));
    e.key = TIKU_KEY_HOME;
    tiku_circuitview_event(v->window, &e, v);
    assert(!v->inspect_scroll.value);
    e = (tiku_event_t){.type = TIKU_EVENT_POINTER_DOWN, .button = 1,
                       .x = v->inspect_bar.x + 7,
                       .y = v->inspect_bar.y + v->inspect_bar.h - 4};
    tiku_circuitview_event(v->window, &e, v);
    assert(v->inspect_scroll.tracking && v->inspect_scroll.arrow_dir == 1);
    e.type = TIKU_EVENT_POINTER_UP;
    tiku_circuitview_event(v->window, &e, v);
    assert(!v->inspect_scroll.tracking && !v->inspect_scroll.arrow_dir);
    tiku_scroll_to(&v->inspect_scroll, 0);
    int track = v->inspect_bar.h - 2 * CV_BAR, pos, thick;
    assert(tiku_scroll_thumb(&v->inspect_scroll, track, &pos, &thick));
    e.type = TIKU_EVENT_POINTER_DOWN;
    e.y = v->inspect_bar.y + CV_BAR + pos + thick / 2;
    tiku_circuitview_event(v->window, &e, v);
    assert(v->inspect_scroll.tracking);
    e.type = TIKU_EVENT_POINTER_MOVE;
    e.y = v->inspect_bar.y + v->inspect_bar.h - CV_BAR;
    tiku_circuitview_event(v->window, &e, v);
    assert(v->inspect_scroll.value == tiku_scroll_max(&v->inspect_scroll));
    e.type = TIKU_EVENT_POINTER_UP;
    tiku_circuitview_event(v->window, &e, v);
    /* In Find, the copy shortcut copies the field's text; on the canvas
     * and the panel it copies the details. */
    tiku_field_start(&v->query, "search text");
    tiku_circuitview_action(v, CIRCUIT_FIND);
    e = (tiku_event_t){.type = TIKU_EVENT_KEY_DOWN, .key = 'c',
                       .modifiers = TIKU_MOD_CMD};
    tiku_circuitview_event(v->window, &e, v);
    assert(!strcmp(tiku_clipboard_text(), "search text"));
    v->dialog.focus = -1;
    tiku_circuitview_event(v->window, &e, v);
    assert(strstr(tiku_clipboard_text(), "Component: U1"));
    tiku_circuitview_action(v, CIRCUIT_DETAILS);
    settle(v);
    assert(!v->inspector.w);
    tiku_circuitview_action(v, CIRCUIT_COPY);
    assert(strstr(tiku_clipboard_text(), "Footprint: Fixture:Test_Footprint"));
    tiku_circuitview_action(v, CIRCUIT_DETAILS);
    settle(v);
    assert(v->inspector.w);
    v->window->frame.w = 280;
    tiku_workspace_draw(v->workspace);
    settle(v);
    assert(!v->inspector.w && !v->inspect_scroll.tracking);
    tiku_circuitview_action(v, CIRCUIT_COPY);
    assert(strstr(tiku_clipboard_text(), "Component: U1"));
    tiku_circuitview_action(v, CIRCUIT_EMPTY);
    tiku_workspace_draw(v->workspace);
    settle(v);
    assert(!tiku_circuitview_enabled(v, CIRCUIT_COPY) &&
           !strstr(v->inspect_text, "Component: U1"));
    v->window->frame.w = 800;
}

/** @brief A drill reload that fails keeps the drawing and selection; one
 *         that succeeds replaces them. */
static void drill_reload(tiku_circuitview_t *v)
{
    char directory[] = "/tmp/tiku-drill-reload-XXXXXX";

    assert(mkdtemp(directory));
    char path[256];
    snprintf(path, sizeof path, "%s/test.drl", directory);
    copy_file("tests/fixtures/circuit/drills.drl", path);
    tiku_circuitview_file(v, path);
    settle(v);
    assert(v->document && !v->project && tiku_circuit_count(v->document) == 4);
    tiku_circuit_t *previous = v->document;
    uint32_t *pixels = v->image.px;
    v->selected = 0;
    FILE *f = fopen(path, "wb");
    assert(f);
    assert(fputs("M48\nMETRIC,LZ\n%\nM30", f) >= 0 && !fclose(f));
    tiku_circuitview_action(v, CIRCUIT_RELOAD);
    settle(v);
    assert(v->document == previous && v->image.px == pixels &&
           v->selected == 0);
    assert(strstr(v->status, "Previous drawing is still displayed"));
    copy_file("tests/fixtures/circuit/drills-routed.drl", path);
    tiku_circuitview_action(v, CIRCUIT_RELOAD);
    settle(v);
    assert(v->document && !v->project && tiku_circuit_count(v->document) == 4);
    assert(!unlink(path) && !rmdir(directory));
}

/** @brief The same for one Gerber layer, with its details copied. */
static void artwork_imports(tiku_circuitview_t *v)
{
    char directory[] = "/tmp/tiku-gerber-reload-XXXXXX";

    assert(mkdtemp(directory));
    char path[256];
    snprintf(path, sizeof path, "%s/test.gbr", directory);
    copy_file("tests/fixtures/circuit/artwork.gbr", path);
    tiku_circuitview_file(v, path);
    settle(v);
    assert(v->document && !v->project && tiku_circuit_count(v->document) == 4);
    assert(strstr(tiku_circuit_warning(v->document), "Gerber preview"));
    v->selected = 0;
    tiku_workspace_draw(v->workspace);
    tiku_circuitview_action(v, CIRCUIT_COPY);
    assert(strstr(tiku_clipboard_text(), "Aperture: D10") &&
           strstr(tiku_clipboard_text(), "Copper,L1,Top"));
    tiku_circuit_t *previous = v->document;
    uint32_t *pixels = v->image.px;
    FILE *f = fopen(path, "wb");
    assert(f && fputs("%AMunsupported*%M02*", f) >= 0 && !fclose(f));
    tiku_circuitview_action(v, CIRCUIT_RELOAD);
    settle(v);
    assert(v->document == previous && v->image.px == pixels &&
           v->selected == 0);
    assert(strstr(v->status, "Previous drawing is still displayed"));
    copy_file("tests/fixtures/circuit/polarity.gbr", path);
    tiku_circuitview_action(v, CIRCUIT_RELOAD);
    settle(v);
    assert(v->document && !v->project && tiku_circuit_count(v->document) == 6);
    assert(!unlink(path) && !rmdir(directory));
}

/** @brief The window's file picker: counts the asks, answers none. */
static void request_picker(void *ctx)
{
    (*(int *)ctx)++;
}

/** @brief Layer stacks: the Layers commands, Reload keeping display
 *         settings, Add Layer and Open Gerber Job through the picker, and
 *         a partial job. */
static void layer_workflow(tiku_circuitview_t *v)
{
    v->window->frame.w = 800;
    v->window->frame.h = 550;
    int picks = 0;
    v->choose = request_picker;
    v->choose_ctx = &picks;
    tiku_circuitview_file(v, "tests/fixtures/circuit/stack.gbrjob");
    settle(v);
    assert(tiku_circuit_layer_count(v->document) == 3 &&
           !tiku_circuit_stack_incomplete(v->document));
    assert(tiku_circuit_count(v->document) == 14 && v->navigator.w);
    tiku_circuitview_sheet(v, 1);
    settle(v);
    tiku_circuit_view_t view = v->view;
    tiku_circuitview_action(v, CIRCUIT_LAYER_OPACITY);
    settle(v);
    tiku_circuitview_action(v, CIRCUIT_LAYER_COLOUR);
    settle(v);
    const tiku_circuit_layer_t *l = tiku_circuit_layer(v->document, 1);
    unsigned colour = l->colour;
    assert(l->opacity == 192);
    tiku_circuitview_action(v, CIRCUIT_LAYER_VISIBLE);
    settle(v);
    assert(tiku_circuit_count(v->document) == 10 &&
           !tiku_circuit_layer(v->document, 1)->visible);
    assert(!memcmp(&view, &v->view, sizeof view));
    tiku_circuitview_action(v, CIRCUIT_LAYER_UP);
    settle(v);
    assert(v->sheet == 0 &&
           !strcmp(tiku_circuit_layer(v->document, 0)->path, "artwork.gbr"));
    tiku_circuitview_action(v, CIRCUIT_LAYER_DOWN);
    settle(v);
    assert(v->sheet == 1);
    tiku_circuitview_action(v, CIRCUIT_RELOAD);
    settle(v);
    l = tiku_circuit_layer(v->document, 1);
    assert(v->sheet == 1 && l->opacity == 192 && l->colour == colour &&
           !l->visible);
    tiku_circuitview_action(v, CIRCUIT_LAYER_VISIBLE);
    settle(v);
    tiku_field_start(&v->query, "D10");
    tiku_circuitview_action(v, CIRCUIT_NEXT);
    settle(v);
    assert(v->selected != SIZE_MAX &&
           v->sheet == tiku_circuit_object_layer(v->document, v->selected));
    /* A click on an object of another layer selects that layer, and the
     * status line names it. */
    tiku_circuitview_sheet(v, 0);
    tiku_workspace_draw(v->workspace);
    settle(v);
    int x, y;
    assert(point_off_layer(v, 0, &x, &y));
    click(v, x, y);
    char named[32];
    snprintf(named, sizeof named, "Layer %zu/%zu", v->sheet + 1,
             tiku_circuit_layer_count(v->document));
    assert(v->sheet != 0 && strstr(v->status, named));
    /* The desktop does not answer a panel that was cancelled: an ask left
     * unanswered disables nothing, and the list's boxes still work. */
    tiku_circuitview_action(v, CIRCUIT_ADD_LAYER);
    assert(picks == 1 && v->picker_action == CIRCUIT_ADD_LAYER);
    assert(tiku_circuitview_enabled(v, CIRCUIT_OPEN) &&
           tiku_circuitview_enabled(v, CIRCUIT_OPEN_JOB) &&
           tiku_circuitview_enabled(v, CIRCUIT_ADD_LAYER) &&
           tiku_circuitview_enabled(v, CIRCUIT_LAYER_REMOVE));
    int shown = tiku_circuit_layer(v->document, v->sheet)->visible;
    tiku_rect_t row = tiku_list_row_rect(&v->sheets, v->sheet_rows,
                                         (int)v->sheet);
    click(v, row.x + 4, row.y + 4);
    settle(v);
    assert(tiku_circuit_layer(v->document, v->sheet)->visible == !shown);
    tiku_circuitview_action(v, CIRCUIT_LAYER_VISIBLE);
    settle(v);
    assert(tiku_circuit_layer(v->document, v->sheet)->visible == shown);
    tiku_circuit_t *old = v->document;
    uint32_t *pixels = v->image.px;
    tiku_circuitview_action(v, CIRCUIT_ADD_LAYER);
    assert(picks == 2);
    tiku_circuitview_picked(v,
                            "tests/fixtures/circuit/transforms.kicad_sch");
    settle(v);
    assert(v->document == old && v->image.px == pixels);
    tiku_circuitview_action(v, CIRCUIT_ADD_LAYER);
    tiku_circuitview_picked(v, "tests/fixtures/circuit/drills-routed.drl");
    settle(v);
    assert(tiku_circuit_layer_count(v->document) == 4 && v->sheet == 0);
    assert(!strcmp(v->path, "tests/fixtures/circuit/stack.gbrjob"));
    tiku_circuitview_action(v, CIRCUIT_LAYER_REMOVE);
    settle(v);
    assert(tiku_circuit_layer_count(v->document) == 3);
    old = v->document;
    pixels = v->image.px;
    tiku_circuitview_action(v, CIRCUIT_OPEN_JOB);
    tiku_circuitview_picked(v, "tests/fixtures/circuit/artwork.gbr");
    settle(v);
    assert(v->document == old && v->image.px == pixels);
    tiku_circuitview_file(v, "tests/fixtures/circuit/partial.gbrjob");
    settle(v);
    assert(tiku_circuit_layer_count(v->document) == 3 &&
           tiku_circuit_stack_incomplete(v->document));
    assert(tiku_circuit_layer(v->document, 0)->scene &&
           !tiku_circuit_layer(v->document, 1)->scene);
    assert(!tiku_circuit_layer(v->document, 2)->scene);
    old = v->document;
    pixels = v->image.px;
    tiku_circuitview_action(v, CIRCUIT_RELOAD);
    settle(v);
    assert(v->document == old && v->image.px == pixels);
    tiku_circuitview_sheet(v, 2);
    tiku_workspace_draw(v->workspace);
    assert(strstr(v->status, "Unavailable") &&
           strstr(v->inspect_text, "../outside.gbr"));

    char directory[] = "/tmp/tiku-layer-reload-XXXXXX";
    assert(mkdtemp(directory));
    char job[256], art[256];
    snprintf(job, sizeof job, "%s/test.gbrjob", directory);
    snprintf(art, sizeof art, "%s/art.gbr", directory);
    FILE *f = fopen(job, "wb");
    assert(f);
    assert(fputs("{\"FilesAttributes\":[{\"Path\":\"art.gbr\","
                 "\"FileFormat\":\"Gerber\"}]}", f) >= 0 && !fclose(f));
    copy_file("tests/fixtures/circuit/artwork.gbr", art);
    tiku_circuitview_file(v, job);
    settle(v);
    old = v->document;
    pixels = v->image.px;
    assert(tiku_circuit_layer(v->document, 0)->format ==
           TIKU_CIRCUIT_LAYER_GERBER);
    copy_file("tests/fixtures/circuit/drills.drl", art);
    tiku_circuitview_action(v, CIRCUIT_RELOAD);
    settle(v);
    assert(v->document == old && v->image.px == pixels &&
           strstr(v->status, "conflicts"));
    copy_file("tests/fixtures/circuit/curved-region.gbr", art);
    tiku_circuitview_action(v, CIRCUIT_RELOAD);
    settle(v);
    assert(v->document != old && tiku_circuit_count(v->document) == 4);
    assert(!unlink(job) && !unlink(art) && !rmdir(directory));
    /* A stack opened from one file is named after it until that layer is
     * removed, and then after the front layer left. */
    tiku_circuitview_file(v, "tests/fixtures/circuit/artwork.gbr");
    settle(v);
    tiku_circuitview_action(v, CIRCUIT_ADD_LAYER);
    tiku_circuitview_picked(v, "tests/fixtures/circuit/drills-routed.drl");
    settle(v);
    assert(tiku_circuit_layer_count(v->document) == 2 &&
           !strcmp(v->path, "tests/fixtures/circuit/artwork.gbr"));
    tiku_circuitview_sheet(v, 1);
    tiku_circuitview_action(v, CIRCUIT_LAYER_REMOVE);
    settle(v);
    assert(tiku_circuit_layer_count(v->document) == 1 &&
           !strcmp(v->path, "tests/fixtures/circuit/drills-routed.drl"));
    /* An answer is applied as its ask meant it only when that still can
     * be: Add Layer asked, then the example opened, adds nothing. */
    tiku_circuitview_action(v, CIRCUIT_ADD_LAYER);
    tiku_circuitview_action(v, CIRCUIT_EXAMPLE);
    tiku_circuitview_picked(v, "tests/fixtures/circuit/drills.drl");
    assert(!v->needs_load && strstr(v->status, "Add Layer needs") &&
           strstr(v->status, "was not added"));
    settle(v);
    assert(tiku_circuit_count(v->document) == 8 &&
           !tiku_circuit_layer_count(v->document) && !v->path[0]);
    /* The worker refuses a layer added to no stack in its own words. */
    tiku_circuit_import_t options = {0};
    tiku_circuit_diagnostic_t diag = {0};
    tiku_circuit_t *added = NULL;
    assert(tiku_circuitview_layer_load(v->document, 1,
               "tests/fixtures/circuit/drills.drl", &options, &added,
               &diag) && !added);
    assert(strstr(diag.message, "Add Layer needs"));
    v->choose = NULL;
    v->choose_ctx = NULL;
}

/** @brief A document's first view is rendered for the canvas it is shown
 *         in, with its list and Selection Details: opening renders once. */
static void first_view(tiku_circuitview_t *v)
{
    const char *files[] = {"tests/fixtures/circuit/basic.kicad_sch",
                           "tests/fixtures/circuit/hierarchy.kicad_sch",
                           "tests/fixtures/circuit/stack.gbrjob",
                           "tests/fixtures/circuit/drills.drl"};

    v->window->frame.w = 800;
    v->window->frame.h = 550;
    tiku_circuitview_action(v, CIRCUIT_EMPTY);
    tiku_workspace_draw(v->workspace);
    settle(v);
    for (size_t i = 0; i < sizeof files / sizeof *files; i++) {
        tiku_circuitview_file(v, files[i]);
        land_import(v);
        assert(v->document && v->image.px && !v->needs_render &&
               v->rendered_generation == v->generation);
        assert(v->image.w == v->canvas.w && v->image.h == v->canvas.h);
        settle(v);
    }
}

/** @brief Show basic.kicad_sch in an empty window of @p w by @p h. */
static void open_basic(tiku_circuitview_t *v, int w, int h)
{
    v->window->frame.w = w;
    v->window->frame.h = h;
    tiku_circuitview_action(v, CIRCUIT_EMPTY);
    tiku_workspace_draw(v->workspace);
    settle(v);
    tiku_circuitview_file(v, "tests/fixtures/circuit/basic.kicad_sch");
    settle(v);
    assert(v->document && v->image.px && !strstr(v->status, "exceeds"));
    assert(v->rendered_generation == v->generation);
    assert((size_t)v->image.w * v->image.h <= TIKU_CIRCUIT_RENDER_PIXELS);
}

/** @brief A window larger than the kit renders at once opens a drawing,
 *         and one maximised that large with a drawing shown renders and
 *         fits it: the image is the largest the kit renders, scaled up. */
static void large_window(tiku_circuitview_t *v)
{
    open_basic(v, 2900, 1800);
    open_basic(v, 3840, 2140);
    assert((size_t)v->canvas.w * v->canvas.h > TIKU_CIRCUIT_RENDER_PIXELS &&
           v->image.w < v->canvas.w);
    open_basic(v, 800, 550);
    v->window->frame.w = 3840;
    v->window->frame.h = 2140;
    tiku_workspace_draw(v->workspace);
    settle(v);
    assert(v->rendered_generation == v->generation &&
           !strstr(v->status, "exceeds") && v->image.w < v->canvas.w);
    tiku_circuitview_action(v, CIRCUIT_IN);
    settle(v);
    assert(!v->fit && v->rendered_generation == v->generation);
    tiku_circuitview_action(v, CIRCUIT_FIT);
    settle(v);
    assert(v->fit && v->rendered_generation == v->generation &&
           !strstr(v->status, "exceeds"));
    /* The smaller image is drawn scaled up to fill the canvas. */
    tiku_rect_t content = tiku_window_content(v->window);
    tiku_surface_t *s = tiku_surface_new(content.x + content.w,
                                         content.y + content.h,
                                         TIKU_C_PANEL);
    assert(s);
    tiku_circuitview_draw(v->window, s, content, v);
    int corner_x = v->canvas.x + v->canvas.w - 10;
    int corner_y = v->canvas.y + v->canvas.h - 10;
    assert(s->px[(size_t)corner_y * s->w + corner_x] != CV_PAPER);
    tiku_surface_free(s);
    v->window->frame.w = 800;
    v->window->frame.h = 550;
    tiku_workspace_draw(v->workspace);
    settle(v);
}

/** @brief The menus: File, Edit, View and Layers; Open Gerber Job...
 *         before Close; Copy and Find under Edit; the keys that copy, fit
 *         and zoom shown beside them. */
static void menus(tiku_circuitview_t *v)
{
    const tiku_menuset_t *set = tiku_window_menus(v->window);
    const char *const titles[] = {"File", "Edit", "View", "Layers"};

    assert(set && set->nmenu == 5);
    for (int i = 0; i < 4; i++) {
        assert(!strcmp(set->menu[i].title, titles[i]));
    }
    const tiku_menu_entry_t *job = menu_entry(set, 0, CIRCUIT_OPEN_JOB);
    const tiku_menu_entry_t *close = menu_entry(set, 0, CIRCUIT_CLOSE);
    assert(job && close && job < close);
    const tiku_menu_entry_t *copy = menu_entry(set, 1, CIRCUIT_COPY);
    assert(copy && copy->sc == 'c' && copy->mods == TIKU_MOD_CMD);
    assert(menu_entry(set, 1, CIRCUIT_FIND) &&
           menu_entry(set, 1, CIRCUIT_NEXT));
    const tiku_menu_entry_t *in = menu_entry(set, 2, CIRCUIT_IN);
    const tiku_menu_entry_t *out = menu_entry(set, 2, CIRCUIT_OUT);
    const tiku_menu_entry_t *fit = menu_entry(set, 2, CIRCUIT_FIT);
    assert(in && in->sc == '+' && !in->mods);
    assert(out && out->sc == '-' && !out->mods);
    assert(fit && fit->sc == '0' && !fit->mods);
}

static void key_event(tiku_circuitview_t *v,int key)
{
    tiku_event_t e={.type=TIKU_EVENT_KEY_DOWN,.key=key};
    tiku_circuitview_event(v->window,&e,v);
}

static void inspection_tools(tiku_circuitview_t *v)
{
    v->window->frame.w=800; v->window->frame.h=550;
    int picks=0; tiku_circuitview_picker(v,request_picker,&picks);
    tiku_circuitview_file(v,"tests/fixtures/circuit/stack.gbrjob"); settle(v);
    tiku_circuitview_action(v,CIRCUIT_MEASURE);
    assert(v->measure);
    double scale=v->view.scale;
    int x=v->canvas.x+20,y=v->canvas.y+20;
    click(v,x,y); click(v,x+30,y+40);
    assert(v->measure_points==2 && !v->dragging);
    assert(fabs(hypot(v->measure_x[1]-v->measure_x[0],v->measure_y[1]-v->measure_y[0])-50/scale)<1e-8);
    tiku_circuitview_action(v,CIRCUIT_MEASURE_COPY);
    assert(strstr(tiku_clipboard_text(),"Distance") && strstr(tiku_clipboard_text(),"mm"));
    char expected[64]; snprintf(expected,sizeof expected,"%.4f",50/scale/25.4);
    tiku_circuitview_action(v,CIRCUIT_UNIT_INCH);
    tiku_circuitview_action(v,CIRCUIT_MEASURE_COPY);
    assert(strstr(tiku_clipboard_text(),expected) && strstr(tiku_clipboard_text(),"in"));
    tiku_circuitview_action(v,CIRCUIT_UNIT_MIL);
    tiku_circuitview_action(v,CIRCUIT_MEASURE_COPY);
    assert(strstr(tiku_clipboard_text(),"mil"));
    double point=v->measure_x[0];
    key_event(v,TIKU_KEY_RIGHT); settle(v);
    assert(v->measure_points==2 && v->measure_x[0]==point);
    tiku_workspace_draw(v->workspace);
    key_event(v,TIKU_KEY_ESCAPE); assert(!v->measure_points && v->measure);
    key_event(v,TIKU_KEY_ESCAPE); assert(!v->measure);
    tiku_circuitview_action(v,CIRCUIT_FIND);
    tiku_event_t typed={.type=TIKU_EVENT_KEY_DOWN,.key='m',.text="m"};
    tiku_circuitview_event(v->window,&typed,v); assert(!v->measure);
    v->dialog.focus=-1;

    tiku_circuit_t *before=tiku_circuit_retain(v->document);
    tiku_circuitview_action(v,CIRCUIT_LAYER_STYLE); assert(v->style_open);
    assert(!tiku_circuitview_enabled(v,CIRCUIT_OPEN));
    tiku_field_start(&v->colour_text,"invalid"); key_event(v,TIKU_KEY_RETURN);
    assert(v->style_open && v->style_error[0] && v->document==before);
    tiku_field_start(&v->colour_text,"#123ABC"); tiku_field_start(&v->opacity_text,"101");
    key_event(v,TIKU_KEY_RETURN); assert(v->style_open && v->document==before);
    tiku_field_start(&v->opacity_text,"50"); key_event(v,TIKU_KEY_RETURN); settle(v);
    assert(!v->style_open && v->document!=before);
    assert(tiku_circuit_layer(v->document,0)->colour==0xff123abc && tiku_circuit_layer(v->document,0)->opacity==128);
    assert(tiku_circuit_layer(before,0)->opacity==255); tiku_circuit_release(before);
    tiku_circuitview_action(v,CIRCUIT_LAYER_STYLE);
    tiku_field_start(&v->colour_text,"#FFFFFF"); key_event(v,TIKU_KEY_ESCAPE);
    assert(!v->style_open && tiku_circuit_layer(v->document,0)->colour==0xff123abc);
    tiku_circuitview_action(v,CIRCUIT_LAYER_ISOLATE); settle(v);
    assert(tiku_circuit_layer(v->document,0)->visible && !tiku_circuit_layer(v->document,1)->visible);
    tiku_circuitview_action(v,CIRCUIT_LAYER_SHOW_ALL); settle(v);
    assert(tiku_circuit_layer(v->document,1)->visible);

    char directory[]="/tmp/tiku-circuit-tools-XXXXXX"; assert(mkdtemp(directory));
    char output[512],alias[512]; snprintf(output,sizeof output,"%s/view.png",directory);
    tiku_circuitview_action(v,CIRCUIT_EXPORT); assert(tiku_circuitview_picking_export(v));
    tiku_circuitview_picked(v,output); settle(v);
    tiku_image_t image={0}; assert(tiku_image_load(output,&image)==TIKU_IMG_OK);
    assert(image.w==v->image.w && image.h>v->image.h);
    assert(!memcmp(image.px,v->image.px,(size_t)v->image.w*v->image.h*sizeof(uint32_t)));
    tiku_image_free(&image);
    tiku_circuitview_action(v,CIRCUIT_EXPORT); tiku_circuitview_picked(v,output); settle(v);
    assert(strstr(v->status,"PNG exported"));
    tiku_circuitview_action(v,CIRCUIT_EXPORT); tiku_circuitview_action(v,CIRCUIT_IN);
    tiku_circuitview_picked(v,output); assert(strstr(v->status,"view changed")); settle(v);
    snprintf(alias,sizeof alias,"%s/source.png",directory);
    copy_file("tests/fixtures/circuit/artwork.gbr",alias);
    struct stat old,now; assert(!stat(alias,&old));
    tiku_circuitview_action(v,CIRCUIT_EXPORT); tiku_circuitview_picked(v,alias); settle(v);
    assert(strstr(v->status,"Export refused") && !stat(alias,&now) && old.st_size==now.st_size);
    assert(!unlink(alias)); assert(!mkfifo(alias,0600));
    tiku_circuitview_action(v,CIRCUIT_EXPORT); tiku_circuitview_picked(v,alias); settle(v);
    assert(strstr(v->status,"Export refused")); assert(!unlink(alias));
    assert(!symlink(output,alias));
    tiku_circuitview_action(v,CIRCUIT_EXPORT); tiku_circuitview_picked(v,alias); settle(v);
    assert(strstr(v->status,"failed")); assert(!unlink(alias));

    tiku_circuitview_settings(v,directory);
    tiku_circuitview_action(v,CIRCUIT_LAYER_DOWN); settle(v);
    tiku_circuitview_action(v,CIRCUIT_LAYER_VISIBLE); settle(v);
    v->units=1; v->show_details=0;
    tiku_circuitview_action(v,CIRCUIT_SAVE_VIEW); settle(v);
    assert(strstr(v->status,"settings saved"));
    tiku_circuitview_action(v,CIRCUIT_EMPTY); assert(!v->document);
    tiku_circuitview_file(v,"tests/fixtures/circuit/stack.gbrjob"); settle(v);
    assert(v->units==1 && !v->show_details && tiku_circuit_layer_count(v->document)==3);
    const tiku_circuit_layer_t *l=tiku_circuit_layer(v->document,1);
    assert(l->colour==0xff123abc && l->opacity==128 && !l->visible);
    /* Corrupt settings are ignored, including trailing bytes. */
    DIR *dir=opendir(directory); assert(dir); struct dirent *entry; char pref[512]="";
    while((entry=readdir(dir))) if(strstr(entry->d_name,".view")) snprintf(pref,sizeof pref,"%s/%s",directory,entry->d_name);
    closedir(dir); assert(pref[0]); FILE *f=fopen(pref,"ab"); assert(f && fputs("bad",f)>=0 && !fclose(f));
    tiku_circuitview_file(v,"tests/fixtures/circuit/stack.gbrjob"); settle(v);
    assert(tiku_circuit_layer(v->document,0)->opacity==255);
    tiku_circuitview_settings(v,output); /* Not a directory. */
    tiku_circuitview_action(v,CIRCUIT_SAVE_VIEW); settle(v);
    assert(strstr(v->status,"could not be saved"));
    tiku_circuitview_settings(v,NULL); v->show_details=1; v->units=0;
    assert(!unlink(pref) && !unlink(output) && !rmdir(directory));
    tiku_circuitview_picker(v,NULL,NULL);
}

int main(void)
{
    tiku_manifest_t manifest;

    assert(!tiku_manifest_read("apps/circuit/circuit.app", &manifest, NULL,
                               0));
    assert(!strcmp(manifest.id, "circuit") &&
           !strcmp(manifest.types, "application/x-kicad-schematic,"
                   "application/x-excellon,application/x-gerber,"
                   "application/x-gerber-job,application/x-schematic"));
    tiku_surface_t *s = tiku_surface_new(1000, 750, TIKU_C_PANEL);
    assert(s);
    tiku_workspace_t *ws = tiku_workspace_new(s);
    assert(ws);
    int dirty = 0;
    tiku_circuitview_t *v = tiku_circuitview_new(ws, &dirty);
    assert(v);
    tiku_circuitview_open(v);
    assert(v->window && !v->document);
    assert(v->window->resizable && v->window->zoomable);
    tiku_workspace_draw(ws);
    const int sizes[] = {14, 16, 24, 32, 64};
    for (size_t i = 0; i < sizeof sizes / sizeof *sizes; i++) {
        const uint32_t *px = tiku_icons_bitmap("circuit", sizes[i]);
        assert(px && !(px[0] >> 24));
    }
    tiku_circuitview_action(v, CIRCUIT_EXAMPLE);
    settle(v);
    assert(v->document && v->image.px && v->image.w == v->canvas.w);
    assert(v->rendered_generation == v->generation);
    tiku_workspace_draw(ws);
    double old = v->view.scale;
    tiku_circuitview_action(v, CIRCUIT_IN);
    settle(v);
    assert(v->view.scale > old && !v->fit);
    /* Rapid zoom lands on the last view asked for, not the last render
     * to finish. */
    for (int i = 0; i < 12; i++) {
        tiku_circuitview_action(v, i & 1 ? CIRCUIT_IN : CIRCUIT_OUT);
        tiku_circuitview_tick(v);
    }
    settle(v);
    assert(v->generation == v->rendered_generation);
    tiku_event_t event = {.type = TIKU_EVENT_POINTER_DOWN, .button = 1,
        .x = v->canvas.x + (int)(26 * v->view.scale + v->view.x),
        .y = v->canvas.y + (int)(12 * v->view.scale + v->view.y)};
    tiku_circuitview_event(v->window, &event, v);
    event.type = TIKU_EVENT_POINTER_UP;
    tiku_circuitview_event(v->window, &event, v);
    assert(v->selected == 3);
    /* Narrow, maximised and restored frames all lay the window out
     * again. */
    const int widths[] = {280, 980, 800};
    for (size_t i = 0; i < sizeof widths / sizeof *widths; i++) {
        v->window->frame.w = widths[i];
        v->window->frame.h = 500;
        tiku_workspace_draw(ws);
        settle(v);
        assert(v->image.w == v->canvas.w);
        for (int k = 0; k < v->dialog.count; k++) {
            tiku_rect_t r = v->dialog.control[k].rect;
            assert(r.x + r.w <= v->window->frame.x + v->window->frame.w);
        }
    }
    tiku_circuitview_action(v, CIRCUIT_IN);
    tiku_circuitview_tick(v);
    tiku_circuitview_action(v, CIRCUIT_CANCEL);
    settle(v);
    assert(strstr(v->status, "cancelled") && !v->needs_render);
    tiku_circuitview_action(v, CIRCUIT_FIT);
    settle(v);
    assert(!strstr(v->status, "cancelled"));
    /* A preview stays inside a tiny window's content clip. */
    tiku_fill(s, (tiku_rect_t){0, 0, s->w, s->h}, 0xff123456);
    tiku_rect_t tiny = {40, 40, 180, 32};
    tiku_circuitview_draw(v->window, s, tiny, v);
    for (int y = 0; y < s->h; y++) {
        for (int x = 0; x < s->w; x++) {
            if (x < tiny.x || x >= tiny.x + tiny.w || y < tiny.y ||
                y >= tiny.y + tiny.h) {
                assert(s->px[(size_t)y * s->w + x] == 0xff123456);
            }
        }
    }
    tiku_circuitview_action(v, CIRCUIT_EMPTY);
    assert(!v->document && !v->image.px);
    imports(v);
    extended_formats(v);
    drill_reload(v);
    artwork_imports(v);
    layer_workflow(v);
    tiku_circuitview_file(v, "tests/fixtures/circuit/drills.drl");
    settle(v);
    assert(v->document && !v->project && tiku_circuit_count(v->document) == 4);
    assert(strstr(tiku_circuit_warning(v->document), "Drill preview"));
    v->selected = 0;
    tiku_workspace_draw(ws);
    tiku_circuitview_action(v, CIRCUIT_COPY);
    assert(strstr(tiku_clipboard_text(), "Tool: T01"));
    tiku_circuit_t *drill_previous = v->document;
    tiku_circuitview_file(v,
                          "tests/fixtures/circuit/drill-board.kicad_pcb");
    settle(v);
    assert(v->document == drill_previous && !v->project);
    tiku_circuitview_action(v, CIRCUIT_RELOAD);
    settle(v);
    assert(v->document && !v->project && tiku_circuit_count(v->document) == 4);
    source_policy();
    inspection(v);
    hierarchy(v);
    many_sheets(v);
    menus(v);
    first_view(v);
    large_window(v);
    inspection_tools(v);
    tiku_circuitview_file(v, "tests/fixtures/circuit/basic.kicad_sch");
    tiku_circuitview_tick(v);
    /* The import's worker outlives the window it was posted for. */
    tiku_circuitview_destroy(v);
    settle(NULL);
    tiku_workspace_free(ws);
    tiku_surface_free(s);

    host_t host = {0};
    tiku_app_services_t services = {.ctx = &host, .open = open_window,
        .frame = frame, .close = close_window, .pick = choose_file,
        .startup_document = "tests/fixtures/circuit/basic.kicad_sch"};
    const tiku_app_descriptor_t *app = tiku_app_v1.app;
    void *state = NULL;
    assert(!app->start(&state, &services) && state && host.opened == 1);
    app->tick(state, 0);
    for (int i = 0; tiku_work_busy() && i < SETTLE_TURNS; i++) {
        usleep(SETTLE_WAIT_US);
        app->tick(state, 0);
    }
    assert(!tiku_work_busy());
    assert(!app->pick(state, WINDOW_ID, CIRCUIT_EXPORT) && host.pickers == 1);
    app->picked(state, WINDOW_ID, NULL);
    host.pickers = 0;
    assert(!app->pick(state, WINDOW_ID, CIRCUIT_OPEN) && host.pickers == 1);
    app->tick(state, 0);
    /* An answer for another window leaves this one as it was: nothing is
     * opened, so no frame is drawn; this window's answer is opened. */
    int frames = host.frames;
    app->picked(state, 999, "tests/fixtures/circuit/drills.drl");
    app->tick(state, 0);
    assert(host.frames == frames);
    app->picked(state, WINDOW_ID,
                "tests/fixtures/circuit/transforms.kicad_sch");
    app->tick(state, 0);
    assert(host.frames > frames);
    assert(!app->pick(state, WINDOW_ID, CIRCUIT_EXAMPLE));
    app->tick(state, 0);
    assert(app->pick(state, WINDOW_ID, CIRCUIT_CLOSE));
    app->tick(state, 0);
    assert(host.closed == 1);
    app->stop(state);
    settle(NULL);
    puts("PASS: measurement, appearance, PNG export safety, saved settings; circuit app ABI and picker, layers, hierarchy/search/"
         "navigation, menus, confined paths, atomic reload, first views, "
         "large windows, resize, cancellation and close during work");
    return 0;
}
