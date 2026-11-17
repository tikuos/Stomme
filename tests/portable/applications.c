/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * applications.c - static lifecycle, scoped pages and delayed storage retirement.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "browser.h"
#include "calculator.h"
#include "tiku_test_pool.h"
#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
static _Alignas(64) unsigned char arena[262144];
static tiku_test_pool_slot_t slots[32];
static tiku_test_pool_t pool;

/** @brief Delayed storage fixture; one provider step completes one page or read. */
typedef struct {
    tiku_paged_storage_t *store;
    const tiku_storage_request_t *request;
    unsigned submits, cancels, steps;
    uint64_t epoch, entries;
    tiku_resource_error_t reject;
    int synchronous_rejected, cancel_synchronous_rejected;
} fixture_t;

/** @brief Check caller scope, retain the view and probe forbidden callback reentry. */
static tiku_resource_error_t submit(void *context, tiku_paged_storage_t *store,
    const tiku_storage_request_t *request)
{
    fixture_t *fixture = context;
    ++fixture->submits;
    if (fixture->reject) return fixture->reject;
    if (!request->caller.authenticated || !(request->caller.scopes & TIKU_RESOURCE_READ) ||
        strcmp(request->caller.principal, "browser")) return TIKU_RESOURCE_DENIED;
    if (fixture->request) return TIKU_RESOURCE_BUSY;
    fixture->store = store;
    fixture->request = request;
    fixture->synchronous_rejected += !tiku_paged_storage_complete(store, request->token,
        TIKU_RESOURCE_OK, 0, fixture->epoch, 1);
    return TIKU_RESOURCE_OK;
}
/** @brief Record cancellation while retaining the view until the next step. */
static void cancel(void *context, tiku_paged_storage_t *store, uint64_t token)
{
    fixture_t *fixture = context;
    ++fixture->cancels;
    fixture->cancel_synchronous_rejected += !tiku_paged_storage_complete(store, token,
        TIKU_RESOURCE_CANCELLED, 0, fixture->epoch, 1);
}
static const tiku_storage_provider_ops_t provider_ops = {submit, cancel};

/** @brief Fill a bounded page or seven-byte short read with deterministic content. */
static int step(fixture_t *fixture)
{
    const tiku_storage_request_t *request = fixture->request;
    tiku_resource_error_t status = TIKU_RESOURCE_OK;
    size_t count = 0;
    int eof = 0;
    CHECK(request);
    ++fixture->steps;
    if (request->epoch && request->epoch != fixture->epoch) status = TIKU_RESOURCE_STALE_CURSOR;
    else if (request->operation == TIKU_STORAGE_LIST) {
        uint64_t id = request->after;
        while (id < fixture->entries && count < request->capacity) {
            tiku_resource_entry_t *entry = &request->entries[count++];
            memset(entry, 0, sizeof *entry);
            entry->node_id = ++id;
            snprintf(entry->path, sizeof entry->path, "/media/file%u", (unsigned)id);
            snprintf(entry->id, sizeof entry->id, "%u", (unsigned)id);
            entry->kind = TIKU_RESOURCE_STREAM;
        }
        eof = id == fixture->entries;
    } else {
        if (!request->node_id || request->node_id > fixture->entries) status = TIKU_RESOURCE_NOT_FOUND;
        while (request->offset + count < 23 && count < request->capacity && count < 7) {
            request->bytes[count] = (unsigned char)(request->offset + count);
            ++count;
        }
        eof = request->offset + count >= 23;
    }
    CHECK(tiku_paged_storage_complete(fixture->store, request->token, status, count, fixture->epoch, eof));
    fixture->request = NULL;
    return 0;
}

/** @brief Bind a provider to a copied authenticated caller. */
static portable_browser_config_t config(fixture_t *fixture)
{
    portable_browser_config_t result = {
        .allocator = tiku_test_pool_allocator(&pool),
        .provider = {&provider_ops, fixture, TIKU_RESOURCE_FEATURE_READ |
            TIKU_RESOURCE_FEATURE_COLLECTION | TIKU_RESOURCE_FEATURE_STREAM},
        .caller = {.principal = "browser", .scopes = TIKU_RESOURCE_READ, .authenticated = 1}
    };
    return result;
}

