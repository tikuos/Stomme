/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_trk_resource_discovery.c - typed local service discovery and live event counts.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_trk_resource_discovery.h"
#include "tiku_trk_resource_values.h"
#include "tiku_resource_literal.h"
#include "tiku_resource_limits.h"
#include "tiku_app.h"
#include <limits.h>
#include <stdlib.h>
struct tiku_trk_resource_discovery {
    tiku_resource_registry_t *registry;
    tiku_resource_stamp_t stamps[4];
    unsigned registered;
    uint64_t next_poll;
    char product[128], version[64];
};
/** @brief Public read-only rows grouped under nonoverlapping provider roots. */
static const struct {
    const char *path;
    tiku_resource_type_t type;
} rows[] = {{"/about/product", TIKU_RESOURCE_TEXT},
            {"/about/abi", TIKU_RESOURCE_INTEGER},
            {"/about/foundation-revision", TIKU_RESOURCE_TEXT},
            {"/about/host", TIKU_RESOURCE_TEXT},
            {"/about/sdk-identity", TIKU_RESOURCE_TEXT},
            {"/about/version", TIKU_RESOURCE_TEXT},
            {"/capabilities/features", TIKU_RESOURCE_COUNTER},
            {"/capabilities/limits", TIKU_RESOURCE_OBJECT},
            {"/capabilities/protocol", TIKU_RESOURCE_INTEGER},
            {"/capabilities/providers", TIKU_RESOURCE_OBJECT},
            {"/events/incarnation", TIKU_RESOURCE_TEXT},
            {"/events/overflow", TIKU_RESOURCE_OBJECT},
            {"/events/sequence", TIKU_RESOURCE_OBJECT},
            {"/events/snapshots", TIKU_RESOURCE_OBJECT},
            {"/schema/descriptors", TIKU_RESOURCE_OBJECT},
            {"/schema/permissions", TIKU_RESOURCE_OBJECT},
            {"/schema/resource-kinds", TIKU_RESOURCE_ARRAY},
            {"/schema/schema-versions", TIKU_RESOURCE_OBJECT}};
/** @brief Construct owned primitive values for typed observation members. */
static tiku_msg_t *text(const char *value)
{
    return tiku_resource_value_string(TIKU_RESOURCE_TEXT, value);
}
/** @brief Encode full-width unsigned counters as decimal text. */
static tiku_msg_t *counter(uint64_t value)
{
    char bytes[21];
    snprintf(bytes, sizeof bytes, "%llu", (unsigned long long)value);
    return tiku_resource_value_string(TIKU_RESOURCE_COUNTER, bytes);
}
#define NUMBER(o, k, n)                                                       \
    trk_value_member(o, k,                                                    \
                     tiku_resource_value_integer(TIKU_RESOURCE_INTEGER, n))
