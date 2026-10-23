/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richdoc_edit.c - paragraph-local replacement and direct formatting.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richdoc_private.h"

#include <string.h>

int
tiku_rd_chars_equal(const tiku_rich_chars_t *a, const tiku_rich_chars_t *b)
{
    return a->present == b->present && a->bold == b->bold &&
        a->italic == b->italic && a->underline == b->underline &&
        a->strike == b->strike && a->size == b->size &&
        !strcmp(a->font, b->font) && a->foreground == b->foreground &&
        a->highlight == b->highlight && a->baseline == b->baseline;
}

void
tiku_rd_append_run(tiku_rich_paragraph_t *out, const tiku_rich_run_t *run)
{
    tiku_rich_run_t *runs = (tiku_rich_run_t *)out->runs;
    tiku_rich_run_t *last;

    if (!run->length)
        return;
    if (out->run_count) {
        last = &runs[out->run_count - 1];
        if (!last->image && !run->image &&
            last->style == run->style && last->reset == run->reset &&
            tiku_rd_link_equal(last->link, run->link) &&
            tiku_rd_chars_equal(&last->chars, &run->chars)) {
            last->length += run->length;
            return;
        }
    }
    runs[out->run_count++] = *run;
    tiku_rd_link_retain(run->link);
    tiku_rd_image_retain(run->image);
}

void
tiku_rd_slice_runs(tiku_rich_paragraph_t *out,
            const tiku_rich_paragraph_t *from,
            size_t start, size_t end, const tiku_rich_chars_t *chars)
{
    size_t i, at = 0, left, right;
    tiku_rich_run_t run;

    for (i = 0; i < from->run_count && at < end; i++) {
        run = from->runs[i];
        left = at > start ? at : start;
        right = at + run.length < end ? at + run.length : end;
        at += run.length;
        if (left >= right)
            continue;
        run.length = right - left;
        if (chars)
            tiku_rd_chars_overlay(&run.chars, chars);
        tiku_rd_append_run(out, &run);
    }
}

size_t
tiku_rd_run_count(const tiku_rich_paragraph_t *p, size_t first, size_t last)
{
    size_t i, at = 0, count = 0;

    if (first == last)
        return 0;
    for (i = 0; i < p->run_count && at < last; i++) {
        if (first < at + p->runs[i].length)
            count++;
        at += p->runs[i].length;
    }
    return count;
}

tiku_rich_run_t
tiku_rd_typing_run(const tiku_rich_paragraph_t *p, size_t offset)
{
    tiku_rich_run_t run = {0};
    size_t i, at = 0;

    if (!offset)
        return run;
    for (i = 0; i < p->run_count; i++) {
        at += p->runs[i].length;
        if (offset <= at)
            return p->runs[i];
    }
    return run;
}

/** @brief Compare text and direct runs of two otherwise identical nodes. */
static int
content_equal(const tiku_rich_paragraph_t *a, const tiku_rich_paragraph_t *b)
{
    size_t i;

    if (a->length != b->length || a->run_count != b->run_count ||
        memcmp(a->text, b->text, a->length))
        return 0;
    for (i = 0; i < a->run_count; i++)
        if (a->runs[i].length != b->runs[i].length ||
            a->runs[i].style != b->runs[i].style ||
            a->runs[i].reset != b->runs[i].reset ||
            a->runs[i].image != b->runs[i].image ||
            !tiku_rd_link_equal(a->runs[i].link, b->runs[i].link) ||
            !tiku_rd_chars_equal(&a->runs[i].chars, &b->runs[i].chars))
            return 0;
    return 1;
}

