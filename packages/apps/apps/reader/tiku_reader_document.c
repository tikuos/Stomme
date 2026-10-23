/* tiku_reader_document.c -- a local document read whole and parsed into a
 * page whose tables start sized to it and grow as it fills them.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_reader_document.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include "tiku_markdown.h"
#include "tiku_html_document.h"

static int reader_parse(tiku_doc_t *doc, const char *source, void *ctx);

/** @brief The allocator in force: the C library's unless a test set one. */
static tiku_reader_allocator_t hooks = { malloc, calloc };

void tiku_reader_test_allocator(const tiku_reader_allocator_t *allocator)
{
    if (allocator) {
        hooks = *allocator;
    } else {
        hooks.malloc_fn = malloc;
        hooks.calloc_fn = calloc;
    }
}

void *tiku_reader_malloc(size_t n)
{
    return hooks.malloc_fn(n);
}

void *tiku_reader_calloc(size_t n, size_t size)
{
    return hooks.calloc_fn(n, size);
}

int tiku_reader_markdown(const char *name)
{
    const char *ext = strrchr(name, '.');
    return ext && (!strcasecmp(ext, ".md") || !strcasecmp(ext, ".markdown"));
}

/** @brief Parse @p source into @p doc, Markdown converted to HTML first
 *         within TIKU_READER_MEMORY_MAX.  The page and the folder search
 *         both parse through here.  @return 1, or 0 when it failed. */
static int reader_parse(tiku_doc_t *doc, const char *source, void *ctx)
{
    const char *name = ctx;
    if (!tiku_reader_markdown(name)) { tiku_doc_read(doc, source); return 1; }
    size_t cap = tiku_markdown_html_size(source);
    if (!cap || cap > TIKU_READER_MEMORY_MAX) return 0;
    char *html = tiku_reader_malloc(cap);
    if (!html) return 0;
    int ok = tiku_markdown_html(html, cap, source);
    if (ok) tiku_doc_read(doc, html);
    free(html); return ok;
}

/** @brief Model options shared by loading, searching and reflow. */
static tiku_html_options_t reader_options(const char *name)
{
    return (tiku_html_options_t){
        .source_bytes=TIKU_READER_MEMORY_MAX, .memory_bytes=TIKU_READER_MEMORY_MAX,
        .allocate=tiku_reader_malloc, .zero=tiku_reader_calloc, .release=free,
        .parse=reader_parse, .parse_ctx=(void *)name
    };
}

void tiku_reader_doc_free(tiku_doc_t *doc)
{
    tiku_html_document_free(doc,NULL);
}

tiku_doc_t *tiku_reader_document(const char *name, const char *source)
{
    tiku_html_options_t options = reader_options(name);
    return tiku_html_document_load(source,256,&options);
}

tiku_doc_t *tiku_reader_document_text(const char *name, const char *source)
{
    tiku_html_options_t options = reader_options(name);
    return tiku_html_document_load(source,0,&options);
}

void tiku_reader_document_layout(tiku_doc_t **current, int width)
{
    tiku_html_options_t options = reader_options(NULL);
    (void)tiku_html_document_reflow(current,width,&options);
}

char *tiku_reader_source(const char *path, const char **why)
{
    struct stat st;
    *why = "Could not read the document.";
    int fd = open(path, O_RDONLY | O_NONBLOCK);
    if (fd < 0) return NULL;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size < 0) {
        close(fd); return NULL;
    }
    if ((uint64_t)st.st_size > TIKU_READER_FILE_MAX) {
        *why = "File exceeds the 8 MiB desktop safety limit; nothing was replaced.";
        close(fd); return NULL;
    }
    size_t cap = (size_t)st.st_size;
    char *source = tiku_reader_malloc(cap + 1);
    FILE *f = fdopen(fd, "rb");
    if (!f || !source) {
        if (f) fclose(f); else close(fd);
        free(source); return NULL;
    }
    size_t n = fread(source, 1, cap, f);
    int extra = fgetc(f), bad = ferror(f);
    fclose(f);
    if (bad || n != cap || extra != EOF || memchr(source, 0, n)) {
        *why = "Document changed during reading, or contains binary data; nothing was replaced.";
        free(source); return NULL;
    }
    source[n] = 0;
    return source;
}
