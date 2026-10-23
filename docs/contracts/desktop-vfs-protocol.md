# Desktop resource protocol

This is the version 1 transport specification for the
[desktop resource service](desktop-vfs.md). It does not describe an implemented
product endpoint. The [runtime API contract](desktop-vfs-runtime.md) names the
implemented subset. JSON examples are a diagnostic representation of typed messages;
the transport uses Support Kit's existing `tiku_msg` codec.

## Local connection and negotiation

Use a separate local stream endpoint under the session's private runtime
directory, owned by the desktop user, with directory mode 0700 and endpoint
mode 0600 on POSIX. Check directory ownership and reject symbolic-link endpoint
replacement. The server authenticates peer credentials where supported and
binds app connections to a launcher-issued, single-use registration capability.
The capability is delivered through an inherited private channel, not an app
manifest, command line or world-readable file. The shell can authorize a local
interactive client by host identity and explicit session policy. An adapter
unable to establish caller identity does not enable privileged operations.
Host credentials identify the user; app registration constrains the API principal
and does not establish a sandbox against other same-user processes.

The existing rendering/serial/network transport does not implicitly authenticate
a control client. Its negotiated features can advertise service discovery, but
version 1 resource mutations use the authenticated local connection.

The client sends `hello` with major/minor version, required and optional features
and requested limits. The server chooses a compatible minor version, supported
features and limits no higher than either peer's maxima. Major mismatch or an
unknown required feature closes negotiation with `version_mismatch` or
`feature_unavailable`. No reads, registration or mutations precede negotiation.
The response gives session incarnation, assigned principal, granted scopes,
provider capabilities and negotiated limits. The server creates principal and
scopes; hello cannot request an authority by claiming an app name.

## Frames and typed values

A frame has a four-byte unsigned big-endian payload length followed by one
flattened `tiku_msg` message. The message code is `0x53564331` (`SVC1`). Reject
an invalid or oversized length before allocating its payload. A zero length,
truncated frame, malformed field or excessive nesting terminates the connection
with a protocol error; pending mutations acquire an uncertain outcome until
their status is reconciled. Decoding consumes exactly the declared frame.

Envelope field names are ASCII. Singular fields occur once; duplicate singular
fields and unknown fields in version 1 are rejected. Repeated fields are
permitted only where this contract names a list. Negotiation is extended before
future versions accept new fields. A decoder checks counts, nesting and byte
limits before constructing the corresponding application objects.

| Semantic field | `tiku_msg` encoding |
| --- | --- |
| Operation, IDs, canonical path, enum, error code | String |
| Schema/protocol version, page size, duration in milliseconds | Nonnegative int32 within the stated bound |
| Revision, sequence, byte size and unsigned counter | Canonical unsigned decimal string |
| Boolean | Bool |
| Signed integer | Int64 |
| Finite number | Double |
| Color | Int64 in the declared unsigned 32-bit range |
| Structured record | Nested message |
| List | Repeated named nested message, with an explicit bounded count |

A typed value is a nested message with `type` and one payload field `value`.
For `object`, repeated `member` messages contain `key` and a typed `value`;
keys are unique. For `array`, repeated `item` messages contain typed values.
Object and array values omit the scalar payload. `null` has no payload.
Text is valid UTF-8 with no embedded NUL. Integer strings have no sign, leading
zeros except `0`, whitespace or exponent, and must fit an unsigned 64-bit value.
Revisions start at `1`; event sequences start at `1`.

Diagnostic JSON represents typed objects/arrays with a `value` object/array;
the codec translates its keys/items to the repeated nested members above.
It uses booleans and signed integers as JSON values and decimal strings for
unsigned counters and revisions. Blob fields use RFC 4648 base64 strings with
padding and no whitespace; the wire field contains the decoded bytes.
Objects have unique keys and numbers are finite. A file scalar is rendered from
the typed value; diagnostic JSON is not an additional mutation parser requirement
for Storage or Application Kit.

## Operations

After negotiation, an envelope contains `op`, `request_id`, `session` and the
fields for that operation. Request IDs are client-generated 128-bit hexadecimal
values. Responses echo the ID and session and contain either `result` or `error`.
Events identify their subscription instead of a request. Server-side caller
context is attached to dispatch and is not a trusted payload field.

