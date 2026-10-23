/*
 * TikuDesktop Translation Kit.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richmarkup_markdown.c - editable Markdown conversion and loss notices.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richmarkup_private.h"
#include "tiku_markdown.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** @brief Retain a cancellation decision across counting and output passes. */
typedef struct {
    const tiku_richio_options_t *options;
    int stopped;
} markdown_cancel_t;

/** @brief Record the first cancellation request without calling back again. */
static int
cancelled(void *context)
{
    markdown_cancel_t *c = context;

    if (!c->stopped && c->options->cancel)
        c->stopped = c->options->cancel(c->options->ctx) != 0;
    return c->stopped;
}

tiku_richio_status_t
tiku_richmarkup_read_markdown(const void *data, size_t length,
    const tiku_richio_options_t *options, tiku_richdoc_t **out, unsigned *notices)
{
    tiku_richio_options_t settings;
    tiku_richio_status_t status;
    char *input, *html;
    size_t needed;
    unsigned differences = 0;
    tiku_markdown_options_t conversion = {.document = 1};
    markdown_cancel_t cancel = {.options = &settings};

    if (out) *out = NULL;
    if (notices) *notices = 0;
    if (!out || (!data && length)) return TIKU_RICHIO_INVALID;
    status = tiku_rio_options(options, &settings);
    if (status != TIKU_RICHIO_OK) return status;
    conversion.cancel = cancelled;
    conversion.context = &cancel;
    if (length > settings.xml_bytes || length >= settings.memory_bytes)
        return TIKU_RICHIO_LIMIT;
    if (length && memchr(data, 0, length)) return TIKU_RICHIO_DAMAGED;
    if (settings.cancel && settings.cancel(settings.ctx)) return TIKU_RICHIO_CANCELLED;
    input = malloc(length + 1);
    if (!input) return TIKU_RICHIO_NO_MEMORY;
    size_t skip = length >= 3 && !memcmp(data, "\xef\xbb\xbf", 3) ? 3 : 0;

    if (length > skip) memcpy(input, (const char *)data + skip, length - skip);
    input[length - skip] = 0;
    needed = tiku_markdown_convert(NULL, SIZE_MAX, input, &conversion);
    if (!needed || needed - 1 > settings.xml_bytes ||
        needed > settings.memory_bytes - length - 1) {
        free(input);
        return cancelled(&cancel) ?
               TIKU_RICHIO_CANCELLED : TIKU_RICHIO_LIMIT;
    }
    html = malloc(needed);
    if (!html) { free(input); return TIKU_RICHIO_NO_MEMORY; }
    if (settings.cancel && settings.cancel(settings.ctx)) status = TIKU_RICHIO_CANCELLED;
    else if (!tiku_markdown_convert(html, needed, input, &conversion))
        status = cancelled(&cancel) ?
                 TIKU_RICHIO_CANCELLED : TIKU_RICHIO_LIMIT;
    free(input);
    if (status == TIKU_RICHIO_OK) {
        if (settings.memory_bytes - needed < 2) status = TIKU_RICHIO_LIMIT;
        else {
            settings.memory_bytes -= needed;
            status = tiku_richmarkup_read_html(html, needed - 1, &settings,
                                               out, &differences);
        }
    }
    free(html);
    if (status == TIKU_RICHIO_OK && notices)
        *notices = differences | TIKU_RICHMARKUP_SYNTAX;
    return status;
}

/** @brief Escape Markdown punctuation without splitting UTF-8 characters. */
static void
escape(tiku_rio_writer_t *w, const char *text, size_t length, int link)
{
    for (size_t i = 0; i < length && w->status == TIKU_RICHIO_OK; i++) {
        if (text[i] == '\n' && !link) tiku_rio_literal(w, "  \n");
        else {
            if (strchr(link ? "\\()" : "\\`*_{}[]()#+-.!>|", text[i]))
                tiku_rio_literal(w, "\\");
            tiku_rio_put(w, text + i, 1);
        }
    }
}

/** @brief Emit one immutable paragraph, retaining supported inline semantics. */
static void
paragraph(tiku_rio_writer_t *w, const tiku_richsnapshot_t *s, size_t index,
          uint64_t ordinal, int continued_list, unsigned *notices)
{
    tiku_rich_paragraph_t p;
    tiku_rich_para_t properties;
    tiku_rich_chars_t c;
    unsigned level;
    size_t at = 0;

    tiku_richsnapshot_paragraph(s, index, &p);
    tiku_richsnapshot_resolve(s, index, 0, &c, &properties);
    level = properties.outline;
    if (index) tiku_rio_literal(w, continued_list ? "\n" : "\n\n");
    if (!p.length) *notices |= TIKU_RICHMARKUP_STRUCTURE;
    if (p.list.kind) {
        for (unsigned i = 0; i < p.list.level; i++) tiku_rio_literal(w, "    ");
        if (p.list.kind == TIKU_RICH_LIST_BULLET) tiku_rio_literal(w, "- ");
        else {
            char value[32];
            snprintf(value, sizeof value, "%llu. ", (unsigned long long)ordinal);
            tiku_rio_literal(w, value);
        }
    } else if (level) {
        for (unsigned i = 0; i < level; i++) tiku_rio_literal(w, "#");
        tiku_rio_literal(w, " ");
    }
    tiku_richsnapshot_resolve(s, index, 0, &c, &properties);
    if ((properties.present & ~TIKU_RICH_OUTLINE) || (p.style && !level))
        *notices |= TIKU_RICHMARKUP_STYLE;
    if (p.list.kind && level) *notices |= TIKU_RICHMARKUP_STRUCTURE;
    for (size_t i = 0; i < p.run_count; i++) {
        const tiku_rich_run_t *run = &p.runs[i];

        tiku_richsnapshot_resolve(s, index, i, &c, &properties);
        if (c.present & ~(TIKU_RICH_BOLD | TIKU_RICH_ITALIC))
            *notices |= TIKU_RICHMARKUP_STYLE;
        if (run->link) tiku_rio_literal(w, "[");
        int bold = (c.present & TIKU_RICH_BOLD) && c.bold;
        int italic = (c.present & TIKU_RICH_ITALIC) && c.italic;

        if (bold) tiku_rio_literal(w, "**");
        if (italic) tiku_rio_literal(w, "_");
        if (run->image) {
            tiku_rio_literal(w, "Image: ");
            escape(w, run->image->alt, strlen(run->image->alt), 0);
            *notices |= TIKU_RICHMARKUP_IMAGES;
        } else escape(w, p.text + at, run->length, 0);
        if (italic) tiku_rio_literal(w, "_");
        if (bold) tiku_rio_literal(w, "**");
        if (run->link) {
            tiku_rio_literal(w, "](");
            escape(w, run->link, strlen(run->link), 1);
            tiku_rio_literal(w, ")");
        }
        at += run->length;
    }
}