/** @brief Validate offsets, text and the replacement's aggregate byte size. */
static tiku_rich_status_t
check_edit(tiku_richdoc_t *doc, const tiku_rd_root_t *root,
            const tiku_rich_paragraph_t *p, const tiku_rich_edit_t *edit,
            tiku_rich_chars_t *chars)
{
    tiku_rich_status_t status;
    size_t retained;

    if (edit->kind != TIKU_RICH_REPLACE && edit->kind != TIKU_RICH_FORMAT &&
        edit->kind != TIKU_RICH_CHARACTER_STYLE_SET &&
        edit->kind != TIKU_RICH_LINK_SET &&
        edit->kind != TIKU_RICH_IMAGE_INSERT &&
        edit->kind != TIKU_RICH_IMAGE_SET)
        return TIKU_RICH_UNSUPPORTED;
    if (edit->start > edit->end ||
        !tiku_rd_boundary(p->text, p->length, edit->start) ||
        !tiku_rd_boundary(p->text, p->length, edit->end))
        return TIKU_RICH_INVALID_RANGE;
    if (edit->kind == TIKU_RICH_LINK_SET) {
        status = tiku_rich_link_validate(edit->link);
        if (status != TIKU_RICH_OK)
            return status;
        return edit->start == edit->end ? TIKU_RICH_NO_CHANGE : TIKU_RICH_OK;
    }
    if (edit->kind == TIKU_RICH_CHARACTER_STYLE_SET) {
        if (!tiku_rd_style_ref(root, edit->style, TIKU_RICH_CHARACTER_STYLE))
            return TIKU_RICH_INVALID_STYLE;
        return edit->start == edit->end ? TIKU_RICH_NO_CHANGE : TIKU_RICH_OK;
    }
    if (edit->kind == TIKU_RICH_FORMAT && (edit->clear_chars & ~511u))
        return TIKU_RICH_INVALID_STYLE;
    status = tiku_rd_chars(&edit->chars, chars);
    if (status != TIKU_RICH_OK)
        return status;
    if (edit->kind == TIKU_RICH_FORMAT)
        return edit->start == edit->end ||
               (!chars->present && !edit->clear_chars)
            ? TIKU_RICH_NO_CHANGE : TIKU_RICH_OK;
    retained = root->bytes - (edit->end - edit->start);
    if (edit->length > doc->limits.text_bytes - retained ||
        edit->length >= SIZE_MAX - retained)
        return TIKU_RICH_LIMIT;
    status = tiku_rd_utf8(edit->text, edit->length,
        edit->kind == TIKU_RICH_IMAGE_INSERT ||
        edit->kind == TIKU_RICH_IMAGE_SET ? 2 : 1);
    if (status != TIKU_RICH_OK)
        return status;
    return edit->start == edit->end && !edit->length
        ? TIKU_RICH_NO_CHANGE : TIKU_RICH_OK;
}

tiku_rd_paragraph_t *
tiku_rd_paragraph_new(tiku_richdoc_t *doc, const tiku_rich_paragraph_t *from,
                       size_t length, size_t capacity,
                       tiku_rich_status_t *status)
{
    tiku_rd_paragraph_t *node;
    tiku_rich_paragraph_t *p;

    if (length == SIZE_MAX || (length && !capacity) ||
        capacity > SIZE_MAX / sizeof(tiku_rich_run_t)) {
        *status = TIKU_RICH_LIMIT;
        return NULL;
    }
    node = tiku_rd_alloc(doc->memory, sizeof *node, status);
    if (!node)
        return NULL;
    atomic_init(&node->refs, 1);
    p = &node->value;
    *p = *from;
    p->text = NULL;
    p->runs = NULL;
    p->run_count = 0;
    p->length = length;
    p->text = tiku_rd_alloc(doc->memory, length + 1, status);
    if (!p->text)
        goto fail;
    if (length) {
        p->runs = tiku_rd_alloc(doc->memory,
            capacity * sizeof *p->runs, status);
        if (!p->runs)
            goto fail;
    }
    return node;
fail:
    tiku_rd_paragraph_drop(node);
    return NULL;
}

/** @brief Copy a selected slice with a named style or changed overrides. */
static void
format_runs(tiku_rich_paragraph_t *out, const tiku_rich_paragraph_t *from,
              const tiku_rich_edit_t *edit, const tiku_rich_chars_t *chars,
              const char *link)
{
    size_t i, at = 0, left, right;
    tiku_rich_run_t run;

    for (i = 0; i < from->run_count && at < edit->end; i++) {
        run = from->runs[i];
        left = at > edit->start ? at : edit->start;
        right = at + run.length < edit->end ? at + run.length : edit->end;
        at += run.length;
        if (left >= right)
            continue;
        run.length = right - left;
        if (edit->kind == TIKU_RICH_LINK_SET)
            run.link = link;
        else if (edit->kind == TIKU_RICH_CHARACTER_STYLE_SET)
            run.style = edit->style;
        else {
            tiku_rd_chars_reset(&run.chars, edit->clear_chars);
            run.reset &= ~edit->clear_chars;
            tiku_rd_chars_overlay(&run.chars, chars);
        }
        tiku_rd_append_run(out, &run);
    }
}

const char *
tiku_rd_typing_link(const tiku_rich_paragraph_t *p, size_t start, size_t end)
{
    size_t i, at = 0;
    const char *left = NULL, *right = NULL;

    if (start < end) {
        for (i = 0; i < p->run_count && at < end; i++) {
            if (at + p->runs[i].length > start) {
                if (!p->runs[i].link ||
                    (left && !tiku_rd_link_equal(left, p->runs[i].link)))
                    return NULL;
                left = p->runs[i].link;
            }
            at += p->runs[i].length;
        }
        return left;
    }
    if (!start || end >= p->length)
        return NULL;
    for (i = 0; i < p->run_count; i++) {
        if (at < start && start <= at + p->runs[i].length)
            left = p->runs[i].link;
        if (at <= end && end < at + p->runs[i].length)
            right = p->runs[i].link;
        at += p->runs[i].length;
    }
    if (!left || !tiku_rd_link_equal(left, right))
        return NULL;
    return left;
}

