/* TikuPDF driven without a desktop: opening, drawing pages on workers,
 * zoom and paging, the outline, links and Back, search, selection and
 * copy, the page field, a file that is not a PDF, and closing.
 * SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <unistd.h>
#include <stdlib.h>
#include "tiku_work.h"
static int fail_alloc, fail_realloc, fail_post, fail_calloc;
static void *audit_calloc(size_t count, size_t size)
{
    if (fail_calloc && !--fail_calloc) return NULL;
    return calloc(count, size);
}
static void *audit_malloc(size_t size)
{
    if (fail_alloc) { fail_alloc = 0; return NULL; }
    return malloc(size);
}
static void *audit_realloc(void *p, size_t size)
{
    if (fail_realloc) { fail_realloc = 0; return NULL; }
    return realloc(p, size);
}
static int audit_post(const tiku_work_job_t *job)
{
    if (fail_post) { fail_post = 0; return -1; }
    return tiku_work_post(job);
}
#define malloc audit_malloc
#define calloc audit_calloc
#define realloc audit_realloc
#define tiku_work_post audit_post
#include "../../apps/pdf/tiku_pdfview.c"
#undef malloc
#undef calloc
#undef realloc
#undef tiku_work_post
#include "tiku_icons.h"
#include "../fixtures/pdf_crypt.inc"
#include "../fixtures/cms.inc"
#include "../fixtures/pubsec.inc"
#include "../fixtures/pdfsig.inc"

#define PAGES 12

typedef struct { char *p; long n, cap, off[64]; int nobj; } pdfw_t;

/** @brief Append @p s to the file being written. */
static void put(pdfw_t *b, const char *s)
{
    long n = (long)strlen(s);
    if (b->n + n + 1 > b->cap) {
        b->cap = (b->n + n + 1) * 2;
        b->p = realloc(b->p, (size_t)b->cap);
        assert(b->p);
    }
    memcpy(b->p + b->n, s, (size_t)n + 1);
    b->n += n;
}

/** @brief Object @p num with @p body; a stream when @p data is given. */
static void object(pdfw_t *b, int num, const char *body, const char *data)
{
    char head[64];
    b->off[num] = b->n;
    if (num >= b->nobj) b->nobj = num + 1;
    if (data) {
        char dict[256];
        snprintf(dict, sizeof dict, "%d 0 obj\n<< /Length %zu >>\nstream\n",
                 num, strlen(data));
        put(b, dict);
        put(b, data);
        put(b, "\nendstream\nendobj\n");
        return;
    }
    snprintf(head, sizeof head, "%d 0 obj\n", num);
    put(b, head);
    put(b, body);
    put(b, "\nendobj\n");
}

/**
 * @brief Twelve pages, each saying its number; the first with a link to
 *        page ten and one to an address, the first and fifth with
 *        "TikuPDF"; an outline of Start, and Ten with Eleven under it.
 */
static pdfw_t document(void)
{
    pdfw_t b = {0};
    char body[512], text[256];
    long xref;

    put(&b, "%PDF-1.4\n");
    object(&b, 1, "<< /Type /Catalog /Pages 2 0 R /Outlines 40 0 R >>", NULL);
    snprintf(body, sizeof body, "<< /Type /Pages /Count %d /Kids [", PAGES);
    for (int i = 0; i < PAGES; i++) {
        char kid[16];
        snprintf(kid, sizeof kid, "%d 0 R ", 10 + i);
        strcat(body, kid);
    }
    strcat(body, "] /MediaBox [0 0 612 792] /Resources << /Font << /F1 3 0 R"
                 " >> >> >>");
    object(&b, 2, body, NULL);
    object(&b, 3, "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>",
           NULL);
    for (int i = 0; i < PAGES; i++) {
        snprintf(body, sizeof body, "<< /Type /Page /Parent 2 0 R /Contents "
                 "%d 0 R%s >>", 25 + i, i == 0 ? " /Annots [50 0 R 51 0 R]"
                                               : "");
        object(&b, 10 + i, body, NULL);
        snprintf(text, sizeof text, "BT /F1 24 Tf 72 700 Td (Page %d) Tj ET%s",
                 i + 1, i == 0 ? " BT /F1 20 Tf 72 600 Td (Hello TikuPDF "
                                 "world) Tj ET" :
                        i == 4 ? " BT /F1 20 Tf 72 300 Td (Find TikuPDF "
                                 "here) Tj ET" : "");
        object(&b, 25 + i, NULL, text);
    }
    object(&b, 40, "<< /Type /Outlines /First 41 0 R /Last 42 0 R >>", NULL);
    object(&b, 41, "<< /Title (Start) /Parent 40 0 R /Next 42 0 R "
                   "/Dest [10 0 R /Fit] >>", NULL);
    object(&b, 42, "<< /Title (Ten) /Parent 40 0 R /Prev 41 0 R /First 43 0 R"
                   " /Last 43 0 R /Count 1 /Dest [19 0 R /XYZ 0 792 0] >>",
           NULL);
    object(&b, 43, "<< /Title (Eleven) /Parent 42 0 R /Dest [20 0 R /Fit] >>",
           NULL);
    object(&b, 50, "<< /Type /Annot /Subtype /Link /Rect [72 590 400 625] "
                   "/Dest [19 0 R /XYZ 0 792 0] >>", NULL);
    object(&b, 51, "<< /Type /Annot /Subtype /Link /Rect [72 100 300 130] "
                   "/A << /S /URI /URI (https://tiku-os.org/pdf) >> >>", NULL);
    xref = b.n;
    snprintf(body, sizeof body, "xref\n0 %d\n0000000000 65535 f \n", b.nobj);
    put(&b, body);
    for (int i = 1; i < b.nobj; i++) {
        snprintf(body, sizeof body, b.off[i] ? "%010ld 00000 n \n"
                                             : "0000000000 65535 f \n",
                 b.off[i]);
        put(&b, body);
    }
    snprintf(body, sizeof body, "trailer\n<< /Size %d /Root 1 0 R >>\n"
             "startxref\n%ld\n%%%%EOF\n", b.nobj, xref);
    put(&b, body);
    return b;
}

static tiku_workspace_t *desk;

/** @brief Draw and collect until the workers are done, as the app's loop
 *         would; a job stuck for five seconds fails the test. */
static void settle(tiku_pdfview_t *c)
{
    for (int i = 0; i < 5000; i++) {
        (void)tiku_work_collect();
        if (desk) tiku_workspace_draw(desk);
        if (!tiku_work_busy() && (!c || (!c->loading && !c->inflight &&
                                         !c->searching))) {
            if (desk) tiku_workspace_draw(desk);
            if (!tiku_work_busy()) return;
        }
        usleep(1000);
    }
    assert(!"a job never finished");
}

static int keys_asked;              /* how many of them were for a key */

/** @brief Count the file-chooser requests, and those for a key file. */
static void chosen(void *ctx, int key)
{
    (*(int *)ctx)++;
    keys_asked += key != 0;
}

/** @brief The content rect's layout, as input and drawing see it. */
static pv_layout_t layout(tiku_pdfview_t *c)
{
    return pv_layout(c, tiku_window_content(c->window));
}

/** @brief A press and a release at @p x, @p y. */
static void click(tiku_pdfview_t *c, int x, int y)
{
    tiku_event_t ev = {.type = TIKU_EVENT_POINTER_DOWN, .button = 1,
                       .x = x, .y = y};
    pv_event(c->window, &ev, c);
    ev.type = TIKU_EVENT_POINTER_UP;
    pv_event(c->window, &ev, c);
}

