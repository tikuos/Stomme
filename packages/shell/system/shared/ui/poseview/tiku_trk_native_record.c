/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_native_record.c - native record codecs for typed shell resources.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_trk_poseview_private.h"
#include "tiku_trk_native_record.h"
#include "tiku_trk_resource_values.h"
#include "tiku_trk_frame.h"
#include "tiku_trk_deskbar.h"
#include <errno.h>
#include <limits.h>

#define NUMBER(object, key, value)                                            \
    trk_value_member(                                                         \
        object, key,                                                          \
        tiku_resource_value_integer(TIKU_RESOURCE_INTEGER, value))
#define STRING(object, key, value)                                            \
    trk_value_member(object, key,                                             \
                     tiku_resource_value_string(TIKU_RESOURCE_TEXT, value))

/** @brief Decode one bounded typed integer from a copied member. */
static int native_number(const tiku_msg_t *object, const char *key,
                         int64_t low, int64_t high, int64_t *out)
{
    tiku_msg_t *value = trk_value_find(object,key);
    const char *type = tiku_msg_find_string(value,"type",0);
    int ok = type && !strcmp(type, "integer") &&
             tiku_msg_find_int64(value, "value", 0, out) && *out >= low &&
             *out <= high;
    tiku_msg_free(value); return ok ? 0 : -1;
}

/** @brief Copy a bounded typed string without shortening it. */
static int native_string(const tiku_msg_t *object, const char *key, char *out,
                         size_t capacity)
{
    tiku_msg_t *value = trk_value_find(object,key);
    const char *type = tiku_msg_find_string(value, "type", 0),
               *text = tiku_msg_find_string(value, "value", 0);
    int ok = type && text && !strcmp(type,"text") && strlen(text) < capacity;
    if (ok) strcpy(out,text);
    tiku_msg_free(value); return ok ? 0 : -1;
}

/** @brief Reverse one native column scalar's byte order. */
static uint32_t native_swap(uint32_t value)
{
    return (value << 24) | ((value & 0xff00u) << 8) |
           ((value >> 8) & 0xff00u) | (value >> 24);
}

/** @brief Whether one column record carries supported public fields. */
static int native_column_valid(const column_rec_t *record)
{
    return record->key == COLUMN_REC_KEY &&
           record->version == COLUMN_REC_VERSION && record->offset >= 0 &&
           record->offset <= 10000 && record->width >= 0 &&
           record->width <= 10000 &&
           record->alignment >= TIKU_TRK_ALIGN_LEFT &&
           record->alignment <= TIKU_TRK_ALIGN_CENTER &&
           ((record->field < TIKU_TRK_COL_ATTR_BASE && !record->attr[0]) ||
            (record->field >= TIKU_TRK_COL_ATTR_BASE && record->attr[0])) &&
           memchr(record->attr, 0, sizeof record->attr) &&
           record->field >= 0 &&
           (record->field <= TIKU_TRK_COL_ORIGINAL ||
            (record->field >= TIKU_TRK_COL_ATTR_BASE &&
             record->field < TIKU_TRK_COL_ATTR_BASE + TIKU_TRK_COL_ATTR_MAX));
}

/** @brief Decode complete native columns, normalizing a supported foreign
 * byte order.
 */
