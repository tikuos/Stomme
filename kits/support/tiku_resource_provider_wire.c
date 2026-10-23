/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_resource_provider_wire.c - bounded provider descriptors and snapshots.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_resource_provider_wire.h"
#include <stdlib.h>
#include <string.h>

/** @brief Require named singular fields, permitting repeated enum choices. */
static int fields(const tiku_msg_t *message, const char *allowed)
{
    int i;
    if (!message || tiku_msg_what(message)) return 0;
    for (i = 0; i < tiku_msg_field_count(message); i++) {
        int count;
        const char *name = tiku_msg_field_at(message, i, NULL, &count), *at = allowed;
        size_t length = strlen(name);
        int found = 0;
        if (count != 1 && strcmp(name, "choice")) return 0;
        while (*at) {
            const char *end = strchr(at, ' ');
            size_t size = end ? (size_t)(end - at) : strlen(at);
            if (size == length && !memcmp(at, name, size)) { found = 1; break; }
            at += size; if (*at) at++;
        }
        if (!found) return 0;
    }
    return 1;
}

/** @brief Add complete typed values and a bounded apply-state string. */
static int snapshot_add(tiku_msg_t *message, const tiku_resource_snapshot_t *snapshot)
{
    return snapshot && !tiku_resource_value_check(snapshot->desired) &&
        !tiku_resource_value_check(snapshot->persisted) && !tiku_resource_value_check(snapshot->applied) &&
        memchr(snapshot->apply_state, 0, sizeof snapshot->apply_state) && snapshot->apply_state[0] &&
        tiku_msg_add_message(message, "desired", snapshot->desired) &&
        tiku_msg_add_message(message, "persisted", snapshot->persisted) &&
        tiku_msg_add_message(message, "applied", snapshot->applied) &&
        tiku_msg_add_string(message, "apply_state", snapshot->apply_state);
}

/** @brief Copy required snapshot fields from an already selected message. */
static tiku_resource_error_t snapshot_read(const tiku_msg_t *message, tiku_resource_snapshot_t *out)
{
    const char *state = tiku_msg_find_string(message, "apply_state", 0);
    if (!state || !state[0] || strlen(state) >= sizeof out->apply_state) return TIKU_RESOURCE_INVALID;
    out->desired = tiku_msg_find_message(message, "desired", 0);
    out->persisted = tiku_msg_find_message(message, "persisted", 0);
    out->applied = tiku_msg_find_message(message, "applied", 0);
    if (tiku_resource_value_check(out->desired) || tiku_resource_value_check(out->persisted) ||
        tiku_resource_value_check(out->applied)) {
        tiku_resource_snapshot_clear(out); return TIKU_RESOURCE_VALUE;
    }
    strcpy(out->apply_state, state);
    return TIKU_RESOURCE_OK;
}

tiku_msg_t *tiku_resource_snapshot_message(const tiku_resource_snapshot_t *snapshot)
{
    tiku_msg_t *message = tiku_msg_new(0);
    if (message && snapshot_add(message, snapshot)) return message;
    tiku_msg_free(message); return NULL;
}

tiku_resource_error_t tiku_resource_snapshot_decode(const tiku_msg_t *message, tiku_resource_snapshot_t *out)
{
    if (!out) return TIKU_RESOURCE_INVALID;
    memset(out, 0, sizeof *out);
    if (!fields(message, "desired persisted applied apply_state")) return TIKU_RESOURCE_INVALID;
    return snapshot_read(message, out);
}

tiku_msg_t *tiku_resource_initial_message(const tiku_resource_initial_t *initial)
{
    tiku_msg_t *message;
    const tiku_resource_spec_t *spec;
    const char *type, *kind;
    size_t i;
    int ok;
    if (!initial) return NULL;
    spec = &initial->spec; type = tiku_resource_type_name(spec->schema.type);
    kind = spec->kind == TIKU_RESOURCE_SETTING ? "setting" : spec->kind == TIKU_RESOURCE_STATE ? "state" :
        spec->kind == TIKU_RESOURCE_ACTION ? "action" : NULL;
    if (!type || !kind || !tiku_resource_path_valid(spec->path) || !spec->schema_version ||
        spec->schema_version > INT32_MAX || !spec->persistence) return NULL;
    message = tiku_msg_new(0);
    ok = message && tiku_msg_add_string(message, "path", spec->path) && tiku_msg_add_string(message, "kind", kind) &&
        tiku_msg_add_string(message, "type", type) && tiku_msg_add_int32(message, "schema_version", (int32_t)spec->schema_version) &&
        tiku_msg_add_string(message, "persistence", spec->persistence) && snapshot_add(message, &initial->initial);
    if (spec->schema.type == TIKU_RESOURCE_INTEGER)
        ok = ok && tiku_msg_add_int64(message, "integer_min", spec->schema.integer_min) &&
            tiku_msg_add_int64(message, "integer_max", spec->schema.integer_max);
    if (spec->schema.type == TIKU_RESOURCE_NUMBER)
        ok = ok && tiku_msg_add_double(message, "number_min", spec->schema.number_min) &&
            tiku_msg_add_double(message, "number_max", spec->schema.number_max);
    if (spec->schema.type == TIKU_RESOURCE_TEXT) {
        if (spec->schema.text_max > TIKU_RESOURCE_TEXT_MAX) ok = 0;
        ok = ok && tiku_msg_add_int32(message, "text_max", (int32_t)spec->schema.text_max);
    }
    if (spec->schema.type == TIKU_RESOURCE_ENUM) {
        if (!spec->schema.choice_count || spec->schema.choice_count > TIKU_RESOURCE_MEMBERS_MAX || !spec->schema.choices) ok = 0;
        for (i = 0; ok && i < spec->schema.choice_count; i++)
            ok = tiku_resource_utf8_valid(spec->schema.choices[i], 127) && tiku_msg_add_string(message, "choice", spec->schema.choices[i]);
    }
    if (ok) return message;
    tiku_msg_free(message); return NULL;
}

