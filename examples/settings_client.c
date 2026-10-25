/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * settings_client.c - standalone SDK reader and asynchronous preference editor.
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

/** @brief Parse one explicitly typed scalar without accepting partial numeric input. */
static tiku_msg_t *parse(const char *type, const char *text)
{
    if (!strcmp(type, "enum")) return tiku_resource_value_string(TIKU_RESOURCE_ENUM, text);
    if (!strcmp(type, "text")) return tiku_resource_value_string(TIKU_RESOURCE_TEXT, text);
    if (!strcmp(type, "boolean")) {
        if (!strcmp(text, "true")) return tiku_resource_value_integer(TIKU_RESOURCE_BOOLEAN, 1);
        if (!strcmp(text, "false")) return tiku_resource_value_integer(TIKU_RESOURCE_BOOLEAN, 0);
    }
    if (!strcmp(type, "integer") || !strcmp(type, "color")) {
        char *end; long long number;
        errno = 0; number = strtoll(text, &end, !strcmp(type, "color") ? 0 : 10);
        if (!errno && end != text && !*end) return tiku_resource_value_integer(!strcmp(type, "color") ? TIKU_RESOURCE_COLOR : TIKU_RESOURCE_INTEGER, number);
    }
    return NULL;
}

/** @brief Submit a complete replacement and wait for its bounded asynchronous outcome. */
static int set(const char *path, const char *type, const char *text)
{
    tiku_msg_t *stamp = call(request("describe", path)), *message, *value, *tuple, *result;
    char job[33]; unsigned i;
    if (!stamp) return -1;
    message = request("set", path); value = parse(type, text); tuple = tiku_msg_new(0);
    if (!message || !value || !tuple || !tiku_msg_add_string(tuple, "provider", tiku_msg_find_string(stamp, "provider", 0)) ||
        !tiku_msg_add_string(tuple, "incarnation", tiku_msg_find_string(stamp, "incarnation", 0)) ||
        !tiku_msg_add_string(tuple, "revision", tiku_msg_find_string(stamp, "revision", 0)) || !tiku_msg_add_message(message, "expected", tuple) ||
        !tiku_msg_add_message(message, "value", value) || !tiku_msg_add_string(message, "durability", "user") || !tiku_msg_add_int32(message, "deadline_ms", 3000)) {
        tiku_msg_free(message); tiku_msg_free(value); tiku_msg_free(tuple); tiku_msg_free(stamp); return -1;
    }
    tiku_msg_free(value); tiku_msg_free(tuple); tiku_msg_free(stamp);
    result = call(message); if (!result) return -1;
    if (!tiku_msg_find_string(result, "job", 0)) { tiku_msg_free(result); return 0; }
    snprintf(job, sizeof job, "%s", tiku_msg_find_string(result, "job", 0)); tiku_msg_free(result);
    for (i = 0; i < 500; i++) {
        const char *state;
        message = request("job-status", NULL);
        if (!message || !tiku_msg_add_string(message, "job", job)) { tiku_msg_free(message); return -1; }
        result = call(message); if (!result) return -1;
        state = tiku_msg_find_string(result, "state", 0);
        if (state && !strcmp(state, "succeeded")) { tiku_msg_free(result); return 0; }
        if (!state || (strcmp(state, "queued") && strcmp(state, "running") && strcmp(state, "awaiting-confirmation"))) {
            fprintf(stderr, "setting job: %s\n", state ? state : "invalid response"); tiku_msg_free(result); return -1;
        }
        tiku_msg_free(result); usleep(10000);
    }
    fprintf(stderr, "setting job deadline; outcome can be reconciled through the SDK\n"); return -1;
}

int main(int argc, char **argv)
{
    tiku_msg_t *result, *value; char text[8192]; int count, status = 1;
    if (argc != 3 && argc != 5) { fprintf(stderr, "usage: settings-client PRIVATE_DIRECTORY PATH [TYPE VALUE]\n"); return 2; }
    channel = tiku_resource_endpoint_connect(argv[1], 5000);
    if (!channel || tiku_resource_channel_negotiate(channel, 11, 0, 5000)) goto done;
    if (argc == 5 && set(argv[2], argv[3], argv[4])) goto done;
    result = call(request("read", argv[2])); if (!result) goto done;
    value = tiku_msg_find_message(result, "desired", 0);
    count = value ? tiku_resource_value_render(value, text, sizeof text) : -1;
    if (count >= 0 && fwrite(text, 1, (size_t)count, stdout) == (size_t)count) status = 0;
    tiku_msg_free(value); tiku_msg_free(result);
done:
    tiku_resource_channel_free(channel); return status;
}
