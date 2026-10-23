/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richdoc_search.c - bounded literal search over immutable paragraphs.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richdoc_private.h"
#include "tiku_richtext_private.h"

#include <string.h>

/** @brief A byte cursor including the virtual LF between paragraphs. */
typedef struct {
    size_t paragraph;         /**< container index */
    size_t byte;              /**< next text byte or separator */
} search_cursor_t;

/** @brief Streaming prefix matcher with a second cursor for result starts. */
struct tiku_richsearch {
    tiku_rd_root_t *root;     /**< retained immutable root */
    uint64_t generation;     /**< captured publication generation */
    char *needle;            /**< copied UTF-8 bytes */
    size_t *prefix;          /**< longest matching proper prefix per byte */
    size_t length;           /**< needle byte length */
    size_t matched;          /**< matched prefix byte count */
    size_t seen;             /**< consumed bytes, saturated at length */
    search_cursor_t cursor;  /**< next input byte */
    search_cursor_t window;  /**< earliest byte in the last length bytes */
    search_cursor_t end;     /**< exclusive search bound */
    unsigned flags;          /**< whole-word filtering */
    uint8_t *boundaries;      /**< separator-inclusive word/grapheme flags */
    size_t *base;            /**< absolute byte base for each paragraph */
    size_t preparing, prepared_byte; /**< incremental segmentation cursors */
    tiku_rt_word_t word;      /**< current paragraph word-boundary state */
    tiku_rt_grapheme_t grapheme; /**< current paragraph grapheme state */
};

/** @brief Consume one text byte or one paragraph separator in constant time. */
static void
advance(const tiku_rd_root_t *root, search_cursor_t *cursor)
{
    if (cursor->byte == root->paragraphs[cursor->paragraph]->value.length) {
        cursor->paragraph++;
        cursor->byte = 0;
    } else
        cursor->byte++;
}

/** @brief Convert an internal byte cursor to a snapshot-relative position. */
static tiku_rich_pos_t
position(const tiku_richsearch_t *search, search_cursor_t cursor,
           tiku_rich_affinity_t affinity)
{
    return (tiku_rich_pos_t){search->root->body,
        search->root->paragraphs[cursor.paragraph]->value.id,
        cursor.byte, affinity};
}

/** @brief Whether the scanner has consumed its entire half-open scope. */
static int
finished(const tiku_richsearch_t *search)
{
    return search->cursor.paragraph == search->end.paragraph &&
           search->cursor.byte == search->end.byte;
}

/** @brief Build the needle's failure links in linear time. */
static void
index_needle(tiku_richsearch_t *search)
{
    size_t i, matched = 0;

    for (i = 1; i < search->length; i++) {
        while (matched && search->needle[i] != search->needle[matched])
            matched = search->prefix[matched - 1];
        if (search->needle[i] == search->needle[matched])
            matched++;
        search->prefix[i] = matched;
    }
}

tiku_rich_status_t
tiku_richsearch_create(const tiku_richsnapshot_t *snapshot,
    tiku_rich_pos_t first, tiku_rich_pos_t last, const char *needle,
    size_t length, tiku_richsearch_t **out)
{
    return tiku_richsearch_create_options(snapshot, first, last, needle,
                                           length, 0, out);
}

/** @brief Reserve charged boundary storage and paragraph byte offsets. */
static tiku_rich_status_t
prepare_storage(const tiku_richsnapshot_t *snapshot, tiku_richsearch_t *search)
{
    size_t count = snapshot->root->count, bytes = snapshot->root->bytes;
    tiku_rich_status_t status = TIKU_RICH_OK;

    if (count > SIZE_MAX - bytes || count > SIZE_MAX / sizeof(size_t))
        return TIKU_RICH_LIMIT;
    search->boundaries = tiku_rd_alloc(snapshot->memory,
                                       bytes + count, &status);
    if (!search->boundaries) return status;
    search->base = tiku_rd_alloc(snapshot->memory,
                                 count * sizeof(size_t), &status);
    if (!search->base) return status;
    for (size_t i = 1; i < count; i++)
        search->base[i] = search->base[i - 1] +
            snapshot->root->paragraphs[i - 1]->value.length + 1;
    return TIKU_RICH_OK;
}