static tiku_msg_t *native_columns(const void *bytes, size_t length)
{
    column_rec_t records[TIKU_TRK_COLUMNS_MAX];
    size_t count = length / sizeof records[0], i, j;
    tiku_msg_t *out = trk_value_container("array"); int order = -1;
    if (length % sizeof records[0] || count > TIKU_TRK_COLUMNS_MAX) goto fail;
    if (length) memcpy(records,bytes,length);
    for (i = 0; i < count; i++) {
        column_rec_t *record = &records[i];
        int swapped = record->key == native_swap(COLUMN_REC_KEY);
        tiku_msg_t *item;
        if (order >= 0 && order != swapped) goto fail;
        order = swapped;
        if (swapped) {
            record->key = native_swap(record->key);
            record->version = (int32_t)native_swap((uint32_t)record->version);
            record->field = (int32_t)native_swap((uint32_t)record->field);
            record->offset = (int32_t)native_swap((uint32_t)record->offset);
            record->width = (int32_t)native_swap((uint32_t)record->width);
            record->alignment =
                (int32_t)native_swap((uint32_t)record->alignment);
        }
        if (!native_column_valid(record)) goto fail;
        for (j = 0; j < i; j++)
            if (records[j].field == record->field ||
                (record->attr[0] && !strcmp(records[j].attr, record->attr)))
                goto fail;
        item = trk_value_container("object");
        if (NUMBER(item, "version", record->version) ||
            NUMBER(item, "field", record->field) ||
            NUMBER(item, "offset", record->offset) ||
            NUMBER(item, "width", record->width) ||
            NUMBER(item, "alignment", record->alignment) ||
            STRING(item, "attribute", record->attr)) {
            tiku_msg_free(item); goto fail;
        }
        if (trk_value_item(out,item)) goto fail;
    }
    if (count) {
        for (i = 0; i < count && records[i].field != TIKU_TRK_COL_NAME; i++) {}
        if (i == count) goto fail;
    }
    return out;
fail: tiku_msg_free(out); return NULL;
}

static const char *const view_keys[] = {
    "icon_size", "scroll",     "icon_x",      "icon_y",       "mini_x",
    "mini_y",    "sort_field", "sort2_field", "sort_reverse", "last_icon"};

/** @brief Native view-state field bounds, checked before conversion to int.
 */
static int native_view_range(int index, int64_t value)
{
    if (index == 0) return value >= 16 && value <= 128;
    if (index == 1) return value >= 0 && value <= TIKU_TRK_SANE_LOCATION;
    if (index < 6)
        return value >= -TIKU_TRK_SANE_LOCATION &&
               value <= TIKU_TRK_SANE_LOCATION;
    if (index < 8)
        return value >= (index == 6 ? 0 : -1) &&
               (value <= TIKU_TRK_COL_ORIGINAL ||
                (value >= TIKU_TRK_COL_ATTR_BASE &&
                 value < TIKU_TRK_COL_ATTR_BASE + TIKU_TRK_COL_ATTR_MAX));
    return value == 0 || value == 1;
}

/** @brief Decode a versioned view without filling omitted legacy fields. */
static tiku_msg_t *native_view(const char *bytes)
{
    char mode[5];
    const char *at = bytes, *space = strchr(bytes, ' ');
    size_t size = space ? (size_t)(space - bytes) : strlen(bytes);
    int i = 0;
    tiku_msg_t *out = trk_value_container("object");
    if (!((size == 4) &&
          (!memcmp(bytes, "icon", 4) || !memcmp(bytes, "mini", 4) ||
           !memcmp(bytes, "list", 4))))
        goto fail;
    memcpy(mode,bytes,4); mode[4] = 0;
    if (NUMBER(out,"version",1) || STRING(out,"mode",mode)) goto fail;
    at += size;
    while (*at) {
        char *end; long long value;
        while (*at == ' ') at++;
        if (!*at) break;
        errno = 0; value = strtoll(at,&end,10);
        if (i == 10 || errno || at == end || (*end && *end != ' ') ||
            !native_view_range(i, value))
            goto fail;
        if (NUMBER(out,view_keys[i++],value)) goto fail;
        at = end;
    }
    if (i != 0 && i != 1 && i != 6 && i != 9 && i != 10) goto fail;
    return out;
fail: tiku_msg_free(out); return NULL;
}