| Operation | Required fields and result |
| --- | --- |
| `list` | Path, page size, optional opaque cursor; returns filtered entries and next cursor |
| `describe` | Path; returns descriptor and provider revision tuple |
| `read` | Path; returns typed snapshot and revision tuple |
| `set` | Path, expected tuple, typed value, durability, deadline; returns snapshot or job |
| `invoke` | Path, expected tuple, typed arguments, deadline; returns typed result or job |
| `transaction` | Provider, expected tuple, list of complete setting replacements, deadline; returns snapshot or error |
| `subscribe` | Provider and path filter; returns subscription ID and starting snapshot |
| `unsubscribe` | Subscription ID; releases it and stops further queued delivery |
| `job-status` | Job ID; returns current state, result/error and expiry |
| `cancel` | Job ID, expected tuple and deadline; returns accepted, too-late or unsupported cancellation |
| `request-status` | `prior_request_id`; returns retained result, pending state or expired/unknown status |
| `stream-open` | Path, expected tuple and unsigned `maximum_bytes`; returns a scoped read lease and content revision |
| `stream-read` | Lease, unsigned `offset` and int32 `byte_count`; returns blob `data` at that lease's frozen revision |
| `stream-close` | Lease; releases the snapshot and is idempotent |

`expected` is an object containing `provider`, `incarnation` and `revision`.
Mutations require the current tuple, including launch and other actions whose
state is owned by a catalog or controller rather than a live window. Resource
paths are resolved to handles before dispatch; handles are checked again on the
owner thread before effects. No wildcard, unconditional update or force flag
bypasses revision or scope checks. A transaction cannot contain an action.

Cancellation targets the job-controller tuple returned by `job-status`, with
provider key `jobs`, rather than the resource's own tuple. The result says
`accepted`, `too_late` or `unsupported`; accepted running cancellation still needs
controller completion. `request-status` has its own request ID and names the prior
mutation through `prior_request_id`. A broker must receive the selected feature
mask after negotiation. Async changes require CHANGE and JOBS; advertising JOBS
at the server does not imply that a client selected it.

Provider registration uses a separate role negotiated on an app connection.
Its messages carry assigned namespace, provider schema, resources, snapshots
and completion/change records. The shell validates the assignment and maintains
authoritative incarnation/revision ordering. A provider cannot replace caller
context, increase granted scopes or publish another app's resources. Public
client and provider operations are distinct even when they share framing.

| Provider operation | Fields and behavior |
| --- | --- |
| `register` | Assigned subtree, schema version, bounded descriptors and initial snapshot; server returns incarnation and revision |
| `publish` | Provider handle, expected tuple, complete changed snapshot and affected paths; server orders the revision and events |
| `complete` | Server-issued dispatch ID and result/error; cannot complete another provider's request |
| `unregister` | Provider handle; stops dispatch and retires the incarnation |

The server sends a provider `dispatch` record containing dispatch ID, resolved
resource, expected tuple, validated arguments, deadline and server-issued caller
scope. The provider checks its owner-thread model generation before applying.
It reports GUI changes through `publish` and completes API requests through
`complete`. While a dispatch owns an accepted revision, a conflicting publication
or mutation is serialized or rejected as stale; it cannot overwrite GUI changes
which occurred before its owner-thread check. A completion publishes its changed
snapshot once, rather than requiring a separate duplicate `publish`. Late or
duplicate completion records cannot repeat a mutation. Provider message bounds
and permissions are checked before installing its descriptors.

Each `list` page belongs to a frozen provider snapshot. Its opaque cursor is
bound to caller, session, provider incarnation, revision, path and expiry. A
changed collection returns `stale_cursor`, requiring a new enumeration; it does
not silently mix snapshots. Descriptors and values are filtered for the caller
before pagination. Cursors expire after 30 seconds.

The server retains mutation request IDs and canonical payload digests, scoped
to the authenticated principal and session. Repeating an ID with identical
content returns its pending or completed outcome without repeating effects;
different content returns `request_id_reused`. Pending IDs are not evicted to
accept another request. A reconnect obtains the same principal only through
server-verified registration or local authority; a claimed ID cannot resume it.
After retention expires, `request-status` returns `outcome_unavailable`. Retrying
an expired request is not safe for an action. Read back relevant state or use the
controller's existing operation identity to reconcile it.

## Bounds and scheduling

These are version 1 maxima. A negotiated or descriptor-specific limit may be
lower. The capability response reports the effective limits. Collection totals
can exceed a page; membership and per-provider registration are bounded.