tiku_rich_status_t
tiku_richsearch_create_options(const tiku_richsnapshot_t *snapshot,
    tiku_rich_pos_t first, tiku_rich_pos_t last, const char *needle,
    size_t length, unsigned flags, tiku_richsearch_t **out)
{
    tiku_rich_status_t status;
    tiku_richsearch_t *search;
    int order;

    if (!out)
        return TIKU_RICH_INVALID;
    *out = NULL;
    if (!snapshot || !length || !needle ||
        (flags & ~TIKU_RICH_SEARCH_WHOLE_WORD))
        return TIKU_RICH_INVALID;
    status = tiku_richsnapshot_compare(snapshot, first, last, &order);
    if (status != TIKU_RICH_OK)
        return status;
    if (order > 0)
        return TIKU_RICH_INVALID_RANGE;
    if (length > SIZE_MAX / sizeof(size_t) ||
        length > snapshot->memory->limit / sizeof(size_t))
        return TIKU_RICH_LIMIT;
    status = tiku_rd_utf8(needle, length, 1);
    if (status != TIKU_RICH_OK)
        return status;
    search = tiku_rd_alloc(snapshot->memory, sizeof *search, &status);
    if (!search)
        return status;
    search->needle = tiku_rd_alloc(snapshot->memory, length, &status);
    if (!search->needle)
        goto fail;
    search->prefix = tiku_rd_alloc(snapshot->memory,
                                    length * sizeof *search->prefix, &status);
    if (!search->prefix)
        goto fail;
    memcpy(search->needle, needle, length);
    search->length = length;
    index_needle(search);
    search->root = snapshot->root;
    atomic_fetch_add_explicit(&search->root->refs, 1, memory_order_relaxed);
    search->generation = snapshot->generation;
    search->cursor = (search_cursor_t){
        tiku_rd_find(search->root, first.paragraph), first.byte};
    search->window = search->cursor;
    search->end = (search_cursor_t){
        tiku_rd_find(search->root, last.paragraph), last.byte};
    search->flags = flags;
    if (flags) {
        status = prepare_storage(snapshot, search);
        if (status != TIKU_RICH_OK) goto fail;
    }
    *out = search;
    return TIKU_RICH_OK;
fail:
    tiku_richsearch_release(search);
    return status;
}

/** @brief Prepare one scalar or paragraph boundary per work unit. */
static void
prepare_boundaries(tiku_richsearch_t *search, size_t work)
{
    while (work-- && search->preparing < search->root->count) {
        const tiku_rich_paragraph_t *p =
            &search->root->paragraphs[search->preparing]->value;
        size_t start = search->prepared_byte;
        size_t base = search->base[search->preparing];
        uint32_t cp;
        tiku_rt_props_t props;

        if (start == p->length) {
            search->boundaries[base] = search->boundaries[base + start] = 3;
            search->preparing++;
            search->prepared_byte = 0;
            memset(&search->word, 0, sizeof search->word);
            memset(&search->grapheme, 0, sizeof search->grapheme);
            continue;
        }
        tiku_rt_decode(p->text, p->length, &search->prepared_byte, &cp);
        props = tiku_rt_properties(cp);
        if (tiku_rt_grapheme(&search->grapheme, props))
            search->boundaries[base + start] |= TIKU_RICH_GRAPHEME;
        if (tiku_rt_word(&search->word, props, p->text, p->length,
                         search->prepared_byte))
            search->boundaries[base + start] |= TIKU_RICH_WORD;
    }
}

tiku_rich_status_t
tiku_richsearch_next(tiku_richsearch_t *search, size_t work,
                      tiku_rich_match_t *out)
{
    const tiku_rich_paragraph_t *p;
    char byte;
    int equal;

    if (!out)
        return TIKU_RICH_INVALID;
    memset(out, 0, sizeof *out);
    if (!search || !work)
        return TIKU_RICH_INVALID;
    if (search->flags && search->preparing < search->root->count) {
        prepare_boundaries(search, work);
        return TIKU_RICH_PENDING;
    }
    while (work-- && !finished(search)) {
        p = &search->root->paragraphs[search->cursor.paragraph]->value;
        byte = search->cursor.byte == p->length ? '\n' :
               p->text[search->cursor.byte];
        equal = byte == search->needle[search->matched];
        if (!equal && search->matched) {
            search->matched = search->prefix[search->matched - 1];
            continue;
        }
        if (equal)
            search->matched++;
        if (search->seen == search->length)
            advance(search->root, &search->window);
        else
            search->seen++;
        advance(search->root, &search->cursor);
        if (search->matched == search->length) {
            if (search->flags &&
                (search->boundaries[search->base[search->window.paragraph] +
                 search->window.byte] != 3 ||
                 search->boundaries[search->base[search->cursor.paragraph] +
                 search->cursor.byte] != 3)) {
                search->matched = search->prefix[search->length - 1];
                continue;
            }
            out->generation = search->generation;
            out->first = position(search, search->window, TIKU_RICH_AFTER);
            out->last = position(search, search->cursor, TIKU_RICH_BEFORE);
            search->matched = 0;
            return TIKU_RICH_OK;
        }
    }
    return finished(search) ? TIKU_RICH_NOT_FOUND : TIKU_RICH_PENDING;
}

void
tiku_richsearch_release(tiku_richsearch_t *search)
{
    if (!search)
        return;
    tiku_rd_root_drop(search->root);
    tiku_rd_free(search->needle);
    tiku_rd_free(search->prefix);
    tiku_rd_free(search->boundaries);
    tiku_rd_free(search->base);
    tiku_rd_free(search);
}
