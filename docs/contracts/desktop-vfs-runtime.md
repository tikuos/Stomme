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
Without this flag the composition registers product identity under `/about`
and connects the shared preference controllers under `/settings`.
`examples/resource_client.c` builds against relocated public SDK headers/archives
and exercises discovery, read, change, watch, stale/invalid/read-only requests,
failed persistence, collection enumeration, streaming, jobs and request lookup.

Observer queues also enforce a 1 MiB per-principal byte ceiling. Accounting
charges record storage and bounded encoding overhead for each queued event.
The first reached byte or 256-event bound produces overflow markers and pauses
incremental delivery. Root metadata reports the effective charged record count;
it can be lower than 256 for multi-path event records.

## Shared preference controller

The desktop binds its existing TrackerSettings table to an owner-thread
`tiku_trk_settings` controller. With the resource endpoint enabled, the controller
registers provider `settings` under `/settings`. Without the endpoint, the same
controller handles GUI saves and external reloads. The table supplies defaults,
setting types and interlocks. Its text file and unknown lines retain their
existing representation.

`/settings/ui/theme` uses the enum choices `light`, `dark` and `monochrome`.
`/settings/ui/global-menu-bar` and `/settings/ui/reduce-motion` are booleans.
Other Tracker preferences are under `/settings/tracker/<lowercase-key>`.
Sound volume is `/settings/sound/volume`, with integer bounds 0 through 100.
Each sound event has `/settings/sound/<event>/enabled` and
`/settings/sound/<event>/clip`. The enabled suffix permits the event directory
and its clip setting to share a parent. TikuDesktop's CurrentDevice and
CurrentDevicePort fields are observed state, absent from KintsugiOS's table.

Setting changes require CHANGE and JOBS. They return queued job identities;
clients reconcile the final outcome with job-status or request-status.
The controller copies proposed values, saves on a worker, and applies successful
completions on the desktop thread before publishing snapshots. GUI controls
submit through the same owner. A failed save retains committed model values.
A changed file conflicts with a prepared save. The file replacement uses
cooperative content checks before creation and rename; it is not a filesystem
compare-and-swap against an unrelated writer.

The reload worker reads regular files up to 65,536 bytes and compares presence,
resolved target and complete bytes. It detects edits that retain size and
modification time, and observes file removal as a return to defaults.
Invalid reload values retain the active model. Startup theme and volume values
use their existing fallback/clamp rules, and a valid GUI save can repair an
invalid file. A symbolic settings path retains its link when its resolved target
is replaced. Workers own their input and detach from a retired controller.

`/settings/profile` is a composite setting containing the complete editable
table, keyed by its existing file names. Missing or unknown keys and inconsistent
interlocked requests are rejected before persistence. The owner validates the
complete proposal and saves one file before applying the model. Product-observed
state is excluded. Granular resources and the profile publish from that same
committed model. Each changed resource has its normal revision/event; this does
not introduce a multi-resource atomic watch event.

The preference controller does not advertise the protocol's atomic transaction
operation. GUI Defaults/Revert proposals and profile replacements are saved as
complete table copies. Deskbar and Fonts use a record owner within that same
settings provider, backed by their session store. The text-file settings retain
user durability; a record owner backed by a memory store reports session
durability.

`examples/settings_client.c` builds using the Application and Support SDK
archives. It reads a typed resource, or submits one scalar change and waits for
its asynchronous outcome:

```sh
settings-client /path/to/private-directory /settings/ui/theme
settings-client /path/to/private-directory /settings/ui/theme enum dark
```

In either product, `make check-settings-service` compiles that example against
a relocated SDK and drives an isolated desktop. It checks API changes against
rendered theme/menu state, GUI changes against the API, failed saves, same-size
and same-timestamp external edits, restart and endpoint cleanup. Linux conductor
results do not establish native macOS behavior.


## Staged session record updates

Storage Kit exports `tiku_state_update.h`. The store owner calls
`tiku_store_update_begin` and edits its copied memory store through
`tiku_store_update_model`. Existing record serializers can write to that copy.
`tiku_store_update_run` persists the isolated inputs on a worker;
`tiku_store_update_finish` reconciles the completed cache on the owner thread.
Workers do not access the live store.

