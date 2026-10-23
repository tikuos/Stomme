# Showing a PDF: the view, or the document

The PDF kit offers a program two ways in, and the choice is the
program's. Both live in the translation kit, which every application
module already links.

| | The view: `tiku_pdf_view.h` | The document: `tiku_pdf.h` |
| --- | --- | --- |
| What you get | A finished pane: pages one under another, scrolled, zoomed, turned, links followed, text found, selected and copied | The parts: a page's pixels at any scale, its text and each character's box, links, the outline, signatures |
| Workers | Run for you: reading and drawing happen on `tiku_work` jobs | Yours to run: a page can take a while to draw |
| How much code | About ten calls | As much as your own viewer needs |
| Good for | A datasheet or manual shown beside the rest of your program | Thumbnails, printing, indexing, or a viewer of your own design |

TikuPDF (`apps/pdf/`) is built directly on `tiku_pdf.h`. The example
`examples/apps/pdfpane.c` (Demos > PDF Pane) is built on the view.

A program that only wants a PDF shown, in the desk's own viewer, needs
neither: `services->open_path(ctx, path)` hands it to TikuPDF.

## The view

Create it with the callbacks it should use, draw it into a rectangle of
your window, give it that window's events, and collect finished jobs in
your loop:

```c
static void changed(void *ctx) { ((app_t *)ctx)->dirty = 1; }

tiku_pdf_view_host_t host = { changed, NULL, NULL, app };
app->view = tiku_pdf_view_new(&host);
tiku_pdf_view_open(app->view, path, NULL);

/* the window's draw callback */
tiku_pdf_view_draw(app->view, surface, body);

/* its event handler, with the same rectangle */
if (tiku_pdf_view_event(app->view, body, ev)) return 1;

/* tick(): the pages the workers drew land here */
tiku_work_collect();

/* stop() */
tiku_pdf_view_free(app->view);
```

Draw and events must use the same rectangle. The view lays itself out
afresh in whatever rectangle it is given, so a window that resizes
needs nothing more.

**What the host callbacks are for.** `changed` asks for a redraw, and
usually just sets a flag. `opened` says how an opening ended. On
`TIKU_PDF_PASSWORD`, ask for the password and call
`tiku_pdf_view_open()` again with it. `address` receives a link that
leads out of the document; without it, the address goes to the
clipboard and the status says so. Every callback runs on your loop.

**What it handles by itself.** The wheel scrolls, and zooms with
Command. A click follows a link, and a drag selects text. The
scrollbars work. The arrows, Page Up and Page Down, Space, Home, End,
`+` and `-` move and zoom, and Escape lets a selection go. Command with
C, A, `[`, `+`, `-` and `0` copies, selects the page, goes back and
zooms. A program that binds any of these keys itself handles them
before passing the event on.

**What your menus and buttons call.** These are `tiku_pdf_view_go`,
`_back`, `_fit` (fit width, fit page, or a fixed scale), `_zoom`,
`_turn`, `_find` (with `TIKU_PDF_FIND_CASE` and `_WORDS`), `_copy` and
`_select_page`. `tiku_pdf_view_can()` says which of them would do
anything now, for enabling menu rows. `tiku_pdf_view_status()` gives a
sentence for a status line: where the link under the pointer leads,
else what happened last. For a "Page 3 of 12" readout, use
`tiku_pdf_view_page()` and `_pages()`.

**What it leaves to you.** It has no password field, only the error
that asks for one. It has no outline column, but
`tiku_pdf_view_outline()` gives the entries it read and
`tiku_pdf_view_follow()` goes to one. There are no thumbnails and no
signature panel; `tiku_pdf.h` has what those need.

## The document

Open it, and ask for what you need. A document is read by one thread at
a time, so give every job for one document the same `tiku_work` key and
never draw a page on your event loop:

```c
tiku_pdf_t *doc;
tiku_pdf_pixmap_t page;

if (tiku_pdf_open(path, &doc) == TIKU_PDF_OK) {
    if (tiku_pdf_render(doc, 0, 96.0 / 72.0, 0, &page) == 0) {
        /* page.px: page.w * page.h pixels, 0xAARRGGBB, top row first */
        tiku_pdf_pixmap_free(&page);
    }
    tiku_pdf_close(doc);
}
```

A scale is pixels to a point: 1 is 72 dpi. `tiku_pdf_render_rect()`
draws a band of a page at a time, for when memory is short. Text comes
as lines (`tiku_pdf_page_text`) or as characters with their boxes
(`tiku_pdf_page_chars`), which is what search highlighting and
selection use. The rest is `tiku_pdf_links`, `tiku_pdf_outline`,
`tiku_pdf_signatures`, and the password and certificate openers.
