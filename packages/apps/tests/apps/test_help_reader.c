/* Native reader tools, local resources and generated documents; no network. */
#ifndef TIKU_TEST_SOURCE_ROOT
#define TIKU_TEST_SOURCE_ROOT "."
#endif

#include <assert.h>
#include "../../apps/reader/tiku_reader_private.h"
#include "../fixtures/reader_wait.h"

static void fixture(const char *root, const char *name, const char *html)
{
    char path[1024]; snprintf(path, sizeof path, "%s/%s", root, name);
    FILE *f = fopen(path, "w"); assert(f); fputs(html, f); fclose(f);
}

/** @brief Whether image @p i was decoded: the 20x8 red fixture's pixels. */
static int decoded(const struct help_reader *r, int i)
{
    const tiku_image_t *im = &r->images[i].image;
    return im->px && im->w == 20 && im->h == 8 &&
           (im->px[0] & 0xFFFFFFu) == TIKU_RGB(200, 10, 20);
}

static void press(tiku_reader_help_window_t *c, unsigned key, unsigned modifiers)
{
    tiku_event_t e = {.type=TIKU_EVENT_KEY_DOWN, .key=key, .modifiers=modifiers};
    tiku_window_send(c->window, &e);
}

/** @brief Drag-select styled glyphs at their measured positions and copy them. */
static void styled_selection(tiku_reader_help_window_t *c, const char *root,
                               tiku_workspace_t *workspace)
{
    fixture(root,"styled.html","<h1>Styled text</h1>"
        "<p style='font-family:serif;font-size:14px;font-weight:bold;"
        "text-align:center;line-height:40px'>ABCDEFGHIJKL</p>"
        "<p style='font-family:monospace;font-size:15px;font-style:italic;"
        "text-align:right;line-height:2'>MNOPQRSTUVWX</p>"
        "<table><tr><td>First column</td><td style='font-family:serif;font-size:14px;padding:6px'>"
        "YZabcdefghij</td></tr></table>");
    assert(tiku_reader_help_window_go(c,"styled.html") == TIKU_READER_HELP_PAGE);
    c->reader->side = 0; tiku_workspace_draw(workspace);
    int selected = 0;
    for (int i = 0; i < c->doc->npiece; i++) {
        const tiku_doc_piece_t *pc = &c->doc->piece[i];
        const char *text = c->doc->text+pc->off;
        if (strncmp(text,"ABCDEFGHIJKL",12) && strncmp(text,"MNOPQRSTUVWX",12) &&
            strncmp(text,"YZabcdefghij",12)) continue;
        assert(pc->len == 12);
        const tiku_doc_run_t *run = &c->doc->run[pc->run];
        char prefix[13]; memcpy(prefix,text,12); prefix[12] = 0;
        prefix[3] = 0; int from = tiku_measure_width(run->face,prefix);
        memcpy(prefix,text,12); prefix[7] = 0;
        int to = tiku_measure_width(run->face,prefix);
        tiku_rect_t box = tiku_doc_piece_rect(c->doc,i);
        help_layout_t l = tiku_reader_layout(c,tiku_window_content(c->window));
        tiku_event_t drag = {.type=TIKU_EVENT_POINTER_DOWN,.button=1,
            .x=l.page.x+box.x+from,.y=l.page.y+box.y-c->bar.value+box.h/2};
        tiku_window_send(c->window,&drag);
        drag.type = TIKU_EVENT_POINTER_MOVE; drag.x = l.page.x+box.x+to;
        tiku_window_send(c->window,&drag);
        drag.type = TIKU_EVENT_POINTER_UP; tiku_window_send(c->window,&drag);
        press(c,'c',TIKU_MOD_CMD);
        char expected[5]; memcpy(expected,text+3,4); expected[4] = 0;
        assert(!strcmp(tiku_clipboard_text(),expected));
        selected++;
    }
    assert(selected == 3);
}