/** @brief Decode native session or tray text into complete public rows. */
static tiku_msg_t *native_lines(tiku_trk_native_record_kind_t kind, char *bytes)
{
    tiku_msg_t *out = trk_value_container("array");
    char *line, *save;
    int count = 0;
    for (line = strtok_r(bytes, "\n", &save); line;
         line = strtok_r(NULL, "\n", &save)) {
        tiku_msg_t *item = trk_value_container("object"); int bad = 0;
        if (kind == TIKU_TRK_RECORD_SESSION) {
            const char *path = line; int minimized = 0;
            if ((line[0] == 'M' || line[0] == 'O') && line[1] == ' ') {
                minimized = line[0] == 'M';
                path += 2;
            }
            if (++count > TIKU_TRK_SESSION_MAX || *path != '/' ||
                strlen(path) >= TIKU_PATH_MAX || strpbrk(path, "\r\t"))
                bad = 1;
            bad |= STRING(item,"path",path);
            bad |= trk_value_member(
                item, "minimized",
                tiku_resource_value_integer(TIKU_RESOURCE_BOOLEAN, minimized));
        } else {
            char *path = strchr(line, '\t'), *height, *end;
            long size = TIKU_TRK_DB_MAX_REPLICANT_H;
            if (!path) {
                tiku_msg_free(item);
                goto fail;
            }
            *path++ = 0;
            height = strchr(path, '\t');
            if (height) {
                *height++ = 0;
                errno = 0;
                size = strtol(height, &end, 10);
                if (errno || end == height || *end)
                    bad = 1;
            }
            if (++count > TIKU_TRK_DB_TRAY_MAX || !*line ||
                strlen(line) >= sizeof((tiku_trk_replicant_t *)0)->label ||
                *path != '/' || strlen(path) >= TIKU_PATH_MAX ||
                strpbrk(path, "\r\t") || size < 1 ||
                size > TIKU_TRK_DB_MAX_REPLICANT_H)
                bad = 1;
            bad |= STRING(item, "label", line);
            bad |= STRING(item, "path", path);
            bad |= NUMBER(item, "height", size);
        }
        if (bad) { tiku_msg_free(item); goto fail; }
        if (trk_value_item(out,item)) goto fail;
    }
    {
        int i,j;
        for (i = 0; i < count; i++) for (j = 0; j < i; j++) {
                tiku_msg_t *a = tiku_msg_find_message(out, "item", i),
                           *b = tiku_msg_find_message(out, "item", j);
                tiku_msg_t *ap = trk_value_find(a, "path"),
                           *bp = trk_value_find(b, "path");
                int duplicate = tiku_resource_value_equal(ap, bp);
                tiku_msg_free(ap);
                tiku_msg_free(bp);
                if (kind == TIKU_TRK_RECORD_REPLICANTS) {
                    ap = trk_value_find(a, "label");
                    bp = trk_value_find(b, "label");
                    duplicate |= tiku_resource_value_equal(ap, bp);
                    tiku_msg_free(ap);
                    tiku_msg_free(bp);
                }
            tiku_msg_free(a); tiku_msg_free(b); if (duplicate) goto fail;
        }
    }
    return out;
fail: tiku_msg_free(out); return NULL;
}

