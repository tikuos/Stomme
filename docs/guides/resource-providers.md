# Resource providers

The resource service connects existing owners to typed discovery and control.
Support Kit defines values, descriptors, caller scopes and stamps. Storage Kit
owns registry, subscriptions, receipts, streams and jobs. Application Kit owns
local transport and the optional native application extension. Shared shell
composition connects these layers. Product providers retain their product policy.

## Choose an owner

A desktop provider registers with `tiku_resource_register` on the registry's
creating thread. Its principal is NULL for a host-owned provider. Host mutations
require the host-control scope in addition to the descriptor's operation scopes.
The native controller validates a proposal and reports its actual result.
Blocking file or device work belongs in an asynchronous job owner; see
`tiku_resource_jobs.h`. Registry callbacks cannot reenter a registry mutation.

A native application exports `tiku_app_resources_v1` alongside `tiku_app_v1`.
The runner assigns its provider identity through the trusted application launch
connection. Application callbacks use relative paths and stay on the app's
creating thread. A process cannot claim provider authority through the public
same-user command endpoint. The [counter example](../../examples/application-resources/README.md)
implements the public callback contract and builds against an exported SDK.

Descriptors identify kind, schema, persistence, availability and scopes. Publish
a setting when an existing owner accepts replacements, an action for an existing
command, state for an observation, a collection for discoverable children and a
stream for a frozen byte capture. Availability records native support or current
absence. It does not remove validation or add a host feature.

## Capture and dispatch

Return owned complete snapshots from capture callbacks. Desired, persisted and
applied values describe their actual owners; an accepted asynchronous request
does not establish durable completion. Catalog callbacks transfer owned initial
snapshots while their relative path strings remain borrowed. Stream callbacks
return owned bytes. The runner copies catalog descriptors and frees returned
values. The optional retirement callback runs before the app's stop callback.

GUI and resource changes call the same native controller. Resource dispatch must
recheck native document/selection contexts, dirty state, read-only policy, undo,
confirmation and the current command's enabled state. A resource revision does
not replace an application-specific context token. Queue admission is distinct
from an effect. Report known effects and use `unknown_outcome` when the native
operation may have happened without an acknowledged result.

Publish visible changes with `tiku_resource_publish`. Registration, replacement
and retirement establish provider incarnations; old handles and queued contexts
remain invalid after replacement. Close streams, jobs and subscriptions in owner
teardown order. The host retires application providers when their native
connection or generation is lost.

## Verify a provider

Check descriptors, kind/schema, permissions, persistence and current lifetime
before counting a binding as covered. Test GUI-to-resource and resource-to-GUI
updates, stale requests, disabled commands, cancellation, provider retirement,
watch overflow and failed persistence. Mutating fixtures use isolated user data
and explicit native test doubles. Physical printing, volume operations and board
access require their own requested qualification lane.

Run affected registry/controller suites and both downstream products when shared
contracts change. The [maintenance contract](../contracts/maintenance.md) defines
source ownership, release inputs, dependency adoption and rollback. The
[schema guide](resource-schema-evolution.md) defines compatibility review.