The persisted baseline, candidate and current sidecar are merged by record key.
Nonoverlapping external changes and unknown attributes are retained. An
incompatible change to a locally edited record returns conflict before
replacement. The complete captured file is checked before creation and rename;
this is cooperative conflict detection. Replacement writes a private file,
syncs it, renames it and syncs its directory. Truncated, duplicate or malformed
records and files above 4 MiB or 4096 records are rejected before replacement.
The TRKSTATE1 layout is preserved.

Cached writes made after preparation survive completion and remain dirty when
they were not in the persisted snapshot. A later conflicting cached change
retains the newer value and reports an uncertain outcome. An active update
makes legacy flush return EBUSY. A detached update also requires successful
staged reconciliation before legacy flush, preventing an older cache from
replacing a completed worker write. Cache reconciliation can be retried after
NO_MEMORY without repeating persistence. Releasing a prepared operation before
its worker starts removes its reservation without a reload barrier.
Free an operation after its worker has
returned; store destruction cancels and detaches active work.

Memory stores support the copied update without claiming persistence. Extended
attribute stores return ENOTSUP from begin; their existing read/write API is
unchanged. The Deskbar/Fonts controller uses the desktop session sidecar, queues
one proposal behind a reload and validates its original model precondition
before dispatching it. It drains accepted work during normal desktop teardown.

## Deskbar and Fonts resources

The shared provider registers the 18 Deskbar fields under
`/settings/deskbar/<field>`, including three-element `recent_enabled` and
`recent_count` arrays. Numeric bounds and interlocks use the existing Deskbar
normalizer. Requests that would be silently clamped, including odd baked font
sizes, are rejected before persistence. GUI normalization retains its existing
behavior.

`/settings/ui/font/family`, `size` and `dropped` use the same record owner.
The family and size aliases under `/settings/deskbar` share their values and
notifications. `/settings/deskbar/profile` and `/settings/ui/font/profile` accept
complete objects keyed by their respective field names. Preparing the Deskbar
record and dropped-font attribute in one copied store permits one sidecar
replacement while retaining unrelated session and window state. Older Deskbar
record versions remain readable.

The owner applies successful completion through the existing Deskbar and font
APIs. A missing dropped face retains its desired and saved name, reports the
actual baked face through applied values, and marks the snapshot as fallback.
A failed save retains the previously committed GUI values. External record edits
are detected by complete content, including edits with unchanged file size and
timestamp. API writes, GUI controls, aliases and restart share these owners.

Deskbar and Fonts show pending saves and failed-write feedback through the bound
record owner. Failure retains committed controls and leaves a retry message;
background reloads retain that message. A later accepted save clears it. Busy
submissions restore committed controls and request a retry.

The record controller does not advertise the protocol's atomic transaction
operation. Its profiles have owner-scoped validation and one-file persistence;
publication still uses per-resource events. Queued jobs use the broker's
cancellation rules; running controller jobs have no cancellation hook.

## Folder window defaults

`/settings/tracker/window-defaults` is an object setting backed by the existing
session `_trk/window_default` record. A complete value contains integer `x`, `y`,
`width` and `height`; an empty object clears the default. Coordinates use native
signed 32-bit values. Width and height must be at least the native frame minima,
180 and 120. Requests reject unknown, missing or incorrectly typed fields before
persistence. A frame is persisted as one native record through the shared staged
store. Clearing writes the native zero-length unset value and retains unrelated
session records. Its proposal edits the default record; the staged store retains
other session records and pending cache changes.

The applied value is the default available to future folder openings. Existing
windows keep their frames. A folder's own saved frame takes precedence, followed
by its parent's frame with the native offset, then the default and the normal
cascade. The root takes no inherited/default frame. Native screen clamping uses
wide arithmetic for coordinate sums, including extreme stored values.

Desired and applied defaults follow the live session cache. Persisted values use
the last known saved cache. Legacy cache edits report `pending_persistence` until
saved. Complete sidecar reload observes same-size and same-timestamp edits. A
queued proposal retains its original default as a dispatch precondition. Failed
writes preserve the committed default; a conflicting later cache edit remains
visible and reports an uncertain outcome. The same record owner supplies native
save feedback and keeps failed-write feedback across background reloads.

Memory-backed settings declare `session` persistence, and sidecar-backed settings
declare `user`. Broker requests must select the declared durability. This default
is separate from current window geometry and from other session record families.
Those resources use their own owners and namespaces.

## File types and preferred handlers