tiku_msg_t *tiku_trk_native_record_decode(tiku_trk_native_record_kind_t kind,
                                          const void *bytes, size_t length)
{
    tiku_msg_t *out; int bad = 0;
    if (length && !bytes) return NULL;
    if (kind == TIKU_TRK_RECORD_COLUMNS) return native_columns(bytes,length);
    if (kind == TIKU_TRK_RECORD_ORIGINAL || kind == TIKU_TRK_RECORD_VIEW ||
        kind == TIKU_TRK_RECORD_SESSION ||
        kind == TIKU_TRK_RECORD_REPLICANTS) {
        char *text;
        if (length > TIKU_PATH_MAX * TIKU_TRK_SESSION_MAX ||
            (length && memchr(bytes, 0, length)))
            return NULL;
        text = malloc(length+1); if (!text) return NULL;
        if (length) memcpy(text,bytes,length);
        text[length] = 0;
        if (kind == TIKU_TRK_RECORD_ORIGINAL)
            out = length < TIKU_PATH_MAX
                      ? tiku_resource_value_string(TIKU_RESOURCE_TEXT, text)
                      : NULL;
        else if (kind == TIKU_TRK_RECORD_VIEW)
            out = length ? native_view(text) : trk_value_container("object");
        else out = native_lines(kind,text);
        free(text); return out;
    }
    out = trk_value_container("object"); if (!length) return out;
    if (kind == TIKU_TRK_RECORD_FRAME) {
        tiku_trk_frame_t frame;
        if (length != sizeof frame) goto fail;
        memcpy(&frame,bytes,sizeof frame);
        if (frame.w < TIKU_TRK_FRAME_MIN_W || frame.h < TIKU_TRK_FRAME_MIN_H)
            goto fail;
        bad |= NUMBER(out, "x", frame.x);
        bad |= NUMBER(out, "y", frame.y);
        bad |= NUMBER(out, "width", frame.w);
        bad |= NUMBER(out, "height", frame.h);
    } else if (kind == TIKU_TRK_RECORD_POSE) {
        pose_info_t pose = {0};
        if (length != sizeof pose && length != sizeof(pose_info_v1_t))
            goto fail;
        memcpy(&pose,bytes,length);
        if (pose.x < -TIKU_TRK_SANE_LOCATION ||
            pose.x > TIKU_TRK_SANE_LOCATION ||
            pose.y < -TIKU_TRK_SANE_LOCATION ||
            pose.y > TIKU_TRK_SANE_LOCATION ||
            (pose.flags & ~POSE_INFO_INVISIBLE))
            goto fail;
        bad |= NUMBER(out, "version", length == sizeof pose ? 2 : 1);
        bad |= NUMBER(out, "x", pose.x);
        bad |= NUMBER(out, "y", pose.y);
        bad |= NUMBER(out, "flags", pose.flags);
    } else if (kind == TIKU_TRK_RECORD_POSITION ||
               kind == TIKU_TRK_RECORD_DOCK) {
        int32_t values[2];
        if (length != sizeof values)
            goto fail;
        memcpy(values, bytes, sizeof values);
        if (kind == TIKU_TRK_RECORD_DOCK &&
            (values[0] < 0 || values[0] >= TIKU_TRK_DB_DOCKS ||
             values[1] < TIKU_TRK_DB_MIN_W || values[1] > TIKU_TRK_DB_MAX_W))
            goto fail;
        bad |= NUMBER(out, kind == TIKU_TRK_RECORD_DOCK ? "dock" : "x",
                      values[0]);
        bad |= NUMBER(out, kind == TIKU_TRK_RECORD_DOCK ? "width" : "y",
                      values[1]);
    } else
        goto fail;
    if (!bad) return out;
fail: tiku_msg_free(out); return NULL;
}

/** @brief Encode declared scalar fields into a complete native structure. */
static int native_struct(tiku_trk_native_record_kind_t kind,
                         const tiku_msg_t *value, void *out, size_t capacity,
                         size_t *length)
{
    int64_t x, y, w, h, flags, version;
    int32_t position[2];
    tiku_trk_frame_t frame;
    pose_info_t pose;
    if (kind == TIKU_TRK_RECORD_FRAME) {
        if (capacity < sizeof frame ||
            native_number(value, "x", INT32_MIN, INT32_MAX, &x) ||
            native_number(value, "y", INT32_MIN, INT32_MAX, &y) ||
            native_number(value, "width", TIKU_TRK_FRAME_MIN_W, INT32_MAX,
                          &w) ||
            native_number(value, "height", TIKU_TRK_FRAME_MIN_H, INT32_MAX,
                          &h))
            return -1;
        frame =
            (tiku_trk_frame_t){(int32_t)x, (int32_t)y, (int32_t)w, (int32_t)h};
        memcpy(out, &frame, sizeof frame);
        *length = sizeof frame;
    } else if (kind == TIKU_TRK_RECORD_POSE) {
        if (capacity < sizeof pose ||
            native_number(value, "version", 1, 2, &version) ||
            native_number(value, "x", -TIKU_TRK_SANE_LOCATION,
                          TIKU_TRK_SANE_LOCATION, &x) ||
            native_number(value, "y", -TIKU_TRK_SANE_LOCATION,
                          TIKU_TRK_SANE_LOCATION, &y) ||
            native_number(value, "flags", 0, POSE_INFO_INVISIBLE, &flags) ||
            (version == 1 && flags))
            return -1;
        memset(&pose, 0, sizeof pose);
        pose.x = (int32_t)x;
        pose.y = (int32_t)y;
        pose.flags = (uint32_t)flags;
        *length = version == 1 ? sizeof(pose_info_v1_t) : sizeof pose;
        memcpy(out, &pose, *length);
    } else {
        int dock = kind == TIKU_TRK_RECORD_DOCK;
        if (capacity < sizeof position ||
            native_number(value, dock ? "dock" : "x", dock ? 0 : INT32_MIN,
                          dock ? TIKU_TRK_DB_DOCKS - 1 : INT32_MAX, &x) ||
            native_number(value, dock ? "width" : "y",
                          dock ? TIKU_TRK_DB_MIN_W : INT32_MIN,
                          dock ? TIKU_TRK_DB_MAX_W : INT32_MAX, &y))
            return -1;
        position[0] = (int32_t)x;
        position[1] = (int32_t)y;
        memcpy(out, position, sizeof position);
        *length = sizeof position;
    }
    return 0;
}

