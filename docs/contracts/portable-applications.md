# Portable applications and storage

The portable profile includes static application lifecycle and queued paged
storage. These contracts use the resource API's caller, kind, entry and error
types. The resource registry implementation and desktop process runtime are not
linked. Products supply storage adapters and decide which applications to run.

## Application ownership

`kits/application/portable/tiku_static_app.h` defines an immutable descriptor
with start, dispatch, visibility, stop and dispose callbacks. Create binds an
allocator, allocates zeroed state and a bounded input queue, and retains the
descriptor. The descriptor and allocator context outlive the application.
Construction failure frees partial allocations without calling application code.

Start runs once and makes successful applications foreground. A failed start
calls stop to cancel partially started work, leaving the owner for destruction.
The start argument is borrowed for that callback; applications copy bindings
they need afterward. Stop invalidates application leases before calling the stop
callback and refuses further dispatch. Repeated stop calls have no further effects.
Destroy requires a stopped application with no leases. Dispose then releases its
children before state is freed. Dispose must accept zeroed state when an
application is destroyed without starting, and partially initialized state after
failed start.

An event dispatch consumes at most the caller's budget. Callbacks must bound
their own work and cannot perform blocking storage operations. Backgrounding
clears queued input, delivers interaction cancellation and calls the optional
visibility callback. Foreground input resumes after activation. Background is a
lifecycle state; platform sleep is outside this contract.

A deferred operation acquires a zero-initialized `tiku_app_lease_t` before it
submits work. A lease retains the application allocation until released. Its
state accessor returns NULL after stop, so completions can retire without
calling application code. Release invokes no callbacks; the owner scheduler
retries destruction afterward. Leases cannot be copied. Provider work must
retire even when its result will be discarded. An adapter that cannot cancel
immediately keeps its request and lease until completion.

Lifecycle calls, provider completions and lease operations share one serialized
owner thread. Lifecycle callbacks cannot reenter lifecycle operations. A lease
can be acquired during start. This runtime provides no process isolation:
trusted host policy supplies each application's services and caller scope.

## Storage binding and requests

`kits/storage/portable/tiku_paged_storage.h` binds an allocator, provider and
copied `tiku_resource_caller_t`. It allocates a page of at most
`TIKU_RESOURCE_MEMBERS_MAX` entries and a read buffer of at most
`TIKU_RESOURCE_STREAM_CHUNK` bytes. Either capacity may be zero to disable that
operation. Requests reuse these buffers; directory size is independent of page capacity.

Providers advertise READ plus COLLECTION and/or STREAM, with optional CHANGE
support. Caller scopes admit each operation. Rename,
delete, replacement, information, directory creation and subtree operations
retain their request until the provider reports completion. The provider owns
publication and writer serialization on its backing medium.

Mutation requests copy paths into the storage session. Replacement requests
borrow immutable input bytes through completion, including cancellation.
Directory and subtree operations carry the expected catalog epoch and item
identity so providers can reject stale work. Storage capability flags describe
the backing volume; the consumer must check them before offering an operation.

List supplies a path, catalog epoch and the previous node ID. Epoch zero and
ID zero start enumeration. A provider returns ordered, increasing, nonzero
IDs that remain stable within that epoch, terminated paths and resource IDs,
and DIRECTORY or STREAM kinds. An empty page must mark end of enumeration.
A short page without end is valid; consumers continue from its last ID.
Changing media or directory membership invalidates the epoch. The provider
reports STALE_CURSOR when a supplied epoch no longer matches.

Read supplies path, epoch, node ID and byte offset. The provider checks that the
identity still denotes the requested item and enforces caller authorization for
both list and read. A successful read may contain fewer bytes than requested.
Zero bytes requires end of stream. Offsets advance by the returned byte count.
The core rejects counts above capacity and stale completion epochs. Providers
must avoid writing beyond the supplied buffers; completion checks cannot undo
an out-of-bounds provider write.

Submit is bounded and nonblocking. Returning OK retains the immutable request
view and its writable output spans. Other returns retain nothing; PENDING is
not a valid provider return. The session reports PENDING to consumers while an
accepted request is outstanding. Completion occurs later, outside submit or
cancel, and carries the request token. Wrong or already retired tokens are
rejected. Provider operations, context and backing media references outlive
retained requests. A provider marshals interrupt or worker results onto the
owner thread before calling complete.

Cancel hides output immediately and asks the provider to retire once. It keeps
request memory until matching completion, discarding even a successful result
that arrives afterward. Close also prohibits new work. Unavailable permanently
closes the session with UNAVAILABLE and cancels any retained request; remount
creates a new session. Destroy refuses pending provider work. Completed output
is borrowed until the next accepted request, cancellation, close or destruction.

## Examples and checks

`examples/portable-browser` selects sixteen entries and a 1024-byte read buffer.
Its application lease retains browser state while storage is pending. The
scheduler calls `portable_browser_poll()` after provider completion, including
for stopped applications, and destroys the app after that lease retires. Poll
updates browser state only while the application is running. The fixture uses
`/media`; the downstream adapter determines the backing store.

`examples/portable-calculator` processes hexadecimal key input and produces
hexadecimal, decimal, octal and binary results. The model in
`packages/apps/models/tiku_word_model.c` includes the existing developer
calculator's integer conversion, bit and checksum implementation. It validates
public width and base arguments without importing the desktop launcher.

Run the examples' deterministic workloads and dependency checks:

```sh
make FOUNDATION_PROFILE=portable-ui check-applications
python3 -m unittest discover -s tests -p test_portable_applications.py
```

The workload enumerates a directory beyond one page, reads short chunks,
checks denied callers, rejects stale epochs and malformed results, removes
media during a read and switches applications with pending work. Startup
allocation failures and repeated switching check allocator return to baseline.
Mutation fixtures remove teardown, completion, count, cursor and dispatch guards
and require the same workload to fail. Host heap wrappers reject direct heap
calls; they do not inspect internal calls inside dynamically linked libc.

The examples are models driven by host fixtures. The
[portable SDK](portable-sdk.md) builds relocated consumers for host and S31. Local media, display,
launcher composition and hardware qualification belong to the embedded product.