The settings provider includes `/settings/associations` and
`/settings/file-types` collections. Their children use the lowercase hexadecimal
UTF-8 bytes of the MIME string as a path component. These identities retain the
same paths across label changes and application discovery order. Each
association has a `preferred` text setting and a read-only `details` object
containing its MIME identity, label and candidate application names. The empty
preferred value selects Automatic. New choices must name a related installed
application; requests cannot supply commands or module paths.

Preferred snapshots retain the configured handler in desired values and the last
known saved handler in persisted values. Cached edits report
`pending_persistence` until their store save completes.
`tiku_store_persisted_read` reads that saved cache without filesystem I/O; memory
stores report their volatile records and xattr stores return ENOTSUP.
The applied value is the first candidate the existing launcher would use.
An unavailable saved handler reports fallback and retains its saved name.
Changes use copied association drafts and staged session updates. They wait for
other store owners, recheck changed rows against their saved preconditions and
preserve unrelated records. Persistence finishes before GUI save completion and
resource success. Failed saves retain the previous live handlers; stale GUI
drafts require reopening the window. A pending save belongs to its accepted
draft, so reopening the window does not transfer its close-on-success callback.

Native file-type children are object settings. A complete record contains
`mime` (text), `description` (text), `icon` (text), `document` (boolean),
`columns` (array of native integer column IDs) and `attributes` (array of text).
The MIME must match the child's hexadecimal path. An empty object deletes a
record. Removed identities remain discoverable for the desktop session, with
empty saved values. `/settings/file-types/upsert` accepts one complete record;
`/settings/file-types/replace` accepts an array of complete records and replaces
the definitions in one native-file save. Empty replacement arrays are valid.
`/settings/file-types/status` reports readiness, errors, saved/applied counts,
file presence, restart state and record/identity limits. Action results use this
summary object. The action argument schema describes the input.

Desired and persisted definitions reflect the last valid file observation or
successful save. Applied definitions retain the startup catalog; changes report
`restart_required` until a new desktop session loads them. File Associations and
other native type consumers continue using that startup catalog. The compatibility
catalog constructor without a file path supplies read-only loaded records.

The file owner reads on workers every 500 ms and captures complete content plus
the resolved target. Same-size and same-timestamp edits are detected. Accepted
writes retain their captured precondition while waiting behind a read. Changed
bytes or symbolic-link targets refuse replacement. Successful saves preserve the
symbolic link and replace its regular-file target, sync the temporary file,
rename it and sync its directory. A directory-sync failure has unknown outcome.
Conflict detection is cooperative and does not provide a filesystem compare-and-swap.
Comments, opaque lines, unchanged rows and unknown column tokens in retained
records survive edits. Removing a record removes its unknown column tokens too.

Files are limited to 64 KiB, 40 records and native rows below 511 bytes before
the newline. The owner retains at most 80 distinct type identities per session;
exceeding the union limit requires a desktop restart. Collection membership
changes invalidate child identities and catalog cursors. Clients relist after a
catalog change. MIME, description and icon fields retain native byte limits;
column and attribute arrays hold at most six entries each. Attribute names are
limited to 18 UTF-8 bytes by the native token parser. Invalid UTF-8, embedded NUL,
control characters in editable fields, duplicate type rows, excess known columns
and malformed rows refuse editing and retain the last valid observation. Unknown
lines without tabs remain opaque. An absent file selects product defaults;
a present empty file selects no definitions. Registry publication allocation
failures are retried and block new writes while resynchronizing.

The owner serializes native-file writes without advertising protocol atomic
transactions. Its batch action persists one file and publishes per-resource
observations. Queued jobs use broker cancellation; running saves have no public
cancellation hook. Shutdown drains work for five seconds, then detaches and asks
remaining workers to stop before replacement.

Open With's make-preferred action and Get Info's Opens with control use the same
staged association owner. The chooser saves every selected type in one update,
validates the handler for each type and launches only after success. Failure
keeps the chooser open with its saved handler unchanged. Closing, retargeting or
destroying a chooser detaches its launch completion; an accepted save may still
finish. The shared owner outlives its bound chooser, which detaches before
teardown. Get Info refreshes open views after commit and after API changes.
Standalone chooser controllers without a bound owner retain their cache API.

The desktop host allocates 8192 registry nodes for settings and catalog entries.
The configurable registry limit is 16384 nodes.
The portable registry's default remains 256. Collection children retain normal
provider revisions, read authority and bounded snapshot rules. The endpoint
remains opt-in through `STOMME_RESOURCE_DIRECTORY`.