/** @brief The canvas point over @p ux, @p uy points on page @p p. */
static void point_on(tiku_pdfview_t *c, int p, double ux, double uy,
                     int *x, int *y)
{
    pv_layout_t l = layout(c);
    *x = pv_page_x(c, l.canvas, p) + (int)(ux * c->scale);
    *y = l.canvas.y + (int)(c->top[p] + uy * c->scale - c->vbar.value);
}

/** @brief Opening, drawing, the page as composed, and the outline. */
static void open_tests(tiku_pdfview_t *c, const char *file)
{
    pv_layout_t l;
    pv_tile_t *t;

    assert(tiku_pdfview_file(c, file));
    assert(!tiku_pdfview_file(c, file));        /* one opening at a time */
    settle(c);
    assert(c->doc && c->npage == PAGES && c->noutline == 3);
    assert(!strcmp(tiku_pdfview_path(c), file));
    assert(strstr(c->window->title, "tiku.pdf"));
    assert(c->fit == PV_BY_WIDTH && c->side && c->nrows == 3);
    l = layout(c);
    assert(fabs(c->scale - (l.canvas.w - 2.0 * PV_MARGIN) / 612) < 1e-9);
    t = pv_tile(c, 0, 1);
    assert(t && t->w == pv_page_w(c, 0) && t->h == pv_page_h(c, 0));
    assert(c->pages[0].text && c->pages[0].nlink == 2 &&
           c->pages[0].nch > 20);
    /* Paper where the page is, the desk's grey between pages. */
    int x = pv_page_x(c, l.canvas, 0) - l.canvas.x + 5;
    int y = (int)(c->top[0] - c->vbar.value) + 5;
    assert(c->view[(size_t)y * c->view_w + x] == 0xffffffffu);
    assert(c->view[(size_t)2 * c->view_w + 2] == 0xff4b4e52u);
    assert(pv_current(c) == 0);
}

/** @brief Zoom steps and fits keep the page in view; paging moves. */
static void view_tests(tiku_pdfview_t *c)
{
    double fitted = c->scale;
    pv_layout_t l;

    pv_action(c, PV_ZOOM_IN);
    l = layout(c);
    assert(c->fit == PV_FIXED && c->scale > fitted && l.hbar.w > 0);
    pv_action(c, PV_ACTUAL);
    l = layout(c);
    assert(c->scale == 1.0);
    pv_action(c, PV_FIT_PAGE);
    l = layout(c);
    assert(pv_page_h(c, 0) <= l.canvas.h - 2 * PV_MARGIN + 1);
    pv_action(c, PV_FIT_WIDTH);
    l = layout(c);
    assert(fabs(c->scale - fitted) < 1e-9 && !l.hbar.w);
    pv_action(c, PV_NEXT);
    assert(pv_current(c) == 1);
    pv_action(c, PV_LAST);
    assert(pv_current(c) == PAGES - 1 && !pv_enabled(c, PV_NEXT));
    pv_action(c, PV_FIRST);
    assert(pv_current(c) == 0 && !pv_enabled(c, PV_PREV));
    settle(c);
}

/** @brief The outline and links go where they say; Back returns. */
static void navigation_tests(tiku_pdfview_t *c)
{
    pv_layout_t l = layout(c);
    tiku_rect_t row = tiku_list_row_rect(&c->list, l.side, 1);
    int x, y;

    click(c, row.x + 60, row.y + row.h / 2);
    assert(pv_page_at(c, c->vbar.value + PV_MARGIN) == 9 && c->nback == 1);
    click(c, row.x + 4, row.y + row.h / 2);     /* the sign folds Ten */
    assert(c->folded[1] && c->nrows == 2);
    pv_action(c, PV_BACK);
    assert(pv_current(c) == 0 && c->nback == 0);
    settle(c);

    /* The link over "Hello TikuPDF world": hovered, then followed. */
    point_on(c, 0, 100, 792 - 610, &x, &y);
    tiku_event_t move = {.type = TIKU_EVENT_POINTER_MOVE, .x = x, .y = y};
    pv_event(c->window, &move, c);
    assert(c->link_at == 0 && strstr(c->hint, "page 10"));
    click(c, x, y);
    assert(pv_page_at(c, c->vbar.value + PV_MARGIN) == 9 && c->nback == 1);
    pv_action(c, PV_BACK);
    settle(c);
    /* The address link is low on the page: scroll it into view first. */
    tiku_scroll_to(&c->vbar, (int)(c->top[0] + (792 - 200) * c->scale));
    point_on(c, 0, 100, 792 - 115, &x, &y);
    assert(pv_in(layout(c).canvas, x, y));
    click(c, x, y);
    assert(!strcmp(tiku_clipboard_text(), "https://tiku-os.org/pdf"));
    assert(strstr(c->status, "Address copied"));
}

/** @brief Find steps through the matches in order and says what it
 *         found; a word not there is said to be not found; Match Case
 *         and Whole Words narrow it and search again. */
static void search_tests(tiku_pdfview_t *c)
{
    tiku_field_start(&c->find, "tikupdf");
    pv_find(c, 1);
    settle(c);
    assert(c->nmatch == 2 && c->match[0].page == 0 && c->match[1].page == 4);
    assert(c->cur == 0 && strstr(c->status, "Match 1 of 2"));
    assert(c->match[0].nr == 1 && c->match[0].r[0][2] > c->match[0].r[0][0]);
    pv_action(c, PV_FIND_NEXT);
    assert(c->cur == 1 && pv_current(c) == 4);
    pv_action(c, PV_FIND_NEXT);
    assert(c->cur == 0);
    tiku_field_start(&c->find, "Hello   TikuPDF");     /* spaces fold */
    pv_find(c, 1);
    settle(c);
    assert(c->nmatch == 1);
    tiku_field_start(&c->find, "no such words");
    pv_find(c, 1);
    settle(c);
    assert(c->nmatch == 0 && strstr(c->status, "Not found"));
    tiku_field_start(&c->find, "tikupdf");
    pv_find(c, 1);
    settle(c);
    assert(c->nmatch == 2);
    pv_action(c, PV_MATCH_CASE);        /* the search starts again, cased */
    settle(c);
    assert(c->match_case && c->nmatch == 0);
    tiku_field_start(&c->find, "TikuPDF");
    pv_find(c, 1);
    settle(c);
    assert(c->nmatch == 2);
    pv_action(c, PV_MATCH_CASE);
    settle(c);
    tiku_field_start(&c->find, "Page 1");
    pv_find(c, 1);
    settle(c);
    assert(c->nmatch > 1);              /* Page 1, Page 10, Page 11 ... */
    pv_action(c, PV_WHOLE_WORDS);
    settle(c);
    assert(c->whole_words && c->nmatch == 1 && c->match[0].page == 0);
    pv_action(c, PV_WHOLE_WORDS);
    settle(c);
    pv_action(c, PV_FIRST);
    settle(c);
}

/** @brief A drag selects the characters it crosses; Copy takes them;
 *         Select Page Text takes the page. */
