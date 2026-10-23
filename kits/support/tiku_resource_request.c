/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_resource_request.c - resource request envelopes and feature gates.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_resource.h"
#include <string.h>

/** @brief Require singular fields from a whitespace-delimited name set. */
static int names(const tiku_msg_t *message, const char *allowed)
{
    int i;
    for (i = 0; i < tiku_msg_field_count(message); i++) {
        int count;
        const char *field = tiku_msg_field_at(message, i, NULL, &count);
        const char *at = allowed;
        size_t length = strlen(field);
        int found = 0;
        if (count != 1) return 0;
        while (*at) {
            const char *end = strchr(at, ' ');
            size_t size = end ? (size_t)(end - at) : strlen(at);
            if (size == length && !memcmp(at, field, size)) { found = 1; break; }
            at += size;
            if (*at) at++;
        }
        if (!found) return 0;
    }
    return 1;
}

/** @brief Validate an expected provider incarnation and positive revision. */
static int expected_valid(const tiku_msg_t *request)
{
    tiku_msg_t *expected = tiku_msg_find_message(request, "expected", 0);
    uint64_t revision;
    const char *provider;
    int valid;
    if (!expected) return 0;
    provider = tiku_msg_find_string(expected, "provider", 0);
    valid = names(expected, "provider incarnation revision") && provider && provider[0] &&
        strlen(provider) < TIKU_RESOURCE_KEY_MAX && !strchr(provider, '/') &&
        tiku_resource_id_valid(tiku_msg_find_string(expected, "incarnation", 0)) &&
        !tiku_resource_counter_parse(tiku_msg_find_string(expected, "revision", 0), &revision) && revision;
    tiku_msg_free(expected);
    return valid;
}