## Native Desktop preferences

`/settings/desktop/bg` is an object with `path` (text), `mode` (integer 0–3),
`x` and `y` (integers from -32768 to 32768). An empty path selects no picture.
Modes retain their native order: offset, centred, scaled and tiled. Paths must
fit the native buffer, use absolute paths when nonempty and contain no tab or
line break. The native image loader determines whether the configured file can
be displayed; `/desktop/backdrop` reports the picture actually drawn.

`/settings/desktop/icon_size` accepts the six sizes exposed by Desktop
preferences: 32, 40, 48, 64, 96 and 128. Its observation can contain a saved native
size from 16 through 128. `/settings/desktop/have_bg` observes whether a picture
path is configured. Applied fields use the native draw result; an unreadable
image retains its configured path while reporting an empty applied image path
and `fallback`. `/settings/desktop/reduce_motion` aliases
`/settings/ui/reduce-motion` through the existing table owner and settings file.

The Desktop window submits picture and icon proposals through the same owner as
resource writes. Accepted values apply after their save; failed saves restore
its accepted model and display retry feedback. Defaults and Revert can change
two native attributes and the table-owned motion preference. These are separate
persistence boundaries. A partial native save reports that some settings saved.
The icon writer preserves the view record's scroll, origins and sort fields.
Native reloads run every 500 ms on copied worker inputs and retain the last
accepted model when a read or parse fails. Saved and cached sidecar fields remain
separate, and a failed save retains its retry feedback through successful
reloads. Background edits retain unchanged native icon sizes outside the six
interactive step values.

`tiku_state_attribute.h` provides copied attribute reads and proposals. Reads
capture the resolved regular file or directory, device/inode identity, attribute
presence and complete bytes, with a 64 KiB limit. Missing and empty attributes
remain distinct. Workers open the resolved path without following a replacement
symlink, verify node identity and prior attribute bytes, then write through that
descriptor and synchronize it. A symlink used at capture time selects that
resolved target. Comparison and replacement are separate filesystem calls and
require cooperating writers. Changed preconditions return conflict; a completed
write whose synchronization fails returns uncertain. Preparation copies its
capture without I/O. The source capture can be freed afterwards.

## Live shell observations

The following resources observe current native owners:

| Paths | Representation |
| --- | --- |
| `/desktop/backdrop` | Drawn image path and dimensions, backdrop colour and current icon size |
| `/desktop/icon-layout`, `/desktop/selection` | Complete loaded poses, including filtered entries, with node identity and unscaled positions |
| `/desktop/menus` | Folder menus, current popup tree and focused window's published menu entries |
| `/deskbar/clock`, `/deskbar/dock`, `/deskbar/frame` | Displayed clock/calendar state, docking and current geometry |
| `/deskbar/replicants`, `/deskbar/tray` | Current replicant readings and application tray items |
| `/deskbar/teams` | Running application rows, published windows and process identifiers |
| `/session/identity`, `/session/state` | Session incarnation, product and process identity, running state and native window counts |
| `/session/restore-policy` | Saved restore/fallback choices and the choices used at boot |
| `/session/startup` | Saved policy followed by installed startup applications and their saved enablement |
| `/session/windows` | Current workspace windows with generation, title, geometry, focus and minimization |

Icon layout, selection, menus, replicants, tray items, teams, startup and window
lists are frozen streams of newline-delimited JSON objects. Clients describe the
resource, open a lease at that revision and read its chunks. An empty list is a
zero-byte stream. Captures are complete within 4 MiB. Failure or excess output
retains the previous complete content and reports `last_good_unavailable`;
recovery publishes a complete replacement. Paths, startup entries, window titles
and application/tray contents require content authority. Callback pointers,
window tags and launch commands are not serialized. Observers inspect their
owners every 250 ms without filesystem access. Unchanged contents retain their
stream identity. Each session leaf has a separate provider incarnation.

## Saved native record families

`/session/records/defaults`, `/session/records/deskbar-prefs` and
`/session/records/ui-font` alias the existing window-default, Deskbar profile and
font-profile setting owners. They retain those owners' codecs, persistence and
live application behavior.

The other families are collections of known native contexts:

