/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_paged_storage.h - scoped enumeration and read provider contracts.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_PAGED_STORAGE_H_
#define TIKU_PAGED_STORAGE_H_
#include "tiku_allocator.h"
#include "tiku_resource_registry.h"

/** @brief Serialized session with one bounded outstanding request. */
typedef struct tiku_paged_storage tiku_paged_storage_t;
/** @brief Request operation; a provider advertises COLLECTION and STREAM features. */
typedef enum { TIKU_STORAGE_LIST, TIKU_STORAGE_READ } tiku_storage_operation_t;
/** @brief Provider-owned request view, valid until matching completion.
 * @note epoch zero starts a catalog. Nonzero epochs and IDs must be checked
 * against current media by the provider. IDs are ordered and stable within
 * an epoch; changing media or catalog membership invalidates the epoch. */
typedef struct {
    tiku_storage_operation_t operation;
    char path[TIKU_RESOURCE_PATH_MAX];
    uint64_t epoch, after, node_id, offset, token;
    tiku_resource_caller_t caller;
    tiku_resource_entry_t *entries;
    unsigned char *bytes;
    size_t capacity;
} tiku_storage_request_t;
/** @brief Bounded nonblocking adapter operations on the session's owner thread.
 * @note OK from submit retains the view until complete; other returns retain
 * nothing. cancel requests retirement but retains the view until complete.
 * Neither callback may call complete synchronously. Context outlives sessions.
 * The provider enforces copied caller scope and checks read identity/epoch. */
typedef struct {
    tiku_resource_error_t (*submit)(void *, tiku_paged_storage_t *, const tiku_storage_request_t *);
    void (*cancel)(void *, tiku_paged_storage_t *, uint64_t token);
} tiku_storage_provider_ops_t;
/** @brief Immutable binding; capabilities are READ, COLLECTION and STREAM only. */
typedef struct {
    const tiku_storage_provider_ops_t *ops;
    void *context;
    uint64_t capabilities;
} tiku_storage_provider_t;
/** @brief Create page/read storage; zero capacity disables that operation. */
tiku_paged_storage_t *tiku_paged_storage_create(tiku_allocator_t, tiku_storage_provider_t,
    const tiku_resource_caller_t *, size_t entries, size_t bytes);
/** @brief Start listing children after ID; zero epoch requires zero after. */
tiku_resource_error_t tiku_paged_storage_list(tiku_paged_storage_t *, const char *path,
    uint64_t epoch, uint64_t after);
/** @brief Read at offset using a listed identity and nonzero catalog epoch. */
tiku_resource_error_t tiku_paged_storage_read(tiku_paged_storage_t *, const char *path,
    uint64_t epoch, uint64_t node_id, uint64_t offset);
/** @brief Retire a matching view; validates count, epoch and enumeration progress.
 * @note count means entries or bytes. eof ends enumeration or reading. Error
 * results expose no output. Cancelled or unavailable requests discard results. */
int tiku_paged_storage_complete(tiku_paged_storage_t *, uint64_t token,
    tiku_resource_error_t, size_t count, uint64_t epoch, int eof);
/** @brief Cancel once, hide output and retain pending provider storage. */
void tiku_paged_storage_cancel(tiku_paged_storage_t *);
/** @brief Permanently close a removed medium with UNAVAILABLE and cancel pending work. */
void tiku_paged_storage_unavailable(tiku_paged_storage_t *);
/** @brief Cancel and close; pending completion must retire before destruction. */
void tiku_paged_storage_close(tiku_paged_storage_t *);
/** @brief Destroy a closed session after provider retirement; busy retains it. */
int tiku_paged_storage_destroy(tiku_paged_storage_t **);
/** @brief Whether the provider retains a request, including after cancellation. */
int tiku_paged_storage_pending(const tiku_paged_storage_t *);
/** @brief Current result; output views exist only after OK. */
tiku_resource_error_t tiku_paged_storage_status(const tiku_paged_storage_t *);
/** @brief Borrow completed output until the next operation/cancel/close. */
const tiku_storage_request_t *tiku_paged_storage_result(const tiku_paged_storage_t *,
    size_t *count, uint64_t *epoch, int *eof);
#endif
