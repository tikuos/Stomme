/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richwatch.c - retained subscriptions and guarded change delivery.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richdoc_private.h"

/** @brief Retained callback descriptor; linked on the owning thread. */
struct tiku_richwatch {
    tiku_richdoc_t *doc;       /**< retained document */
    tiku_richwatch_t *next;    /**< next live subscription */
    tiku_rich_changed_fn callback;
    void *context;            /**< borrowed callback context */
};

tiku_rich_status_t
tiku_richwatch_create(tiku_richdoc_t *doc, tiku_rich_changed_fn callback,
                       void *context, tiku_richwatch_t **out)
{
    tiku_richwatch_t *watch;
    tiku_rich_status_t status = TIKU_RICH_OK;

    if (!out)
        return TIKU_RICH_INVALID;
    *out = NULL;
    if (!doc || !callback)
        return TIKU_RICH_INVALID;
    if (doc->notifying)
        return TIKU_RICH_BUSY;
    if (doc->watch_count >= 1024 || doc->refs == SIZE_MAX)
        return TIKU_RICH_LIMIT;
    watch = tiku_rd_alloc(doc->memory, sizeof *watch, &status);
    if (!watch)
        return status;
    watch->doc = doc;
    watch->callback = callback;
    watch->context = context;
    watch->next = doc->watches;
    doc->watches = watch;
    doc->watch_count++;
    tiku_richdoc_retain(doc);
    *out = watch;
    return TIKU_RICH_OK;
}

tiku_rich_status_t
tiku_richwatch_destroy(tiku_richwatch_t *watch)
{
    tiku_richdoc_t *doc;
    tiku_richwatch_t **at;

    if (!watch)
        return TIKU_RICH_OK;
    doc = watch->doc;
    if (doc->notifying)
        return TIKU_RICH_BUSY;
    at = &doc->watches;
    while (*at != watch)
        at = &(*at)->next;
    *at = watch->next;
    doc->watch_count--;
    tiku_rd_free(watch);
    tiku_richdoc_release(doc);
    return TIKU_RICH_OK;
}

void
tiku_rd_notify(tiku_richdoc_t *doc, tiku_rich_change_kind_t kind,
                 uint64_t origin)
{
    const tiku_rich_change_t change = {kind, doc->generation,
        doc->root->state, origin, tiku_richdoc_dirty(doc)};
    tiku_richwatch_t *watch;

    doc->notifying = 1;
    for (watch = doc->watches; watch; watch = watch->next)
        watch->callback(watch->context, doc, &change);
    for (tiku_richsession_t *session = doc->sessions; session; session = session->next)
        tiku_rs_notify_selection(session);
    doc->notifying = 0;
}