/** @brief Encode a contiguous set of native view fields, preserving legacy
 * omissions.
 */
static int native_view_encode(const tiku_msg_t *value, char *out,
                              size_t capacity, size_t *length)
{
    char mode[5];
    int64_t version, number;
    int count = tiku_msg_count(value, "member"), i, size;
    if (count < 2 || count > 12 ||
        native_number(value, "version", 1, 1, &version) ||
        native_string(value, "mode", mode, sizeof mode) ||
        (strcmp(mode, "icon") && strcmp(mode, "mini") && strcmp(mode, "list")))
        return -1;
    size = snprintf(out, capacity, "%s", mode);
    if (size < 0 || (size_t)size >= capacity)
        return -1;
    *length = (size_t)size;
    for (i = 0; i < count-2; i++) {
        if (native_number(value, view_keys[i], INT32_MIN, INT32_MAX,
                          &number) ||
            !native_view_range(i, number))
            return -1;
        size = snprintf(out+*length,capacity-*length," %lld",(long long)number);
        if (size < 0 || (size_t)size >= capacity-*length) return -1;
        *length += (size_t)size;
    }
    return 0;
}

/** @brief Encode bounded column rows using their native structure and
 * version.
 */
static int native_columns_encode(const tiku_msg_t *value, void *out,
                                 size_t capacity, size_t *length)
{
    column_rec_t records[TIKU_TRK_COLUMNS_MAX];
    int count = tiku_msg_count(value, "item"), i;
    if (count > TIKU_TRK_COLUMNS_MAX ||
        capacity < (size_t)count * sizeof records[0])
        return -1;
    memset(records,0,sizeof records);
    for (i = 0; i < count; i++) {
        tiku_msg_t *item = tiku_msg_find_message(value, "item", i);
        int64_t version, field, offset, width, alignment;
        int bad;
        bad = native_number(item, "version", COLUMN_REC_VERSION,
                            COLUMN_REC_VERSION, &version) ||
              native_number(item, "field", 0, INT32_MAX, &field) ||
              native_number(item, "offset", 0, 10000, &offset) ||
              native_number(item, "width", 0, 10000, &width) ||
              native_number(item, "alignment", 0, TIKU_TRK_ALIGN_CENTER,
                            &alignment) ||
              native_string(item, "attribute", records[i].attr,
                            sizeof records[i].attr) ||
              tiku_msg_count(item, "member") != 6;
        tiku_msg_free(item); if (bad) return -1;
        records[i].key = COLUMN_REC_KEY;
        records[i].version = (int32_t)version;
        records[i].field = (int32_t)field;
        records[i].offset = (int32_t)offset;
        records[i].width = (int32_t)width;
        records[i].alignment = (int32_t)alignment;
        if (!native_column_valid(&records[i])) return -1;
    }
    *length = (size_t)count * sizeof records[0];
    if (*length)
        memcpy(out, records, *length);
    return 0;
}