static void selection_tests(tiku_pdfview_t *c)
{
    const pv_page_t *pg = &c->pages[0];
    long h = -1;
    int x0, y0, x1, y1;

    assert(!pv_enabled(c, PV_COPY));
    for (long i = 0; i < pg->nch; i++)
        if (pg->ch[i].cp == 'H') { h = i; break; }
    assert(h >= 0 && pg->ch[h + 4].cp == 'o');
    point_on(c, 0, pg->ch[h].x0 + 1, (pg->ch[h].y0 + pg->ch[h].y1) / 2,
             &x0, &y0);
    point_on(c, 0, pg->ch[h + 4].x1 - 1, (pg->ch[h].y0 + pg->ch[h].y1) / 2,
             &x1, &y1);
    tiku_event_t ev = {.type = TIKU_EVENT_POINTER_DOWN, .button = 1,
                       .x = x0, .y = y0};
    pv_event(c->window, &ev, c);
    ev.type = TIKU_EVENT_POINTER_MOVE; ev.x = x1; ev.y = y1;
    pv_event(c->window, &ev, c);
    ev.type = TIKU_EVENT_POINTER_UP;
    pv_event(c->window, &ev, c);
    assert(c->selected && pv_enabled(c, PV_COPY));
    pv_action(c, PV_COPY);
    assert(!strcmp(tiku_clipboard_text(), "Hello"));
    click(c, x1, y1 + 200);                     /* a click lets it go */
    assert(!c->selected);
    pv_action(c, PV_SELECT_ALL);
    pv_action(c, PV_COPY);
    assert(!strncmp(tiku_clipboard_text(), "Page 1\nHello TikuPDF world",
                    25));
}

/** @brief Copy takes past the old 4 KB: page 1's text lent six thousand
 *         e acutes, two bytes each, and all twelve thousand bytes go. */
static void large_copy_test(tiku_pdfview_t *c)
{
    pv_page_t *pg = &c->pages[0];
    tiku_pdf_char_t *kept = pg->ch, *many = calloc(6000, sizeof *many);
    long nch = pg->nch;
    int text = pg->text;

    assert(many != NULL);
    for (int i = 0; i < 6000; i++) many[i].cp = 0xe9;
    pg->ch = many; pg->nch = 6000; pg->text = 1;
    c->anchor = (pv_mark_t){0, 0}; c->focus = (pv_mark_t){0, 6000};
    c->selected = 1;
    pv_action(c, PV_COPY);
    assert(tiku_clipboard_length() == 12000 &&
           !strncmp(tiku_clipboard_text(), "\xc3\xa9\xc3\xa9", 4) &&
           strstr(c->status, "Copied 6000 characters"));
    pg->ch = kept; pg->nch = nch; pg->text = text; c->selected = 0;
    free(many);
}

/** @brief The page field goes where it is told, and says when it cannot. */
static void field_tests(tiku_pdfview_t *c)
{
    tiku_dialog_focus(&c->dialog, PV_PAGE_FIELD);
    tiku_field_start(&c->go, "7");
    pv_go_typed(c);
    assert(pv_page_at(c, c->vbar.value + PV_MARGIN) == 6);
    assert(c->dialog.focus == PV_CANVAS);
    tiku_dialog_focus(&c->dialog, PV_PAGE_FIELD);
    tiku_field_start(&c->go, "99");
    pv_go_typed(c);
    assert(strstr(c->status, "no page 99"));
    tiku_dialog_focus(&c->dialog, PV_CANVAS);
}

/** @brief The canvas point over @p ux, @p uy points of page @p p as it
 *         is drawn, wherever the view's turn puts them. */
static void turned_on(tiku_pdfview_t *c, int p, double ux, double uy,
                      int *x, int *y)
{
    tiku_pdf_turn_point(c->pw[p], c->ph[p], c->turn, 0, &ux, &uy);
    point_on(c, p, ux, uy, x, y);
}

/** @brief Rotate Right and Left turn every page, a quarter at a time:
 *         what was drawn turns at once, a page drawn while the view
 *         turned lands turned, and links, selection, search and the
 *         outline find their places on the turned pages. */
static void rotation_tests(tiku_pdfview_t *c)
{
    const pv_page_t *pg = &c->pages[0];
    double fitted, mid;
    pv_tile_t *t;
    pv_layout_t l;
    long h = -1;
    int x0, y0, x1, y1;
    tiku_pdf_pixmap_t pm = {0, 0, NULL};

    pv_action(c, PV_FIRST);
    settle(c);
    fitted = c->scale;
    assert(pv_enabled(c, PV_ROTATE_RIGHT) && c->turn == 0);
    pv_action(c, PV_ROTATE_RIGHT);
    t = pv_tile(c, 0, 0);
    assert(c->turn == 90 && t && t->w > t->h);  /* turned before drawn */
    settle(c);
    l = layout(c);
    assert(fabs(c->scale - (l.canvas.w - 2.0 * PV_MARGIN) / 792) < 1e-9);
    t = pv_tile(c, 0, 1);
    assert(t && t->w == pv_page_w(c, 0) && t->h == pv_page_h(c, 0) &&
           t->w > t->h && pv_current(c) == 0);

    /* The link over "Hello TikuPDF world", where the turn put it. */
    turned_on(c, 0, 100, 792 - 610, &x0, &y0);
    tiku_event_t move = {.type = TIKU_EVENT_POINTER_MOVE, .x = x0, .y = y0};
    pv_event(c->window, &move, c);
    assert(c->link_at == 0 && strstr(c->hint, "page 10"));

    /* A drag down the turned line selects "Hello". */
    for (long i = 0; i < pg->nch; i++)
        if (pg->ch[i].cp == 'H') { h = i; break; }
    mid = (pg->ch[h].y0 + pg->ch[h].y1) / 2;
    turned_on(c, 0, pg->ch[h].x0 + 1, mid, &x0, &y0);
    turned_on(c, 0, pg->ch[h + 4].x1 - 1, mid, &x1, &y1);
    assert(abs(x0 - x1) < 3 && y1 > y0 + 20);   /* the line runs down */
    tiku_event_t ev = {.type = TIKU_EVENT_POINTER_DOWN, .button = 1,
                       .x = x0, .y = y0};
    pv_event(c->window, &ev, c);
    ev.type = TIKU_EVENT_POINTER_MOVE; ev.x = x1; ev.y = y1;
    pv_event(c->window, &ev, c);
    ev.type = TIKU_EVENT_POINTER_UP;
    pv_event(c->window, &ev, c);
    pv_action(c, PV_COPY);
    assert(!strcmp(tiku_clipboard_text(), "Hello"));
    c->selected = 0;

    /* The second match is on page 5, brought into view turned. */
    tiku_field_start(&c->find, "TikuPDF");
    pv_find(c, 1);
    settle(c);
    pv_action(c, PV_FIND_NEXT);
    assert(c->cur == 1 && pv_current(c) == 4);
    pv_action(c, PV_FIRST);
    c->query[0] = 0;
    c->nmatch = 0;

    /* The outline's Ten, then Back; Rotate Left undoes Rotate Right. */
    pv_action(c, PV_OUTLINE);
    if (!c->side) pv_action(c, PV_OUTLINE);
    l = layout(c);
    tiku_rect_t row = tiku_list_row_rect(&c->list, l.side, 1);
    click(c, row.x + 60, row.y + row.h / 2);
    assert(pv_page_at(c, c->vbar.value + PV_MARGIN) == 9);
    pv_action(c, PV_BACK);
    pv_action(c, PV_ROTATE_LEFT);
    settle(c);
    assert(c->turn == 0 && fabs(c->scale - fitted) < 1e-9);

    /* A page drawn while the view turns lands turned, pixel for pixel
     * the page drawn turned; Cmd-R turns as the menu does. */
    pv_action(c, PV_ACTUAL);
    pv_go(c, 7, 0, 0);
    tiku_workspace_draw(desk);
    assert(pv_pending(c, 7));
    tiku_event_t cmd = {.type = TIKU_EVENT_KEY_DOWN, .key = 'r',
                        .modifiers = TIKU_MOD_CMD};
    pv_event(c->window, &cmd, c);
    assert(c->turn == 90);
    settle(c);
    t = pv_tile(c, 7, 1);
    assert(t && t->w == 792 && t->h == 612);
    assert(tiku_pdf_render(c->doc->pdf, 7, 1.0, 90, &pm) == 0 &&
           pm.w == t->w && pm.h == t->h &&
           !memcmp(pm.px, t->px, (size_t)pm.w * pm.h * sizeof *pm.px));
    tiku_pdf_pixmap_free(&pm);
    pv_action(c, PV_ROTATE_RIGHT);
    pv_action(c, PV_ROTATE_RIGHT);
    pv_action(c, PV_ROTATE_RIGHT);
    assert(c->turn == 0);
    pv_action(c, PV_FIT_WIDTH);
    pv_action(c, PV_FIRST);
    settle(c);
}