#define TEXT(o, k, v) trk_value_member(o, k, text(v))
/** @brief Describe the native endpoint's host offer, distinct from peer selection. */
static tiku_msg_t *limits(void)
{
    tiku_resource_protocol_limits_t native;
    tiku_resource_protocol_limits_default(&native);
    tiku_msg_t *raw = tiku_resource_protocol_limits_message(&native);
    tiku_msg_t *out = trk_value_container("object");
    int bad = !raw || !out || TEXT(out, "scope", "host-offer");
    for (int i = 0; raw && i < tiku_msg_field_count(raw); i++) {
        tiku_msg_type_t type;
        int count;
        const char *name = tiku_msg_field_at(raw, i, &type, &count);
        int32_t n;
        if (!name || !tiku_msg_find_int32(raw, name, 0, &n) ||
            NUMBER(out, name, n))
            bad = 1;
    }
    tiku_msg_free(raw);
    if (bad) {
        tiku_msg_free(out);
        return NULL;
    }
    return out;
}
/** @brief Publish counts without private provider names or resource content. */
static tiku_msg_t *providers(tiku_trk_resource_discovery_t *owner)
{
    tiku_resource_registry_observation_t state;
    if (!tiku_resource_registry_observe(owner->registry, &state))
        return NULL;
    tiku_msg_t *out = trk_value_container("object");
    int bad = !out || NUMBER(out, "registered", state.providers) ||
              NUMBER(out, "capacity", state.provider_capacity) ||
              NUMBER(out, "nodes", state.nodes) ||
              NUMBER(out, "node_capacity", state.node_capacity);
    if (bad) {
        tiku_msg_free(out);
        return NULL;
    }
    return out;
}
/** @brief Exclude discovery's own subscriptions from self-referential event telemetry. */
static tiku_msg_t *events(tiku_trk_resource_discovery_t *owner, unsigned row)
{
    tiku_resource_event_observation_t state;
    if (!tiku_resource_events_observe(owner->registry, "events", &state))
        return NULL;
    tiku_msg_t *out = trk_value_container("object");
    int bad = !out || TEXT(out, "excluded_provider", "events");
    if (row == 11)
        bad |=
            NUMBER(out, "paused_subscriptions", state.paused) ||
            TEXT(out, "recovery",
                 "discard deltas and resubscribe with a new atomic snapshot");
    else if (row == 12)
        bad |= TEXT(out, "scope", "per-subscription") ||
               trk_value_member(out, "maximum_active_sequence",
                                counter(state.maximum_sequence));
    else
        bad |= NUMBER(out, "active_subscriptions", state.subscriptions) ||
               NUMBER(out, "queued_events", state.queued) ||
               TEXT(out, "initial", "atomic provider snapshot");
    if (bad) {
        tiku_msg_free(out);
        return NULL;
    }
    return out;
}
/** @brief Encode the scope definitions without granting caller authority. */
static tiku_msg_t *permissions(void)
{
    tiku_msg_t *out = trk_value_container("object");
    int bad = !out || NUMBER(out, "read", TIKU_RESOURCE_READ) ||
              NUMBER(out, "write", TIKU_RESOURCE_WRITE) ||
              NUMBER(out, "invoke", TIKU_RESOURCE_INVOKE) ||
              NUMBER(out, "content", TIKU_RESOURCE_CONTENT) ||
              NUMBER(out, "cross_app", TIKU_RESOURCE_CROSS_APP) ||
              NUMBER(out, "host_control", TIKU_RESOURCE_HOST_CONTROL) ||
              NUMBER(out, "provider", TIKU_RESOURCE_PROVIDER);
    if (bad) {
        tiku_msg_free(out);
        return NULL;
    }
    return out;
}
/** @brief Materialize a complete value from generated identity and native contracts. */
static tiku_msg_t *capture(tiku_trk_resource_discovery_t *owner, unsigned row)
{
    switch (row) {
    case 0:
        return text(owner->product);
    case 1:
        return tiku_resource_value_integer(TIKU_RESOURCE_INTEGER,
                                           TIKU_APP_ABI);
    case 2:
        return text(TIKU_FOUNDATION_REVISION);
    case 3:
        return text(TIKU_SDK_HOST);
    case 4:
        return text(TIKU_SDK_IDENTITY);
    case 5:
        return text(owner->version);
    case 6:
        return counter(TIKU_RESOURCE_FEATURES &
                       ~TIKU_RESOURCE_FEATURE_PROVIDER);
    case 7:
        return limits();
    case 8:
        return tiku_resource_value_integer(TIKU_RESOURCE_INTEGER, 1);
    case 9:
        return providers(owner);
    case 10:
        return text(tiku_resource_registry_session(owner->registry));
    case 11:
    case 12:
    case 13:
        return events(owner, row);
    case 15:
        return permissions();
    default:
        break;
    }
    const char *literal =
        row == 14
            ? "{\"operation\":\"describe\",\"version_field\":\"schema_version\",\"identity_fields\":[\"provider\",\"incarnation\",\"revision\"],\"fields\":[\"path\",\"id\",\"kind\",\"value_schema\",\"availability\",\"persistence\",\"read_scopes\",\"change_scopes\",\"host_control_required\",\"sensitive\"]}"
        : row == 16
            ? "[\"directory\",\"collection\",\"setting\",\"state\",\"action\",\"stream\"]"
            : "{\"field\":\"describe.schema_version\",\"minimum\":1,\"maximum\":2147483647,\"compatibility\":\"owner-defined\"}";
    tiku_msg_t *out = NULL;
    return tiku_resource_literal_parse("json", literal, &out) ? NULL : out;
}
/** @brief Copy one provider group atomically before publishing its discovery rows. */
static int group(tiku_trk_resource_discovery_t *owner, unsigned index)
{
    static const char *const keys[] = {"about", "capabilities", "events",
                                       "schema"};
    static const char *const prefixes[] = {"/about", "/capabilities",
                                           "/events", "/schema"};
    unsigned first = index ? 6 + (index - 1) * 4 : 0;
    unsigned count = index ? 4 : 6;
    tiku_resource_initial_t initial[6] = {0};
    int bad = 0;
    for (unsigned i = 0; i < count; i++) {
        initial[i].spec = (tiku_resource_spec_t){
            .path = rows[first + i].path,
            .kind = TIKU_RESOURCE_STATE,
            .schema_version = 1,
            .schema = {.type = rows[first + i].type,
                       .text_max = TIKU_RESOURCE_TEXT_MAX,
                       .integer_min = 0,
                       .integer_max = INT_MAX},
            .public_read = 1,
            .read_scopes = TIKU_RESOURCE_READ,
            .persistence = "none",
            .controller = "desktop-discovery"};
        bad |= trk_value_snapshot(&initial[i].initial,
                                  capture(owner, first + i), "observed");
    }
    if (!bad)
        bad = tiku_resource_register(owner->registry, keys[index],
                                     prefixes[index], NULL, initial, count,
                                     NULL, NULL, &owner->stamps[index]);
    for (unsigned i = 0; i < count; i++)
        tiku_resource_snapshot_clear(&initial[i].initial);
    return bad;
}
tiku_trk_resource_discovery_t *
tiku_trk_resource_discovery_open(tiku_resource_registry_t *registry,
                                 const char *product, const char *version)
{
    if (!registry || !product || !version || strlen(product) >= 128 ||
        strlen(version) >= 64)
        return NULL;
    tiku_trk_resource_discovery_t *owner = calloc(1, sizeof *owner);
    if (!owner)
        return NULL;
    owner->registry = registry;
    strcpy(owner->product, product);
    strcpy(owner->version, version);
    for (unsigned i = 0; i < 4; i++) {
        if (group(owner, i)) {
            tiku_trk_resource_discovery_close(owner);
            return NULL;
        }
        owner->registered++;
    }
    tiku_trk_resource_discovery_poll(owner, 0);
    return owner;
}
void tiku_trk_resource_discovery_poll(tiku_trk_resource_discovery_t *owner,
                                      uint64_t now)
{
    static const unsigned refresh[] = {9, 11, 12, 13};
    if (!owner || (now && now < owner->next_poll))
        return;
    owner->next_poll = now + 100;
    tiku_resource_caller_t reader = {.authenticated = 1,
                                     .scopes = TIKU_RESOURCE_READ};
    strcpy(reader.principal, "00000000000000000000000000000001");
    for (unsigned i = 0; i < sizeof refresh / sizeof refresh[0]; i++) {
        unsigned row = refresh[i], provider = row == 9 ? 1 : 2;
        tiku_resource_snapshot_t next = {0}, previous = {0};
        if (trk_value_snapshot(&next, capture(owner, row), "observed"))
            continue;
        if (!tiku_resource_read(owner->registry, &reader, rows[row].path,
                                &previous) &&
            !tiku_resource_value_equal(next.applied, previous.applied))
            (void)tiku_resource_publish(
                owner->registry, &owner->stamps[provider], rows[row].path,
                &next, &owner->stamps[provider]);
        tiku_resource_snapshot_clear(&previous);
        tiku_resource_snapshot_clear(&next);
    }
}
void tiku_trk_resource_discovery_close(tiku_trk_resource_discovery_t *owner)
{
    if (!owner)
        return;
    while (owner->registered)
        (void)tiku_resource_unregister(owner->registry,
                                       &owner->stamps[--owner->registered]);
    free(owner);
}
