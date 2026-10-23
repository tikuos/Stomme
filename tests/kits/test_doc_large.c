/* Larger caller-owned tables must not wrap the original 16-bit indices. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "tiku_doc.h"
#include "tiku_markdown.h"

/** @brief Validate the storage contract and preservation on invalid binding. */
static void storage_contract(tiku_doc_t *d, tiku_doc_limits_t limits)
{
    size_t bytes = tiku_doc_storage_size(&limits);
    assert(bytes);
    void *memory = malloc(bytes + 8); assert(memory);
    tiku_doc_init(d); assert(tiku_doc_read(d, "<h1>Retained</h1>"));
    assert(!tiku_doc_init_storage(d, &limits, memory, bytes - 1));
    assert(!strcmp(tiku_doc_title(d), "Retained"));
    assert(!tiku_doc_init_storage(d, &limits, (char *)memory + 1, bytes));
    assert(tiku_doc_init_storage(d, &limits, memory, bytes));
    assert(d->storage == memory && d->limits.text == limits.text);
    limits.text = -1; assert(!tiku_doc_storage_size(&limits));
    limits.text = INT32_MAX; assert(!tiku_doc_storage_size(&limits));
    assert(!tiku_doc_storage_size(NULL));
}

/** @brief One preformatted run and one block may both exceed 32767. */
static void tall_code(tiku_doc_t *d)
{
    char *html = malloc(120032); assert(html);
    strcpy(html, "<pre>");
    for (int i = 0; i < 120000; i++) html[i + 5] = i % 20 == 19 ? '\n' : 'x';
    strcpy(html + 120005, "</pre>");
    void *storage = d->storage;
    assert(tiku_doc_read(d, html) && d->storage == storage);
    assert(d->run[0].len == 120000);
    assert(tiku_doc_layout(d, 320) > 32767 && d->block[0].h > 32767);
    assert(d->nline > 5000 && tiku_doc_check(d));
    assert(!(d->full & (TIKU_DOC_FULL_LINES | TIKU_DOC_FULL_PIECES)));
    free(html);
}

/** @brief Late anchors and links retain full-width run/block/line indices. */
static void many_blocks(tiku_doc_t *d)
{
    char *html = malloc(400000); assert(html);
    size_t at = 0;
    for (int i = 0; i < 33000; i++) {
        memcpy(html + at, "<p>x</p>", 8); at += 8;
    }
    strcpy(html + at, "<h1 id=\"last\">Last heading</h1>"
        "<p><a href=\"#last\">Late link</a></p>");
    assert(tiku_doc_read(d, html));
    assert(d->nblock > 32767 && d->nrun > 32767);
    (void)tiku_doc_layout(d, 320);
    assert(d->nline > 32767 && d->npiece > 32767);
    assert(d->anchor[0].block == 33000);
    assert(d->link[0].run0 > 32767 && d->link[0].piece0 > 32767);
    assert(tiku_doc_anchor_y(d, "last") > 32767);
    assert(tiku_doc_check(d));
    assert(!(d->full & (TIKU_DOC_FULL_LINES | TIKU_DOC_FULL_PIECES)));
    free(html);
}

/** @brief Conversion allocates its exact output and bounds pathological scans. */
static void markdown_size(void)
{
    const char *md = "# Sizing\n\n**Bold** and <literal> & text.\n";
    size_t size = tiku_markdown_html_size(md); assert(size);
    char *html = malloc(size); assert(html);
    assert(tiku_markdown_html(html, size, md) && strlen(html) + 1 == size);
    assert(!tiku_markdown_html(html, size - 1, md) && !html[0]);
    free(html);
    char *bad = malloc(16385); assert(bad);
    memset(bad, '[', 16384); bad[16384] = 0;
    assert(!tiku_markdown_html_size(bad));
    free(bad);
}

int main(void)
{
    tiku_doc_t *d = calloc(1, sizeof *d); assert(d);
    tiku_doc_limits_t limits = {524288, 65536, 65536, 65536,
                                131072, 1024, 1024, 64};
    storage_contract(d, limits);
    tall_code(d); many_blocks(d); markdown_size();
    free(d->storage); free(d);
    puts("PASS: large document storage, 32-bit runs/geometry/indices and Markdown sizing");
    return 0;
}
