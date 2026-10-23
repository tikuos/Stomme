/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_resource.h - typed desktop resource values and request identities.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_RESOURCE_H_
#define TIKU_RESOURCE_H_

#include "tiku_msg.h"
#include <stdint.h>

/** @brief Message code for the local resource protocol. */
#define TIKU_RESOURCE_MESSAGE_CODE UINT32_C(0x53564331)
/** @brief Discovery, descriptions and snapshot reads. */
#define TIKU_RESOURCE_FEATURE_READ UINT64_C(1)
/** @brief Typed setting and action requests. */
#define TIKU_RESOURCE_FEATURE_CHANGE UINT64_C(2)
/** @brief Snapshot subscriptions and change events. */
#define TIKU_RESOURCE_FEATURE_WATCH UINT64_C(4)
/** @brief Async job status, progress and cancellation. */
#define TIKU_RESOURCE_FEATURE_JOBS UINT64_C(8)
/** @brief Assigned application provider publication and controller messages. */
#define TIKU_RESOURCE_FEATURE_PROVIDER UINT64_C(16)
/** @brief Provider-scoped atomic setting groups. */
#define TIKU_RESOURCE_FEATURE_TRANSACTION UINT64_C(32)
/** @brief Bounded collection catalogs. */
#define TIKU_RESOURCE_FEATURE_COLLECTION UINT64_C(64)
/** @brief Frozen, scoped binary read leases. */
#define TIKU_RESOURCE_FEATURE_STREAM UINT64_C(128)
/** @brief Supported protocol feature bits. */
#define TIKU_RESOURCE_FEATURES UINT64_C(255)
/** @brief Maximum retained bytes per stream or connection. */
#define TIKU_RESOURCE_STREAM_BYTES (4u * 1024u * 1024u)
/** @brief Maximum bytes in one stream read. */
#define TIKU_RESOURCE_STREAM_CHUNK 32768u
/** @brief Maximum settings in one atomic replacement group. */
#define TIKU_RESOURCE_TRANSACTION_MAX 16
/** @brief Maximum combined canonical path bytes, including terminators. */
#define TIKU_RESOURCE_GROUP_PATH_BYTES 4096

/** @brief Canonical backend path storage, including its terminator. */
#define TIKU_RESOURCE_PATH_MAX 481
/** @brief Hexadecimal identity storage, including its terminator. */
#define TIKU_RESOURCE_ID_MAX 33
/** @brief Provider key storage, including its terminator. */
#define TIKU_RESOURCE_KEY_MAX 128
/** @brief Maximum text value length in UTF-8 bytes. */
#define TIKU_RESOURCE_TEXT_MAX 4096
/** @brief Maximum encoded payload length in bytes. */
#define TIKU_RESOURCE_FRAME_MAX 65536
/** @brief Maximum members or items within one value. */
#define TIKU_RESOURCE_MEMBERS_MAX 128
/** @brief Maximum nested message depth, including envelopes. */
#define TIKU_RESOURCE_DEPTH_MAX 8

/** @brief Resource operation results; zero denotes success. */
typedef enum {
    TIKU_RESOURCE_OK = 0,
    TIKU_RESOURCE_INVALID,
    TIKU_RESOURCE_VALUE,
    TIKU_RESOURCE_LIMIT,
    TIKU_RESOURCE_DENIED,
    TIKU_RESOURCE_NOT_FOUND,
    TIKU_RESOURCE_READ_ONLY,
    TIKU_RESOURCE_STALE,
    TIKU_RESOURCE_OWNER_LOST,
    TIKU_RESOURCE_UNAVAILABLE,
    TIKU_RESOURCE_UNSUPPORTED,
    TIKU_RESOURCE_BUSY,
    TIKU_RESOURCE_NO_MEMORY,
    TIKU_RESOURCE_ID_REUSED,
    TIKU_RESOURCE_UNKNOWN_OUTCOME,
    TIKU_RESOURCE_PERSISTENCE_FAILED,
    TIKU_RESOURCE_APPLY_FAILED,
    TIKU_RESOURCE_RESTART_REQUIRED,
    TIKU_RESOURCE_CONFIRMATION_REQUIRED,
    TIKU_RESOURCE_PENDING,
    TIKU_RESOURCE_DEADLINE,
    TIKU_RESOURCE_CANCELLED,
    TIKU_RESOURCE_OUTCOME_UNAVAILABLE,
    TIKU_RESOURCE_STALE_CURSOR,
    TIKU_RESOURCE_ATOMIC_UNSUPPORTED
} tiku_resource_error_t;