/** @brief Decode bounded embedded PNGs through Reader's resource callbacks. */
static void embedded_images(tiku_reader_help_window_t *c, const char *root,
                               tiku_workspace_t *workspace)
{
    const char *data = "iVBORw0KGgoAAAANSUhEUgAAABQAAAAICAYAAAD5nd/tAAAAFklEQVR4nGM4wSXyn5qYYdTAUQNJxwCGyS8wuJRVjAAAAABJRU5ErkJggg==";
    char html[512];
    snprintf(html,sizeof html,"<figure><img src='data:image/png;base64,%s' width='40' alt='Embedded alternate'>"
             "<figcaption>Embedded caption</figcaption></figure>",data);
    fixture(root,"embedded.html",html);
    assert(tiku_reader_help_window_go(c,"embedded.html") == TIKU_READER_HELP_PAGE);
    tiku_workspace_draw(workspace);
    assert(c->reader->nimages == 1 && decoded(c->reader,0));
    assert(c->doc->image[0].w == 40 && c->doc->image[0].h == 16);
    assert(strstr(c->reader->text,"Embedded caption"));
    char *large = malloc(9000); assert(large);
    int n = sprintf(large,"<img src='data:image/png;base64,");
    memset(large+n,' ',5000); n += 5000;
    snprintf(large+n,9000-(size_t)n,"%s' alt='Long embedded'>",data);
    fixture(root,"embedded-large.html",large); free(large);
    assert(tiku_reader_help_window_go(c,"embedded-large.html") == TIKU_READER_HELP_PAGE);
    tiku_workspace_draw(workspace);
    assert(c->reader->nimages == 1 && decoded(c->reader,0));
    fixture(root,"embedded-refused.html","<img src='data:image/svg+xml;base64,AAAA' alt='Refused alternate'>");
    assert(tiku_reader_help_window_go(c,"embedded-refused.html") == TIKU_READER_HELP_PAGE);
    tiku_workspace_draw(workspace);
    assert(c->reader->nimages == 1 && c->reader->images[0].failed &&
           !c->reader->images[0].image.px && strstr(c->reader->text,"Refused alternate"));
    fixture(root,"embedded-alpha.html","<img src='data:image/png;base64,"
        "iVBORw0KGgoAAAANSUhEUgAAABQAAAAICAYAAAD5nd/tAAAAFklEQVR4nGM4wSXSQE3MMGrgqIGkYwCCfd/BjkfrbgAAAABJRU5ErkJggg=='>");
    assert(tiku_reader_help_window_go(c,"embedded-alpha.html") == TIKU_READER_HELP_PAGE);
    tiku_workspace_draw(workspace);
    tiku_surface_t *probe = tiku_surface_new(50,30,0);assert(probe);
    assert(!tiku_surface_rescale(probe,2,0));
    tiku_fill(probe,(tiku_rect_t){7,7,20,8},0x204060);
    tiku_surface_t *previous = c->reader->surface;c->reader->surface = probe;
    tiku_reader_image_draw(c,c->doc->text+c->doc->image[0].src,(tiku_rect_t){7,7,20,8});
    assert((probe->px[14*100+14] & 0xffffffu) == 0x742439);
    c->reader->surface = previous;tiku_surface_free(probe);
}

/** @brief Exercise the richer fixture through local Help and generated reports. */
static void showcase(tiku_reader_help_window_t *c, const char *root,
                         tiku_workspace_t *workspace)
{
    FILE *f = fopen(TIKU_TEST_SOURCE_ROOT "/tests/fixtures/html/showcase.html","rb");assert(f);
    assert(!fseek(f,0,SEEK_END));long n = ftell(f);assert(n > 0 && n < 16384);
    rewind(f);char *html = malloc((size_t)n+1);assert(html);
    assert(fread(html,1,(size_t)n,f) == (size_t)n);html[n] = 0;fclose(f);
    fixture(root,"showcase.html",html);
    assert(tiku_reader_help_window_go(c,"showcase.html") == TIKU_READER_HELP_PAGE);
    tiku_workspace_draw(workspace);
    assert(strstr(c->reader->text,"TAIL_SHOWCASE") && !strstr(c->reader->text,"HIDDEN_DRAFT"));
    assert(c->reader->nimages == 2 && c->reader->images[0].embedded);
    int image_block = c->doc->image[0].block;
    tiku_scroll_to(&c->bar,c->doc->block[image_block].y);tiku_workspace_draw(workspace);
    assert(decoded(c->reader,0));
    assert(tiku_reader_help_window_find(c,"TAIL_SHOWCASE",0));
    assert(tiku_reader_help_window_go(c,"#detail") == TIKU_READER_HELP_ANCHOR);
    c->reader->block = -1;
    for (int i = 0; i < c->doc->nblock; i++)
        if (c->doc->block[i].kind == TIKU_DOC_BLOCK_PRE) c->reader->block = i;
    assert(tiku_reader_help_window_copy(c,1) && strstr(tiku_clipboard_text(),"renderer.sample()"));
    assert(tiku_reader_help_window_html(c,"Styled report",html));free(html);
    tiku_workspace_draw(workspace);
    assert(strstr(c->reader->text,"Styled report") && strstr(c->reader->text,"TAIL_SHOWCASE") &&
           !strstr(c->reader->text,"HIDDEN_DRAFT") && tiku_doc_check(c->doc));
}

