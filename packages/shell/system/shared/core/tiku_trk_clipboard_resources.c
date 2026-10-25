/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_clipboard_resources.c - native clipboard metadata and bounded
 * transfer.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_trk_clipboard_resources.h"
#include "tiku_trk_resource_arguments.h"
#include "tiku_trk_resource_observer.h"
#include "tiku_clipboard.h"
#include <stdlib.h>
#include <unistd.h>
struct tiku_trk_clipboard_resources {
    tiku_trk_resource_observer_t *observer;
    unsigned stamp;
    char generation[33];
    int initialized;
};
static const tiku_trk_observation_t fields[] = {
    {"/clipboard/availability", 0, 3},
    {"/clipboard/generation", 0, 3},
    {"/clipboard/owner", 0, 3},
    {"/clipboard/types", 0, 3},
    {"/clipboard/actions/read-content", 2, 3},
    {"/clipboard/actions/publish-content", 2, 3}};
/** @brief Assign a new transfer generation after an observed native change.
 */
static int refresh(tiku_trk_clipboard_resources_t *owner)
{
    unsigned stamp = tiku_clipboard_stamp();
    if (owner->initialized && owner->stamp == stamp)
        return 0;
    char id[33];if(tiku_resource_new_id(id)){owner->initialized=0;return -1;}
    memcpy(owner->generation, id, sizeof id);
    owner->stamp = stamp;
    owner->initialized = 1;
    return 0;
}
/** @brief Clipboard metadata is represented by states rather than stream
 * rows.
 */
static int count(void *context, const char *path)
{
    (void)context;
    (void)path;
    return 0;
}
/** @brief Copy native metadata without copying content or claiming a
 * publisher.
 */
static tiku_msg_t *capture(void *context,const char *path,int index)
{
    tiku_trk_clipboard_resources_t *owner = context;
    tiku_msg_t *out = trk_value_container("object");
    int bad = 0;
    (void)index;
    if(!strcmp(path,"/clipboard/availability")) {
        bad |= TRK_BOOL(out, "available", owner->initialized);
        bad |= TRK_TEXT(out, "source", "native-process-store");
        bad |= TRK_INT(out, "read_chunk_bytes", 2048);
        bad |= TRK_INT(out, "publish_binary_bytes", 3072);
        bad |= TRK_INT(out, "publish_formats", 1);
        bad |= TRK_TEXT(out, "external_import", "native_host_events");
        bad |= TRK_BOOL(out, "force_external_import", 0);
    } else if(!strcmp(path,"/clipboard/generation")) {
        bad |= TRK_TEXT(out, "token", owner->generation);
        bad |= trk_counter(out, "native_stamp", owner->stamp);
    } else if(!strcmp(path,"/clipboard/owner")) {
        bad |= TRK_TEXT(out, "store", "desktop-interface-clipboard");
        bad |= TRK_INT(out, "pid", getpid());
        bad |= TRK_BOOL(out, "publisher_known", 0);
    } else {
        tiku_clipboard_format_t format;
        size_t i = 0;
        bad |= trk_counter(out, "text_bytes", tiku_clipboard_length());
        bad |= TRK_TEXT(out, "text_mime", "text/plain;charset=utf-8");
        for(;tiku_clipboard_format_at(i,&format);i++) {
            char key[32];
            snprintf(key, sizeof key, "mime%zu", i);
            bad |= TRK_TEXT(out, key, format.mime);
            snprintf(key, sizeof key, "bytes%zu", i);
            bad |= trk_counter(out, key, format.bytes);
        }
        bad|=TRK_INT(out,"typed_count",i);
    }
    if(bad){tiku_msg_free(out);return NULL;}return out;
}
/** @brief Validate the native MIME token syntax before an atomic publication.
 */