/** @brief Encode session or tray rows without injecting additional native
 * lines.
 */
static int native_lines_encode(tiku_trk_native_record_kind_t kind,
                               const tiku_msg_t *value, char *out,
                               size_t capacity, size_t *length)
{
    int count = tiku_msg_count(value,"item"), i;
    if (count > (kind == TIKU_TRK_RECORD_SESSION ? TIKU_TRK_SESSION_MAX
                                                 : TIKU_TRK_DB_TRAY_MAX))
        return -1;
    *length = 0;
    for (i = 0; i < count; i++) {
        tiku_msg_t *item = tiku_msg_find_message(value, "item", i);
        char path[TIKU_PATH_MAX], label[16];
        int size, bad;
        bad = native_string(item, "path", path, sizeof path) || *path != '/' ||
              strpbrk(path, "\r\n\t");
        if (kind == TIKU_TRK_RECORD_SESSION) {
            tiku_msg_t *flag = trk_value_find(item, "minimized");
            const char *type = tiku_msg_find_string(flag, "type", 0);
            int minimized;
            bad |= !type || strcmp(type, "boolean") ||
                   !tiku_msg_find_bool(flag, "value", 0, &minimized) ||
                   tiku_msg_count(item, "member") != 2;
            tiku_msg_free(flag);
            size = bad ? -1
                       : snprintf(out + *length, capacity - *length, "%c %s\n",
                                  minimized ? 'M' : 'O', path);
        } else {
            int64_t height;
            bad |= native_string(item, "label", label, sizeof label) ||
                   !*label || strpbrk(label, "\r\n\t") ||
                   native_number(item, "height", 1,
                                 TIKU_TRK_DB_MAX_REPLICANT_H, &height) ||
                   tiku_msg_count(item, "member") != 3;
            size = bad ? -1
                       : snprintf(out + *length, capacity - *length,
                                  "%s\t%s\t%lld\n", label, path,
                                  (long long)height);
        }
        tiku_msg_free(item);
        if (size < 0 || (size_t)size >= capacity - *length)
            return -1;
        *length += (size_t)size;
    }
    return 0;
}

int tiku_trk_native_record_encode(tiku_trk_native_record_kind_t kind,
                                  const tiku_msg_t *value, void *out,
                                  size_t capacity, size_t *length)
{
    const char *type; size_t written = 0; int error; tiku_msg_t *decoded;
    if (!out || !length || tiku_resource_value_check(value) ||
        kind == TIKU_TRK_RECORD_ORIGINAL)
        return -1;
    type = tiku_msg_find_string(value,"type",0);
    if (strcmp(type, kind == TIKU_TRK_RECORD_COLUMNS ||
                             kind == TIKU_TRK_RECORD_SESSION ||
                             kind == TIKU_TRK_RECORD_REPLICANTS
                         ? "array"
                         : "object"))
        return -1;
    if ((!strcmp(type, "array") && !tiku_msg_count(value, "item")) ||
        (!strcmp(type, "object") && !tiku_msg_count(value, "member"))) {
        *length = 0;
        return 0;
    }
    if (kind == TIKU_TRK_RECORD_COLUMNS)
        error = native_columns_encode(value, out, capacity, &written);
    else if (kind == TIKU_TRK_RECORD_VIEW)
        error = native_view_encode(value, out, capacity, &written);
    else if (kind == TIKU_TRK_RECORD_SESSION ||
             kind == TIKU_TRK_RECORD_REPLICANTS)
        error = native_lines_encode(kind, value, out, capacity, &written);
    else error = native_struct(kind,value,out,capacity,&written);
    if (error) return -1;
    decoded = tiku_trk_native_record_decode(kind,out,written);
    error = decoded && tiku_resource_value_equal(decoded,value) ? 0 : -1;
    tiku_msg_free(decoded); if (!error) *length = written; return error;
}
