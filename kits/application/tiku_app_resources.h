/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_app_resources.h - versioned owner callbacks for native application resources.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_APP_RESOURCES_H_
#define TIKU_APP_RESOURCES_H_
#include "tiku_app.h"
#include "tiku_resource_provider_client.h"
/** @brief Native application resource assignment handshake. */
#define TIKU_APP_RESOURCE_HELLO UINT32_C(0x41565231)
/** @brief Optional module symbol containing its owner callback table. */
#define TIKU_APP_RESOURCE_EXPORT "tiku_app_resources_v1"
/** @brief Current model callback contract version. */
#define TIKU_APP_RESOURCE_VERSION 1u
/** @brief Owner-thread resource transport; application models stay module-owned. */
typedef struct tiku_app_resources tiku_app_resources_t;
/** @brief Versioned module extension; callbacks receive the application's live state.
 *  @note Catalog paths are relative and borrowed until the next callback. Capture
 *        returns owned typed snapshots or owned complete stream bytes. Change
 *        rechecks model identities and permissions before using its native controller.
 *        No callback runs after resources are freed, before application stop. */
typedef struct {
    uint32_t version, size;
    void (*retire)(void *state);
    uint64_t (*catalog_revision)(void *state);
    int (*catalog)(void *state, tiku_app_resources_t *, size_t index,
                   tiku_resource_initial_t *out);
    tiku_resource_error_t (*capture)(void *state, tiku_app_resources_t *,
                                     const char *path,
                                     tiku_resource_snapshot_t *out);
    tiku_resource_error_t (*stream)(void *state, tiku_app_resources_t *,
                                    const char *path, void **bytes,
                                    size_t *size);
    tiku_resource_error_t (*change)(void *state, tiku_app_resources_t *,
                                    const char *path, const tiku_msg_t *value,
                                    int invoke, tiku_resource_snapshot_t *out,
                                    tiku_resource_effect_t *effect);
} tiku_app_resource_export_t;
/** @brief Optional validated extension retained with a loaded module, or NULL. */
const tiku_app_resource_export_t *
tiku_app_resource_bindings(const tiku_app_descriptor_t *);
/** @brief Create the native runner's resource owner; NULL disables resource transport. */
tiku_app_resources_t *
tiku_app_resources_new(const tiku_app_services_t *,
                       const tiku_app_descriptor_t *,
                       const tiku_app_resource_export_t *);
/** @brief Attach start()'s model before receiving provider controller input. */
void tiku_app_resources_model(tiku_app_resources_t *, void *state);
/** @brief Consume one native assignment or correlated resource message; zero for other messages. */
int tiku_app_resources_message(tiku_app_resources_t *, const tiku_msg_t *);
/** @brief Send bounded owner requests and changed captures on the application thread. */
void tiku_app_resources_poll(tiku_app_resources_t *, int64_t now_us);
/** @brief Request native exit after the current controller result is acknowledged. */
void tiku_app_resources_request_exit(tiku_app_resources_t *);
/** @brief Whether native window closure must wait for the controller result. */
int tiku_app_resources_hold_exit(const tiku_app_resources_t *);
/** @brief Whether an accepted native exit has a reply, transport failure or expired wait. */
int tiku_app_resources_finished(const tiku_app_resources_t *);
/** @brief Retire transport state before the application's model is destroyed. */
void tiku_app_resources_free(tiku_app_resources_t *);
/** @brief Observe a native runner window; closed removes its retained metadata. */
void tiku_app_resources_window(tiku_app_resources_t *, uint32_t,
                               const char *title, int width, int height,
                               int closed);
/** @brief Copy native menu entries while their borrowed source remains valid. */
void tiku_app_resources_menus(tiku_app_resources_t *, uint32_t,
                              const tiku_menuset_t *);
/** @brief Capture complete recorded control facts from a native frame. */
void tiku_app_resources_controls(tiku_app_resources_t *, uint32_t,
                                 const struct tiku_dl *);
/** @brief Format a relative catalog path into owner-borrowed scratch storage. */
const char *tiku_app_resources_path(tiku_app_resources_t *, const char *format,
                                    ...);
/** @brief Stable owner-local document ID; borrowed until the next document-ID call.
 *  @note Domain is nonempty and at most 31 bytes. Native identities are nonzero. */
const char *tiku_app_resources_document_named(tiku_app_resources_t *,
                                              const char *domain,
                                              uint64_t identity);
/** @brief Stable opaque document ID for an owner-created nonzero lifetime identity. */
const char *tiku_app_resources_document(tiku_app_resources_t *,
                                        uint64_t identity);
/** @brief Build an owned typed object or array. */
tiku_msg_t *tiku_app_resource_object(int array);
/** @brief Append an owned value and release its temporary hold; zero on success. */
int tiku_app_resource_member(tiku_msg_t *, const char *key, tiku_msg_t *value);
/** @brief Append an owned array value and release its temporary hold. */
int tiku_app_resource_item(tiku_msg_t *, tiku_msg_t *value);
/** @brief Find an owned object member value; NULL when absent. */
tiku_msg_t *tiku_app_resource_find(const tiku_msg_t *, const char *key);
/** @brief Build three independent holds for a complete scalar/object observation. */
int tiku_app_resource_snapshot(tiku_resource_snapshot_t *, tiku_msg_t *owned,
                               const char *phase);
/** @brief Build initial metadata for a frozen stream whose content is not captured. */
tiku_msg_t *tiku_app_resource_stream_metadata(void);
/** @brief Require an exact object key set, rejecting unknown or repeated members. */
int tiku_app_resource_keys(const tiku_msg_t *, const char *const *,
                           size_t count);
/** @brief Copy one bounded UTF-8 text argument. */
int tiku_app_resource_text(const tiku_msg_t *, const char *key, char *out,
                           size_t capacity);
/** @brief Read one integer argument inside an inclusive native range. */
int tiku_app_resource_integer(const tiku_msg_t *, const char *key, int64_t low,
                              int64_t high, int64_t *out);
/** @brief Make a finite typed numeric value, or NULL for nonfinite input. */
tiku_msg_t *tiku_app_resource_number_value(double value);
/** @brief Read an exact boolean or integer typed scalar. */
int tiku_app_resource_scalar(const tiku_msg_t *, tiku_resource_type_t,
                             int64_t *);
/** @brief Make an unsigned decimal counter value. */
tiku_msg_t *tiku_app_resource_counter_value(uint64_t value);
/** @brief Hash a bounded typed observation for native selection preconditions. */
int tiku_app_resource_token(const tiku_msg_t *, char out[65]);
/** @brief Create a relative descriptor and consume its initial owned value. */
int tiku_app_resource_initial(tiku_resource_initial_t *, const char *,
                              tiku_resource_kind_t, tiku_resource_type_t,
                              tiku_msg_t *);
/** @brief Read a canonical counter argument. */
int tiku_app_resource_counter(const tiku_msg_t *, const char *key,
                              uint64_t *out);
#endif
