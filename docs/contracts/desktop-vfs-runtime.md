# Desktop resource runtime APIs

Support Kit provides `tiku_resource.h`, Storage Kit provides
`tiku_resource_registry.h`, `tiku_resource_backend.h`, `tiku_resource_jobs.h` and
`tiku_resource_service.h`, and Application Kit provides `tiku_resource_channel.h`.
They expose resource primitives, a connection broker, asynchronous controller
jobs, collections, frozen streams and a negotiated local channel. The shared
desktop composition starts and mounts the service when explicitly configured.
The [service specification](desktop-vfs.md) defines the intended namespace;
the [protocol specification](desktop-vfs-protocol.md) includes operations beyond
the APIs described here.

## Values and descriptors

Values are owned `tiku_msg_t` messages with a stable `type` key. Scalar payloads
use `value`; object members use repeated messages with `key` and a typed `value`;
array items are repeated typed messages. The scalar constructors return owned
messages, and `tiku_resource_value_check` validates complete values. It rejects
invalid UTF-8, embedded string NUL, duplicate object keys, nonfinite numbers,
noncanonical unsigned counters, unknown fields and exceeded bounds.

`tiku_resource_message_check` checks flattened message structure before decoding.
It bounds field/value counts, nesting and frame size and rejects duplicate field
names, noncanonical booleans and trailing bytes. `tiku_resource_message_decode`
allocates codec objects after that preflight. Resource values and the local
channel use these checks without changing the general `tiku_msg` codec.

Registration copies descriptor strings, choices and initial snapshots. A scalar
schema describes its type, integer/number bounds, text length and enum choices.
Composite changes also require the controller's validation callback. Controllers
retain responsibility for member schemas, persistence, undo, confirmation and
application-specific rules. Registered kinds are directory, setting, state,
action, collection and stream. Controllers can register synchronous or
asynchronous changes. Collections and streams have owner-thread publication APIs.

## Registry ownership and mutation

Create, operate and destroy a registry on its creating thread. Callbacks run on
that thread without a registry mutex. A callback cannot register, remove, publish
or dispatch another mutation reentrantly; these operations return `busy`.
Read operations return copies and do not call a provider. Close backend adapters
and brokers before destroying their registry. A live broker prevents registry
destruction. Host clock injection precedes providers, brokers and subscriptions;
the supplied clock returns nonzero monotonic milliseconds without registry calls.

`tiku_resource_register` atomically installs a nonoverlapping provider subtree,
with copied descriptors and snapshots. Parents are implicit directories and
consume registry node capacity. Failed registration removes its staged nodes.
`tiku_resource_unregister` retires that incarnation, queues removal notifications
and invalidates its value paths. Backend node IDs are not reused during a
registry lifetime. A replacement provider receives another incarnation and
object IDs, even if its paths and provider key match the retired provider.

`tiku_resource_read`, `describe`, `entry` and `list` filter for the supplied caller.
An unauthenticated context can read resources explicitly marked `public_read`.
Sensitive content requires the content scope and, for another application's
content, cross-application authority. Implicit parent directories are hidden
when they contain no visible resource. `list` returns bounded pages at a catalog
epoch; changing that epoch invalidates an unfinished enumeration.

`tiku_resource_change` requires an authenticated principal, required scopes,
provider incarnation/revision and request ID. Observed state cannot be replaced.
It validates before calling the controller, checks the expected revision and
retains the synchronous result for replay. An identical retained request returns
its outcome; reusing that ID with another payload returns `request_id_reused`.
Retention is 60 seconds within a bounded cache; capacity eviction can occur
earlier. An expired or evicted action must not be retried without reconciliation.

The controller transfers independently owned desired, persisted and applied
values to its result. A failed callback without a snapshot asserts no effects.
It returns `unknown_outcome` for uncertain effects. Invalid completion data or an
uncertain outcome makes the resource unavailable until its owner publishes a
valid snapshot. A save failure can publish differing persisted/applied values
with `persistence_failed`; it is not reported as durable success.

`tiku_resource_publish` publishes a controller's complete snapshot against the
current stamp. Changed snapshots increment provider revision and catalog epoch;
unchanged setting snapshots retain their revision. Accepted action completions
advance revision even when their returned values compare equal. Reads supply
the current provider stamp for related settings. This API does not save files or
detect external edits; the existing controller owns those operations.

