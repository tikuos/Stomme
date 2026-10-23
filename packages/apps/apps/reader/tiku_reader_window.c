/* tiku_reader_window.c -- a viewer window's life: opening it for Help or a
 * document, closing it, reading a local document into it, Reload, the
 * zoom box, generated reports, and TikuReader's own profile.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_app_product.h"
#include "tiku_reader_private.h"

static void reader_title(char *out, size_t cap, const char *title);

/** @brief TikuReader's identity, the one a NULL profile selects. */
static const tiku_reader_profile_t reader_profile = {
    .name = TIKU_PRODUCT_READER_NAME, .icon = TIKU_PRODUCT_READER_ICON,
    .bookmark_env = TIKU_PRODUCT_READER_TIKU_READER_BOOKMARKS,
    .bookmark_file = TIKU_PRODUCT_READER_BOOKMARK_FILE,
    .help_page = TIKU_PRODUCT_READER_HELP_PAGE,
    .welcome_html = TIKU_PRODUCT_READER_WELCOME_TITLE
        "<p>A quiet place to read local documents.</p>"
        "<h2>Open a document</h2><p>Choose Open or press Ctrl+O "
        "to browse Markdown "
        "(.md, .markdown) and HTML (.html, .htm) files.</p>"
        "<h2>Find your way</h2><p>Follow local links, move Back "
        "and Forward, or show Outline and Bookmarks from View. "
        "Ctrl+L selects the Location bar. Find opens a search bar "
        "below the page; Escape hides it. Edit &gt; Search folder "
        "looks through documents beside this one.</p>"
        "<h2>Read safely</h2><p>Documents are read-only. "
        "Scripts, forms and web requests are not run. "
        "Reload reads your latest saved changes.</p>"
};

/** @brief The window went away, by its close box or by Close: the folder
 *         search stops and nothing points at the window. */
static void reader_window_gone(tiku_window_t *window, void *ctx)
{
    tiku_reader_help_window_t *c = ctx;
    (void)window;
    tiku_reader_search_cancel(c);
    tiku_scroll_release(&c->bar);
    c->window = NULL;
    tiku_dialog_init(&c->dialog);
    if (c->dirty) *c->dirty = 1;
}

void tiku_reader_help_window_init(tiku_reader_help_window_t *c,
                               tiku_workspace_t *workspace,
                               const char *root, int screen_w, int screen_h,
                               int *dirty)
{
    memset(c, 0, sizeof *c);
    c->profile = &reader_profile;
    c->workspace = workspace;
    c->screen_w = screen_w;
    c->screen_h = screen_h;
    c->dirty = dirty;
    c->focus_link = TIKU_DOC_NONE;
    snprintf(c->root, sizeof c->root, "%s", root ? root : "");
    tiku_scroll_init(&c->bar, tiku_font_plain()->height);
    tiku_dialog_init(&c->dialog);
    tiku_reader_init(c);
}

void tiku_reader_help_window_opener(tiku_reader_help_window_t *c,
                                 tiku_reader_help_open_fn fn, void *ctx)
{
    c->opener = fn;
    c->opener_ctx = ctx;
}

const char *tiku_reader_help_window_status(const tiku_reader_help_window_t *c)
{ return c->status; }

const char *tiku_reader_help_window_page(const tiku_reader_help_window_t *c)
{ return c->page; }

const char *tiku_reader_help_window_wanted(const tiku_reader_help_window_t *c)
{ return c->wanted; }

tiku_window_t *tiku_reader_help_window_handle(tiku_reader_help_window_t *c)
{ return c->window; }

int tiku_reader_help_window_ready(const tiku_reader_help_window_t *c)
{
    return c != NULL && c->root[0] != '\0';
}

void tiku_reader_help_window_open_page(tiku_reader_help_window_t *c,
                                    const char *page)
{
    if (c == NULL) return;
    if (c->window && c->window->open) {
        /* Already open on some other page: it goes to this one, and the
         * page it was showing joins the history the way a followed link
         * does. */
        tiku_workspace_activate(c->workspace, c->window);
        if (page != NULL && *page) (void)tiku_reader_help_window_go(c, page);
        return;
    }
    tiku_reader_help_window_open(c);
    if (page != NULL && *page && c->window != NULL) {
        if (tiku_reader_help_window_go(c, page) == TIKU_READER_HELP_PAGE) {
            /* Opened on the page itself: the window starts with no
             * history. */
            c->nback = c->nforward = 0;
        }
    }
}