static void press(tiku_pdfview_t *c, int key);

/** @brief Toolbar button @p id's box, or an empty one. */
static tiku_rect_t tool_box(tiku_pdfview_t *c, int id)
{
    (void)layout(c);
    for (int i = 0; i < c->dialog.count; i++)
        if (c->dialog.control[i].id == id &&
            c->dialog.control[i].kind == TIKU_DIALOG_BUTTON)
            return c->dialog.control[i].rect;
    return (tiku_rect_t){0, 0, 0, 0};
}

/** @brief A checksum of the surface's pixels in @p r. */
static unsigned long area_sum(const tiku_surface_t *s, tiku_rect_t r)
{
    unsigned long h = 5381;
    for (int y = r.y; y < r.y + r.h; y++)
        for (int x = r.x; x < r.x + r.w; x++)
            h = h * 33 + (s->px[(size_t)y * s->w + x] & 0xffffffu);
    return h;
}

/** @brief The toolbar: every button an icon from the desk's set, square,
 *         with a name and a tip; the one under the pointer lit and its tip
 *         shown below it, until a key is pressed; an icon clicked does what
 *         its menu row does. */
static void toolbar_tests(tiku_pdfview_t *c, tiku_surface_t *s)
{
    tiku_rect_t zoom, below, turn;
    unsigned long plain;
    int buttons = 0, x, y;

    (void)layout(c);
    for (int i = 0; i < c->dialog.count; i++) {
        const tiku_dialog_control_t *d = &c->dialog.control[i];
        if (d->kind != TIKU_DIALOG_BUTTON) continue;
        assert(tiku_icons_find(pv_tool_icon(d->id)) != NULL);
        assert(*pv_tool_tip(d->id) && *pv_label(d->id));
        assert(d->rect.w == d->rect.h && d->rect.w >= 30);
        buttons++;
    }
    assert(buttons == 14 + (c->nsig > 0));  /* Signatures: signed ones */
    zoom = tool_box(c, PV_ZOOM_IN);
    below = (tiku_rect_t){zoom.x, zoom.y + zoom.h + 4, 60, 12};
    tiku_workspace_draw(desk);
    plain = area_sum(s, below);
    x = zoom.x + zoom.w / 2;
    y = zoom.y + zoom.h / 2;
    tiku_event_t move = {.type = TIKU_EVENT_POINTER_MOVE, .x = x, .y = y};
    pv_event(c->window, &move, c);
    tiku_workspace_draw(desk);
    assert(c->hover_tool == PV_ZOOM_IN && area_sum(s, below) != plain);
    press(c, TIKU_KEY_SHIFT);
    tiku_workspace_draw(desk);
    assert(!c->hover_tool && area_sum(s, below) == plain);
    turn = tool_box(c, PV_ROTATE_RIGHT);
    click(c, turn.x + turn.w / 2, turn.y + turn.h / 2);
    assert(c->turn == 90);
    turn = tool_box(c, PV_ROTATE_LEFT);
    click(c, turn.x + turn.w / 2, turn.y + turn.h / 2);
    assert(c->turn == 0);
    tiku_dialog_focus(&c->dialog, PV_CANVAS);
    settle(c);
}

/** @brief A key pressed in the viewer. */
static void press(tiku_pdfview_t *c, int key)
{
    tiku_event_t ev = {.type = TIKU_EVENT_KEY_DOWN, .key = key};

    pv_event(c->window, &ev, c);
}

/** @brief The Pages column: each page drawn small, the page in view
 *         marked and followed, a click, a key or the wheel moving it, a
 *         thumbnail on its way let go when another document opens, and
 *         Outline taking the column back. */
static void thumbnail_tests(tiku_pdfview_t *c, tiku_surface_t *s,
                            const char *file)
{
    int rh = pv_thumb_row(), back, x, y;
    tiku_event_t wheel = {.type = TIKU_EVENT_WHEEL, .dy = 2};
    pv_layout_t l;
    pv_tile_t *t;

    pv_action(c, PV_FIRST);
    pv_action(c, PV_THUMBS);
    assert(c->side && c->thumbs);
    settle(c);
    l = layout(c);
    t = pv_thumb(c, 0);
    assert(l.side.w == PV_SIDE_W && t && t->px && !t->pending);
    assert(t->w <= PV_THUMB_W && t->h <= PV_THUMB_H && t->h >= PV_THUMB_H - 1);
    assert(pv_thumb(c, 1) && pv_thumb(c, 1)->px && !pv_thumb(c, 6));
    /* Paper inside, the page in view ringed with the selection. */
    x = l.side.x + (l.side.w - t->w) / 2;
    y = l.side.y + 6 + (PV_THUMB_H - t->h) / 2;
    assert((s->px[(size_t)(y + 2) * s->w + x + 2] & 0xffffffu) == 0xffffffu);
    assert((s->px[(size_t)(y - 3) * s->w + x + 5] & 0xffffffu) ==
           (TIKU_C_SELECT & 0xffffffu));

    back = c->nback;
    click(c, l.side.x + l.side.w / 2, l.side.y + rh + rh / 2);
    assert(pv_current(c) == 1 && c->nback == back + 1);
    assert(c->dialog.focus == PV_SIDE);
    press(c, TIKU_KEY_END);
    assert(pv_current(c) == PAGES - 1);
    settle(c);
    assert(c->thumb_top == PAGES * rh - l.side.h);
    assert(pv_thumb(c, PAGES - 1) && pv_thumb(c, PAGES - 1)->px);
    press(c, TIKU_KEY_UP);
    assert(pv_current(c) == PAGES - 2);
    press(c, TIKU_KEY_HOME);
    settle(c);
    assert(pv_current(c) == 0 && c->thumb_top == 0);
    wheel.x = l.side.x + 10;
    wheel.y = l.side.y + 10;
    pv_event(c->window, &wheel, c);
    settle(c);
    assert(c->thumb_top == rh && pv_current(c) == 0);

    /* Rows not drawn yet: one is asked for, then the document goes. */
    c->thumb_top = 6 * rh;
    tiku_workspace_draw(desk);
    assert(c->thumb_busy == 1 && pv_thumb(c, 6)->pending);
    assert(tiku_pdfview_file(c, file));
    settle(c);
    assert(c->doc && !c->thumb_busy && c->thumb_top == 0);
    assert(pv_thumb(c, 0) && pv_thumb(c, 0)->px && !pv_thumb(c, 6));

    pv_action(c, PV_OUTLINE);
    assert(c->side && !c->thumbs);
    pv_action(c, PV_THUMBS);
    pv_action(c, PV_THUMBS);
    assert(!c->side && !c->thumbs && !layout(c).side.w);
    pv_action(c, PV_OUTLINE);
    assert(c->side && !c->thumbs && layout(c).side.w);
    tiku_dialog_focus(&c->dialog, PV_CANVAS);
}