Descriptor `read_scopes` and `change_scopes` are per-resource requirements.
Changing a setting also requires `TIKU_RESOURCE_WRITE`; invoking an action
requires `TIKU_RESOURCE_INVOKE`. An app-owned provider checks its principal and
requires cross-app authority for another principal. A host-owned provider also
requires `TIKU_RESOURCE_HOST_CONTROL` for changes. Granting an app permission to
edit its own settings does not grant permission to change host-owned settings.
Sensitive operations add `TIKU_RESOURCE_CONTENT` and the descriptor's scopes.

## Observation and backend adapter

`tiku_resource_watch_open` returns a filtered provider snapshot and activates its
queue in one owner-thread operation. Events contain paths, incarnations,
revisions and subscription sequences. Overflow clears incremental data, emits an
overflow marker and pauses that watch. Obtain another snapshot/watch to resume.
Removal events drain before a retired watch becomes invalid. Closing a watch
releases its queue. Host connection cleanup closes its owned subscriptions.

The default capacities are 16 providers, 256 nodes including implicit parents,
32 watches, 16 events per watch and 16 retained mutation outcomes. Configurable
maxima are 256 providers, 4,096 nodes, 256 watches, 256 events per watch and 16
outcomes. A principal has at most 32 watches and 256 queued events. Snapshots
and messages have a 65,536-byte encoded bound; values have 128 members, nesting
depth 8 and text length 4,096 bytes. Registered snapshots reserve three outer
message levels for job/error envelopes; their encoded values have depth at most
5. Excess depth is rejected during registration and setting validation before
controller dispatch. Watch snapshots exceeding their page/message bound fail
before a subscription is installed. Value comparison ignores object-member and
scalar-field order while preserving array order.

`tiku_resource_backend_open` binds a copied read context. It implements visible
stat/list/read and identity lookup through the current `tiku_backend_ops_t`
layout. Scalars use canonical UTF-8; composites use diagnostic JSON. Action files
return an identity descriptor without invoking the action; the typed describe
API supplies its schema. A changed catalog reports an incomplete change result
so the existing file view relists it. Context-free writes return
`requires_context` with no controller call. No filesystem permissions, mkdir,
rename or action execution are enabled by the adapter. Close/recreate a view
when its caller authority changes.

## Connection broker

`tiku_resource_service_new` binds a registry, authenticated principal, scope mask
and selected protocol features. The host creates it after negotiation using
`tiku_resource_channel_features`, rather than assuming its offered features were
selected. `service_dispatch` checks that immutable grant, session, complete frame,
request schema and feature mask before translating list, describe, read, set,
invoke, subscribe, unsubscribe, job-status, cancel and request-status operations.
A registered resource can describe the features required for its changes.
Async changes require both CHANGE and JOBS; they are rejected before acceptance
when the connection did not select JOBS.

The registry holds at most 64 broker connections. Each broker owns 16 cursor
leases and 32 subscription mappings. Cursors bind exact paths, catalog epochs,
connection identity and a 30-second monotonic expiry. A changed epoch or another
connection's token returns `stale_cursor`. Consumed leases can be retired when
capacity is needed; unread leases are not overwritten. Pagination also respects
encoded byte size, so a page can contain fewer entries than its requested count.
An exhausted unread-lease table returns busy. Describing root reports effective
frame, depth, node, page, connection, cursor, watch, receipt and job bounds.

Subscription IDs are random public tokens mapped to native handles inside that
connection. One connection cannot consume or unsubscribe another's watch, even
when their principals match. `service_event` returns full session/provider stamps,
sequence, kind and typed affected paths. A negative event result requires the
host to retire the connection; its incremental cache cannot be resumed.
`service_free` releases cursor/watch state and cancels queued requests belonging
to that connection. Running work retains its controller lifecycle.

Changes use the existing controller's declared persistence scope. A request for
another durability scope returns unsupported; it does not silently write a user
preference as a volatile update. Retained IDs include expected tuples, typed
values and protocol deadline/durability. Reordered equivalent values replay the
same result. Changed content under a retained ID returns `request_id_reused`.
Cancellation acknowledgements also retain their request identity. A retry can
return its original acceptance after the job revision changes, without another
controller cancellation call. Retained snapshots and acknowledgements recheck
read/content scopes when a principal reconnects.

