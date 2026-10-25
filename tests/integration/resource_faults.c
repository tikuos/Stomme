/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * resource_faults.c - resource allocation failures and atomic publication.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_resource_registry.h"
#include "tiku_resource_literal.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static long budget = -1;
static unsigned calls, failures;
void *__real_malloc(size_t);
void *__real_calloc(size_t, size_t);
void *__real_realloc(void *, size_t);
/** @brief Fail one selected allocation and count attempted allocation sites. */
static int fail(void)
{
    calls++;
    if (budget > 0 && --budget == 0) {
        budget = -1;
        failures++;
        return 1;
    }
    return 0;
}
/** @brief Inject one allocation refusal before delegating to the host allocator. */
void *__wrap_malloc(size_t n)
{
    return fail() ? NULL : __real_malloc(n);
}
/** @brief Inject one zeroed-allocation refusal. */
void *__wrap_calloc(size_t n, size_t size)
{
    return fail() ? NULL : __real_calloc(n, size);
}
/** @brief Preserve the original allocation when resize is refused. */
void *__wrap_realloc(void *p, size_t n)
{
    return fail() ? NULL : __real_realloc(p, n);
}
/** @brief Cover descriptor string allocations without a second injection point. */
char *__wrap_strdup(const char *text)
{
    if (fail())
        return NULL;
    size_t n = strlen(text) + 1;
    char *out = __real_malloc(n);
    if (out)
        memcpy(out, text, n);
    return out;
}
/** @brief Start a reproducible allocation sequence. */
static void arm(long at)
{
    calls = failures = 0;
    budget = at;
}
/** @brief Create a bounded native registry on the calling thread. */
static tiku_resource_registry_t *registry(void)
{
    tiku_resource_limits_t limits = {.providers = 2,
                                     .resources = 16,
                                     .watches = 2,
                                     .events_per_watch = 2,
                                     .receipts = 4};
    return tiku_resource_registry_new(&limits);
}
/** @brief Build two independent complete native initial snapshots. */
static void initial(tiku_resource_initial_t values[2])
{
    static const char *const paths[] = {"/fixture/one", "/fixture/two"};
    static const char *const choices[] = {"off", "on"};
    memset(values, 0, sizeof(*values) * 2);
    for (int i = 0; i < 2; i++) {
        values[i].spec =
            (tiku_resource_spec_t){.path = paths[i],
                                   .kind = TIKU_RESOURCE_STATE,
                                   .schema_version = 1,
                                   .schema = {.type = TIKU_RESOURCE_ENUM,
                                              .choices = choices,
                                              .choice_count = 2,
                                              .text_max = 3},
                                   .public_read = 1,
                                   .read_scopes = TIKU_RESOURCE_READ,
                                   .persistence = "none",
                                   .controller = "allocation-fixture"};
        values[i].initial.desired =
            tiku_resource_value_string(TIKU_RESOURCE_ENUM, "on");
        values[i].initial.persisted =
            tiku_resource_value_string(TIKU_RESOURCE_ENUM, "on");
        values[i].initial.applied =
            tiku_resource_value_string(TIKU_RESOURCE_ENUM, "on");
        strcpy(values[i].initial.apply_state, "observed");
        assert(values[i].initial.desired && values[i].initial.persisted &&
               values[i].initial.applied);
    }
}
/** @brief Execute one operation, verifying its public failure and retained state. */
static unsigned exercise(int operation, long at)
{
    tiku_resource_caller_t caller = {.scopes = TIKU_RESOURCE_READ};
    if (operation == 0) {
        arm(at);
        tiku_resource_registry_t *r = registry();
        unsigned n = calls;
        budget = -1;
        tiku_resource_registry_free(r);
        return n;
    }
    if (operation == 4) {
        tiku_msg_t *value = NULL;
        arm(at);
        tiku_resource_error_t e = tiku_resource_literal_parse(
            "json",
            "{\"one\":[true,3,\"text\"],\"two\":{\"$type\":\"enum\",\"$"
            "value\":\"on\"}}",
            &value);
        unsigned n = calls;
        budget = -1;
        assert((!e && value && !tiku_resource_value_check(value)) ||
               (e && !value));
        tiku_msg_free(value);
        return n;
    }
    tiku_resource_registry_t *r = registry();
    tiku_resource_initial_t values[2];
    tiku_resource_stamp_t stamp;
    initial(values);
    assert(r);
    if (operation != 1)
        assert(!tiku_resource_register(r, "fixture", "/fixture", NULL, values,
                                       2, NULL, NULL, &stamp));
    tiku_msg_t *out = NULL;
    tiku_resource_watch_t watch = 0;
    arm(at);
    tiku_resource_error_t e;
    if (operation == 1)
        e = tiku_resource_register(r, "fixture", "/fixture", NULL, values, 2,
                                   NULL, NULL, &stamp);
    else if (operation == 2)
        e = tiku_resource_describe(r, &caller, "/fixture/one", &out);
    else
        e = tiku_resource_watch_open(r, &caller, "fixture", "/fixture", &watch,
                                     &out);
    unsigned n = calls;
    budget = -1;
    tiku_resource_registry_observation_t counts;
    assert(tiku_resource_registry_observe(r, &counts));
    if (operation == 1) {
        assert(counts.providers == (e ? 0u : 1u));
        assert(!e || counts.nodes == 1);
        tiku_resource_snapshot_t snapshot = {0};
        tiku_resource_error_t got =
            tiku_resource_read(r, &caller, "/fixture/one", &snapshot);
        assert(e ? got == TIKU_RESOURCE_NOT_FOUND : got == TIKU_RESOURCE_OK);
        tiku_resource_snapshot_clear(&snapshot);
    } else if (operation == 2)
        assert(e ? !out : out != NULL);
    else {
        assert(counts.watches == (e ? 0u : 1u));
        assert(e ? !out : out != NULL);
        if (!e)
            tiku_resource_watch_close(r, watch);
    }
    tiku_msg_free(out);
    for (int i = 0; i < 2; i++)
        tiku_resource_snapshot_clear(&values[i].initial);
    tiku_resource_registry_free(r);
    return n;
}
/** @brief Refuse each allocation site in creation, registration, observation and parsing. */
int main(void)
{
    unsigned attempts = 0;
    for (int operation = 0; operation < 5; operation++) {
        unsigned n = exercise(operation, -1);
        assert(n && n < 4096);
        for (unsigned at = 1; at <= n + 1; at++) {
            (void)exercise(operation, at);
            assert(at > n || failures == 1);
            attempts++;
        }
        printf("PASS allocation operation %d: %u selected sites\n", operation,
               n);
    }
    printf("PASS resource allocation failure matrix: %u attempts\n", attempts);
    return 0;
}