| Limit | Maximum |
| --- | --- |
| Encoded frame payload | 65,536 bytes |
| Canonical backend path | 480 UTF-8 bytes; mounted path must fit existing `TIKU_PATH_MAX` |
| Single path component | 127 bytes, excluding terminator |
| Text setting value | 4,096 UTF-8 bytes |
| Message/value nesting | 8 levels |
| Object members or array items | 128 per value, within frame limit |
| List page | 128 entries, within frame limit |
| Registered resources | 4,096 per provider, 65,536 per session |
| Outstanding requests | 32 per client, 256 per session |
| Transaction replacements | 32, within one provider and frame limit |
| Subscriptions | 32 per client, 256 per session |
| Event queue | 256 events and 1 MiB per client; first reached bound applies |
| Active jobs | 16 per client, 128 per session |
| Completed jobs and mutation outcomes | 128 each per principal, bounded by 4 MiB combined |
| Completed outcome retention | 60 seconds; earlier capacity eviction reports unavailable |
| Dispatch/acceptance deadline | 30,000 milliseconds |
| Job execution deadline | 300,000 milliseconds, including confirmation wait |
| Read stream leases | 8 per client, 64 per session; 30-second idle expiry |
| Stream read chunk | 32,768 bytes, carried as a bounded blob |
| Retained stream snapshots | 4 MiB per client; larger content uses an existing content API |

Fail before effects with `limit_exceeded` or `busy` when an accepted request
would exceed a bound. A large response is paginated or returns `limit_exceeded`,
never a truncated success. Timeout before dispatch returns `deadline_exceeded`
with no effects. Timeout after dispatch returns a job or `unknown_outcome` and
does not authorize a retry. Owners can advertise shorter deadlines. The server
uses monotonic time for deadlines and retention.

Version 1 streams are scoped read snapshots. Opening checks the content scope,
revision and retention limit. Reads do not observe a mixture of document
generations. An owner unable to retain the bounded snapshot returns
`unsupported` or `limit_exceeded`. Mutating document or clipboard content uses
its existing controller API or a typed action with validated content references;
this contract does not add arbitrary chunk writes to settings. The complete
frame bound includes blob encoding overhead.

Disconnect removes subscriptions and discards undelivered pages. It cancels
queued uncommitted mutations. Running actions retain bounded jobs and their
existing controller cancellation policy; they can complete after disconnect.
Loss of the provider returns `owner_lost` before dispatch and `unknown_outcome`
after effects may have begun. Service restart rotates session incarnation.

## Errors and events

Errors contain stable `code`, bounded diagnostic `message`, and `effect` equal
to `none`, `partial` or `unknown`. They can include a current snapshot or job
reference when the caller has access. An unauthorized caller does not receive
private values in an error. Control flow uses codes rather than message text.

| Code | Meaning |
| --- | --- |
| `invalid_request`, `invalid_value`, `limit_exceeded` | Envelope, schema or bound rejected before effects |
| `unauthenticated`, `permission_denied` | Caller lacks identity or required scope |
| `not_found`, `unsupported`, `unavailable` | Missing resource or declared product/host limitation |
| `read_only`, `requires_context` | Observed state or legacy backend cannot accept this mutation |
| `stale_revision`, `stale_handle`, `stale_cursor` | Caller must obtain a current identity/snapshot |
| `atomic_unsupported` | Owner cannot commit the requested group atomically |
| `confirmation_required` | Existing interactive controller must resolve the request |
| `persistence_failed`, `apply_failed`, `restart_required` | Persistence/application differ; inspect returned state |
| `busy`, `deadline_exceeded`, `cancelled` | Queue/deadline/cancellation outcome with stated effect |
| `owner_lost`, `unknown_outcome`, `outcome_unavailable` | Reconcile owner state; effects are not safely repeatable |
| `request_id_reused` | Same retained request ID has a different payload |

Events carry `subscription`, `session`, `provider`, `incarnation`, `revision`,
`sequence`, `kind` and affected paths. Kinds are `changed`, `added`, `removed`
and `overflow`. Sensitive values are not placed in ordinary change records.
On overflow the server clears queued incremental records, sends one marker and
pauses that subscription. The client discards its incremental cache, unsubscribes
and starts another atomic snapshot/subscription. After connection loss it repeats
negotiation and checks session incarnation before reusing cached identities.

## Example flows

The [example records](desktop-vfs-examples.json) use diagnostic JSON. They cover
a descriptor/read, conditional write, stale and unauthorized requests, an
atomic setting group, a confirmation job, an observed-state rejection and
subscription overflow. They are specification examples, not server test results.
The example theme choices retain their legacy `DarkMode` integer mapping; the
examples do not change that preference file format.
The `example.atomic` provider illustrates a controller which advertises atomic
support; it does not imply that an existing preference controller supports it.