Replies have one correlated result or structured error and pass complete-frame
preflight. A synchronous controller which completes after its request deadline
returns `unknown_outcome`; `request-status` can reconcile its retained result
without repeating the mutation. The host's resource controllers still own actual
preference saves and GUI updates.

## Asynchronous controller jobs

`tiku_resource_register_async` registers copied descriptors and an asynchronous
controller. `job_submit` checks kind, authority, revision, schema, capacity and
controller validation before retaining a queued request. Pending request identities
are not evicted to admit another mutation. Active capacity is at most 16 jobs
and is further limited by available receipt slots. At most 16 completed jobs are
retained for 60 seconds, with earlier capacity eviction reported as unavailable.

`jobs_pump` runs on the registry thread. It rechecks lifetime, revision and owner
validation before invoking a queued start callback. A running job reserves its
provider against another queued start. The callback copies the job ID, path and
value before returning pending with empty result values. It must not block.
Workers retain their own input/lifetime and marshal completion to the registry
thread. Progress is 0 through 1000, with a controller-owned confirmation-wait
state; successful completion reports 1000. The protocol has no approval override.

Job status contains a job-controller stamp and a separate resource stamp.
Cancellation uses the current job-controller stamp, whose provider key is `jobs`.
Queued cancellation prevents dispatch. Running cancellation is an accepted
request to the controller, not a claim that effects were undone; the controller
must complete the job. Its hook is not repeated after acceptance. Terminal jobs
report too late. Status/results belong to their principal and retained scopes,
so a reconnect does not restore revoked content authority.

Queued acceptance deadlines are at most 30000 ms. The controller execution
budget is at most 300000 ms from acceptance, including confirmation wait.
Expiry before dispatch is a failed request with no effects. Running expiry or
owner loss produces an unknown outcome; live resource data becomes unavailable
when its effects cannot be reconciled. A late, duplicate or retired-incarnation
completion cannot publish over a replacement provider or a newer GUI snapshot.
Owner loss retires queued work without callbacks and running work without
calling a retired owner. A controller publishes a fresh validated snapshot to
restore a resource after uncertain effects.

Completed snapshots retain separate desired, persisted and applied values.
The job/receipt APIs do not perform preference-file I/O or supply application
undo rules. Provider workers and lifecycle adapters supply those operations.

## Local transport

`tiku_resource_channel_client` and `server` duplicate connected Unix stream
descriptors and retain their creating-thread affinity. The caller retains its
original descriptor. They reject other socket families/types. The host supplies
the stream through a private launch or session channel; these functions do not
create a named listener or change filesystem permissions.

The server obtains peer credentials from the OS and requires its effective UID.
A host authorization callback binds launch/interactive policy to the principal
and scopes. It must not infer application authority from a claimed name, PID or
SDK identity. Linux credentials identify the peer at socket creation; passing a
descriptor does not identify each later writer. The host must control endpoint
distribution. The API does not sandbox other processes of the same user.

Negotiation selects version 1.0 and the intersection of offered/requested
features. Current feature bits are read/describe/list, typed change, watch,
asynchronous job control, assigned application providers and atomic setting groups. Unknown required features fail before dispatch. After negotiation the
channel checks session incarnation, request identity, supported envelope fields,
typed arguments, bounds and selected features before its host callback runs.
Payload authority fields are rejected. The host callback translates supported
operations into registry calls and returns a correlated typed reply.

`send` and `receive` transfer complete bounded frames within a monotonic timeout.
Length validation precedes payload allocation; partial/error frames retire the
connection. `step` processes one server handshake or request. The host services
events separately on the owner thread and performs connection cleanup through
the `closed` callback. There is no background worker or implicit event loop.
Client code correlates replies and subscription events on the same stream.

## Assigned application providers

The host calls `tiku_resource_service_provider_enable` after negotiation and
launch-policy authorization. This requires feature bit 16 and the separate
provider scope bit 64. Neither host-control scope nor an offered provider feature
assigns a namespace. An enabled connection reserves `/apps/<principal>` and
provider key `app.<principal>` until it closes. Another connection for that
principal returns busy, including while the first connection is unregistered.
The registry's provider capacity bounds assignments as well as registrations.

