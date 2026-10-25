# Desktop resource service

This document specifies version 1 of the desktop resource service. The
[runtime API contract](desktop-vfs-runtime.md) describes the implemented registry,
read adapter, connection broker, asynchronous jobs, local channel and connected
shared desktop providers. The path table defines the intended namespace; the
runtime contract identifies implemented providers. The filesystem backend
layout, application ABI and preference formats retain their current behavior.
The [protocol contract](desktop-vfs-protocol.md) defines messages and limits.

## Ownership and dependencies

| Layer | Ownership |
| --- | --- |
| Support Kit | Resource identities, typed values, descriptors, request/result records and encoding |
| Storage Kit | Provider registry, revision checks, snapshots, subscriptions, job records and filesystem adapter |
| Application Kit | Local client/provider transport, authenticated caller context and disconnect handling |
| Shared shell | Desktop providers, policy, owner-thread dispatch and connection of Storage to Application |
| Product | Application composition, board tools and product-owned document providers |
| Existing controllers | Validation, model changes, confirmation, persistence, undo and live application |

Support contracts have no dependency on Storage, Application, Interface or a
product. Storage does not import Application or shell headers. Application uses
the Support records without importing Storage. The shell connects those layers.
Structured encoding uses `kits/support/tiku_msg.h`; it does not add a dependency
on Translation Kit. Public companion APIs use opaque handles and copied values.
The native application extension is declared in `tiku_app_resources.h`.
The provider transport client is declared in `tiku_resource_provider_client.h`.
Application models retain their native controllers and private structures.

The layout of `tiku_backend_ops_t` in `kits/storage/tiku_model.h` and
`TIKU_APP_ABI` value 5 are preserved. Capability negotiation gates the new
transport. An older peer continues using its existing application protocol.
SDK content identity still governs module compatibility as specified in
[SDK export](sdk.md).

## Namespace and identity

Canonical paths are relative to the resource backend. The shell mounts that
backend at `/.stomme` and publishes a Desktop System navigation entry. Mounting
must first check the host entry, existing mounts and path-length limits. A
collision leaves the host entry visible and reports `mount_collision`; an
explicit configured alternate prefix is required to mount elsewhere. The
namespace does not reinterpret host `/sys`, `/proc` or `/dev` paths.

| Path | Resource family |
| --- | --- |
| `/about`, `/capabilities`, `/schema` | Composition, supported protocol, limits and provider descriptors |
| `/settings/ui`, `/settings/tracker`, `/settings/sound` | Shared UI and legacy preference settings |
| `/settings/desktop`, `/settings/deskbar` | Desktop and Deskbar settings |
| `/settings/associations`, `/settings/file-types` | Existing type and preferred-application controllers |
| `/desktop`, `/deskbar`, `/session` | Desktop state, tray, startup and session restore |
| `/windows/<id>`, `/workspaces/<id>` | Window state and the existing workspace stack |
| `/apps/catalog/<app-key>` | Installed manifest and launch/open actions |
| `/apps/<id>` | A running application's identity, menus, controls and commands |
| `/apps/<id>/preferences`, `/apps/<id>/documents/<id>` | App preferences and document-session resources |
| `/services/<key>`, `/jobs/<id>` | Registered service lifecycle and asynchronous requests |
| `/display/<id>`, `/audio`, `/input` | Supported native display, desktop sound and input state |
| `/printers/native`, `/volumes/<id>` | Native print requests and supported volume operations |
| `/clipboard`, `/notifications`, `/accessibility` | Scoped metadata, content and supported actions |
| `/devices/<id>` | Product-owned device providers |
| `/events`, `/metrics` | Change subscription discovery and read-only diagnostics |

Schema paths use `<id>` placeholders; actual paths contain opaque identifiers.
Application keys come from the product's manifest catalog. Provider keys and
stable command keys use ASCII letters, digits, dot, hyphen and underscore.
Components are case-sensitive. A client cannot supply an empty component,
`..`, `.`, embedded NUL, percent-encoded separator, repeated separator or
trailing separator. Root is `/`. Clients use enumerated paths rather than
constructing paths from displayed labels or host filenames.

An object ID is 128 bits represented as 32 lowercase hexadecimal characters.
Its lifetime is scoped to the session and provider incarnation. A new object
does not reuse a retired lifetime, including when a PID or native window number
is reused. A handle contains session incarnation, provider incarnation, object
ID and schema version. The registry rejects a handle for an earlier lifetime.
Persistent provider/setting keys survive restarts; live object handles do not.