/** @brief Enumerate beyond page capacity, inspect reads and reject stale media epochs. */
static int storage(void)
{
    fixture_t fixture = {.epoch = 41, .entries = 1027};
    portable_browser_config_t cfg = config(&fixture);
    tiku_paged_storage_t *store = tiku_paged_storage_create(cfg.allocator, cfg.provider, &cfg.caller, 16, 1024);
    size_t calls, count, total = 0;
    uint64_t epoch = 0, after = 0, old_token;
    int eof = 0;
    const tiku_storage_request_t *result;
    CHECK(store);
    calls = pool.calls;
    cfg.caller.scopes = 0;
    while (!eof) {
        CHECK(tiku_paged_storage_list(store, "/media", epoch, after) == TIKU_RESOURCE_PENDING);
        CHECK(!tiku_paged_storage_result(store, &count, &epoch, &eof));
        CHECK(!step(&fixture));
        result = tiku_paged_storage_result(store, &count, &epoch, &eof);
        CHECK(result && count <= 16);
        for (size_t i = 0; i < count; ++i) CHECK(result->entries[i].node_id == total + i + 1);
        total += count;
        if (count) after = result->entries[count - 1].node_id;
    }
    CHECK(total == 1027 && fixture.steps == 65 && pool.calls == calls);
    CHECK(tiku_paged_storage_list(store, "/media", epoch, 0) == TIKU_RESOURCE_PENDING);
    CHECK(!step(&fixture));
    result = tiku_paged_storage_result(store, &count, &epoch, &eof);
    CHECK(result);
    CHECK(tiku_paged_storage_read(store, result->entries[0].path, epoch, 1, 0) == TIKU_RESOURCE_PENDING);
    CHECK(!step(&fixture));
    result = tiku_paged_storage_result(store, &count, &epoch, &eof);
    CHECK(result && count == 7 && !eof && result->bytes[6] == 6);
    old_token = result->token;
    CHECK(tiku_paged_storage_read(store, "/media/file1", epoch, 1, 7) == TIKU_RESOURCE_PENDING);
    CHECK(!tiku_paged_storage_complete(store, old_token, TIKU_RESOURCE_OK, 7, epoch, 0));
    CHECK(tiku_paged_storage_list(store, "/media", 0, 0) == TIKU_RESOURCE_BUSY);
    CHECK(!step(&fixture));
    CHECK(tiku_paged_storage_read(store, "/media/file1", epoch, 1, 21) == TIKU_RESOURCE_PENDING);
    CHECK(!step(&fixture));
    result = tiku_paged_storage_result(store, &count, &epoch, &eof);
    CHECK(result && count == 2 && eof && result->bytes[1] == 22);
    CHECK(tiku_paged_storage_read(store, "/media/file1", epoch, 1, UINT64_MAX) == TIKU_RESOURCE_INVALID);
    CHECK(tiku_paged_storage_result(store, &count, &epoch, &eof) == result);
    ++fixture.epoch;
    CHECK(tiku_paged_storage_read(store, "/media/file1", epoch, 1, 0) == TIKU_RESOURCE_PENDING);
    CHECK(!step(&fixture));
    CHECK(tiku_paged_storage_status(store) == TIKU_RESOURCE_STALE_CURSOR);
    CHECK(!tiku_paged_storage_result(store, &count, &epoch, &eof));
    CHECK(tiku_paged_storage_list(store, "/media", 0, 0) == TIKU_RESOURCE_PENDING);
    tiku_paged_storage_cancel(store);
    tiku_paged_storage_cancel(store);
    CHECK(fixture.cancels == 1 && fixture.cancel_synchronous_rejected == 1);
    CHECK(tiku_paged_storage_pending(store));
    CHECK(!step(&fixture));
    CHECK(tiku_paged_storage_status(store) == TIKU_RESOURCE_CANCELLED);
    CHECK(tiku_paged_storage_read(store, "/media/file1", fixture.epoch, 1, 0) == TIKU_RESOURCE_PENDING);
    tiku_paged_storage_unavailable(store);
    CHECK(!tiku_paged_storage_destroy(&store));
    CHECK(!step(&fixture));
    CHECK(tiku_paged_storage_status(store) == TIKU_RESOURCE_UNAVAILABLE);
    CHECK(tiku_paged_storage_list(store, "/media", 0, 0) == TIKU_RESOURCE_UNAVAILABLE);
    CHECK(tiku_paged_storage_destroy(&store));
    CHECK(!pool.used && !pool.live && !pool.faults);
    return 0;
}

