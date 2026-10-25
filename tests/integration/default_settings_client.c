/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * default_settings_client.c - independent SDK folder-default and catalog checks.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_resource_endpoint.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static tiku_resource_channel_t *channel;

/** @brief Build a session-correlated request with a fresh mutation identity. */
static tiku_msg_t *request(const char *op, const char *path)
{
    char id[33]; tiku_msg_t *message = tiku_msg_new(TIKU_RESOURCE_MESSAGE_CODE);
    if (!message || tiku_resource_new_id(id) || !tiku_msg_add_string(message, "op", op) ||
        !tiku_msg_add_string(message, "request_id", id) || !tiku_msg_add_string(message, "session", tiku_resource_channel_session(channel)) ||
        (path && !tiku_msg_add_string(message, "path", path))) { tiku_msg_free(message); return NULL; }
    return message;
}

/** @brief Exchange one request, print structured failures and return an owned result. */
static tiku_msg_t *call(tiku_msg_t *message)
{
    tiku_msg_t *reply = NULL, *result, *error;
    if (!message || tiku_resource_channel_send(channel, message, 5000) || tiku_resource_channel_receive(channel, &reply, 5000)) {
        fprintf(stderr, "resource transport failed\n"); tiku_msg_free(message); return NULL;
    }
    result = tiku_msg_find_message(reply, "result", 0);
    error = tiku_msg_find_message(reply, "error", 0);
    if (error) fprintf(stderr, "%s\n", tiku_msg_find_string(error, "code", 0));
    tiku_msg_free(error); tiku_msg_free(reply); tiku_msg_free(message); return result;
}