tiku_resource_error_t tiku_resource_request_check(const tiku_msg_t *request, uint64_t features)
{
    const char *op = tiku_msg_find_string(request, "op", 0), *extra = NULL;
    const char *path, *durability;
    tiku_msg_t *value;
    uint64_t required;
    int32_t number;
    char allowed[256];
    tiku_resource_error_t error;
    if (!op || tiku_msg_what(request) != TIKU_RESOURCE_MESSAGE_CODE ||
        !tiku_resource_id_valid(tiku_msg_find_string(request, "request_id", 0)) ||
        !tiku_resource_id_valid(tiku_msg_find_string(request, "session", 0))) return TIKU_RESOURCE_INVALID;
    if (!strcmp(op, "read") || !strcmp(op, "describe")) { required = TIKU_RESOURCE_FEATURE_READ; extra = "path"; }
    else if (!strcmp(op, "list")) { required = TIKU_RESOURCE_FEATURE_READ; extra = "path page_size cursor"; }
    else if (!strcmp(op, "set")) { required = TIKU_RESOURCE_FEATURE_CHANGE; extra = "path expected value durability deadline_ms"; }
    else if (!strcmp(op, "invoke")) { required = TIKU_RESOURCE_FEATURE_CHANGE; extra = "path expected arguments deadline_ms"; }
    else if (!strcmp(op, "transaction")) { required = TIKU_RESOURCE_FEATURE_CHANGE | TIKU_RESOURCE_FEATURE_TRANSACTION; extra = "provider expected changes deadline_ms"; }
    else if (!strcmp(op, "subscribe")) { required = TIKU_RESOURCE_FEATURE_WATCH; extra = "provider path"; }
    else if (!strcmp(op, "unsubscribe")) { required = TIKU_RESOURCE_FEATURE_WATCH; extra = "subscription"; }
    else if (!strcmp(op, "job-status")) { required = TIKU_RESOURCE_FEATURE_JOBS; extra = "job"; }
    else if (!strcmp(op, "cancel")) { required = TIKU_RESOURCE_FEATURE_JOBS; extra = "job expected deadline_ms"; }
    else if (!strcmp(op, "request-status")) { required = TIKU_RESOURCE_FEATURE_CHANGE; extra = "prior_request_id"; }
    else if (!strcmp(op,"stream-open")) {required=TIKU_RESOURCE_FEATURE_STREAM;extra="path expected maximum_bytes";}
    else if (!strcmp(op,"stream-read")) {required=TIKU_RESOURCE_FEATURE_STREAM;extra="lease offset byte_count";}
    else if (!strcmp(op,"stream-close")) {required=TIKU_RESOURCE_FEATURE_STREAM;extra="lease";}
    else if (!strcmp(op, "provider-register")) { required = TIKU_RESOURCE_FEATURE_PROVIDER; extra = "resources"; }
    else if (!strcmp(op, "provider-publish")) { required = TIKU_RESOURCE_FEATURE_PROVIDER; extra = "path expected snapshot"; }
    else if (!strcmp(op, "provider-unregister")) { required = TIKU_RESOURCE_FEATURE_PROVIDER; extra = "expected"; }
    else if (!strcmp(op, "provider-status")) { required = TIKU_RESOURCE_FEATURE_PROVIDER; extra = ""; }
    else if (!strcmp(op, "provider-complete")) { required = TIKU_RESOURCE_FEATURE_PROVIDER; extra = "dispatch expected snapshot code effect"; }
    else if (!strcmp(op, "provider-progress")) { required = TIKU_RESOURCE_FEATURE_PROVIDER; extra = "dispatch expected progress awaiting_confirmation"; }
    else return TIKU_RESOURCE_UNSUPPORTED;
    if ((features & required) != required) return TIKU_RESOURCE_UNSUPPORTED;
    strcpy(allowed, "op request_id session "); strcat(allowed, extra);
    if (!names(request, allowed)) return TIKU_RESOURCE_INVALID;
    if(required==TIKU_RESOURCE_FEATURE_STREAM){
        uint64_t bytes;
        if(!strcmp(op,"stream-open"))return tiku_resource_path_valid(tiku_msg_find_string(request,"path",0)) && expected_valid(request) &&
            !tiku_resource_counter_parse(tiku_msg_find_string(request,"maximum_bytes",0),&bytes) && bytes && bytes<=TIKU_RESOURCE_STREAM_BYTES ? TIKU_RESOURCE_OK:TIKU_RESOURCE_INVALID;
        if(!tiku_resource_id_valid(tiku_msg_find_string(request,"lease",0)))return TIKU_RESOURCE_INVALID;
        if(!strcmp(op,"stream-close"))return TIKU_RESOURCE_OK;
        return !tiku_resource_counter_parse(tiku_msg_find_string(request,"offset",0),&bytes) &&
            tiku_msg_find_int32(request,"byte_count",0,&number) && number>0 && number<=(int32_t)TIKU_RESOURCE_STREAM_CHUNK ? TIKU_RESOURCE_OK:TIKU_RESOURCE_INVALID;
    }
    if (!strcmp(op, "transaction")) {
        const char *provider = tiku_msg_find_string(request, "provider", 0);
        tiku_resource_replacement_t items[TIKU_RESOURCE_TRANSACTION_MAX] = {0};
        size_t count = 0;
        tiku_msg_t *changes, *expected;
        int matches;
        if (!provider || !expected_valid(request) || !tiku_msg_find_int32(request, "deadline_ms", 0, &number) ||
            number < 1 || number > 30000) return TIKU_RESOURCE_INVALID;
        expected = tiku_msg_find_message(request, "expected", 0);
        matches = !strcmp(provider, tiku_msg_find_string(expected, "provider", 0)); tiku_msg_free(expected);
        if (!matches) return TIKU_RESOURCE_INVALID;
        changes = tiku_msg_find_message(request, "changes", 0);
        error = tiku_resource_replacements_decode(changes, items, &count);
        tiku_msg_free(changes); tiku_resource_replacements_clear(items, count);
        return error;
    }
    if (required == TIKU_RESOURCE_FEATURE_PROVIDER) {
        if (!strcmp(op, "provider-register"))
            return tiku_msg_has(request, "resources", TIKU_MSG_T_MESSAGE) ? TIKU_RESOURCE_OK : TIKU_RESOURCE_INVALID;
        if (!strcmp(op, "provider-status")) return TIKU_RESOURCE_OK;
        if (!expected_valid(request)) return TIKU_RESOURCE_INVALID;
        if (!strcmp(op, "provider-publish"))
            return tiku_resource_path_valid(tiku_msg_find_string(request, "path", 0)) &&
                tiku_msg_has(request, "snapshot", TIKU_MSG_T_MESSAGE) ? TIKU_RESOURCE_OK : TIKU_RESOURCE_INVALID;
        if (!strcmp(op, "provider-unregister")) return TIKU_RESOURCE_OK;
        if (!tiku_resource_id_valid(tiku_msg_find_string(request, "dispatch", 0))) return TIKU_RESOURCE_INVALID;
        if (!strcmp(op, "provider-progress")) {
            int awaiting;
            return tiku_msg_find_int32(request, "progress", 0, &number) && number >= 0 && number <= 1000 &&
                tiku_msg_find_bool(request, "awaiting_confirmation", 0, &awaiting) ? TIKU_RESOURCE_OK : TIKU_RESOURCE_INVALID;
        }
        return tiku_msg_find_string(request, "code", 0) && tiku_msg_find_string(request, "effect", 0) ?
            TIKU_RESOURCE_OK : TIKU_RESOURCE_INVALID;
    }
    if (!strcmp(op, "request-status"))
        return tiku_resource_id_valid(tiku_msg_find_string(request, "prior_request_id", 0)) ? TIKU_RESOURCE_OK : TIKU_RESOURCE_INVALID;
    if (required == TIKU_RESOURCE_FEATURE_JOBS) {
        if (!tiku_resource_id_valid(tiku_msg_find_string(request, "job", 0))) return TIKU_RESOURCE_INVALID;
        if (!strcmp(op, "cancel") && (!expected_valid(request) ||
            !tiku_msg_find_int32(request, "deadline_ms", 0, &number) || number < 1 || number > 30000)) return TIKU_RESOURCE_INVALID;
        return TIKU_RESOURCE_OK;
    }
    if (!strcmp(op, "unsubscribe"))
        return tiku_resource_id_valid(tiku_msg_find_string(request, "subscription", 0)) ? TIKU_RESOURCE_OK : TIKU_RESOURCE_INVALID;
    path = tiku_msg_find_string(request, "path", 0);
    if (!tiku_resource_path_valid(path)) return TIKU_RESOURCE_INVALID;
    if (!strcmp(op, "list")) {
        const char *cursor = tiku_msg_find_string(request, "cursor", 0);
        if (!tiku_msg_find_int32(request, "page_size", 0, &number) || number < 1 || number > 128 ||
            (tiku_msg_count(request, "cursor") && !tiku_resource_id_valid(cursor))) return TIKU_RESOURCE_INVALID;
    }
    if (!strcmp(op, "subscribe")) {
        const char *provider = tiku_msg_find_string(request, "provider", 0);
        if (!provider || !provider[0] || strlen(provider) >= TIKU_RESOURCE_KEY_MAX || strchr(provider, '/'))
            return TIKU_RESOURCE_INVALID;
    }
    if (required != TIKU_RESOURCE_FEATURE_CHANGE) return TIKU_RESOURCE_OK;
    if (!expected_valid(request) || !tiku_msg_find_int32(request, "deadline_ms", 0, &number) ||
        number < 1 || number > 30000) return TIKU_RESOURCE_INVALID;
    if (!strcmp(op, "set")) {
        durability = tiku_msg_find_string(request, "durability", 0);
        if (!durability || (strcmp(durability, "user") && strcmp(durability, "session") && strcmp(durability, "document")))
            return TIKU_RESOURCE_INVALID;
    }
    value = tiku_msg_find_message(request, !strcmp(op, "set") ? "value" : "arguments", 0);
    error = tiku_resource_value_check(value);
    tiku_msg_free(value);
    return error;
}