static int mime_valid(const char *mime)
{
    size_t slash=0,n=strlen(mime);if(!n||n>127)return 0;
    for(size_t i=0;i<n;i++) {
        unsigned char c=(unsigned char)mime[i];
        if(c=='/'){if(!i||slash)return 0;slash=i;
        } else if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                     (c >= '0' && c <= '9') || strchr("!#$&^_.+-", c)))
            return 0;
    }
    return slash&&slash+1<n;
}
/** @brief Resolve one exact current native representation and byte range. */
static tiku_resource_error_t read_arguments(const tiku_msg_t *value,
                                            const void **bytes, size_t *length,
                                            size_t *offset, size_t *maximum,
                                            char mime[128])
{
    const char *keys[] = {"generation", "representation", "mime", "offset",
                          "maximum"};
    char representation[16];
    int64_t at, limit;
    if (!trk_arguments_keys(value, keys, 5) ||
        !trk_arguments_text(value, "representation", representation,
                            sizeof representation) ||
        !trk_arguments_integer(value, "offset", 0, TIKU_CLIPBOARD_LIMIT,
                               &at) ||
        !trk_arguments_integer(value, "maximum", 1, 2048, &limit))
        return TIKU_RESOURCE_VALUE;
    if(!strcmp(representation,"text")) {
        if(trk_arguments_has(value,"mime"))return TIKU_RESOURCE_VALUE;
        *bytes = tiku_clipboard_text();
        *length = tiku_clipboard_length();
        snprintf(mime, 128, "text/plain;charset=utf-8");
    } else if(!strcmp(representation,"typed")) {
        if (!trk_arguments_text(value, "mime", mime, 128))
            return TIKU_RESOURCE_VALUE;
        *bytes = tiku_clipboard_find(mime, length);
        if (!*bytes)
            return TIKU_RESOURCE_NOT_FOUND;
    } else return TIKU_RESOURCE_VALUE;
    if((size_t)at>*length)return TIKU_RESOURCE_VALUE;
    *offset=(size_t)at;*maximum=(size_t)limit;return 0;
}
/** @brief Validate exact text and optional binary payload without changing
 * the store.
 */
static tiku_resource_error_t publish_arguments(const tiku_msg_t *value,
                                               char text[4097], char mime[128],
                                               unsigned char bytes[3072],
                                               int *length)
{
    const char *keys[] = {"generation", "text", "mime", "data_base64"};
    char encoded[4097];
    int has_mime = trk_arguments_has(value, "mime"),
        has_data = trk_arguments_has(value, "data_base64");
    if (!trk_arguments_keys(value, keys, 4) ||
        !trk_arguments_text(value, "text", text, 4097) || has_mime != has_data)
        return TIKU_RESOURCE_VALUE;
    *length=0;mime[0]=0;
    if(has_mime) {
        if (!trk_arguments_text(value, "mime", mime, 128) ||
            !mime_valid(mime) ||
            !trk_arguments_text(value, "data_base64", encoded,
                                sizeof encoded) ||
            (*length = trk_base64_decode(encoded, bytes, 3072)) <= 0)
            return TIKU_RESOURCE_VALUE;
    }
    return 0;
}
/** @brief Require a current transfer generation before touching content. */
static tiku_resource_error_t validate(void *context, const char *path,
                                      const tiku_msg_t *value, int invoke)
{
    tiku_trk_clipboard_resources_t *owner=context;char generation[33];
    if(!invoke)return TIKU_RESOURCE_UNSUPPORTED;
    if (!trk_arguments_text(value, "generation", generation,
                            sizeof generation))
        return TIKU_RESOURCE_VALUE;
    if(!owner->initialized)return TIKU_RESOURCE_UNAVAILABLE;
    if (owner->stamp != tiku_clipboard_stamp() ||
        strcmp(generation, owner->generation))
        return TIKU_RESOURCE_STALE;
    if(!strcmp(path,"/clipboard/actions/read-content")) {
        const void *bytes;
        size_t length, offset, maximum;
        char mime[128];
        return read_arguments(value, &bytes, &length, &offset, &maximum, mime);
    }
    if(!strcmp(path,"/clipboard/actions/publish-content")) {
        char text[4097], mime[128];
        unsigned char bytes[3072];
        int length;
        return publish_arguments(value, text, mime, bytes, &length);
    }
    return TIKU_RESOURCE_UNSUPPORTED;
}
/** @brief Copy one bounded read result or publish atomically through
 * Interface Kit.
 */
