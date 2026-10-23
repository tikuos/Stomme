/*
 * KintsugiDesktop v0.01 Application Kit.
 *
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 *
 * tiku_clip_rpc.c - bounded clipboard pairs and publication acknowledgments.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_clip_rpc.h"
#include "tiku_clipboard.h"
#include <string.h>

/** @brief Add a complete representation pair, with no partial success. */
static int
pair(tiku_msg_t *message, const char *text, const char *mime, const void *data,
     size_t bytes)
{
    size_t length;

    if (!text)
        return 0;
    length = strlen(text);
    if (length > TIKU_CLIPBOARD_LIMIT ||
        bytes > TIKU_CLIPBOARD_LIMIT - length ||
        (mime ? !data || !bytes || strlen(mime) > 127 : bytes != 0))
        return 0;
    return tiku_msg_add_string(message, "text", text) &&
           (!mime || (tiku_msg_add_string(message, "mime", mime) &&
                      tiku_msg_add_blob(message, "data", data, bytes)));
}

tiku_msg_t *
tiku_clip_request(int64_t id, int operation, const char *text, const char *mime,
                  const void *data, size_t bytes)
{
    tiku_msg_t *message;

    if (id <= 0 || operation < TIKU_CLIP_PROBE || operation > TIKU_CLIP_SET)
        return NULL;
    message = tiku_msg_new(TIKU_MSG_CLIP_REQUEST);
    if (!message || !tiku_msg_add_int64(message, "request", id) ||
        !tiku_msg_add_int32(message, "version", 1) ||
        !tiku_msg_add_int32(message, "operation", operation) ||
        (operation == TIKU_CLIP_SET &&
         !pair(message, text, mime, data, bytes))) {
        tiku_msg_free(message);
        return NULL;
    }
    return message;
}

/** @brief Recognize finite text formats whose consumers parse owned bytes. */
static int
readable(const char *mime)
{
    return mime && (!strcmp(mime, "text/html") || !strcmp(mime, "text/rtf") ||
                    !strcmp(mime, "application/rtf"));
}
tiku_msg_t *
tiku_clip_request_format(int64_t id, const char *mime)
{
    if (id <= 0 || !readable(mime))
        return NULL;
    tiku_msg_t *m = tiku_msg_new(TIKU_MSG_CLIP_REQUEST);
    if (!m || !tiku_msg_add_int64(m, "request", id) ||
        !tiku_msg_add_int32(m, "version", 3) ||
        !tiku_msg_add_int32(m, "operation", TIKU_CLIP_GET) ||
        !tiku_msg_add_string(m, "wanted", mime)) {
        tiku_msg_free(m);
        return NULL;
    }
    return m;
}

/** @brief Validate and atomically import ordered request representations. */
static int
publish(const tiku_msg_t *request, int version)
{
    tiku_clipboard_format_t formats[TIKU_CLIPBOARD_FORMATS] = {0};
    const char *text = tiku_msg_find_string(request, "text", 0);
    size_t count = tiku_msg_count(request, "mime");

    if (!text || tiku_msg_count(request, "text") != 1 ||
        count > (version == 1 ? 1 : TIKU_CLIPBOARD_FORMATS) ||
        tiku_msg_count(request, "data") != (int)count)
        return 0;
    for (size_t i = 0; i < count; i++) {
        formats[i].mime = tiku_msg_find_string(request, "mime", i);
        formats[i].data =
            tiku_msg_find_blob(request, "data", i, &formats[i].bytes);
    }
    return tiku_clipboard_import_formats(text, strlen(text), formats, count);
}

tiku_msg_t *
tiku_clip_request_formats(int64_t id, const char *text,
                          const tiku_clipboard_format_t *formats, size_t count)
{
    tiku_msg_t *message;
    size_t total;

    if (id <= 0 || !text || count > TIKU_CLIPBOARD_FORMATS ||
        (count && !formats) || strlen(text) > TIKU_CLIPBOARD_LIMIT)
        return NULL;
    total = strlen(text);
    message = tiku_msg_new(TIKU_MSG_CLIP_REQUEST);
    if (!message || !tiku_msg_add_int64(message, "request", id) ||
        !tiku_msg_add_int32(message, "version", 2) ||
        !tiku_msg_add_int32(message, "operation", TIKU_CLIP_SET) ||
        !tiku_msg_add_string(message, "text", text))
        goto fail;
    for (size_t i = 0; i < count; i++) {
        if (!formats[i].mime || strlen(formats[i].mime) > 127 ||
            !formats[i].data || !formats[i].bytes ||
            formats[i].bytes > TIKU_CLIPBOARD_LIMIT - total ||
            !tiku_msg_add_string(message, "mime", formats[i].mime) ||
            !tiku_msg_add_blob(message, "data", formats[i].data,
                               formats[i].bytes))
            goto fail;
        total += formats[i].bytes;
    }
    return message;
fail:
    tiku_msg_free(message);
    return NULL;
}

tiku_msg_t *
tiku_clip_reply(const tiku_msg_t *request)
{
    tiku_msg_t *reply;
    int64_t id;
    int32_t operation, version;
    const char *mime;
    const void *data;
    size_t bytes = 0;

    if (tiku_msg_what(request) != TIKU_MSG_CLIP_REQUEST ||
        tiku_msg_count(request, "request") != 1 ||
        tiku_msg_count(request, "version") != 1 ||
        tiku_msg_count(request, "operation") != 1 ||
        !tiku_msg_find_int64(request, "request", 0, &id) || id <= 0 ||
        !tiku_msg_find_int32(request, "version", 0, &version) ||
        (version != 1 && version != 2 && version != 3) ||
        !tiku_msg_find_int32(request, "operation", 0, &operation) ||
        operation < TIKU_CLIP_PROBE || operation > TIKU_CLIP_SET ||
        (version == 2 && operation != TIKU_CLIP_SET) ||
        (version == 3 &&
         (operation != TIKU_CLIP_GET ||
          tiku_msg_count(request, "wanted") != 1 ||
          !readable(tiku_msg_find_string(request, "wanted", 0)))))
        return NULL;
    reply = tiku_msg_new(TIKU_MSG_CLIP_REPLY);
    if (!reply || !tiku_msg_add_int64(reply, "request", id) ||
        !tiku_msg_add_int32(reply, "version", version) ||
        !tiku_msg_add_int32(reply, "operation", operation) ||
        !tiku_msg_add_int32(reply, "accepted", 1))
        goto fail;
    if (operation == TIKU_CLIP_PROBE) {
        if (!tiku_msg_add_int32(reply, "formats", TIKU_CLIPBOARD_FORMATS) ||
            !tiku_msg_add_int32(reply, "read_format", 1))
            goto fail;
    } else if (operation == TIKU_CLIP_GET) {
        if (version == 3) {
            mime = tiku_msg_find_string(request, "wanted", 0);
            data = tiku_clipboard_find(mime, &bytes);
            if (!data && !strcmp(mime, "text/rtf")) {
                mime = "application/rtf";
                data = tiku_clipboard_find(mime, &bytes);
            }
            if (!data)
                mime = NULL;
        } else
            data = tiku_clipboard_data(&mime, &bytes);
        if (!pair(reply, tiku_clipboard_text(), mime, data, bytes))
            goto fail;
    } else if (operation == TIKU_CLIP_SET) {
        if (!publish(request, version))
            goto fail;
    }
    return reply;
fail:
    tiku_msg_free(reply);
    return NULL;
}