Each provider has an opaque incarnation and an unsigned 64-bit revision. An
accepted visible change increments the provider revision, including a GUI or
external-file change. An unchanged effective value does not increment it.
The provider must rotate its incarnation before revision wraparound. Revisions
are compared within one incarnation, never between providers.

Public menu/control collections are bounded by the registered application's
published GUI. Each publication has a revision and opaque control handles.
Commands with stable catalog keys retain those keys; private numeric menu IDs
do not become stable public names. A control request supplies its publication
revision and document/selection context. A rebuilt menu or changed selection
can invalidate that request. Disabled commands remain disabled through this API.

## Descriptors and values

A descriptor contains the following fields. Read and describe responses carry
the descriptor's schema version, provider incarnation and current revision.

| Field | Meaning |
| --- | --- |
| `path`, `provider`, `schema_version` | Canonical identity and versioned public shape |
| `kind` | `directory`, `collection`, `setting`, `state`, `action` or `stream` |
| `value_schema` | Type, units, bounds, enum choices, members and collection limit |
| `access` | Permitted operations and required caller scopes |
| `availability` | `available`, `unavailable` or `unsupported`, with a reason code |
| `persistence` | `none`, `session`, `user` or `document`, with the existing owner |
| `apply` | `live`, `asynchronous` or `restart`, and supported transaction group |
| `sensitive` | Content requiring a separate scope and omission from recursive dumps |
| `limits` | Per-resource bounds within the negotiated transport limits |

Types are boolean, signed integer, unsigned counter, finite number, UTF-8 text,
enum, 32-bit color, object and bounded array. Rectangles, selections and paths
are named object schemas with units and validation. A missing value is distinct
from zero and an empty string. Null is permitted when the schema declares it.
Color schemas declare RGB or ARGB channel order. Numbers reject NaN and infinity.
Binary document/clipboard data uses a scoped bounded stream or an existing
content API; it is not encoded as an unbounded setting.

State is read-only. A setting supports complete typed replacement after
validation. An action accepts typed arguments and returns a result or job.
A stream advertises bounded access, offset/length behavior and content scopes.
Directories enumerate resources; they do not accept file creation, rename,
deletion, permission changes or arbitrary host paths.

The descriptor expresses the controller's actual accepted range. For example,
`DarkMode` maps to `/settings/ui/theme` with enum values `light`, `dark` and
`monochrome`, retaining stored integers 0, 1 and 2. Sound volume retains the
controller's percentage range. Font size and family use the Deskbar font owner.
An alias describes the same identity and revision; it does not create a second
value, default, persistence record or event. Legacy preference names remain
storage keys, including their case, even when resource paths differ.

## Reads, changes and persistence

A snapshot copies related fields at one provider revision. It can report
`desired`, `persisted` and `applied` values when those differ, plus
`apply_state` and a bounded error. A snapshot spanning providers contains
separate revision tuples and makes no claim of global simultaneity.

A mutation supplies expected provider incarnation/revision, request ID and a
complete typed value or action arguments. The provider checks caller authority,
availability, lifetime, revision, schema and controller rules before applying
it. Two writes with the same expected revision cannot both change the provider.
GUI and external-file updates participate in that same revision ordering.

A provider transaction validates its complete setting group before publication.
It advertises atomic support only when its controller and persistence owner can
commit or roll back the group without exposing a partial value. Otherwise the
request returns `atomic_unsupported` before effects. A multi-provider client
batch returns individual outcomes; it is not a distributed transaction.

Retain existing preference files, sidecars, document formats and defaults.
Startup precedence is controller defaults, validated saved values, then explicit
volatile overrides. Volatile overrides are session-scoped and do not overwrite
saved settings. Providers without override support reject that operation.
Persisted values remain unchanged after a volatile request. Clearing an override
uses an explicit action and reapplies the saved or default value.

The existing owner orders durable replacement and live application. A provider
must not acknowledge durable success until its existing save contract succeeds.
Where a live change precedes a failed save, return the distinct desired,
persisted and applied values with `persistence_failed`. Where saving succeeds
but live application fails, report `apply_failed` or `restart_required` with
the saved value. Publish a revision/event for each resulting visible state,
including partial failure. Do not return a success-shaped result for that state.

