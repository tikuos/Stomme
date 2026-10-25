/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * stomme_resources.c - local desktop resource command client using the public SDK.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_resource_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
/** @brief Supported commands operate inside the desktop resource namespace. */
static void usage(void)
{
    fputs(
        "usage: stomme-resources [--directory PRIVATE_DIRECTORY] COMMAND ...\n"
        "  list PATH | describe PATH | read PATH | stream PATH\n"
        "  set PATH TYPE VALUE | invoke PATH [JSON_ARGUMENTS]\n"
        "  watch PATH [EVENT_COUNT] [TIMEOUT_MS]\n"
        "  job JOB_ID | cancel JOB_ID | receipt REQUEST_ID\n"
        "Types: boolean integer number text enum counter color json\n"
        "set/invoke print an initial receipt; use job to observe completion.\n"
        "The directory defaults to STOMME_RESOURCE_DIRECTORY.\n",
        stderr);
}
/** @brief Print a complete JSON result without a partially formatted success. */
static int print(const tiku_msg_t *message)
{
    char *bytes = malloc(1024 * 1024);
    int n =
        bytes ? tiku_resource_client_format(message, bytes, 1024 * 1024) : -1;
    int ok = n >= 0 && fwrite(bytes, 1, (size_t)n, stdout) == (size_t)n;
    free(bytes);
    return ok ? 0 : 1;
}
/** @brief Report structured failures and the retained mutation reconciliation ID. */
static int failure(tiku_resource_client_t *client, tiku_resource_error_t error)
{
    tiku_msg_t *details = tiku_resource_client_error(client);
    fprintf(stderr, "%s", tiku_resource_error_name(error));
    const char *id = tiku_resource_client_mutation(client);
    if (*id)
        fprintf(stderr, " request_id=%s", id);
    const char *effect = tiku_msg_find_string(details, "effect", 0);
    if (effect)
        fprintf(stderr, " effect=%s", effect);
    fputc('\n', stderr);
    tiku_msg_free(details);
    return error == TIKU_RESOURCE_UNKNOWN_OUTCOME ? 4 : 1;
}
/** @brief Parse bounded unsigned decimal control arguments. */
static int integer(const char *text, unsigned low, unsigned high,
                   unsigned *out)
{
    uint64_t value;
    if (tiku_resource_counter_parse(text, &value) || value < low ||
        value > high)
        return 0;
    *out = (unsigned)value;
    return 1;
}
/** @brief Return monotonic milliseconds for a bounded watch interval. */
static uint64_t now(void)
{
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t))
        return 0;
    return (uint64_t)t.tv_sec * 1000 + (uint64_t)t.tv_nsec / 1000000;
}
/** @brief Enumerate pages; a changed catalog reports stale_cursor and exits nonzero. */
static int list(tiku_resource_client_t *client, const char *path)
{
    char cursor[33] = {0};
    do {
        tiku_msg_t *page = NULL;
        tiku_resource_error_t e =
            tiku_resource_client_list(client, path, cursor, &page);
        if (e)
            return failure(client, e);
        int bad = print(page);
        const char *next = tiku_msg_find_string(page, "next_cursor", 0);
        if (next && *next && !tiku_resource_id_valid(next)) {
            tiku_msg_free(page);
            return failure(client, TIKU_RESOURCE_INVALID);
        }
        snprintf(cursor, sizeof cursor, "%s", next ? next : "");
        tiku_msg_free(page);
        if (bad)
            return 1;
    } while (*cursor);
    return 0;
}
/** @brief Print the atomic subscription snapshot, followed by bounded live events. */
static int watch(tiku_resource_client_t *client, const char *path,
                 unsigned count, unsigned timeout)
{
    tiku_msg_t *description = NULL, *initial = NULL, *event = NULL;
    tiku_resource_error_t e =
        tiku_resource_client_observe(client, "describe", path, &description);
    if (!e)
        e = tiku_resource_client_watch(client, description, path, &initial);
    tiku_msg_free(description);
    if (e)
        return failure(client, e);
    char id[33];
    const char *subscription =
        tiku_msg_find_string(initial, "subscription", 0);
    if (!tiku_resource_id_valid(subscription)) {
        tiku_msg_free(initial);
        return failure(client, TIKU_RESOURCE_INVALID);
    }
    strcpy(id, subscription);
    int bad = print(initial);
    tiku_msg_free(initial);
    fflush(stdout);
    for (unsigned i = 0; !bad && i < count; i++) {
        uint64_t start = now();
        do {
            e = tiku_resource_client_event(client, 100, &event);
            if (e == TIKU_RESOURCE_PENDING)
                usleep(1000);
        } while (e == TIKU_RESOURCE_PENDING && now() &&
                 now() - start < timeout);
        if (e) {
            bad = e == TIKU_RESOURCE_PENDING ? 3 : failure(client, e);
            break;
        }
        const char *kind = tiku_msg_find_string(event, "kind", 0);
        bad = print(event);
        fflush(stdout);
        if (kind && (!strcmp(kind, "removed") || !strcmp(kind, "overflow")))
            bad = 1;
        tiku_msg_free(event);
        event = NULL;
    }
    (void)tiku_resource_client_unwatch(client, id);
    return bad;
}
/** @brief Execute one explicitly typed local resource command. */
int main(int argc, char **argv)
{
    int at = 1;
    const char *directory = getenv("STOMME_RESOURCE_DIRECTORY");
    if (argc > 1 && !strcmp(argv[1], "--help")) {
        usage();
        return 0;
    }
    if (argc > 3 && !strcmp(argv[1], "--directory")) {
        directory = argv[2];
        at = 3;
    }
    if (!directory || !*directory || argc - at < 2) {
        usage();
        return 2;
    }
    const char *command = argv[at++], *path = argv[at++];
    int remaining = argc - at;
    tiku_resource_client_t *client =
        tiku_resource_client_open(directory, 5000);
    if (!client) {
        fputs("resource endpoint unavailable\n", stderr);
        return 1;
    }
    tiku_msg_t *result = NULL, *description = NULL, *value = NULL;
    tiku_resource_error_t e = TIKU_RESOURCE_OK;
    int status = 0;
    if (!strcmp(command, "list") && !remaining)
        status = list(client, path);
    else if ((!strcmp(command, "describe") || !strcmp(command, "read")) &&
             !remaining) {
        e = tiku_resource_client_observe(client, command, path, &result);
        if (!e)
            status = print(result);
    } else if ((!strcmp(command, "job") || !strcmp(command, "cancel") ||
                !strcmp(command, "receipt")) &&
               !remaining) {
        e = !strcmp(command, "job")
                ? tiku_resource_client_job(client, path, &result)
            : !strcmp(command, "cancel")
                ? tiku_resource_client_cancel(client, path, &result)
                : tiku_resource_client_receipt(client, path, &result);
        if (!e)
            status = print(result);
    } else if ((!strcmp(command, "set") && remaining == 2) ||
               (!strcmp(command, "invoke") && remaining <= 1)) {
        int invoke = !strcmp(command, "invoke");
        e = tiku_resource_literal_parse(
            invoke ? "json" : argv[at],
            invoke ? (remaining ? argv[at] : "{}") : argv[at + 1], &value);
        if (!e)
            e = tiku_resource_client_observe(client, "describe", path,
                                             &description);
        if (!e)
            e = tiku_resource_client_change(client, description, value, invoke,
                                            &result);
        if (!e) {
            status = print(result);
            fprintf(stderr, "request_id=%s\n",
                    tiku_resource_client_mutation(client));
        }
    } else if (!strcmp(command, "stream") && !remaining) {
        e = tiku_resource_client_observe(client, "describe", path,
                                         &description);
        void *bytes = NULL;
        size_t length = 0;
        if (!e)
            e = tiku_resource_client_stream(client, description,
                                            TIKU_RESOURCE_STREAM_BYTES, &bytes,
                                            &length);
        if (!e && fwrite(bytes, 1, length, stdout) != length)
            status = 1;
        free(bytes);
    } else if (!strcmp(command, "watch") && remaining <= 2) {
        unsigned count = 1, timeout = 30000;
        if ((remaining && !integer(argv[at], 1, 1000000, &count)) ||
            (remaining == 2 && !integer(argv[at + 1], 1, 30000, &timeout))) {
            usage();
            status = 2;
        } else
            status = watch(client, path, count, timeout);
    } else {
        usage();
        status = 2;
    }
    if (e)
        status = failure(client, e);
    tiku_msg_free(result);
    tiku_msg_free(description);
    tiku_msg_free(value);
    tiku_resource_client_close(client);
    return status;
}