static void markdown(tiku_reader_help_window_t *c, const char *root, tiku_workspace_t *workspace)
{
    setenv("TIKU_LANG", "en", 1);
    fixture(root, "guide.MD", "# Markdown guide\n\nClock **frequency** in Markdown.\n\n"
        "## Example\n\n```basic\nPRINT \"café <&>\"\nEND\n```\n\n"
        "![sample](picture.png)\n\n[Next](next.html)\n");
    fixture(root, "other.markdown", "# Another Markdown page\n\nClock frequency.\n");
    assert(tiku_reader_help_window_go(c, "guide.MD#example") == TIKU_READER_HELP_PAGE);
    tiku_workspace_draw(workspace);
    assert(!strcmp(tiku_doc_title(c->doc), "Markdown guide"));
    assert(c->reader->nimages == 1 && decoded(c->reader, 0));
    assert(tiku_doc_anchor_y(c->doc, "example") >= 0);
    assert(tiku_reader_help_window_copy(c, 1));
    assert(!strcmp(tiku_clipboard_text(), "PRINT \"café <&>\"\nEND\n"));
    assert(tiku_reader_help_window_find(c, "clock frequency", 0));
    tiku_reader_action(c, HELP_OUTLINE); tiku_workspace_draw(workspace);
    assert(c->reader->nrows == 2);
    tiku_reader_search(c); reader_wait(c); assert(c->reader->nrows == 4);
    int found = 0;
    for (int i = 0; i < c->reader->nrows; i++)
        if (!strcmp(c->reader->rows[i].page, "guide.MD")) found++;
    assert(found == 1);
    assert(tiku_reader_help_window_go(c, tiku_doc_link_href(c->doc, 0)) == TIKU_READER_HELP_PAGE);
    assert(!strcmp(c->page, "next.html"));
    assert(tiku_reader_help_window_back(c) && !strcmp(c->page, "guide.MD"));
    assert(tiku_reader_help_window_forward(c) && !strcmp(c->page, "next.html"));
    char locale[1200]; snprintf(locale, sizeof locale, "%s/fr", root);
    fixture(locale, "guide.MD", "# Guide français\n\nBonjour Markdown.\n");
    setenv("TIKU_LANG", "fr", 1);
    assert(tiku_reader_help_window_go(c, "guide.MD") == TIKU_READER_HELP_PAGE);
    assert(strstr(c->reader->text, "Bonjour Markdown"));
    assert(tiku_reader_help_window_go(c, "other.markdown") == TIKU_READER_HELP_PAGE);
    assert(strstr(c->reader->text, "Another Markdown"));
    char path[1200]; snprintf(path, sizeof path, "%s/escape.md", root);
    assert(!symlink("../outside.html", path));
    assert(tiku_reader_help_window_go(c, "escape.md") == TIKU_READER_HELP_NOWHERE);
    setenv("TIKU_LANG", "en", 1);
}