`provider-register` contains a `resources` message with 1 through 32 repeated
`entry` messages. Entries contain `path`, `kind`, `schema_version`, `type`,
`persistence`, `desired`, `persisted`, `applied` and `apply_state`. Kinds are
setting, state and action; parent directories are implicit. Schema fields are
`integer_min`/`integer_max`, `number_min`/`number_max`, `text_max`, or repeated
`choice` strings for the corresponding type. Choice count is bounded by 128;
each choice is at most 127 UTF-8 bytes. Settings use user, session or document
persistence; state/actions may also use none. Registration copies and validates
the complete subtree atomically. Each path must descend from the assignment.
Provider-supplied access, controller, identity and namespace fields are rejected.

Descriptors are private and sensitive. Reading requires read/content scopes;
a caller from another application also needs cross-app scope. Editable settings
require write scope and actions require invoke scope. The registration reply
contains the assigned prefix, registered flag and host-created provider stamp.
`provider-status` returns the same metadata without exposing resource values.

`provider-publish` supplies a path, expected stamp and `snapshot` message with
complete desired/persisted/applied values and apply state. The connection can
publish its registered resources at the current revision. `provider-unregister`
requires the current stamp and retires the incarnation. GUI publication and job
completion use the registry's revision/event ordering. A failed or uncertain
application effect is reconciled by publishing a fresh complete snapshot.

Consumer changes enter the shared async job engine. The host pumps jobs and
consumes `service_event` on the registry thread. A `provider-dispatch` event
contains session, random dispatch identity, public job ID, expected stamp, path,
typed value, invocation flag, remaining execution milliseconds, original request
ID and origin principal/scopes. Origin scopes contain read/write/invoke/content
bits and describe the consumer request; they grant no authority to the provider.
Before returning pending, the adapter allocates and preflights the entire event.
A dispatch is delivered once. A completion requires this connection's delivered
identity and original expected stamp; knowing a public job ID is insufficient.

The application receives events on its owner thread, validates against its live
model generation, then performs or queues its controller operation. Model-specific
validation occurs in that application; host queue acceptance checks authority
and the declared schema. `provider-progress` reports 0 through 1000 plus a
boolean confirmation-wait state. Cancellation produces a `provider-cancel`
event for the same dispatch. It requests cancellation and does not claim rollback.
`provider-complete` reports a terminal code and effect (none, partial or unknown),
with a complete snapshot required on success. The job engine rejects stale,
expired, duplicate and retired-owner completions.

Provider management retains four request/outcome pairs per assigned connection
for up to 60 seconds, with earlier capacity eviction. These records are separate
from consumer mutation receipts, so pending consumer work does not prevent its
owner from completing. Named-field and typed-object order do not change request
identity; repeated entries and arrays retain order. Reusing a retained ID with
changed content returns `request_id_reused`. After an unavailable outcome,
query assignment status and current resource/job state before another mutation.
Connection loss retires the assignment; a new connection registers a new
incarnation. Owner requests from an earlier incarnation cannot affect it.

`service_free` unregisters the provider before releasing its adapter. Queued
owner work fails without callbacks; running work acquires an unknown outcome.
The host's channel `closed` callback must free its service. Owner dispatch,
progress, completion, publications and destruction have registry-thread affinity.
There are no callbacks into an adapter after service destruction.

Application Kit's `tiku_resource_provider_client` builds these requests and
checks correlated replies/events using Support definitions. It does not include
Storage or perform socket I/O. The application multiplexes events/replies through
its channel loop. The helper permits one outstanding request and retains up to
16 active dispatches and 16 acknowledged dispatch identities. Repeated retained
events are rejected before application model code runs. The application retains
an outbound request for retry and releases copied call values with `call_clear`.
It must retire worker inputs before destroying its client. Replacing a channel
requires a new client and registration.

The shared desktop shell starts the resource service when
`STOMME_RESOURCE_DIRECTORY` names its private runtime directory. Host callbacks
rebuild broker grants and bind the channel's selected limits before dispatch.
`test_resource_service.c` checks consumer IPC; `test_resource_provider.c` checks
assigned namespaces and an independent provider process. The shared desktop composition also supplies an explicitly enabled test provider. Existing application ABI 5 and backend layouts remain
unchanged; the added SDK inputs require rebuilt SDK/application identities.