| Family | Public value |
| --- | --- |
| `columns`, `desk-columns`, `disks-columns` | Array of version-2 column fields, offsets, widths in reference-font units, alignment and attribute name |
| `view-state`, `desk-view-state`, `disks-view-state` | Version-1 mode and present native size, scroll, origin and sort fields |
| `pose-info` | Native position version, unscaled coordinates and the invisible flag |
| `frame` | Saved folder rectangle |
| `deskbar-prefs-frame` | Saved preferences-window position |
| `dock` | Native dock enumeration and bar width |
| `replicants` | Array of label, namespace path and requested height |
| `session` | Ordered array of folder paths and minimized flags |
| `original` | Read-only original path retained by the Trash owner |

Each opaque context directory has a `node` state object with its source node and
storage kind, and a `value` setting or read-only original-path state. Contexts
come from loaded desktop/folder views and cached session-store records. They remain
cataloged until the desktop session ends. New contexts replace that family's
catalog and invalidate its child handles and outstanding list cursors. Clients
relist after a membership change and after restart. Paths remain scoped by the
resource session and provider incarnation.

The catalog retains up to 1800 contexts. `/session/records/status` reports its
count, capacity, overflow, pending families and unavailable entries. `complete`
means the catalog has published its discovered contexts within that capacity.
Unsupported, malformed or unreadable records retain their last valid value with
`last_good_unavailable`. An existing captured malformed attribute can be repaired
by a validated replacement. Other attributes in the store are preserved.
`tiku_state_cached_each` visits cached sidecar/memory records without filesystem
access; native attribute stores report ENOTSUP. Attribute reloads use copied
workers every second.

Native record settings expose saved configuration separately from current live
owners. Their applied field is an empty value of the declared shape and their
apply state is `saved_for_next_open`; live observations provide current state.
Successful writes also notify loaded pose, view, column, frame, docking and
preferences-position owners. Explicit view/column clears survive unrelated
position saves; a later view/column edit writes its changed fields. Initial
Desktop placement reserves the configured menu strip before restoring poses.
The initial merged Desktop listing uses the pose view's 4096-entry capacity;
explicit model replacement invalidates outstanding directory loads. Subsequent
native saves can normalize records through those owners' existing formats. Replicant configuration applies at the
next desktop start. A saved session replacement supersedes shutdown's automatic
folder-list capture for that desktop session. It does not close current folders.

An empty object or array clears an editable record. Column codecs check native
versions, byte order, names, unique fields, required Name column and numeric
bounds. Position, frame, docking and pose codecs enforce their native ranges.
Text codecs reject overflowing integers, unsupported field combinations,
duplicate folder/replicant entries and injected line separators. Sidecar record
writes retain their existing 4096-byte limit. Sidecar changes use staged store
updates; native attribute changes use captured node/attribute preconditions.
During dispatched record writes, the provider defers catalog and value
publication until completion so a refresh cannot invalidate its own mutation. Running record
jobs have no cancellation callback. Shutdown drains accepted work for five
seconds, then detaches worker completions from the released owners.

`/session/records/capture` saves the session owner's current restorable folder
list; `/session/records/clear` saves an empty next-start list. Both actions take
an empty object, return asynchronous jobs and preserve the current windows.
Saved folder entries contain namespace paths and minimized flags. Startup
application entries remain owned by the existing Startup preferences controller.

## Windows and the composited workspace

The desktop registry reserves 64 provider slots. Portable registry defaults
remain 16 providers and 256 resources. Each live window uses a separate provider
incarnation under `/windows/<id>`. Its opaque ID is allocated at registration;
a closed native slot, replaced application window or new connection gets a new
ID. `/workspaces/<id>` identifies the desktop's composited workspace for that
resource session. The current desktop has one workspace; its arrangement reports
`workspace_count: 1` and `switch_supported: false`.

Each window publishes object states named `identity`, `owner`, `title`,
`geometry`, `constraints`, `focus`, `visibility` and `workspace`. Identity retains
the resource session, native window generation and workspace ID. Owner identifies
an application connection and its window generation/local ID, an embedded app,
or the desktop. Native pointers, tags and callbacks are not serialized. Geometry
contains outer-frame coordinates, dimensions and native zoom state. Constraints
report the available commands alongside size controls and frame limits.

The workspace publishes `identity`, `active`, `arrangement` and `windows` object
states. `windows` maps stacking-order indexes to opaque window IDs. Arrangement
reports the registered window count and whether discovery is complete. Capture
or publication failure retains the previous complete observation with
`last_good_unavailable`. Window registration failure leaves discovery incomplete.
Observers reconcile on the desktop thread after application-session updates and
before resource requests are dispatched.