static tiku_resource_error_t start(void *context, const char *job,
                                   const char *path, const tiku_msg_t *value,
                                   int invoke, tiku_resource_snapshot_t *out)
{
    tiku_trk_clipboard_resources_t *owner = context;
    tiku_resource_error_t error = validate(context, path, value, invoke);
    (void)job;
    if(error)return error;
    tiku_msg_t *result=trk_value_container("object");int bad=0;
    if(!strcmp(path,"/clipboard/actions/read-content")) {
        const void *bytes;
        size_t length, offset, maximum;
        char mime[128], encoded[2733];
        error =
            read_arguments(value, &bytes, &length, &offset, &maximum, mime);
        if (error) {
            tiku_msg_free(result);
            return error;
        }
        size_t size=length-offset;if(size>maximum)size=maximum;
        if (trk_base64_encode((const unsigned char *)bytes + offset, size,
                              encoded, sizeof encoded) < 0) {
            tiku_msg_free(result);
            return TIKU_RESOURCE_VALUE;
        }
        bad |= TRK_TEXT(result, "encoding", "base64");
        bad |= TRK_TEXT(result, "data", encoded);
        bad |= TRK_TEXT(result, "mime", mime);
        bad |= TRK_INT(result, "offset", offset);
        bad |= TRK_INT(result, "next_offset", offset + size);
        bad |= trk_counter(result, "total_bytes", length);
        bad |= TRK_BOOL(result, "complete", offset + size == length);
    } else {
        char text[4097],mime[128];unsigned char bytes[3072];int length;
        error = publish_arguments(value, text, mime, bytes, &length);
        if (error) {
            tiku_msg_free(result);
            return error;
        }
        if (!tiku_clipboard_import_typed(text, strlen(text),
                                         mime[0] ? mime : NULL, bytes,
                                         (size_t)length)) {
            tiku_msg_free(result);
            return TIKU_RESOURCE_NO_MEMORY;
        }
        if (refresh(owner)) {
            tiku_msg_free(result);
            return TIKU_RESOURCE_UNKNOWN_OUTCOME;
        }
        bad |= TRK_TEXT(result, "phase", "native_store_published");
        bad |= TRK_TEXT(result, "external_export", "native_host_poll");
    }
    bad|=TRK_TEXT(result,"generation",owner->generation);
    if(bad){tiku_msg_free(result);return TIKU_RESOURCE_UNKNOWN_OUTCOME;}
    return trk_value_snapshot(out, result, "completed")
               ? TIKU_RESOURCE_UNKNOWN_OUTCOME
               : 0;
}
tiku_trk_clipboard_resources_t *
tiku_trk_clipboard_resources_open(tiku_resource_registry_t *registry)
{
    tiku_trk_clipboard_resources_t *owner = calloc(1, sizeof *owner);
    if (!owner)
        return NULL;
    if(refresh(owner)){free(owner);return NULL;}
    const tiku_resource_async_controller_t controller = {.validate = validate,
                                                         .start = start};
    owner->observer = tiku_trk_resource_observer_open_controlled(
        registry, "native-clipboard", "/clipboard", fields, 6, count, capture,
        &controller, owner);
    if(!owner->observer){free(owner);return NULL;}return owner;
}
void tiku_trk_clipboard_resources_poll(tiku_trk_clipboard_resources_t *owner,
                                       uint64_t now)
{
    if(!owner)return;
    (void)refresh(owner);tiku_trk_resource_observer_poll(owner->observer,now);
}
void tiku_trk_clipboard_resources_close(tiku_trk_clipboard_resources_t *owner)
{
    if(!owner)return;
    tiku_trk_resource_observer_close(owner->observer);free(owner);
}