/** @brief Reject malformed pages, no-progress results, count overrun and false capabilities. */
static int invalid_results(void)
{
    fixture_t fixture = {.epoch = 1, .entries = 2};
    portable_browser_config_t cfg = config(&fixture);
    tiku_paged_storage_t *store;
    uint64_t token;
    cfg.provider.capabilities |= TIKU_RESOURCE_FEATURE_CHANGE;
    CHECK(!tiku_paged_storage_create(cfg.allocator, cfg.provider, &cfg.caller, 1, 8));
    cfg = config(&fixture);
    CHECK(!tiku_paged_storage_create(cfg.allocator, cfg.provider, &cfg.caller, SIZE_MAX, 8));
    store = tiku_paged_storage_create(cfg.allocator, cfg.provider, &cfg.caller, 1, 8);
    CHECK(store);
    for (int mode = 0; mode < 5; ++mode) {
        CHECK(tiku_paged_storage_list(store, "/media", 1, 1) == TIKU_RESOURCE_PENDING);
        token = fixture.request->token;
        tiku_resource_entry_t *entry = fixture.request->entries;
        memset(entry, 0, sizeof *entry);
        entry->node_id = mode == 2 ? 1 : 2;
        strcpy(entry->path, "/media/a"); strcpy(entry->id, "2");
        entry->kind = TIKU_RESOURCE_STREAM;
        if (mode == 3) memset(entry->path, 'x', sizeof entry->path);
        CHECK(tiku_paged_storage_complete(store, token, TIKU_RESOURCE_OK,
            mode == 0 ? 2 : mode == 1 ? 0 : 1, mode == 4 ? 2 : 1, 0));
        CHECK(tiku_paged_storage_status(store) == (mode == 4 ? TIKU_RESOURCE_STALE_CURSOR : TIKU_RESOURCE_INVALID));
        CHECK(!tiku_paged_storage_complete(store, token, TIKU_RESOURCE_OK, 1, 1, 1));
        fixture.request = NULL;
    }
    CHECK(tiku_paged_storage_read(store, "/media/a", 1, 2, 0) == TIKU_RESOURCE_PENDING);
    CHECK(tiku_paged_storage_complete(store, fixture.request->token, TIKU_RESOURCE_OK, 9, 1, 1));
    CHECK(tiku_paged_storage_status(store) == TIKU_RESOURCE_INVALID);
    fixture.request = NULL;
    fixture.reject = TIKU_RESOURCE_DENIED;
    CHECK(tiku_paged_storage_list(store, "/media", 0, 0) == TIKU_RESOURCE_DENIED);
    CHECK(!tiku_paged_storage_pending(store));
    tiku_paged_storage_close(store);
    CHECK(tiku_paged_storage_destroy(&store));
    cfg.caller.scopes = 0; fixture.reject = 0;
    store = tiku_paged_storage_create(cfg.allocator, cfg.provider, &cfg.caller, 1, 8);
    CHECK(store && tiku_paged_storage_list(store, "/media", 0, 0) == TIKU_RESOURCE_DENIED);
    tiku_paged_storage_close(store);
    CHECK(tiku_paged_storage_destroy(&store) && !pool.used);
    return 0;
}