int main(void)
{
    char base[] = "/tmp/tiku-help-reader.XXXXXX"; assert(mkdtemp(base));
    char root[1024], path[1024], marks[1024];
    snprintf(root, sizeof root, "%s/help", base); assert(!mkdir(root, 0700));
    snprintf(marks, sizeof marks, "%s/bookmarks", base); setenv("TIKU_HELP_BOOKMARKS", marks, 1);
    setenv("TIKU_LANG", "en", 1);
    fixture(root, "index.html", "<h1>Reader</h1><p>Clock <b>frequency</b> and clock frequency.</p>"
        "<h2>Examples</h2><pre>PRINT &quot;café &lt;&amp;&gt;&quot;\nEND\n</pre>"
        "<img src=\"picture.png\" alt=\"sample\"><p><a href=\"next.html#missing\">Next</a></p>");
    fixture(root, "next.html", "<h1>Next topic</h1><p>Clock frequency reference.</p>");
    tiku_surface_t *im = tiku_surface_new(20, 8, TIKU_RGB(200,10,20)); assert(im);
    snprintf(path, sizeof path, "%s/help/picture.png", base);
    assert(!tiku_surface_png(im, path)); tiku_surface_free(im);
    tiku_surface_t *surface = tiku_surface_new(1000, 760, TIKU_C_PANEL);
    tiku_workspace_t *workspace = tiku_workspace_new(surface); int dirty = 0;
    tiku_reader_help_window_t c;
    tiku_reader_help_window_init(&c, workspace, root, 1000, 760, &dirty);
    tiku_reader_help_window_open(&c); tiku_workspace_draw(workspace);
    assert(c.reader && c.reader->nimages == 1 && decoded(c.reader, 0));
    assert(tiku_reader_help_window_find(&c, "clock frequency", 0));
    assert(c.reader->matches == 2); int first = c.reader->match;
    assert(tiku_reader_help_window_find(&c, "clock frequency", 0) && c.reader->match > first);
    assert(tiku_reader_help_window_find(&c, "clock frequency", 1) && c.reader->match == first);
    assert(tiku_reader_help_window_copy(&c, 1));
    assert(!strcmp(tiku_clipboard_text(), "PRINT \"café <&>\"\nEND\n"));
    tiku_dialog_focus(&c.dialog, HELP_PAGE); press(&c, 'a', TIKU_MOD_CMD); press(&c, 'c', TIKU_MOD_CMD);
    assert(strstr(tiku_clipboard_text(), "Clock frequency") && !strstr(tiku_clipboard_text(), "<b>"));
    /* Real pointer drag selects UTF-8 text at the glyph positions drawn. */
    for (int i = 0; i < c.doc->npiece; i++) {
        const tiku_doc_piece_t *pc = &c.doc->piece[i];
        if (strncmp(c.doc->text+pc->off, "PRINT", 5)) continue;
        help_layout_t l = tiku_reader_layout(&c, tiku_window_content(c.window));
        tiku_rect_t box = tiku_doc_piece_rect(c.doc, i);
        tiku_event_t drag = {.type=TIKU_EVENT_POINTER_DOWN, .button=1,
            .x=l.page.x+box.x, .y=l.page.y+box.y-c.bar.value+box.h/2};
        tiku_window_send(c.window, &drag);
        drag.type = TIKU_EVENT_POINTER_MOVE; drag.x += box.w;
        tiku_window_send(c.window, &drag);
        drag.type = TIKU_EVENT_POINTER_UP; tiku_window_send(c.window, &drag);
        press(&c, 'c', TIKU_MOD_CMD);
        assert(strstr(tiku_clipboard_text(), "café <&>")); break;
    }
    press(&c, 'f', TIKU_MOD_CMD); assert(c.dialog.focus == HELP_FIND);
    tiku_field_start(&c.reader->query, "");
    tiku_event_t e; tiku_text_input_t input = {0};
    assert(tiku_text_input_set(&input, "café", strlen("café")));
    while (tiku_text_input_next(&input, &e)) tiku_window_send(c.window, &e);
    press(&c, TIKU_KEY_RETURN, 0);
    assert(c.reader->matches == 1 && !strcmp(c.reader->query.text, "café"));
    press(&c, 'g', TIKU_MOD_CMD); assert(c.reader->matches == 1);
    tiku_field_start(&c.reader->query, "clock frequency"); tiku_reader_search(&c);
    reader_wait(&c);
    assert(c.reader->nrows == 2 && c.reader->rows[0].snippet[0]);
    tiku_reader_action(&c, HELP_OUTLINE); tiku_workspace_draw(workspace);
    assert(c.reader->nrows == 2);
    tiku_dialog_focus(&c.dialog, HELP_SIDE); tiku_list_select_only(&c.reader->list, 0);
    press(&c, TIKU_KEY_LEFT, 0); tiku_workspace_draw(workspace); assert(c.reader->nrows == 1);
    press(&c, TIKU_KEY_RIGHT, 0); tiku_workspace_draw(workspace); assert(c.reader->nrows == 2);
    tiku_reader_bookmark(&c); assert(c.reader->nmarks == 1 && !access(marks, R_OK));
    assert(tiku_reader_help_window_go(&c, "next.html#missing") == TIKU_READER_HELP_PAGE);
    tiku_workspace_draw(workspace); assert(strstr(c.status, tiku_str("help.no_such_place")));
    assert(tiku_reader_help_window_html(&c, "Report <safe>", "<p>Generated &amp; readable.</p>"));
    tiku_workspace_draw(workspace);
    assert(strstr(c.reader->text, "Report <safe>") && strstr(c.reader->text, "Generated & readable."));
    char report[64]; strcpy(report, c.page);
    char *oversized = malloc(TIKU_READER_HELP_PAGE_MAX + 1); assert(oversized);
    memset(oversized, 'x', TIKU_READER_HELP_PAGE_MAX); oversized[TIKU_READER_HELP_PAGE_MAX] = 0;
    assert(!tiku_reader_help_window_html(&c, "Too large", oversized) && !strcmp(c.page, report));
    free(oversized);
    assert(tiku_reader_help_window_back(&c) && !strcmp(c.page, "next.html"));
    assert(tiku_reader_help_window_forward(&c) && !strcmp(c.page, report));
    char *large = malloc(7000); assert(large);
    strcpy(large, "<pre>"); memset(large+5, 'x', 5000); strcpy(large+5005, "</pre>");
    assert(tiku_reader_help_window_html(&c, "Long code", large));
    tiku_workspace_draw(workspace); tiku_clipboard_set("keep");
    assert(tiku_reader_help_window_copy(&c, 1));   /* 5000 bytes: past the old 4 KB */
    size_t xs = 0;
    for (const char *q = tiku_clipboard_text(); *q; q++) xs += *q == 'x';
    assert(xs == 5000); free(large);
    snprintf(path, sizeof path, "%s/help/fr", base); assert(!mkdir(path, 0700));
    fixture(path, "index.html", "<h1>Aide française</h1><p>Bonjour.</p>");
    setenv("TIKU_LANG", "fr", 1); assert(tiku_reader_help_window_home(&c));
    assert(strstr(c.reader->text, "Bonjour"));
    assert(tiku_reader_help_window_go(&c, "next.html") == TIKU_READER_HELP_PAGE);
    assert(strstr(c.reader->text, "Next topic"));
    fixture(base, "outside.html", "<h1>Outside</h1>");
    snprintf(path, sizeof path, "%s/help/escape.html", base); assert(!symlink("../outside.html", path));
    assert(tiku_reader_help_window_go(&c, "escape.html") == TIKU_READER_HELP_NOWHERE);
    assert(!tiku_reader_path(&c, "../outside.html", path, sizeof path));
    assert(!tiku_reader_path(&c, "https://example.com/picture.png", path, sizeof path));
    markdown(&c, root, workspace);
    setenv("TIKU_LANG", "en", 1);
    snprintf(path, sizeof path, "%s/help/long.html", base); FILE *f = fopen(path, "w"); assert(f);
    for (int i = 0; i < 60; i++) fprintf(f, "<h2 id='p%d'>Section %d</h2><p>Longer text which wraps when the reading window becomes narrow, preserving this paragraph.</p>", i, i);
    fclose(f);
    assert(tiku_reader_help_window_go(&c, "long.html#p20") == TIKU_READER_HELP_PAGE);
    tiku_workspace_draw(workspace); tiku_doc_place_t at = tiku_doc_place(c.doc, c.bar.value);
    c.window->frame.w = 670; tiku_workspace_draw(workspace);
    assert(tiku_doc_place(c.doc, c.bar.value).block == at.block);
    tiku_font_set_size(16); tiku_workspace_draw(workspace);
    assert(tiku_doc_place(c.doc, c.bar.value).block == at.block);
    assert(tiku_reader_help_window_go(&c, "next.html") == TIKU_READER_HELP_PAGE);
    tiku_font_set_size(12); tiku_workspace_draw(workspace);
    assert(tiku_reader_help_window_back(&c)); tiku_workspace_draw(workspace);
    assert(tiku_doc_place(c.doc, c.bar.value).block == at.block);
    /* Outline rows name blocks of the page they were built from, and the
     * fold flags are reallocated with each page -- smaller when the new
     * page has fewer blocks.  A fold key pressed after the page changed
     * and before the next frame rebuilt the rows wrote past them. */
    snprintf(path, sizeof path, "%s/help/many.html", base);
    f = fopen(path, "w"); assert(f);
    for (int i = 0; i < 300; i++) fprintf(f, "<h2>Section %d</h2>", i);
    fclose(f);
    snprintf(path, sizeof path, "%s/help/few.html", base);
    f = fopen(path, "w"); assert(f);
    fputs("<h1>Few</h1><p>", f);
    for (int i = 0; i < 4000; i++) fputs("word ", f);
    fputs("</p>", f); fclose(f);
    assert(tiku_reader_help_window_go(&c, "many.html") == TIKU_READER_HELP_PAGE);
    c.reader->side = 1; tiku_workspace_draw(workspace);
    assert(c.reader->nrows == HELP_ROWS);
    tiku_dialog_focus(&c.dialog, HELP_SIDE);
    tiku_list_select_only(&c.reader->list, c.reader->nrows - 1);
    assert(tiku_reader_help_window_go(&c, "few.html") == TIKU_READER_HELP_PAGE);
    assert(c.doc->nblock < HELP_ROWS && c.reader->nrows == HELP_ROWS);
    assert(c.bar.value == 0);           /* a new page starts at its top */
    press(&c, TIKU_KEY_LEFT, 0);        /* folds nothing, writes nothing */
    press(&c, TIKU_KEY_RETURN, 0);      /* opens nothing: the row is stale */
    assert(c.bar.value == 0);           /* so the page did not jump */
    tiku_workspace_draw(workspace);
    assert(c.reader->nrows <= 1 && !strcmp(c.page, "few.html"));

    fixture(root, "headings.html", "<H1>One</H1><H2>Two</H2><H3>Three</H3>"
            "<H4 ID='four'>Four</H4><H5>Five</H5><H6>Six</H6><H4>Sibling</H4>");
    assert(tiku_reader_help_window_go(&c, "headings.html") == TIKU_READER_HELP_PAGE);
    c.reader->side = 1; tiku_workspace_draw(workspace);
    tiku_reader_outline(&c);
    assert(c.reader->nrows == 7);
    for (int i = 0; i < 6; i++) assert(c.reader->rows[i].level == i + 1);
    assert(c.reader->rows[3].children && !c.reader->rows[5].children);
    c.reader->folded[3] = 1;
    tiku_reader_outline(&c);
    assert(c.reader->nrows == 5 && !strcmp(c.reader->rows[4].title, "Sibling"));
    c.reader->folded[3] = 0;
    tiku_reader_outline(&c);
    assert(tiku_reader_help_window_find(&c, "Six", 0));
    assert(tiku_reader_help_window_go(&c, "#four") == TIKU_READER_HELP_ANCHOR);

    styled_selection(&c,root,workspace);
    embedded_images(&c,root,workspace);
    showcase(&c,root,workspace);
    c.window->frame.w = 820; c.reader->side = 1; tiku_workspace_draw(workspace);
    assert(!tiku_surface_png(surface, "/tmp/tiku-help-reader.png"));
    tiku_reader_help_window_destroy(&c);
    tiku_reader_help_window_init(&c, workspace, root, 1000, 760, &dirty);
    assert(c.reader->nmarks == 1 && !strcmp(c.reader->marks[0].page, "index.html"));
    tiku_reader_help_window_destroy(&c); tiku_workspace_free(workspace); tiku_surface_free(surface);
    puts("PASS: help search, copying, images, reports, bookmarks, localization, confinement and reading position");
    return 0;
}