/** @brief Emit bounded Markdown, marking structural and layout differences. */
static void
document(tiku_rio_writer_t *w, const tiku_richsnapshot_t *s, unsigned *notices)
{
    uint64_t numbers[9] = {0}, parent = 0;
    tiku_rich_list_kind_t kinds[9] = {0};
    size_t index = 0;
    int previous_list = 0;

    for (size_t i = 0; i < tiku_richsnapshot_blocks(s); i++) {
        tiku_rich_block_t block;

        if (w->status != TIKU_RICHIO_OK) return;
        tiku_richsnapshot_block(s, i, &block);
        if (block.kind == TIKU_RICH_BLOCK_TABLE)
            *notices |= TIKU_RICHMARKUP_STRUCTURE;
        if (block.kind != TIKU_RICH_BLOCK_PARAGRAPH || block.parent != parent) {
            memset(numbers, 0, sizeof numbers);
            memset(kinds, 0, sizeof kinds);
            previous_list = 0;
        }
        if (block.kind != TIKU_RICH_BLOCK_PARAGRAPH) continue;
        parent = block.parent;
        tiku_rich_paragraph_t p;

        tiku_richsnapshot_paragraph(s, index, &p);
        if (!p.list.kind) {
            memset(numbers, 0, sizeof numbers);
            memset(kinds, 0, sizeof kinds);
        } else {
            unsigned level = p.list.level;

            for (unsigned j = level + 1; j < 9; j++) {
                numbers[j] = 0; kinds[j] = TIKU_RICH_LIST_NONE;
            }
            if (kinds[level] != p.list.kind) numbers[level] = 0;
            kinds[level] = p.list.kind;
            if (p.list.kind == TIKU_RICH_LIST_DECIMAL) {
                if (p.list.restart) numbers[level] = p.list.restart;
                else numbers[level]++;
            }
        }
        paragraph(w, s, index++, numbers[p.list.level],
                  previous_list && p.list.kind, notices);
        previous_list = p.list.kind != 0;
    }
    tiku_rio_literal(w, "\n");
}

tiku_richio_status_t
tiku_richmarkup_write_markdown(const tiku_richsnapshot_t *snapshot,
    const tiku_richio_options_t *options, unsigned char **out, size_t *length,
    unsigned *notices)
{
    tiku_rio_writer_t w = {0};
    unsigned differences = TIKU_RICHMARKUP_PAGES | TIKU_RICHMARKUP_SYNTAX;

    if (out) *out = NULL;
    if (length) *length = 0;
    if (notices) *notices = 0;
    if (!snapshot || !out || !length) return TIKU_RICHIO_INVALID;
    w.status = tiku_rio_options(options, &w.options);
    if (w.status != TIKU_RICHIO_OK) return w.status;
    if (tiku_richsnapshot_bookmarks(snapshot)) differences |= TIKU_RICHMARKUP_BOOKMARKS;
    document(&w, snapshot, &differences);
    if (w.status != TIKU_RICHIO_OK) return w.status;
    w.data = malloc(w.used + 1);
    if (!w.data) return TIKU_RICHIO_NO_MEMORY;
    w.used = 0;
    document(&w, snapshot, &differences);
    if (w.status != TIKU_RICHIO_OK) { free(w.data); return w.status; }
    w.data[w.used] = 0;
    *out = w.data;
    *length = w.used;
    if (notices) *notices = differences;
    return TIKU_RICHIO_OK;
}

void
tiku_richmarkup_report(unsigned notices, char *out, size_t capacity)
{
    static const char *const descriptions[] = {
        "Some styles differ", "Active content omitted", "Pictures use alt text",
        "Some structure differs", "Markdown/HTML subset", "Page layout differs",
        "Bookmark targets omitted"
    };
    size_t used = 0;

    if (!out || !capacity) return;
    *out = 0;
    for (unsigned i = 0; i < 7; i++) if (notices & (1u << i)) {
        int n = snprintf(out + used, capacity - used, "%s%s", used ? "; " : "",
                         descriptions[i]);

        if (n < 0 || (size_t)n >= capacity - used) break;
        used += (size_t)n;
    }
}