/** @brief Switch applications and retain stopped browser state until completion retires. */
static int applications(void)
{
    for (unsigned cycle = 0; cycle < 100; ++cycle) {
        fixture_t fixture = {.epoch = 3, .entries = 1027};
        portable_browser_config_t cfg = config(&fixture);
        tiku_app_lease_t observer = {0};
        tiku_static_app_t *app = tiku_static_app_create(cfg.allocator, &portable_browser_ops, 4);
        portable_browser_t *browser;
        CHECK(app && tiku_static_app_start(app, &cfg) == TIKU_RESOURCE_OK);
        CHECK(tiku_app_lease_begin(app, &observer));
        browser = tiku_app_lease_state(&observer);
        CHECK(browser && !portable_browser_poll(browser));
        CHECK(!step(&fixture) && portable_browser_poll(browser));
        CHECK(browser->count == 16 && browser->after == 16);
        CHECK(portable_browser_read(browser, 0, 0) == TIKU_RESOURCE_PENDING);
        CHECK(tiku_static_app_stop(app));
        CHECK(!tiku_app_lease_state(&observer));
        CHECK(!tiku_static_app_destroy(&app));
        tiku_app_lease_release(&observer);
        CHECK(!tiku_static_app_destroy(&app));
        CHECK(fixture.cancels == 1);
        size_t retiring_bytes = pool.used;
        tiku_static_app_t *replacement = tiku_static_app_create(cfg.allocator, &portable_calculator_ops, 4);
        CHECK(replacement && tiku_static_app_start(replacement, NULL) == TIKU_RESOURCE_OK);
        tiku_input_event_t input = {.type = TIKU_EVENT_KEY_DOWN, .key = 'A'};
        CHECK(tiku_static_app_post(replacement, &input));
        CHECK(tiku_static_app_dispatch(replacement, 1) == 1);
        CHECK(tiku_static_app_stop(replacement) && tiku_static_app_destroy(&replacement));
        CHECK(pool.used == retiring_bytes);
        CHECK(!step(&fixture));
        CHECK(portable_browser_poll(browser));
        CHECK(browser->bytes == 0 && browser->status == TIKU_RESOURCE_PENDING);
        CHECK(portable_browser_next(browser) == TIKU_RESOURCE_CANCELLED);
        CHECK(!fixture.request);
        CHECK(tiku_static_app_destroy(&app) && !pool.used && !pool.live);

        app = tiku_static_app_create(cfg.allocator, &portable_calculator_ops, 4);
        CHECK(app && tiku_static_app_start(app, NULL) == TIKU_RESOURCE_OK);
        CHECK(tiku_app_lease_begin(app, &observer));
        portable_calculator_t *calculator = tiku_app_lease_state(&observer);
        tiku_input_event_t event = {.type = TIKU_EVENT_KEY_DOWN, .key = 'F'};
        CHECK(tiku_static_app_post(app, &event));
        CHECK(tiku_static_app_post(app, &event));
        CHECK(tiku_static_app_dispatch(app, 0) == 0 && calculator->value == 0);
        CHECK(tiku_static_app_dispatch(app, 1) == 1 && calculator->value == 15);
        CHECK(tiku_static_app_dispatch(app, 1) == 1 && calculator->value == 255);
        CHECK(!strcmp(calculator->output[1], "255"));
        CHECK(tiku_static_app_post(app, &event));
        CHECK(tiku_static_app_foreground(app, 0));
        CHECK(!tiku_static_app_post(app, &event) && !tiku_static_app_dispatch(app, 1));
        CHECK(tiku_static_app_foreground(app, 1));
        CHECK(tiku_static_app_dispatch(app, 1) == 0 && calculator->value == 255);
        CHECK(!tiku_static_app_destroy(&app));
        tiku_app_lease_release(&observer);
        CHECK(tiku_static_app_stop(app) && tiku_static_app_stop(app));
        CHECK(tiku_static_app_destroy(&app) && !pool.used && !pool.live && !pool.faults);
    }
    return 0;
}

/** @brief Fail each allocation across application construction and browser startup. */
static int startup_failures(void)
{
    for (size_t fail = 1; fail <= 7; ++fail) {
        fixture_t fixture = {.epoch = 1, .entries = 2};
        portable_browser_config_t cfg = config(&fixture);
        CHECK(tiku_test_pool_reset(&pool));
        pool.fail_at = fail;
        tiku_static_app_t *app = tiku_static_app_create(cfg.allocator, &portable_browser_ops, 4);
        if (app) {
            CHECK(tiku_static_app_start(app, &cfg) == TIKU_RESOURCE_NO_MEMORY);
            CHECK(tiku_static_app_destroy(&app));
        }
        CHECK(!pool.used && !pool.live && !pool.faults && !fixture.request);
    }
    CHECK(tiku_test_pool_reset(&pool));
    return 0;
}

/** @brief Callback fixture for failed-start work and interaction cancellation. */
typedef struct {
    tiku_static_app_t *app;
    tiku_app_lease_t *pending;
    unsigned *stops, *disposes;
    int pressed, cancellations, fail;
} lifecycle_t;
/** @brief Retain external deferred work and optionally fail after starting it. */
static tiku_resource_error_t lifecycle_start(void *state, tiku_static_app_t *app, void *argument)
{
    lifecycle_t *probe = state;
    *probe = *(lifecycle_t *)argument;
    probe->app = app;
    if (!tiku_app_lease_begin(app, probe->pending)) return TIKU_RESOURCE_INVALID;
    return probe->fail ? TIKU_RESOURCE_UNAVAILABLE : TIKU_RESOURCE_OK;
}
/** @brief Record cancellation and verify callback reentry is refused. */
static void lifecycle_dispatch(void *state, tiku_queue_result_t result, const tiku_input_event_t *event)
{
    lifecycle_t *probe = state;
    if (tiku_static_app_stop(probe->app)) probe->pressed = -100;
    else if (result == TIKU_QUEUE_CANCEL) { probe->pressed = 0; ++probe->cancellations; }
    else if (event->type == TIKU_EVENT_POINTER_DOWN) probe->pressed = 1;
    else if (event->type == TIKU_EVENT_POINTER_UP) probe->pressed = 0;
}
/** @brief Count shutdown without releasing externally retained completion storage. */
static void lifecycle_stop(void *state)
{ ++*((lifecycle_t *)state)->stops; }
/** @brief Count disposal after the external completion lease retires. */
static void lifecycle_dispose(void *state)
{ ++*((lifecycle_t *)state)->disposes; }