void tiku_reader_help_window_open(tiku_reader_help_window_t *c)
{
    tiku_rect_t frame;

    if (c->window && c->window->open) {
        tiku_workspace_activate(c->workspace, c->window);
        return;
    }
    if (c->document_mode && !c->doc) {
        c->doc = calloc(1, sizeof *c->doc);
        if (c->doc) {
            tiku_doc_init(c->doc);
            tiku_doc_read(c->doc, c->profile->welcome_html);
            tiku_reader_loaded(c);
        }
    } else if (!c->document_mode && !tiku_reader_page_read(c, "index.html")) {
        /* No folder, or no page in it: the window still opens, and says
         * so. */
        if (!c->doc) {
            c->doc = calloc(1, sizeof *c->doc);
            if (c->doc) tiku_doc_init(c->doc);
        }
        if (c->doc) (void)tiku_doc_read(c->doc, "");
        tiku_reader_say(c, tiku_str("help.no_folder"));
    }
    c->laid_for = 0;
    if (!c->document_mode) c->nback = c->nforward = 0;
    c->focus_link = TIKU_DOC_NONE;
    tiku_dialog_init(&c->dialog);
    frame.w = HELP_W;
    frame.h = HELP_H;
    frame.x = (c->screen_w - frame.w) / 2;
    frame.y = (c->screen_h - frame.h) / 3;
    /* Leave the right-hand Deskbar clear of the reader's Open/Reload row. */
    if (c->document_mode && frame.x > 24) frame.x = 24;
    if (frame.x < 0) frame.x = 0;
    if (frame.y < 0) frame.y = 0;
    c->window = tiku_workspace_open(c->workspace,
                                    c->document_mode ? c->profile->name :
                                                       tiku_str("title.help"),
                                    frame, tiku_reader_draw, c, NULL);
    if (c->window) {
        tiku_window_set_handlers(c->window, tiku_reader_window_event, NULL,
                                 reader_window_gone);
        if (c->document_mode) tiku_reader_document_menus(c);
    }
    if (c->dirty) *c->dirty = 1;
}

void tiku_reader_help_window_close(tiku_reader_help_window_t *c)
{
    tiku_reader_search_cancel(c);
    tiku_scroll_release(&c->bar);
    if (c->window && c->window->open) {
        tiku_workspace_close(c->workspace, c->window);
    }
    c->window = NULL;
    tiku_dialog_init(&c->dialog);
    if (c->dirty) *c->dirty = 1;
}

void tiku_reader_help_window_destroy(tiku_reader_help_window_t *c)
{
    tiku_reader_help_window_close(c);
    tiku_reader_doc_free(c->doc);
    c->doc = NULL;
    if (c->reader) {
        tiku_reader_images_clear(c->reader);
        for (int i = 0; i < HELP_REPORTS; i++) free(c->reader->reports[i].html);
        free(c->reader->text); free(c->reader->starts); free(c->reader->folded);
        free(c->reader); c->reader = NULL;
    }
}

void tiku_reader_document_window_init(tiku_reader_help_window_t *c,
    tiku_workspace_t *workspace, int screen_w, int screen_h, int *dirty,
    void (*choose_file)(void *ctx), void *ctx)
{
    tiku_reader_document_window_init_profile(c, workspace, screen_w, screen_h,
                                           dirty, choose_file, ctx, NULL);
}

void tiku_reader_document_window_init_profile(tiku_reader_help_window_t *c,
    tiku_workspace_t *workspace, int screen_w, int screen_h, int *dirty,
    void (*choose_file)(void *ctx), void *ctx,
    const tiku_reader_profile_t *profile)
{
    tiku_reader_help_window_init(c, workspace, "", screen_w, screen_h, dirty);
    c->document_mode = 1;
    if (profile) c->profile = profile;
    (void)tiku_app_product_register_icons();
    c->choose_file = choose_file; c->choose_ctx = ctx;
    if (c->reader) {
        c->reader->nmarks = 0;
        tiku_reader_bookmarks_load(c);
    }
}