/** @brief Write @p e's encrypted fixture to @p path. */
static void write_locked(const enc_t *e, const char *path)
{
    encw_t b = enc_make(e);
    FILE *f = fopen(path, "wb");

    assert(f && fwrite(b.p, 1, (size_t)b.n, f) == (size_t)b.n);
    fclose(f);
    free(b.p);
}

/** @brief Return in the focused field, as the keyboard sends it. */
static void enter(tiku_pdfview_t *c)
{
    tiku_event_t ev = {.type = TIKU_EVENT_KEY_DOWN, .key = TIKU_KEY_RETURN};
    pv_event(c->window, &ev, c);
}

/** @brief A locked PDF asks for its password in a separate panel
 *         and forgets it once tried; the author's no-copy is kept. */
static void locked_tests(tiku_pdfview_t *c, const char *dir)
{
    static const enc_t locked = {"locked", 5, 6, 256, 1, "secret", "boss", -4};
    static const enc_t nocopy = {"nocopy", 2, 3, 128, 0, "", "owner", -20};
    char path[256], other[256];

    snprintf(path, sizeof path, "%s/locked.pdf", dir);
    snprintf(other, sizeof other, "%s/nocopy.pdf", dir);
    write_locked(&locked, path);
    write_locked(&nocopy, other);
    assert(tiku_pdfview_file(c, path));
    settle(c);
    assert(c->locked && strstr(c->status, "locked") && c->npage == PAGES);
    assert(c->dialog.focus == PV_PASS_FIELD);
    pv_layout_t auth = layout(c);
    assert(auth.password.y >= auth.find.y + auth.find.h);
    assert(pv_enabled(c, PV_UNLOCK) && pv_enabled(c, PV_CANCEL_UNLOCK));
    char saved_find[TIKU_FIELD_MAX];
    snprintf(saved_find, sizeof saved_find, "%s", c->find.text);
    tiku_field_start(&c->pass, "wrong");
    enter(c);
    assert(!c->pass.text[0]);                   /* forgotten once tried */
    settle(c);
    assert(c->locked && strstr(c->status, "does not open"));
    tiku_field_start(&c->pass, "secret");
    enter(c);
    settle(c);
    assert(!c->locked && c->npage == 1 && c->may_copy);
    assert(!strcmp(saved_find, c->find.text));
    assert(c->pages[0].text && c->pages[0].nch > 5);
    assert(tiku_pdfview_file(c, other));
    settle(c);
    assert(c->npage == 1 && !c->may_copy);
    tiku_clipboard_set("before");
    pv_select_page(c, 0);
    pv_action(c, PV_COPY);
    assert(!strcmp(tiku_clipboard_text(), "before"));
    assert(strstr(c->status, "does not allow"));
    assert(tiku_pdfview_file(c, path));
    settle(c);
    tiku_field_start(&c->pass, "do not retain me");
    auth = layout(c);
    for (int i = 0; i < c->dialog.count; i++) {
        tiku_dialog_control_t button = c->dialog.control[i];
        if (button.id == PV_CANCEL_UNLOCK) {
            click(c, button.rect.x + 4, button.rect.y + 4);
            break;
        }
    }
    assert(!c->locked && !c->pass.text[0] && !c->keyfile[0]);
    /* cancelling retains the old document */
    assert(c->npage == 1 && !c->may_copy);
    unlink(path);
    unlink(other);
}

/** @brief Base64 @p b64 (and @p more after it) written to @p path, with
 *         the byte at @p at, when it is not negative, made @p to. */