## Atomic provider setting groups

`tiku_resource_register_atomic` registers a synchronous provider whose owner can
prepare and atomically commit a group of settings. The atomic controller includes
its single-setting controller plus prepare, commit and discard callbacks. Its
setting descriptions expose `atomic`, `transaction_group` and the transaction
feature bit. Synchronous and asynchronous providers without this contract return
`atomic_unsupported` before owner dispatch. Remote application registration does
not grant atomic support.

`prepare` validates the complete group against the live owner model, stages its
persistence work without effects, and returns planned desired/persisted/applied
snapshots plus an owner token. The registry checks each snapshot and reserves the
bounded response and retained result before calling commit. `commit` atomically
saves/applies the group. An ordinary failure asserts that the owner rolled back
without effects; uncertain or partial effects return `unknown_outcome`. Discard
runs once after preparation, including validation, deadline and allocation
failures. It releases the token and uncommitted staging state. These callbacks
run on the registry thread and must not block or reenter mutation.

The `transaction` request requires change feature bit 2 and transaction feature
bit 32. Its fields are `provider`, `expected`, `changes` and `deadline_ms`. The
provider must match the expected tuple. `changes` contains an int32 `count` and
that many repeated `entry` messages, each with canonical `path`, complete typed
`value` and `durability`. Durability must match the setting's declared persistence
scope. The group contains 1 through 16 unique settings from that provider and at
most 4096 path bytes including terminators. State nodes and actions are rejected.
Read visibility, write/content/cross-app/host scopes, availability, revision and
schema are checked before callbacks. The owner supplies cross-setting validation.

The response contains the provider stamp, count and repeated entries with path
and complete snapshot values. Result bytes and nesting reserve the reply/error
envelope before commit; an oversized planned result fails without effects. The
1 through 30000 ms deadline includes preparation. Expiry before commit releases
the preparation without effects. A successful commit which completes after its
deadline returns an unknown response outcome; `request-status` retains the actual
committed result for reconciliation.

Success advances the provider revision and catalog epoch once, including an
accepted group whose values compare equal. The registry installs the complete
prepared group before publishing one caller-filtered watch record. Native events
carry `affected_count` and NUL-separated paths in `affected`; `path` contains the
first visible affected path. The broker emits the corresponding typed paths array.
Prefix and content filtering apply per path; a group consumes one event queue
slot and one subscription sequence. An uncertain commit marks the requested
settings unavailable at one revision and emits one grouped invalidation. A fresh
owner publication reconciles their state. Ordinary rolled-back failures leave
registry values, revision and event queues unchanged.

Transaction IDs share the registry's bounded consumer receipt table with single
changes and jobs. Group identity includes the expected tuple, deadline, paths,
durability and semantic typed values. Path order does not change identity; a
retained equivalent request does not prepare or commit again. Changed contents
under that ID are rejected. Retained results aggregate the read/content scopes
of the whole group and survive provider retirement until receipt eviction or
expiry. The broker's request-status operation uses the same retained outcome.

`test_resource_transaction.c` covers an owner which stages a file replacement,
commits by rename, then updates its model. It exercises preparation rejection,
failed persistence, uncertain effects, deadlines, filtered grouped events, replay
and an independent authenticated local client. It does not connect a product
preference controller or assert native host filesystem durability guarantees.

## Collections and frozen streams

Collection nodes enumerate through the registry and read backend.
`collection_replace` prepares and validates a complete replacement catalog before
installing it on the registry thread. Paths must descend from the collection.
Replacement checks capacity and the expected provider stamp before effects,
replaces child identities, advances the catalog epoch/revision and emits removal
and collection-change records. Old enumeration cursors are stale. Empty catalogs
are valid. Native controllers retain ownership of their descriptors and values.

Stream owners publish copied content with `stream_publish`; each snapshot is at
most 4 MiB. Metadata includes byte size and a fresh content identity. The registry
bounds retained blobs to 16 MiB, including versions retained by readers, and
bounds active frozen handles to 64. A content lease requires read/content and
app-ownership scopes plus the current provider stamp. Reads retain one byte
generation across subsequent publications. Provider retirement or collection
replacement invalidates its handles. Current read authority is checked on each
read. Native callers close handles before destroying the registry.

