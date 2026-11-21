/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_paged_storage.h - scoped enumeration, reads and checked file mutations.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_PAGED_STORAGE_H_
#define TIKU_PAGED_STORAGE_H_
#include "tiku_allocator.h"
#include "tiku_resource_registry.h"

/** @brief Serialized session with one bounded outstanding request. */
typedef struct tiku_paged_storage tiku_paged_storage_t;
/** @brief Request operation; providers advertise COLLECTION, STREAM and CHANGE features. */
typedef enum { TIKU_STORAGE_LIST, TIKU_STORAGE_READ, TIKU_STORAGE_RENAME, TIKU_STORAGE_DELETE, TIKU_STORAGE_REPLACE, TIKU_STORAGE_MKDIR, TIKU_STORAGE_TREE_RENAME, TIKU_STORAGE_TREE_DELETE, TIKU_STORAGE_DIRECTORY_UPGRADE, TIKU_STORAGE_TREE_COPY } tiku_storage_operation_t;
/** @brief Provider-owned request view, valid until matching completion.
 * @note epoch zero starts a catalog. Nonzero epochs and IDs must be checked
 * against current media by the provider. IDs are ordered and stable within
 * an epoch; changing media or catalog membership invalidates the epoch. */
typedef struct {
    tiku_storage_operation_t operation;
    char path[TIKU_RESOURCE_PATH_MAX], destination[TIKU_RESOURCE_PATH_MAX];
    uint64_t epoch, after, node_id, offset, token;
    tiku_resource_caller_t caller;
    tiku_resource_entry_t *entries;
    unsigned char *bytes;
    size_t capacity;
    const unsigned char *input;
    size_t input_bytes;
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
/** @brief Immutable binding; capabilities are READ, COLLECTION, STREAM and CHANGE. */
typedef struct {
    const tiku_storage_provider_ops_t *ops;
    void *context;
    uint64_t capabilities;
} tiku_storage_provider_t;
/** @brief Create one owned allocation with disjoint fixed request buffers.
 * Zero page/read capacity disables that operation. */
tiku_paged_storage_t *tiku_paged_storage_create(tiku_allocator_t, tiku_storage_provider_t,
    const tiku_resource_caller_t *, size_t entries, size_t bytes);
/** @brief Start listing children after ID; zero epoch requires zero after. */
tiku_resource_error_t tiku_paged_storage_list(tiku_paged_storage_t *, const char *path,
    uint64_t epoch, uint64_t after);
/** @brief Read at offset using a listed identity and nonzero catalog epoch. */
tiku_resource_error_t tiku_paged_storage_read(tiku_paged_storage_t *, const char *path,
    uint64_t epoch, uint64_t node_id, uint64_t offset);
/** @brief Rename a listed file to an unused absolute destination through a CHANGE provider.
 * @note Provider checks source path, identity and epoch, destination scope and
 * conflicts. The session copies both paths and retains its request until
 * completion. Successful mutation returns zero output and the resulting epoch.
 */
tiku_resource_error_t tiku_paged_storage_rename(tiku_paged_storage_t *,const char *path,
    uint64_t epoch,uint64_t node_id,const char *destination);
/** @brief Delete one listed file through a CHANGE provider with checked identity/epoch.
 * @note Cancellation retains provider storage and does not imply rollback.
 */
tiku_resource_error_t tiku_paged_storage_delete(tiku_paged_storage_t *,const char *path,
    uint64_t epoch,uint64_t node_id);
/** @brief Atomically create or replace file contents through a CHANGE provider.
 * @note A zero node ID requires an absent destination; a nonzero ID identifies
 * the listed file to replace at the supplied nonzero epoch. The session copies
 * the path but borrows immutable input until pending becomes false, including
 * after cancellation. NULL input requires zero bytes. Input must not alias the
 * session or its output buffers. Providers may refuse unsupported replacement
 * or their size limit. Success reports the complete input byte count and new
 * epoch, with eof set. Failure can leave publication uncertain; consumers must
 * refresh before retrying. Atomic publication exposes complete file versions.
 */
tiku_resource_error_t tiku_paged_storage_replace(tiku_paged_storage_t *,const char *path,
    uint64_t epoch,uint64_t node_id,const void *input,size_t bytes);
/** @brief Create an unused persistent directory through a CHANGE provider.
 * @note A nonzero catalog epoch is required. The provider may refuse volumes
 * without persistent directories. The path is copied through retirement. */
tiku_resource_error_t tiku_paged_storage_mkdir(tiku_paged_storage_t *,const char *,uint64_t epoch);
/** @brief Atomically rename a listed directory and its descendants to an unused path.
 * @note Provider checks the listed rank/epoch, typed or implicit root, mapped
 * names and conflicts. No merge or overwrite is implied. Paths are copied.
 */
tiku_resource_error_t tiku_paged_storage_tree_rename(tiku_paged_storage_t *,const char *,uint64_t epoch,
    uint64_t node_id,const char *destination);
/** @brief Permanently delete a listed directory and its descendants atomically.
 * @note Consumers confirm recursive intent. Provider checks identity and epoch;
 * cancellation retains the request and does not imply rollback. */
tiku_resource_error_t tiku_paged_storage_tree_delete(tiku_paged_storage_t *,const char *,uint64_t epoch,uint64_t node_id);
/** @brief Copy a listed directory to an absent destination while retaining its source.
 * @note The provider validates identity, epoch, scope, conflicts and size limits.
 * The session copies paths and borrows writable workspace through retirement,
 * including cancellation. Workspace must not alias session or output storage.
 * Success returns zero output bytes; publication is atomic across descendants. */
tiku_resource_error_t tiku_paged_storage_tree_copy(tiku_paged_storage_t *,const char *,uint64_t epoch,
    uint64_t node_id,const char *destination,void *workspace,size_t capacity);
/** @brief Publish monotonic prepared bytes for a matching pending tree-copy token.
 * @note Zero total means preflight or an empty tree. A nonzero total is fixed.
 * Prepared bytes are not visible until successful transaction completion. */
int tiku_paged_storage_set_progress(tiku_paged_storage_t *,uint64_t token,uint64_t prepared,uint64_t total);
/** @brief Read tree-copy progress while pending or after success; errors hide progress. */
int tiku_paged_storage_progress(const tiku_paged_storage_t *,uint64_t *prepared,uint64_t *total);
/** @brief Explicitly enable persistent directories on the current volume.
 * @note Requires a nonzero current epoch. The provider enforces host-control
 * authority and preserves file contents. The caller obtains informed consent:
 * older firmware cannot open the volume after completion. Cancellation retains
 * ownership and can require remount and explicit resume. No formatting occurs. */
tiku_resource_error_t tiku_paged_storage_directory_upgrade(tiku_paged_storage_t *,uint64_t epoch);
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
/** @brief Attach complete-file metadata to the current asynchronous read.
 * @note Call before completion on the owner thread. A stale token, cancelled
 * request or callback reentry is refused. Version zero means unspecified. */
int tiku_paged_storage_describe(tiku_paged_storage_t *,uint64_t token,uint64_t bytes,uint64_t version);
/** @brief Copy metadata from the last successful described read.
 * @note Pending, cancelled, failed or undescribed reads return zero. A newly
 * admitted request invalidates prior metadata. Output pointers are optional. */
int tiku_paged_storage_info(const tiku_paged_storage_t *,uint64_t *bytes,uint64_t *version);
#endif