/** @brief Public kinds; actions require invocation rather than replacement. */
typedef enum {
    TIKU_RESOURCE_DIRECTORY,
    TIKU_RESOURCE_SETTING,
    TIKU_RESOURCE_STATE,
    TIKU_RESOURCE_ACTION,
    TIKU_RESOURCE_COLLECTION,
    TIKU_RESOURCE_STREAM
} tiku_resource_kind_t;

/** @brief Typed values supported by the resource codec. */
typedef enum {
    TIKU_RESOURCE_NULL,
    TIKU_RESOURCE_BOOLEAN,
    TIKU_RESOURCE_INTEGER,
    TIKU_RESOURCE_COUNTER,
    TIKU_RESOURCE_NUMBER,
    TIKU_RESOURCE_TEXT,
    TIKU_RESOURCE_ENUM,
    TIKU_RESOURCE_COLOR,
    TIKU_RESOURCE_OBJECT,
    TIKU_RESOURCE_ARRAY
} tiku_resource_type_t;

/** @brief Authority granted by trusted host policy, not a request payload. */
/** @brief Scope for protected resource reads. */
#define TIKU_RESOURCE_READ       UINT64_C(1)
/** @brief Scope for replacing editable settings. */
#define TIKU_RESOURCE_WRITE      UINT64_C(2)
/** @brief Scope for invoking public actions. */
#define TIKU_RESOURCE_INVOKE     UINT64_C(4)
/** @brief Additional scope for sensitive content. */
#define TIKU_RESOURCE_CONTENT    UINT64_C(8)
/** @brief Authority to access permitted resources of another app. */
#define TIKU_RESOURCE_CROSS_APP  UINT64_C(16)
/** @brief Additional authority for host-owned changes. */
#define TIKU_RESOURCE_HOST_CONTROL UINT64_C(32)
/** @brief Authority to request an assigned application provider connection. */
#define TIKU_RESOURCE_PROVIDER UINT64_C(64)

/** @brief Caller context supplied by the host's authenticated connection. */
typedef struct {
    char principal[TIKU_RESOURCE_ID_MAX];
    uint64_t scopes;
    int authenticated;
} tiku_resource_caller_t;

/** @brief Provider identity and revision, compared within one incarnation. */
typedef struct {
    char provider[TIKU_RESOURCE_KEY_MAX];
    char incarnation[TIKU_RESOURCE_ID_MAX];
    uint64_t revision;
} tiku_resource_stamp_t;

/** @brief Scalar bounds and choices copied during provider registration.
 *  @note Composite members are validated by the owning controller. */
typedef struct {
    tiku_resource_type_t type;
    int64_t integer_min, integer_max;
    double number_min, number_max;
    size_t text_max;
    const char *const *choices;
    size_t choice_count;
} tiku_resource_schema_t;

/** @brief Public descriptor copied during registration. */
typedef struct {
    const char *path;
    tiku_resource_kind_t kind;
    unsigned schema_version;
    tiku_resource_schema_t schema;
    uint64_t read_scopes, change_scopes;
    int public_read, sensitive;
    const char *persistence;
    const char *controller;
} tiku_resource_spec_t;

/** @brief Snapshot values owned by their recipient; release with snapshot_clear. */
typedef struct {
    tiku_resource_stamp_t stamp;
    tiku_msg_t *desired, *persisted, *applied;
    char apply_state[32];
} tiku_resource_snapshot_t;

/** @brief A provider's initial descriptor and snapshot, copied on registration. */
typedef struct {
    tiku_resource_spec_t spec;
    tiku_resource_snapshot_t initial;
} tiku_resource_initial_t;