/** @brief Populate a replacement paragraph from validated spans. */
static void
fill_paragraph(tiku_richdoc_t *doc, tiku_rich_paragraph_t *out,
                 const tiku_rich_paragraph_t *from,
                 const tiku_rich_edit_t *edit, const tiku_rich_chars_t *chars,
                 const char *link, const tiku_rich_image_t *image,
                 int image_set)
{
    char *text = (char *)out->text;
    tiku_rich_run_t run;

    tiku_rd_slice_runs(out, from, 0, edit->start, NULL);
    if (edit->kind != TIKU_RICH_REPLACE) {
        memcpy(text, from->text, from->length);
        format_runs(out, from, edit, chars, link);
    } else {
        memcpy(text, from->text, edit->start);
        if (edit->length)
            memcpy(text + edit->start, edit->text, edit->length);
        memcpy(text + edit->start + edit->length, from->text + edit->end,
               from->length - edit->end);
        run = tiku_rd_typing_run(from, edit->start + (image_set ? 1 : 0));
        run.image = image;
        if (!image_set)
            run.link = tiku_rd_typing_link(from, edit->start, edit->end);
        run.length = edit->length;
        if (doc->initiator && doc->initiator->typing)
            tiku_rs_typing(doc->initiator, &run);
        tiku_rd_chars_overlay(&run.chars, chars);
        tiku_rd_append_run(out, &run);
    }
    tiku_rd_slice_runs(out, from, edit->end, from->length, NULL);
}

tiku_rd_paragraph_t *
tiku_rd_edit(tiku_richdoc_t *doc, const tiku_rd_root_t *root,
              const tiku_rich_edit_t *edit, tiku_rich_status_t *status)
{
    size_t index = tiku_rd_find(root, edit->paragraph), length;
    const tiku_rich_paragraph_t *p;
    tiku_rd_paragraph_t *node;
    tiku_rich_chars_t chars;
    const char *link = NULL;
    const tiku_rich_image_t *image = NULL;
    tiku_rich_edit_t normalized;
    int image_set = edit->kind == TIKU_RICH_IMAGE_SET;

    if (index == SIZE_MAX) {
        *status = TIKU_RICH_INVALID_RANGE;
        return NULL;
    }
    p = &root->paragraphs[index]->value;
    if (edit->kind == TIKU_RICH_IMAGE_INSERT ||
        edit->kind == TIKU_RICH_IMAGE_SET) {
        normalized = *edit;
        normalized.text = "\xef\xbf\xbc";
        normalized.length = 3;
        edit = &normalized;
        *status = tiku_rich_image_validate(edit->image);
        if (*status != TIKU_RICH_OK) return NULL;
        if (edit->kind == TIKU_RICH_IMAGE_SET) {
            size_t at = 0, i = 0;

            while (i < p->run_count && at < edit->start)
                at += p->runs[i++].length;
            if (at != edit->start || i == p->run_count ||
                !p->runs[i].image || edit->end - edit->start != 3) {
                *status = TIKU_RICH_INVALID_RANGE;
                return NULL;
            }
            image = p->runs[i].image;
        }
    }
    *status = check_edit(doc, root, p, edit, &chars);
    if (*status != TIKU_RICH_OK)
        return NULL;
    if (edit->kind == TIKU_RICH_IMAGE_INSERT ||
        edit->kind == TIKU_RICH_IMAGE_SET) {
        image = tiku_rd_image_new(doc->memory, edit->image, image, status);
        if (!image) return NULL;
        normalized.kind = TIKU_RICH_REPLACE;
    }
    length = edit->kind != TIKU_RICH_REPLACE ? p->length :
             p->length - (edit->end - edit->start) + edit->length;
    if (p->run_count > SIZE_MAX - 3) {
        *status = TIKU_RICH_LIMIT;
        tiku_rd_image_drop(image);
        return NULL;
    }
    node = tiku_rd_paragraph_new(doc, p, length, p->run_count + 3, status);
    if (!node) {
        tiku_rd_image_drop(image);
        return NULL;
    }
    if (edit->kind == TIKU_RICH_LINK_SET) {
        link = tiku_rd_link_new(doc->memory, edit->link, status);
        if (*status != TIKU_RICH_OK) {
            tiku_rd_paragraph_drop(node);
            return NULL;
        }
    }
    fill_paragraph(doc, &node->value, p, edit, &chars, link, image, image_set);
    tiku_rd_link_drop(link);
    tiku_rd_image_drop(image);
    if (node->value.run_count >
        doc->limits.runs - (root->runs - p->run_count))
        *status = TIKU_RICH_LIMIT;
    else if (content_equal(p, &node->value))
        *status = TIKU_RICH_NO_CHANGE;
    if (*status != TIKU_RICH_OK) {
        tiku_rd_paragraph_drop(node);
        return NULL;
    }
    return node;
}