The broker exposes stream-open, stream-read and stream-close under stream feature
bit 128. A connection retains at most eight leases and 4 MiB, narrowed by negotiated
limits. Lease identities belong to that connection, including across callers with
the same principal. Idle leases expire after 30 seconds. Reads carry canonical
unsigned offsets and request at most 32768 bytes. End-of-stream returns an empty
blob; offsets past the end fail. Closing an unknown or already closed valid lease
is idempotent. Disconnect releases leases. Stream operations do not write content.
Remote provider registration advertises the descriptor kinds its adapter supports;
its metadata publication operation does not transfer binary content. Native
content providers supply frozen stream snapshots.

## Peer limits and desktop endpoint

`tiku_resource_channel_offer` sets an unnegotiated connection's limit offer. Hello
exchanges complete validated limit records and selects the minimum for each
field: frame bytes, nesting, typed text, composite members, pages, transactions,
subscriptions, active jobs, provider descriptors, leases, chunks and retained
stream bytes. The supported frame range is 4096 through 65536 bytes; chunk size
reserves 1024 bytes for framing metadata. Legacy peers without limit records use
the fixed default profile. A client with a narrower offer rejects a legacy reply
which cannot establish those limits. Unknown fields, invalid ranges and a server
selection above the client offer are rejected.

Channel framing checks its configured/selected frame limit before payload
allocation. Selected depth/text/member ceilings apply to inbound and outbound
messages. The host copies selected limits into the broker before its first
request; later replacement is busy. The broker enforces page, subscription, job,
provider-registration, transaction and stream ceilings. Atomic transactions
reserve a result within the selected response limits before commit. Root
descriptions report effective limits. An event which cannot fit the selected
profile retires the connection and requires a fresh snapshot on reconnect.

Application Kit's endpoint helper binds `control.sock` in an absolute directory
owned by the desktop UID with mode 0700. The socket has mode 0600. It rejects a
final-directory symlink, wrong ownership/mode and any existing socket/file at the
endpoint. Cleanup removes the socket with its recorded inode; replacement paths
are retained. Client connection checks directory/socket ownership and permissions
and the connected server's OS UID. This endpoint does not establish a sandbox
against other processes of the same user.

The desktop composition owns its registry, endpoint, eight connection slots and
read backend on the desktop thread. A later desktop startup failure drains
filesystem work and retires the optional resource endpoint. It starts when
`STOMME_RESOURCE_DIRECTORY=/absolute/private/directory` is set, mounts at
`/.stomme`, and rejects an existing host node or mount at that prefix.
`STOMME_RESOURCE_MOUNT` selects an explicit canonical alternative which leaves
space for resource paths in the host backend path bound. A startup failure is
reported; it does not hide an existing node or silently change the prefix.

The named endpoint authorizes same-UID interactive session control: read, write,
invoke, content, cross-app and host-control scopes. It excludes provider scope
and provider-role negotiation. Applications require a separate trusted launch
connection for provider registration. The host does not infer application
authority from an SDK identity, app name or claimed PID.

The event loop accepts nonblocking descriptors, checks for one complete buffered
frame before processing, handles at most two requests per turn, and bounds each
local I/O wait to 5 ms. Incomplete/idle connections retire after 30 seconds. Jobs
and observer events run on the registry thread. Shutdown retires peers, detaches
the read mount and destroys its endpoint/backend/registry after file work drains.

`STOMME_RESOURCE_TEST_PROVIDER=1` enables synthetic settings, read-only state,
actions, failing persistence, a collection, a stream and queued work under
`/test` and `/test-work`. The test stop action requests orderly desktop shutdown.
Without this flag the composition registers product identity under `/about`;
existing product preferences are owned by their controllers.
`examples/resource_client.c` builds against relocated public SDK headers/archives
and exercises discovery, read, change, watch, stale/invalid/read-only requests,
failed persistence, collection enumeration, streaming, jobs and request lookup.

Observer queues also enforce a 1 MiB per-principal byte ceiling. Accounting
charges record storage and bounded encoding overhead for each queued event.
The first reached byte or 256-event bound produces overflow markers and pauses
incremental delivery. Root metadata reports the effective charged record count;
it can be lower than 256 for multi-path event records.