void tiku_resource_initial_clear(tiku_resource_initial_t *initial)
{
    size_t i;
    if (!initial) return;
    free((void *)initial->spec.path); free((void *)initial->spec.persistence);
    for (i = 0; i < initial->spec.schema.choice_count; i++) free((void *)initial->spec.schema.choices[i]);
    free((void *)initial->spec.schema.choices);
    tiku_resource_snapshot_clear(&initial->initial); memset(initial, 0, sizeof *initial);
}

tiku_resource_error_t tiku_resource_initial_decode(const tiku_msg_t *message, tiku_resource_initial_t *out)
{
    const char *path, *kind, *type, *persistence;
    char allowed[256] = "path kind type schema_version persistence desired persisted applied apply_state";
    int32_t version, maximum;
    int t, i, count;
    tiku_resource_error_t error = TIKU_RESOURCE_INVALID;
    if (!out) return error;
    memset(out, 0, sizeof *out);
    path = tiku_msg_find_string(message, "path", 0); kind = tiku_msg_find_string(message, "kind", 0);
    type = tiku_msg_find_string(message, "type", 0); persistence = tiku_msg_find_string(message, "persistence", 0);
    if (!tiku_resource_path_valid(path) || !kind || !type || !persistence ||
        !tiku_msg_find_int32(message, "schema_version", 0, &version) || version < 1) return error;
    if (!strcmp(kind, "setting")) out->spec.kind = TIKU_RESOURCE_SETTING;
    else if (!strcmp(kind, "state")) out->spec.kind = TIKU_RESOURCE_STATE;
    else if (!strcmp(kind, "action")) out->spec.kind = TIKU_RESOURCE_ACTION;
    else return error;
    if (strcmp(persistence, "user") && strcmp(persistence, "session") && strcmp(persistence, "document") &&
        (strcmp(persistence, "none") || out->spec.kind == TIKU_RESOURCE_SETTING)) return error;
    for (t = TIKU_RESOURCE_NULL; t <= TIKU_RESOURCE_ARRAY; t++) if (!strcmp(type, tiku_resource_type_name(t))) break;
    if (t > TIKU_RESOURCE_ARRAY) return error;
    out->spec.schema.type = t; out->spec.schema_version = (unsigned)version;
    if (t == TIKU_RESOURCE_INTEGER) {
        strcat(allowed, " integer_min integer_max");
        if (!tiku_msg_find_int64(message, "integer_min", 0, &out->spec.schema.integer_min) ||
            !tiku_msg_find_int64(message, "integer_max", 0, &out->spec.schema.integer_max)) return error;
    } else if (t == TIKU_RESOURCE_NUMBER) {
        strcat(allowed, " number_min number_max");
        if (!tiku_msg_find_double(message, "number_min", 0, &out->spec.schema.number_min) ||
            !tiku_msg_find_double(message, "number_max", 0, &out->spec.schema.number_max)) return error;
    } else if (t == TIKU_RESOURCE_TEXT) {
        strcat(allowed, " text_max");
        if (!tiku_msg_find_int32(message, "text_max", 0, &maximum) || maximum < 1 || maximum > TIKU_RESOURCE_TEXT_MAX) return error;
        out->spec.schema.text_max = (size_t)maximum;
    } else if (t == TIKU_RESOURCE_ENUM) {
        char **choices;
        strcat(allowed, " choice"); count = tiku_msg_count(message, "choice");
        if (count < 1 || count > TIKU_RESOURCE_MEMBERS_MAX) return error;
        choices = calloc((size_t)count, sizeof *choices);
        if (!choices) return TIKU_RESOURCE_NO_MEMORY;
        out->spec.schema.choices = (const char *const *)choices;
        for (i = 0; i < count; i++) {
            const char *choice = tiku_msg_find_string(message, "choice", i);
            if (!tiku_resource_utf8_valid(choice, 127)) goto fail;
            choices[i] = strdup(choice);
            if (!choices[i]) { error = TIKU_RESOURCE_NO_MEMORY; goto fail; }
            out->spec.schema.choice_count++;
        }
    }
    if (!fields(message, allowed)) goto fail;
    out->spec.path = strdup(path); out->spec.persistence = strdup(persistence);
    if (!out->spec.path || !out->spec.persistence) { error = TIKU_RESOURCE_NO_MEMORY; goto fail; }
    error = snapshot_read(message, &out->initial);
    if (!error) return TIKU_RESOURCE_OK;
fail:
    tiku_resource_initial_clear(out); return error;
}