An external reload validates through the same controller, detects content
changes even when size and timestamps match, and updates revisions/observers.
Invalid content leaves the last valid settings active and exposes a reload
error. Content fingerprints and origin tracking prevent write/reload loops.
Providers that require restart declare that behavior in their descriptors.

## Authorization and user interaction

The desktop creates caller context from an authenticated local connection.
Payload names, PIDs, app IDs and SDK identities cannot grant authority.
The shell grants a local interactive control client explicit session authority;
an app connection starts with its own registration and resource scopes.
Reading a non-sensitive descriptor does not grant mutation permission.

Scopes distinguish metadata read, settings changes, owned-app control,
cross-app control, lifecycle actions, clipboard content, document content,
accessibility actions and device operations. Provider policy may narrow them.
Registration is limited to the assigned app subtree; a remote app cannot register
shared `/settings`, another app's subtree or a product device provider.
Private values and their existence are filtered before enumeration, watch or
recursive dump, not redacted after an unrestricted snapshot is sent.

The existing GUI confirmation owner handles dirty-document close, destructive
file operations and session ending. A request returns `confirmation_required`
or an awaiting-confirmation job without doing the destructive operation.
Only the controller's interactive confirmation resolves that job. Setting an
`approved` argument is not a substitute. A document command retains its existing
undo, save, dirty-state and read-only checks.

The local API does not sandbox a process with host filesystem access. It creates
no network administration endpoint. The production file-manager command-file
compatibility API remains separate from the development UI test interpreter;
neither becomes an unrestricted transport for this service.

## Lifetime, jobs and watches

The registry copies values and retains lifetime tokens before dispatch. It does
not call provider code while holding its registry lock. Callbacks run on the
controller's owning thread. Blocking work uses workers; completion returns to
the owner thread and checks the retained lifetime before touching GUI state.
Provider unregister first prevents new dispatch, invalidates handles, resolves
outstanding requests and drains retained callbacks before freeing provider data.

An asynchronous action returns a job ID after acceptance. States are `queued`,
`running`, `awaiting_confirmation`, `succeeded`, `failed`, `cancelled` and
`unknown_outcome`. Cancellation stops pending work where supported; it does not
undo completed effects. A cancellation response says whether it was accepted,
too late or unsupported. Provider loss after dispatch may produce
`unknown_outcome`; a client must reconcile state rather than repeat an action.
Job records identify owner, request ID, revision, progress, result/error and
retention expiry. Sensitive results inherit their operation's scopes.

Subscription creation atomically captures a snapshot revision and starts event
delivery after it. Events carry session/provider incarnations, provider revision,
subscription sequence, kind and affected paths. Related transaction changes are
published as one revision. Queues are bounded. Overflow produces an `overflow`
marker, stops incremental delivery and requires a fresh snapshot/subscription.
Sequences are per subscription, not a global ordering across providers.
Provider removal produces `removed` while that subscription can still deliver;
if it cannot deliver, the client discovers loss through overflow or disconnect.

## Backend adaptation and host capabilities

The existing `stat`, `list`, `read`, `path_of_id` and change operations expose
non-sensitive resources through a backend bound to an explicit read context.
The context-free instance is restricted to public metadata. Node IDs are mapped
to resource lifetimes; a removed node cannot silently identify a replacement.
Scalar reads use canonical UTF-8 with a trailing newline. Booleans are `true`
or `false`, numbers are locale-independent, enums are their stable keys, and
objects/arrays use the protocol's diagnostic JSON mapping. Reads of action nodes
return descriptors rather than executing them.

Legacy `write` cannot carry expected revision or authenticated caller context.
It returns `requires_context` without effects; backend permission bits omit
write/execute for those nodes. The resource inspector uses the typed companion
API for mutation. A future host filesystem transport must stage a complete
request with caller context and define flush/close errors before enabling writes.
Ordinary host `cat` cannot reach the internal mount without such a transport.

Availability follows the existing host/product implementation. The workspace
provider describes the existing window stack; it does not promise virtual
desktop switching. Audio describes desktop event sounds, not a host-wide mixer.
`/printers/native` describes native print submission/cancellation, not printer
administration. Volume providers expose supported scan/unmount/eject behavior.
Notification state covers the current desktop owners, not an invented host
notification archive. TikuDesktop supplies board providers; KintsugiOS retains
its advanced document/presentation providers and disables board services.
Build-time provider and host selections are read-only composition metadata.