static void write_b64(const char *path, const char *b64, const char *more,
                      long at, int to)
{
    static const char abc[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    unsigned char *p = malloc(strlen(b64) + (more ? strlen(more) : 0));
    size_t n = 0;
    FILE *f = fopen(path, "wb");

    for (int k = 0; k < 2; k++, b64 = more) {
        unsigned v = 0;
        int bits = 0;
        for (; b64 && *b64 && *b64 != '='; b64++) {
            v = v << 6 | (unsigned)(strchr(abc, *b64) - abc);
            if ((bits += 6) >= 8) {
                bits -= 8;
                p[n++] = (unsigned char)(v >> bits);
            }
        }
    }
    if (at >= 0) p[at] = (unsigned char)to;
    assert(p && f && fwrite(p, 1, n, f) == n);
    fclose(f);
    free(p);
}

/** @brief Type @p text into the focused field and press Return. */
static void type_enter(tiku_pdfview_t *c, tiku_field_t *f, const char *text)
{
    tiku_field_start(f, text);
    enter(c);
    settle(c);
}

/** @brief A document sealed to a certificate: its key file asked for at
 *         once, then its password; a wrong password, a key it was not
 *         sealed to, and a file that is no key each said so. */
static void sealed_tests(tiku_pdfview_t *c, const char *dir)
{
    char pdf[256], other[256], key[256];

    snprintf(pdf, sizeof pdf, "%s/sealed.pdf", dir);
    snprintf(other, sizeof other, "%s/other.pdf", dir);
    snprintf(key, sizeof key, "%s/me.p12", dir);
    write_b64(pdf, ps_aes256, NULL, -1, 0);
    write_b64(other, ps_other, NULL, -1, 0);
    write_b64(key, ck_p12_legacy, NULL, -1, 0);
    keys_asked = 0;
    assert(!tiku_pdfview_key(c, key));          /* nothing is waiting */
    assert(tiku_pdfview_file(c, pdf));
    settle(c);
    assert(c->locked && c->sealed && keys_asked == 1 &&
           strstr(c->status, "sealed to a certificate") &&
           pv_enabled(c, PV_OPEN_KEY));
    enter(c);                                   /* Return asks again */
    assert(keys_asked == 2);
    assert(tiku_pdfview_key(c, key) && strstr(c->status, "me.p12"));
    type_enter(c, &c->pass, "wrong");
    assert(c->locked && c->keyfile[0] && !c->pass.text[0] &&
           strstr(c->status, "does not open the key file"));
    type_enter(c, &c->pass, "secret");
    assert(!c->locked && !c->sealed && c->npage == 1 && c->pages &&
           !strcmp(c->path, pdf));
    settle(c);
    assert(c->pages[0].text && c->pages[0].nch > 10);
    assert(tiku_pdfview_file(c, other));
    settle(c);
    assert(tiku_pdfview_key(c, key));
    type_enter(c, &c->pass, "secret");
    assert(c->locked && !c->keyfile[0] &&
           strstr(c->status, "not one this PDF was sealed to"));
    assert(tiku_pdfview_key(c, other));         /* a PDF is not a key */
    type_enter(c, &c->pass, "");
    assert(c->locked && strstr(c->status, "not a key TikuPDF reads"));
    tiku_event_t esc = {.type = TIKU_EVENT_KEY_DOWN, .key = TIKU_KEY_ESCAPE};
    pv_event(c->window, &esc, c);
    assert(!c->locked && !c->sealed && !pv_enabled(c, PV_OPEN_KEY));
    unlink(pdf); unlink(other); unlink(key);
}

/** @brief Signed documents: the Signatures column opens on them, a card a
 *         signature, its field tinted on the page; a changed one says so. */
static void signature_tests(tiku_pdfview_t *c, const char *dir)
{
    char two[256], bent[256];
    const char *label;
    int tone, x, y, back;
    pv_layout_t l;
    long at;

    snprintf(two, sizeof two, "%s/signed.pdf", dir);
    snprintf(bent, sizeof bent, "%s/changed.pdf", dir);
    write_b64(two, sg_rsa, sg_two, -1, 0);
    assert(tiku_pdfview_file(c, two));
    settle(c);
    l = layout(c);
    assert(c->nsig == 2 && c->side && c->sigs && l.side.w &&
           pv_enabled(c, PV_SIGS) && strstr(c->status, "2 signatures"));
    label = pv_sig_pill(c, 0, &tone);
    assert(!strcmp(label, "Integrity verified") && tone == TIKU_TONE_WARN);
    char trust[400];
    pv_sig_note(c, 0, trust, sizeof trust);
    assert(strstr(trust, "identity unverified"));
    assert(!strcmp(c->sig[1].signer, "Tiku EC Signer"));
    /* The first field, Author's, tinted green on the page. */
    point_on(c, 0, (c->sig[0].x0 + c->sig[0].x1) / 2,
             (c->sig[0].y0 + c->sig[0].y1) / 2, &x, &y);
    tiku_workspace_draw(desk);
    assert(c->view[(size_t)(y - l.canvas.y) * c->view_w + (x - l.canvas.x)]
           != 0xffffffffu);
    back = c->nback;
    click(c, l.side.x + 40, l.side.y + 20);     /* the first card */
    assert(c->nback == back + 1 && pv_current(c) == 0);
    pv_action(c, PV_OUTLINE);
    assert(!c->sigs);
    pv_action(c, PV_SIGS);
    assert(c->side && c->sigs);

    /* sg_rsa with its page changed after signing, as pdfsig was shown. */
    write_b64(bent, sg_rsa, NULL, -1, 0);
    {
        FILE *f = fopen(bent, "rb");
        char buf[8192];
        size_t n = fread(buf, 1, sizeof buf, f);
        fclose(f);
        char *hit = memmem(buf, n, "Signed by its author", 20);
        assert(hit);
        at = hit - buf + 10;
    }
    write_b64(bent, sg_rsa, NULL, at, 'I');
    assert(tiku_pdfview_file(c, bent));
    settle(c);
    label = pv_sig_pill(c, 0, &tone);
    assert(c->nsig == 1 && !strcmp(label, "Document changed") &&
           tone == TIKU_TONE_FAIL &&
           strstr(c->status, "Its signature does not hold"));
    unlink(two); unlink(bent);
}

/** @brief File > Open Recent's rows as the window published them: the
 *         labels under Open Recent, into @p out, one a line. */
static void recent_menu(tiku_pdfview_t *c, char *out, size_t cap)
{
    const tiku_menu_list_t *m = &c->window->menus.menu[0];
    int i = 0;

    out[0] = 0;
    while (i < m->nitem && strcmp(m->item[i].label, "Open Recent")) i++;
    assert(i < m->nitem && m->item[i].level == 0);
    for (i++; i < m->nitem && m->item[i].level == 1; i++) {
        strncat(out, m->item[i].separator ? "-" : m->item[i].label,
                cap - strlen(out) - 2);
        strcat(out, "\n");
    }
}

/** @brief Whether @p r, inside its frame, is the page colour and nothing
 *         else on @p s: no words, no list. */
static int blank(const tiku_surface_t *s, tiku_rect_t r)
{
    for (int y = r.y + 1; y < r.y + r.h - 1; y++)
        for (int x = r.x + 1; x < r.x + r.w - 1; x++)
            if ((s->px[(size_t)y * s->w + x] & 0xffffffu) !=
                (TIKU_C_DOC & 0xffffffu))
                return 0;
    return 1;
}

/** @brief Where each document was left comes back when it opens again,
 *         turn and zoom too; Open Recent lists the documents, newest first,
 *         while the empty window on @p s stays blank; lines the file should
 *         not hold are passed over; a document gone is said so; Clear Menu
 *         forgets. */
static void recent_tests(tiku_pdfview_t *c, tiku_surface_t *s,
                         const char *file, const char *other,
                         const char *list)
{
    char real[PATH_MAX], menu[512], line[1024];
    tiku_pdfview_t *d;
    double scale;
    int dirty = 0, picks = 0;
    FILE *f;

    assert(realpath(file, real));
    assert(tiku_pdfview_file(c, file));
    settle(c);
    pv_action(c, PV_ZOOM_IN);
    pv_action(c, PV_ROTATE_RIGHT);
    layout(c);
    pv_go(c, 6, 40, 0);
    settle(c);
    scale = c->scale;
    assert(c->fit == PV_FIXED && pv_page_at(c, c->vbar.value) == 6);
    assert(tiku_pdfview_file(c, other));
    settle(c);
    assert(c->turn == 0 && c->fit == PV_BY_WIDTH && pv_current(c) == 0);
    assert(c->nrecent >= 2 && !strcmp(c->recent[1].path, real) &&
           c->recent[1].page == 6 && c->recent[1].turn == 90);
    recent_menu(c, menu, sizeof menu);
    assert(!strncmp(menu, "second.pdf\ntiku.pdf\n", 20) &&
           strstr(menu, "-\nClear Menu\n"));

    pv_action(c, PV_RECENT + 1);                /* tiku.pdf, from the menu */
    settle(c);
    assert(!strcmp(c->path, real) && c->turn == 90 && c->fit == PV_FIXED);
    assert(fabs(c->scale - scale) < 0.006 &&
           pv_page_at(c, c->vbar.value) == 6);
    f = fopen(list, "r");
    assert(f && fgets(line, sizeof line, f) &&
           !strcmp(line, "TikuPDF recent 1\n"));
    assert(fgets(line, sizeof line, f) && !strncmp(line, "7\t", 2) &&
           strstr(line, "\t90\t") && strstr(line, real));
    fclose(f);

    /* Another viewer reads the list as this one left it. */
    d = tiku_pdfview_new(desk, &dirty, chosen, &picks);
    assert(d && d->nrecent == c->nrecent &&
           !strcmp(d->recent[0].path, real) && d->recent[0].page == 6);
    tiku_pdfview_destroy(d);

    /* What the file should not hold is passed over, not guessed at. */
    f = fopen(list, "w");
    assert(f);
    fprintf(f, "TikuPDF recent 1\njunk\n3\t0\t0\twidth\t45\t/x.pdf\n"
               "0\t0\t0\twidth\t0\t/x.pdf\n1\t0\t0\tbig\t0\t/x.pdf\n"
               "1\t0\t0\twidth\t0\trelative.pdf\n"
               "2\t10.5\t3\t150\t180\t%s\n1\t0\t0\tpage\t0\t/gone.pdf\n",
            real);
    fclose(f);
    d = tiku_pdfview_new(desk, &dirty, chosen, &picks);
    assert(d && d->nrecent == 2 && d->recent[0].page == 1 &&
           d->recent[0].fit == PV_FIXED && d->recent[0].scale == 1.5 &&
           d->recent[0].turn == 180 && d->recent[0].down == 10.5 &&
           !d->recent[0].gone && d->recent[1].gone);
    tiku_pdfview_destroy(d);

    /* The empty window stays blank though documents were read: they are
     * under File > Open Recent, and a click on it opens none of them. */
    tiku_pdfview_close(c);
    settle(c);
    tiku_pdfview_open(c);
    tiku_rect_t page = layout(c).canvas;
    tiku_event_t move = {.type = TIKU_EVENT_POINTER_MOVE,
                         .x = page.x + page.w / 2, .y = page.y + page.h / 2};
    pv_event(c->window, &move, c);
    tiku_workspace_draw(desk);
    assert(!c->doc && c->nrecent >= 2 && !c->status[0]);
    assert(blank(s, page));
    click(c, page.x + 60, page.y + page.h / 2);
    settle(c);
    assert(!c->doc);

    /* A document gone from where it was is said so, and not offered. */
    snprintf(c->recent[1].path, sizeof c->recent[1].path, "%s.gone", real);
    c->recent[1].gone = 0;
    pv_action(c, PV_RECENT + 1);
    assert(strstr(c->status, "no longer at") && c->recent[1].gone);
    recent_menu(c, menu, sizeof menu);
    assert(!strstr(menu, ".gone"));

    pv_action(c, PV_CLEAR_RECENT);
    assert(c->nrecent == 0 && !pv_enabled(c, PV_CLEAR_RECENT));
    f = fopen(list, "r");
    assert(f && fgets(line, sizeof line, f) && !fgets(line, sizeof line, f));
    fclose(f);
    recent_menu(c, menu, sizeof menu);
    assert(!menu[0]);
}

static void search_failure_tests(tiku_pdfview_t *viewer)
{
    pv_search_t j = {0};
    tiku_pdfview_t c = {0};
    pv_doc_t doc = {0};
    tiku_pdf_char_t *chars = calloc(PV_MATCHES + 1, sizeof *chars);
    assert(chars);
    for (int i = 0; i <= PV_MATCHES; i++) chars[i].cp = 'a';
    j.nq = 1; j.q[0] = 'a';
    pv_search_page(&j, 0, chars, PV_MATCHES + 1);
    assert(j.limited && j.nmatch == PV_MATCHES);
    c.workspace = desk; c.npage = 100; c.cur = -1;
    j.owner = &c; j.to = 24;
    pv_search_done(&j);
    assert(c.search_limited && !c.searching && c.searched == 0);
    assert(strstr(c.status, "limit reached"));
    pv_show_match(&c, 0);
    assert(strstr(c.status, "limit reached"));
    free(c.match); free(j.match);
    c.match = NULL; c.nmatch = 0; c.cur = -1;
    c.search_limited = 0;
    j = (pv_search_t){.owner = &c, .nq = 1, .q = {'a'}};
    fail_alloc = 1;
    pv_search_page(&j, 0, chars, 1);
    assert(j.failed && !j.nmatch);
    pv_search_done(&j);
    assert(c.search_failed && strstr(c.status, "incomplete") &&
           !strstr(c.status, "Not found"));
    c.search_failed = 0;
    j.failed = 0;
    pv_search_page(&j, 0, chars, 1);
    fail_realloc = 1;
    pv_search_done(&j);
    assert(c.search_failed && c.nmatch == 0);
    free(j.match);
    c.search_failed = 0; c.doc = &doc;
    j = (pv_search_t){.owner = &c, .doc = &doc, .scanned = 24, .to = 24};
    fail_post = 1;
    pv_search_done(&j);
    assert(c.search_failed && !c.searching && c.searched == 24);
    assert(strstr(c.status, "incomplete"));
    free(chars);

    viewer->query[0] = 0;
    tiku_field_start(&viewer->find, "TikuPDF");
    fail_post = 1;
    pv_find(viewer, 1);
    assert(viewer->search_failed && !viewer->searching);
    /* unchanged query must retry, not step an empty result */
    pv_find(viewer, 1);
    settle(viewer);
    assert(!viewer->search_failed && viewer->nmatch > 0);
}

static void reliability_tests(tiku_pdfview_t *c, const char *file)
{
    assert(tiku_pdfview_file(c, file)); settle(c);
    pv_action(c, PV_NEXT); settle(c);
    pv_doc_t *before = c->doc;
    int page = pv_current(c), scroll = c->vbar.value;
    for (int fail = 1; fail <= 5; fail++) {
        pv_load_t job = {.owner=c, .generation=c->generation};
        snprintf(job.path, sizeof job.path, "%s", file);
        pv_load_run(&job); assert(job.err == TIKU_PDF_OK);
        fail_calloc = fail; pv_load_done(&job); assert(!fail_calloc);
        assert(c->doc == before && c->npage == PAGES);
        assert(pv_current(c) == page && c->vbar.value == scroll);
        assert(!strcmp(c->path, file) &&
               strstr(c->status, "Not enough memory"));
        tiku_pdf_close(job.pdf); free(job.pw); free(job.ph);
        tiku_pdf_outline_free(job.outline, job.noutline);
        tiku_pdf_signatures_free(job.sig, job.nsig); free(job.title);
    }
    c->selected = 0;
    tiku_field_start(&c->find, "selected query");
    tiku_field_key(&c->find, TIKU_FIELD_SELECT_ALL, 0, 0);
    tiku_dialog_focus(&c->dialog, PV_FIND_FIELD);
    assert(pv_enabled(c, PV_COPY));
    pv_menu_pick(c->window, PV_COPY, c);
    assert(!strcmp(tiku_clipboard_text(), "selected query"));
    c->dialog.focus = PV_PASS_FIELD; /* password panel's focus state */
    assert(!pv_enabled(c, PV_COPY));
    tiku_dialog_focus(&c->dialog, PV_CANVAS);

    int dirty = 0;
    tiku_pdfview_t *other = tiku_pdfview_new(desk, &dirty, NULL, NULL);
    assert(other && tiku_pdfview_file(other, file)); settle(other);
    pv_go(c, 0, 0, 0); settle(c);
    pv_action(c, PV_BOOKMARK);
    assert(c->nbookmarks == 1 && c->bookmarks[0].page == 0);
    pv_set_scale(other, PV_FIXED, 1.25); pv_turn(other, 90); settle(other);
    pv_go(other, 2, 100, 0); settle(other);
    pv_action(other, PV_BOOKMARK);
    assert(other->nbookmarks == 2); /* merge a stale window's addition */
    assert(pv_bookmarks_read(c) && c->nbookmarks == 2);
    pv_action(c, PV_BOOKMARK_GO+1); settle(c);
    assert(pv_current(c) == 2);
    assert(c->turn == 90 && c->fit == PV_FIXED && fabs(c->scale-1.25) < 1e-9);
    pv_action(c, PV_BOOKMARK); assert(c->nbookmarks == 1);
    assert(pv_bookmarks_read(other) && other->nbookmarks == 1);
    tiku_pdfview_destroy(other); settle(c);

    /* A failed render can be retried without reopening the document. */
    int p = PAGES-1;
    c->pages[p].failed = 1;
    assert(pv_enabled(c, PV_RETRY) && !pv_request(c, p));
    pv_action(c, PV_RETRY);
    assert(!c->pages[p].failed && !pv_enabled(c, PV_RETRY));
    pv_go(c, p, 0, 0); settle(c);
    assert(pv_tile(c, p, 1));
}

/** @brief Bookmarks the file cannot take stay in the window for the rest
 *         of the session; a lock another window holds changes nothing. */
static void bookmark_fallback_tests(const char *dir, const char *file,
                                    const char *bookmarks)
{
    char lock[300], missing[300], folder[300], kept[320], kept_lock[330];
    int dirty = 0;

    /* Another window holds the lock: busy, and the list stays. */
    snprintf(lock, sizeof lock, "%s.lock", bookmarks);
    int held = open(lock, O_RDWR | O_CREAT, 0600);
    assert(held >= 0 && !flock(held, LOCK_EX));
    tiku_pdfview_t *v = tiku_pdfview_new(desk, &dirty, NULL, NULL);
    assert(v && tiku_pdfview_file(v, file)); settle(v);
    int before = v->nbookmarks;
    pv_go(v, 3, 0, 0); settle(v);
    pv_action(v, PV_BOOKMARK);
    assert(strstr(v->status, "busy") && v->nbookmarks == before);
    close(held);
    tiku_pdfview_destroy(v); settle(NULL);

    /* No folder for the file: kept here, across later changes. */
    snprintf(missing, sizeof missing, "%s/missing/bookmarks", dir);
    setenv("TIKU_PDF_BOOKMARKS", missing, 1);
    v = tiku_pdfview_new(desk, &dirty, NULL, NULL);
    assert(v && tiku_pdfview_file(v, file)); settle(v);
    pv_go(v, 1, 0, 0); settle(v);
    pv_action(v, PV_BOOKMARK);
    assert(v->nbookmarks == 1 && strstr(v->status, "session only"));
    pv_go(v, 2, 0, 0); settle(v);
    pv_action(v, PV_BOOKMARK);
    assert(v->nbookmarks == 2 && strstr(v->status, "session only"));
    assert(access(missing, F_OK));
    tiku_pdfview_destroy(v); settle(NULL);

    /* The lock is taken but saving fails: the next change keeps it. */
    snprintf(folder, sizeof folder, "%s/readonly", dir);
    snprintf(kept, sizeof kept, "%s/bookmarks", folder);
    snprintf(kept_lock, sizeof kept_lock, "%s.lock", kept);
    assert(!mkdir(folder, 0700));
    setenv("TIKU_PDF_BOOKMARKS", kept, 1);
    v = tiku_pdfview_new(desk, &dirty, NULL, NULL);
    assert(v && tiku_pdfview_file(v, file)); settle(v);
    pv_go(v, 1, 0, 0); settle(v);
    pv_action(v, PV_BOOKMARK);
    assert(v->nbookmarks == 1 && !v->bookmarks_session);
    assert(!chmod(folder, 0500));
    pv_go(v, 2, 0, 0); settle(v);
    pv_action(v, PV_BOOKMARK);
    assert(v->nbookmarks == 2 && strstr(v->status, "saving bookmarks failed"));
    pv_go(v, 4, 0, 0); settle(v);
    pv_action(v, PV_BOOKMARK);
    assert(v->nbookmarks == 3 && strstr(v->status, "session only"));
    assert(!chmod(folder, 0700));
    assert(pv_bookmarks_read(v) && v->nbookmarks == 1); /* the file's one */
    tiku_pdfview_destroy(v); settle(NULL);
    assert(!unlink(kept) && !unlink(kept_lock) && !rmdir(folder));
    setenv("TIKU_PDF_BOOKMARKS", bookmarks, 1);
}

int main(int argc, char **argv)
{
    char dir[] = "/tmp/tikupdf-XXXXXX", file[256], junk[256], shot[256];
    char other[256], list[256], bookmarks[256], bookmark_lock[272];
    pdfw_t b = document();
    int dirty = 0, picks = 0;
    FILE *f;

    (void)argv;
    assert(mkdtemp(dir));
    snprintf(file, sizeof file, "%s/tiku.pdf", dir);
    snprintf(other, sizeof other, "%s/second.pdf", dir);
    snprintf(junk, sizeof junk, "%s/notes.pdf", dir);
    snprintf(shot, sizeof shot, "%s/viewer.png", dir);
    snprintf(list, sizeof list, "%s/recent", dir);
    setenv("TIKU_PDF_RECENT", list, 1);         /* never the person's own */
    snprintf(bookmarks, sizeof bookmarks, "%s/bookmarks", dir);
    snprintf(bookmark_lock, sizeof bookmark_lock, "%s.lock", bookmarks);
    setenv("TIKU_PDF_BOOKMARKS", bookmarks, 1);
    f = fopen(file, "wb");
    assert(f && fwrite(b.p, 1, (size_t)b.n, f) == (size_t)b.n);
    fclose(f);
    f = fopen(other, "wb");
    assert(f && fwrite(b.p, 1, (size_t)b.n, f) == (size_t)b.n);
    fclose(f);
    free(b.p);
    f = fopen(junk, "wb");
    assert(f && fputs("just some notes\n", f) >= 0);
    fclose(f);

    tiku_surface_t *s = tiku_surface_new(1000, 680, TIKU_C_BACKDROP);
    desk = tiku_workspace_new(s);
    tiku_pdfview_t *c = tiku_pdfview_new(desk, &dirty, chosen, &picks);
    assert(c);
    tiku_pdfview_open(c);
    assert(c->window && !c->doc && !pv_enabled(c, PV_COPY));
    pv_action(c, PV_OPEN);
    assert(picks == 1);
    open_tests(c, file);
    view_tests(c);
    navigation_tests(c);
    search_tests(c);
    selection_tests(c);
    large_copy_test(c);
    field_tests(c);
    toolbar_tests(c, s);
    rotation_tests(c);
    thumbnail_tests(c, s, file);
    search_failure_tests(c);
    pv_action(c, PV_FIRST);
    settle(c);
    tiku_field_start(&c->find, "TikuPDF");
    pv_find(c, 1);
    settle(c);
    tiku_workspace_draw(desk);
    if (argc > 1) assert(tiku_surface_png(s, shot) == 0);

    /* Not a PDF: said so, and the document shown stays. */
    assert(tiku_pdfview_file(c, junk));
    settle(c);
    assert(strstr(c->status, "not a PDF") && c->npage == PAGES);
    locked_tests(c, dir);
    sealed_tests(c, dir);
    signature_tests(c, dir);
    recent_tests(c, s, file, other, list);
    reliability_tests(c, file);
    bookmark_fallback_tests(dir, file, bookmarks);

    tiku_pdfview_close(c);
    settle(c);
    assert(!c->window && !c->doc && !c->npage);
    tiku_pdfview_open(c);
    assert(c->window && !c->doc);
    assert(tiku_pdfview_file(c, file));
    tiku_pdfview_destroy(c);
    tiku_workspace_free(desk);
    desk = NULL;
    settle(NULL);
    tiku_workspace_free(tiku_workspace_new(s));
    tiku_surface_free(s);
    if (argc > 1) printf("Preview: %s\n", shot);
    else {
        unlink(file); unlink(other); unlink(junk); unlink(list);
        unlink(bookmarks); unlink(bookmark_lock);
        rmdir(dir);
    }
    puts("PDF viewer: open, draw, the toolbar's icons, zoom, turn, outline, "
         "links, find, copy, thumbnails, passwords, certificates, "
         "signatures, places and recent documents, close PASS");
    return 0;
}
