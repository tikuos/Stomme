# Resource Inspector and local clients

Resource Inspector is a native application selected by TikuDesktop and KintsugiOS.
Open it from Applications. It connects to the desktop's private local service
using the public Application Kit client. The application uses native controls;
its transport runs on a worker thread. Normal operation does not require a
scripting interpreter.

## Session and namespace

A desktop session creates a private directory under `/tmp/stomme-desktop-XXXXXX`
when `STOMME_RESOURCE_DIRECTORY` is unset or empty. The session exports the chosen
directory to child applications. Inspector's Endpoint field shows that host
filesystem path; Copy endpoint copies it for a command client. Normal session
shutdown removes the automatically created directory after the resource service
closes. An explicit `STOMME_RESOURCE_DIRECTORY=/absolute/private/directory`
selects an existing or newly created private directory instead.

`control.sock` authenticates the local operating system user. Access is limited
by negotiated scopes, features, resource metadata and native owner validation.
The endpoint grants interactive same-user desktop control; it does not isolate
applications running as that user. Keep the directory private. The client does
not scan other sessions or select a directory automatically.

Paths such as `/settings/ui/theme`, `/apps`, `/windows` and `/services` belong to
the resource namespace. They are not host files and are not inputs to `open(2)`.
The desktop's internal read backend mounts this tree at `/.stomme` by default.
The SDK and command client use namespace paths beginning with `/`, without that
mount prefix. Streams may contain host paths as data; such values remain host
paths and retain their native permission and confirmation requirements.

## Inspect and change

Connect to an endpoint, select a catalog entry or enter an absolute namespace
path, then press Inspect. The response view shows the descriptor, schema,
availability, access scopes, revision, desired/applied values and result metadata.
Next page advances a bounded catalog page. A changed catalog requires a fresh
inspection when its cursor expires.

Settings accept typed JSON in Proposal. Apply value uses the displayed revision
and declared durability. Invoke action submits the action's typed object
arguments. Native owners validate proposals and retain their confirmation flows.
Changing the path or endpoint requires another inspection before mutation.
Read-only and unavailable resources do not enable mutation controls.

A live watch marks changed observations as requiring refresh. It preserves the
proposal editor; press Inspect to obtain current values before another change.
Watch overflow also requires a new snapshot. Pending jobs display their ID and
status. Cancel job requests native cancellation; completion can race with that
request. Reconcile queries the last mutation's retained receipt without executing
it again. An unknown outcome requires reconciliation, not automatic resubmission.
A new session may no longer retain an earlier session's receipt.

Read stream previews UTF-8 captures up to 65536 bytes. Use the command client to
export larger or binary streams within the negotiated stream bound. Resource
Inspector's exported menus provide observation and window controls. Proposal
submission is available through its native controls.

## Command client

Product core packages install `system/stomme-resources`. Within a build it is
`build/sv3/runtime/system/stomme-resources` when `BUILD=build/sv3` is selected.
Stomme builds it with `make resource-client`. An SDK-only build example is in
[`examples/resource-control`](../../examples/resource-control/README.md).

```sh
export STOMME_RESOURCE_DIRECTORY=/absolute/private/session
stomme-resources list /
stomme-resources describe /settings/ui/theme
stomme-resources read /settings/ui/theme
stomme-resources set /settings/ui/theme enum dark
stomme-resources watch /settings/ui/theme 2 30000
stomme-resources invoke /apps/catalog/resource-inspector/actions/launch '{}'
stomme-resources read /about/product
```

Use `describe` to check a resource's kind before selecting `read` or `stream`;
stream operations require a stream resource.

`list` prints successive JSON page objects. `read`, `describe`, `job`, `receipt`
and mutation commands print JSON result objects. Set/invoke return an initial
receipt or job; they do not wait for an asynchronous effect. Their diagnostic
output includes `request_id`. Pass the returned job ID to `job` or `cancel`, or
the request ID to `receipt`. A successful command exit establishes successful
request handling; check a job's terminal state and code for its final outcome.

```sh
stomme-resources job RETURNED_JOB_ID
stomme-resources cancel RETURNED_JOB_ID
stomme-resources receipt RETURNED_REQUEST_ID
```

Scalar set types are `boolean`, `integer`, `number`, `text`, `enum`, `counter`
and `color`. Their arguments are literal scalar text. The `json` type accepts
JSON objects, arrays and scalars. Lossless typed JSON uses these wrappers:

```json
{"$type":"enum","$value":"dark"}
```

Counters use decimal text, for example
`{"$type":"counter","$value":"18446744073709551615"}`. Colors use an unsigned
32-bit number. `$type` and `$value` are reserved object keys in this literal
syntax. Invalid JSON, duplicate members, non-finite numbers, out-of-range values
and excessive nesting are rejected before dispatch. The native owner remains
authoritative for model constraints.

Watch prints an atomic initial snapshot followed by the requested event count.
Its timeout applies to each awaited event. Idle timeout exits 3; an uncertain
mutation transport outcome exits 4; other errors exit 1 and usage errors exit 2.
Watch removal or overflow exits nonzero and requires a fresh subscription.
No command automatically retries a mutation. Network, FUSE and MCP transports
are outside this local client interface.