/** @brief Append one typed member and release its temporary allocation. */
static int member(tiku_msg_t *object, const char *key, tiku_msg_t *value)
{
    tiku_msg_t *entry = tiku_msg_new(0);
    int ok = entry && value && tiku_msg_add_string(entry, "key", key) && tiku_msg_add_message(entry, "value", value) && tiku_msg_add_message(object, "member", entry);
    tiku_msg_free(entry); tiku_msg_free(value); return ok;
}
/** @brief Encode native frame fields from complete integer command arguments. */
static tiku_msg_t *frame(char **arguments)
{
    static const char *const keys[] = {"x", "y", "width", "height"};
    tiku_msg_t *out = tiku_msg_new(0);
    int i;
    if (!out || !tiku_msg_add_string(out, "type", "object")) goto fail;
    for (i = 0; i < 4; i++) {
        char *end; long long number;
        errno = 0; number = strtoll(arguments[i], &end, 10);
        if (errno || end == arguments[i] || *end || !member(out, keys[i], tiku_resource_value_integer(TIKU_RESOURCE_INTEGER, number))) goto fail;
    }
    return out;
fail:
    tiku_msg_free(out); return NULL;
}
/** @brief Copy the server's current provider stamp into a mutation precondition. */
static int expected(tiku_msg_t *message, const tiku_msg_t *stamp)
{
    tiku_msg_t *tuple = tiku_msg_new(0);
    int ok = tuple && tiku_msg_add_string(tuple, "provider", tiku_msg_find_string(stamp, "provider", 0)) &&
        tiku_msg_add_string(tuple, "incarnation", tiku_msg_find_string(stamp, "incarnation", 0)) &&
        tiku_msg_add_string(tuple, "revision", tiku_msg_find_string(stamp, "revision", 0)) && tiku_msg_add_message(message, "expected", tuple);
    tiku_msg_free(tuple); return ok;
}
/** @brief Submit one replacement or action and require a terminal saved outcome. */
static int change(const char *path, tiku_msg_t *value, int invoke)
{
    tiku_msg_t *stamp = call(request("describe", path)), *message, *result;
    char job[33]; unsigned i;
    if (!stamp || !value) { tiku_msg_free(stamp); return -1; }
    message = request(invoke ? "invoke" : "set", path);
    if (!message || !expected(message, stamp) || !tiku_msg_add_message(message, invoke ? "arguments" : "value", value) ||
        (!invoke && !tiku_msg_add_string(message, "durability", "user")) || !tiku_msg_add_int32(message, "deadline_ms", 3000)) {
        tiku_msg_free(message); tiku_msg_free(stamp); return -1;
    }
    tiku_msg_free(stamp); result = call(message);
    if (!result) return -1;
    if (!tiku_msg_find_string(result, "job", 0)) { tiku_msg_free(result); return -1; }
    snprintf(job, sizeof job, "%s", tiku_msg_find_string(result, "job", 0)); tiku_msg_free(result);
    for (i = 0; i < 500; i++) {
        const char *state;
        message = request("job-status", NULL);
        if (!message || !tiku_msg_add_string(message, "job", job)) { tiku_msg_free(message); return -1; }
        result = call(message); if (!result) return -1;
        state = tiku_msg_find_string(result, "state", 0);
        if (state && !strcmp(state, "succeeded")) { tiku_msg_free(result); return 0; }
        if (!state || (strcmp(state, "queued") && strcmp(state, "running"))) {
            fprintf(stderr, "settings job: %s\n", state ? state : "invalid"); tiku_msg_free(result); return -1;
        }
        tiku_msg_free(result); usleep(10000);
    }
    return -1;
}
/** @brief Render saved and active values without interpreting private native models. */
static int read_value(const char *path)
{
    const char *const names[] = {"desired", "persisted", "applied"};
    tiku_msg_t *result = call(request("read", path));
    unsigned i;
    if (!result) return -1;
    printf("{\"apply_state\":\"%s\"", tiku_msg_find_string(result, "apply_state", 0));
    for (i = 0; i < 3; i++) {
        char text[16000]; tiku_msg_t *value = tiku_msg_find_message(result, names[i], 0);
        int n = value ? tiku_resource_value_render(value, text, sizeof text) : -1;
        tiku_msg_free(value);
        if (n < 0) { tiku_msg_free(result); return -1; }
        printf(",\"%s\":%.*s", names[i], n, text);
    }
    puts("}"); tiku_msg_free(result); return 0;
}
/** @brief Verify expected discovery kinds and typed observations from a supplied path list. */
static int audit(const char *path)
{
    FILE *file = fopen(path, "r");
    char line[1024]; int count = 0, error = 0;
    if (!file) return -1;
    while (fgets(line, sizeof line, file)) {
        char *tab = strchr(line, '\t'), *newline;
        tiku_msg_t *description, *snapshot = NULL;
        const char *kind;
        if (!tab) { error = 1; break; }
        *tab++ = 0; newline = strchr(tab, '\n'); if (newline) *newline = 0;
        description = call(request("describe", line));
        kind = description ? tiku_msg_find_string(description, "kind", 0) : NULL;
        if (!kind || strcmp(kind, tab)) { fprintf(stderr, "missing catalog contract: %s %s\n", line, tab); tiku_msg_free(description); error = 1; break; }
        if (!strcmp(kind, "setting") || !strcmp(kind, "state")) {
            const char *const parts[] = {"desired", "persisted", "applied"}; unsigned i;
            snapshot = call(request("read", line));
            for (i = 0; i < 3; i++) {
                tiku_msg_t *value = snapshot ? tiku_msg_find_message(snapshot, parts[i], 0) : NULL;
                if (!value || tiku_resource_value_check(value)) error = 1;
                tiku_msg_free(value);
            }
        }
        tiku_msg_free(snapshot); tiku_msg_free(description); if (error) break; count++;
    }
    if (ferror(file)) error = 1;
    if (fclose(file)) error = 1;
    if (!error) printf("%d catalog bindings verified\n", count);
    return error ? -1 : 0;
}
int main(int argc, char **argv)
{
    const char *path = "/settings/tracker/window-defaults";
    tiku_msg_t *value = NULL;
    int status = 1;
    if (argc < 3) return 2;
    channel = tiku_resource_endpoint_connect(argv[1], 5000);
    if (!channel || tiku_resource_channel_negotiate(channel, 75, 0, 5000)) goto done;
    if (argc == 3 && !strcmp(argv[2], "read")) { status = read_value(path) ? 1 : 0; goto done; }
    if (argc == 4 && !strcmp(argv[2], "audit")) { status = audit(argv[3]) ? 1 : 0; goto done; }
    if (argc == 3 && !strcmp(argv[2], "clear")) { value = tiku_msg_new(0); if (value && !tiku_msg_add_string(value, "type", "object")) goto done; }
    else if (argc == 7 && !strcmp(argv[2], "set")) value = frame(argv + 3);
    else goto done;
    if (value && !change(path, value, 0)) status = 0;
done:
    tiku_msg_free(value); tiku_resource_channel_free(channel); return status;
}