/** @brief Effects known by the controller when reporting an outcome. */
typedef enum {
    TIKU_RESOURCE_EFFECT_NONE,
    TIKU_RESOURCE_EFFECT_PARTIAL,
    TIKU_RESOURCE_EFFECT_UNKNOWN
} tiku_resource_effect_t;

/** @brief Release owned snapshot values and zero its fields. */
void tiku_resource_snapshot_clear(tiku_resource_snapshot_t *snapshot);

/** @brief A borrowed complete setting replacement and requested persistence scope. */
typedef struct {
    const char *path;
    const tiku_msg_t *value;
    const char *durability;
} tiku_resource_replacement_t;

/** @brief Validate complete unique paths, typed values and persistence scopes. */
tiku_resource_error_t tiku_resource_replacements_check(const tiku_resource_replacement_t *, size_t count);
/** @brief Encode a bounded replacement list; the caller frees its message. */
tiku_msg_t *tiku_resource_replacements_message(const tiku_resource_replacement_t *, size_t count);
/** @brief Decode owned list inputs into a zeroed array of TRANSACTION_MAX slots. */
tiku_resource_error_t tiku_resource_replacements_decode(const tiku_msg_t *,
    tiku_resource_replacement_t *, size_t *count);
/** @brief Release entries allocated by replacements_decode. */
void tiku_resource_replacements_clear(tiku_resource_replacement_t *, size_t count);

/** @brief Stable error code suitable for protocol replies. */
const char *tiku_resource_error_name(tiku_resource_error_t error);
/** @brief Stable value type key, or NULL for an invalid type. */
const char *tiku_resource_type_name(tiku_resource_type_t type);
/** @brief Whether a path is canonical ASCII, absolute and within bounds. */
int tiku_resource_path_valid(const char *path);
/** @brief Whether an identity has 32 lowercase hexadecimal digits. */
int tiku_resource_id_valid(const char *id);
/** @brief Generate an identity from host entropy; zero on success. */
int tiku_resource_new_id(char out[TIKU_RESOURCE_ID_MAX]);
/** @brief Parse canonical unsigned decimal text; zero on success. */
int tiku_resource_counter_parse(const char *text, uint64_t *out);
/** @brief Whether text is valid UTF-8 without embedded NUL, within max bytes. */
int tiku_resource_utf8_valid(const char *text, size_t max);

/** @brief Validate a supported client envelope against negotiated features. */
tiku_resource_error_t tiku_resource_request_check(const tiku_msg_t *request,
                                                  uint64_t features);

/** @brief Preflight a flattened message's bounds, names, types and nesting. */
tiku_resource_error_t tiku_resource_message_check(const void *bytes, size_t size);
/** @brief Check a flattened message with reserved outer message levels. */
tiku_resource_error_t tiku_resource_message_check_nested(const void *bytes,
    size_t size, unsigned ancestors);
/** @brief Decode a preflighted message; output is NULL on failure. */
tiku_resource_error_t tiku_resource_message_decode(const void *bytes, size_t size,
                                                   tiku_msg_t **out);

/** @brief Validate one complete typed value, including bounded nested values. */
tiku_resource_error_t tiku_resource_value_check(const tiku_msg_t *value);
/** @brief Create a scalar string value; caller frees the result. */
tiku_msg_t *tiku_resource_value_string(tiku_resource_type_t type,
                                     const char *value);
/** @brief Create a boolean, integer or color value; caller frees the result. */
tiku_msg_t *tiku_resource_value_integer(tiku_resource_type_t type, int64_t value);
/** @brief Render canonical scalar text or diagnostic JSON, followed by newline.
 *  @return Bytes written, or -1 without a truncated successful result. */
int tiku_resource_value_render(const tiku_msg_t *value, void *out, size_t max);
/** @brief Compare typed values ignoring object/field order; invalids differ. */
int tiku_resource_value_equal(const tiku_msg_t *a, const tiku_msg_t *b);

#endif