/** @brief Failed start retains state; overflow and background release interactions. */
static int lifecycle(void)
{
    const tiku_static_app_ops_t ops = {sizeof(lifecycle_t), lifecycle_start,
        lifecycle_dispatch, NULL, lifecycle_stop, lifecycle_dispose};
    for (int fail = 0; fail <= 1; ++fail) {
        unsigned stops = 0, disposes = 0;
        tiku_app_lease_t pending = {0};
        lifecycle_t argument = {.pending = &pending, .stops = &stops, .disposes = &disposes, .fail = fail};
        tiku_static_app_t *app = tiku_static_app_create(tiku_test_pool_allocator(&pool), &ops, 2);
        CHECK(app && tiku_static_app_start(app, &argument) == (fail ? TIKU_RESOURCE_UNAVAILABLE : TIKU_RESOURCE_OK));
        if (!fail) {
            lifecycle_t *state = tiku_app_lease_state(&pending);
            tiku_input_event_t event = {.type = TIKU_EVENT_POINTER_DOWN};
            CHECK(tiku_static_app_post(app, &event));
            CHECK(tiku_static_app_dispatch(app, 1) == 1 && state->pressed == 1);
            CHECK(tiku_static_app_post(app, &event) && tiku_static_app_post(app, &event));
            event.type = TIKU_EVENT_POINTER_UP;
            CHECK(!tiku_static_app_post(app, &event));
            CHECK(tiku_static_app_dispatch(app, 1) == 1 && !state->pressed && state->cancellations == 1);
            event.type = TIKU_EVENT_POINTER_DOWN;
            CHECK(tiku_static_app_post(app, &event) && tiku_static_app_dispatch(app, 1) == 1);
            CHECK(tiku_static_app_foreground(app, 0) && !state->pressed && state->cancellations == 2);
            CHECK(tiku_static_app_stop(app));
        }
        CHECK(stops == 1 && !disposes && !tiku_app_lease_state(&pending));
        CHECK(!tiku_static_app_destroy(&app));
        tiku_app_lease_release(&pending);
        CHECK(tiku_static_app_destroy(&app) && disposes == 1 && !pool.used);
    }
    return 0;
}

/** @brief Fixed external integer and checksum vectors for the shared model. */
static int calculator_vectors(void)
{
    uint64_t word = 42;
    unsigned char bytes[64]; size_t length;
    char text[65];
    CHECK(tiku_word_parse("18446744073709551615", 10, 64, &word) && word == UINT64_MAX);
    CHECK(!tiku_word_parse("18446744073709551616", 10, 64, &word) && word == UINT64_MAX);
    CHECK(!tiku_word_parse("1", 10, 0, &word));
    CHECK(!tiku_word_parse("100", 16, 8, &word));
    CHECK(tiku_word_format(UINT64_MAX, 16, text) && !strcmp(text, "FFFFFFFFFFFFFFFF"));
    CHECK(!tiku_word_format(1, 1, text));
    CHECK(tiku_word_swap(0x12345678, 32) == 0x78563412);
    CHECK(tiku_word_extract(0xF0, 7, 4) == 15);
    CHECK(tiku_word_extract(UINT64_MAX, 63, 0) == UINT64_MAX);
    CHECK(tiku_word_crc32((const unsigned char *)"123456789", 9) == 0xCBF43926);
    CHECK(tiku_word_crc16((const unsigned char *)"123456789", 9) == 0x29B1);
    CHECK(tiku_word_bytes("00 FF 1234", bytes, &length) && length == 4 && bytes[1] == 255);
    CHECK(!tiku_word_bytes("0 F", bytes, &length));
    return 0;
}

int main(void)
{
    int result;
    CHECK(tiku_test_pool_init(&pool, arena, sizeof arena, slots, 32));
    if ((result = startup_failures()) || (result = storage()) || (result = invalid_results()) ||
        (result = applications()) || (result = lifecycle()) || (result = calculator_vectors())) {
        fprintf(stderr, "Application/storage check failed at line %d\n", result);
        return 1;
    }
#ifdef TIKU_SERVICE_HEAP_TRAP
    extern unsigned tiku_service_heap_calls;
    CHECK(!tiku_service_heap_calls);
#endif
    printf("Application/storage: 100 switches, 1027 entries; peak=%zu, live=%zu, faults=%zu\n", pool.peak, pool.live, pool.faults);
    return 0;
}