Window actions are `focus`, `maximize`, `minimize`, `move`, `request-close`,
`resize` and `restore`. They require authenticated content, invocation,
cross-application and host-control authority. Plain windows retain their native
restrictions. Hidden windows cannot be focused. Unsupported operations return
`unsupported`; a native pointer gesture in progress returns `busy`.

Move accepts exactly `{x, y}`, integers within -32768 through 32768. Resize
accepts exactly `{width, height}`, with width 180–8192 and height 90–8192. These
are outer-frame dimensions. Other actions accept an empty object. Commands pass
through native tracking, docking, content fitting and app event delivery. Move
keeps the tab reachable using the native screen constraints. Maximization uses
the folder's native content fit or the application's available desktop area.
Restore restores a minimized window and its saved pre-zoom rectangle.

Actions use queued jobs and recheck current native membership, slot generation,
connection generation and capabilities before dispatch. A changed provider
revision rejects a queued proposal. Retirement removes the provider's resources
and fails its queued jobs; outstanding handles cannot target another window.
Each provider emits its changed and removed resources through normal watches.
Workspace membership observations and directory enumeration discover new IDs.

A successful action result reports `request_dispatched` with its window ID,
generation and action. Request-close confirms that the request was dispatched;
it does not assert that an application accepted closing. Remote requests queue
Cmd-W through the app protocol without minting an input-gesture grant. Embedded
requests use the application's event route. Native close callbacks can retain
the window. Folder closure uses its lifecycle owner and preserves saved state.
Unsaved-document decisions remain with the application.

## Applications, services and native jobs

The shared shell derives `/apps/catalog/<manifest-id>/actions/launch` and `open`
from its loaded application inventory. Launch accepts an empty object. Open
accepts one absolute `path` text member and requires a document-capable app.
A successful request records `launch_requested`; the child can subsequently fail.
`/services/runner/applications` and `failures` retain the launch owner's current
children and last 64 exits, including report paths and expected-stop state.
The catalog's `manifests`, `capabilities`, `types` and `associations` are complete
frozen NDJSON streams. Capture failure retains the previous stream and marks
it `last_good_unavailable`.

Each connected application or pending child has a lifetime-bound `/apps/<id>`
provider with identity, manifest, capabilities, windows and state observations.
Manifest attribution requires an authenticated peer PID matching a current
owned child. External connections with no verified manifest retain that fact.
Activation restores and focuses an existing owned window. `request-quit` queues
the application's normal quit shortcut. Acceptance does not establish that the
application agreed to close. Retired identities cannot control a replacement.
These host actions require invoke, cross-application and host-control scopes.
They provide no force-quit or input-gesture grant.

`/services/supervisor` observes a snapshot sent by the actual parent supervisor
through an authenticated local socket. It includes child PID, prior exit and
restart count. Direct desktop execution reports supervision unavailable.
An authenticated supervisor restart can recover an abandoned control socket.
Recovery holds the private directory lock, checks socket ownership and mode,
requires a refused connection and verifies the same inode before removal.
Live listeners, links and ordinary files are preserved. Ordinary endpoint open
continues to refuse preexisting paths.
`/notifications` copies the existing alert, progress and status models.
`/services/lifecycle` reports observed and registered entity counts and capacity.
The host registry permits 256 providers and 16384 nodes; portable defaults remain
16 providers and 256 nodes.

`/jobs/<id>` observes worker or file-engine generations. The work pool copies
at most 64 active items and its last 64 collected outcomes. Its void callbacks
supply lifecycle state, without application-result or progress semantics.
Per-item cancellation is unsupported because each owner's completion and cleanup
contracts remain authoritative. File-engine jobs expose actual progress, errors,
counts and cancellation. Cancellation rechecks the exact operation generation
under the engine lock; an old identity cannot cancel a later operation.

The six `/desktop/file-manager/actions/TIKU_TRK_VERB_*` actions use the same
native scripting controller as the command-file interface. Arguments contain
`window`, `generation` and `order_token` from `/desktop/file-manager/state`, plus
`property` and `specifier`. Optional `index` and `end` are nonnegative integers;
`path` or up to eight `paths` are literal text. The order token covers ordered
paths and selection flags. Changed generations or order reject the request
before dispatch. Native script errors remain explicit in the result, and deletes
use the native Trash operation. `/session/actions/request-end` and `request-quit`
open the existing quit confirmation; `restore` uses the saved-folder controller.