int tiku_reader_document_window_zoom(tiku_reader_help_window_t *c, tiku_rect_t area)
{
    if (!c || !c->document_mode || !c->reader || !c->window ||
        !c->window->open || area.w < 180 || area.h < 120) return 0;
    tiku_window_t *w = c->window;
    tiku_rect_t z = c->reader->zoom_frame;
    if (w->zoom_valid && w->frame.x == z.x && w->frame.y == z.y &&
        w->frame.w == z.w && w->frame.h == z.h) {
        w->frame = w->saved_zoom; w->zoom_valid = 0;
    } else {
        w->saved_zoom = w->frame; w->zoom_valid = 1;
        w->frame = tiku_inset(area, 2);
        c->reader->zoom_frame = w->frame;
    }
    c->reader->hover_tool = 0;
    if (c->dirty) *c->dirty = 1;
    return 1;
}

int tiku_reader_document_window_file(tiku_reader_help_window_t *c, const char *path)
{
    if (!c || !c->document_mode) return 0;
    char from[TIKU_PATH_MAX];
    snprintf(from, sizeof from, "%s", c->page);
    tiku_reader_help_position_t at = tiku_reader_position(c);
    c->status[0] = 0;
    if (!tiku_reader_page_read(c, path)) {
        if (!c->status[0]) tiku_reader_say(c, "Cannot read document. Choose an existing local .md, .markdown, .html or .htm file.");
        if (c->dirty) *c->dirty = 1;
        return 0;
    }
    if (strcmp(from, c->page)) tiku_reader_back_push(c, from, at);
    c->nforward = 0;
    tiku_reader_say(c, "");
    tiku_reader_help_window_open(c);
    if (c->dirty) *c->dirty = 1;
    return 1;
}

int tiku_reader_document_window_reload(tiku_reader_help_window_t *c)
{
    if (!c || !c->document_mode || c->page[0] != '/') return 0;
    char path[TIKU_PATH_MAX]; snprintf(path, sizeof path, "%s", c->page);
    tiku_reader_help_position_t at = tiku_reader_position(c);
    c->status[0] = 0;
    if (!tiku_reader_page_read(c, path)) {
        if (!c->status[0]) tiku_reader_say(c, "Reload failed; the displayed document has been kept.");
        if (c->dirty) *c->dirty = 1;
        return 0;
    }
    tiku_scroll_to(&c->bar, tiku_doc_place_y(c->doc, at.place));
    tiku_reader_say(c, "Reloaded from disk.");
    if (c->dirty) *c->dirty = 1;
    return 1;
}
/** @brief @p title, "Report" when NULL, into @p out with &, < and >
 *         escaped and control characters dropped. */
static void reader_title(char *out, size_t cap, const char *title)
{
    size_t n = 0;
    for (const char *s = title ? title : "Report"; *s && n + 7 < cap; s++) {
        const char *entity = *s == '&' ? "&amp;" : *s == '<' ? "&lt;" : *s == '>' ? "&gt;" : NULL;
        if (entity) { size_t len = strlen(entity); memcpy(out+n, entity, len); n += len; }
        else if ((unsigned char)*s >= 32) out[n++] = *s;
    }
    out[n] = 0;
}

int tiku_reader_help_window_html(tiku_reader_help_window_t *c, const char *title, const char *html)
{
    if (!c || !c->reader || !html) return 0;
    char escaped[768]; reader_title(escaped, sizeof escaped, title);
    size_t n = strnlen(html, TIKU_READER_HELP_PAGE_MAX) + strlen(escaped) + 16;
    if (n >= TIKU_READER_HELP_PAGE_MAX) { tiku_reader_say(c, "Report exceeds the document size limit."); return 0; }
    char *copy = malloc(n); if (!copy) return 0;
    strcpy(copy, "<h1>"); strcat(copy, escaped); strcat(copy, "</h1>"); strcat(copy, html);
    struct help_reader *r = c->reader;
    int slot = r->report_id++ % HELP_REPORTS;
    free(r->reports[slot].html); r->reports[slot].html = copy;
    snprintf(r->reports[slot].name, sizeof r->reports[slot].name, "@report%d", r->report_id);
    tiku_reader_help_window_open_page(c, r->reports[slot].name);
    return c->window != NULL && !strcmp(c->page, r->reports[slot].name);
}
