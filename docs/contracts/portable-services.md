# Portable services

The portable profile provides allocator-bound buffers, a serialized input queue
and monotonic time helpers. These services use explicit provider contexts and
require no desktop process runtime. Shared code includes no firmware headers.

## Memory ownership

`kits/support/portable/tiku_allocator.h` declares an immutable operations table
and a context binding. Providers implement allocation and release, honor the
requested power-of-two alignment and retain their storage until release.
Both the operations table and context must outlive their allocations.
A provider documents its own thread-safety; the bounded test pool is confined
to one owner thread.

Initialize `tiku_owned_buffer_t` with `tiku_buffer_init()` before resizing.
Initialization requires fresh storage or a disposed buffer. It accepts a
nonzero power-of-two alignment and both provider operations. A buffer retains
a copy of the binding, so changes to the caller's binding do not redirect frees.
Live owners cannot be copied by value; moving ownership requires clearing the
source and preserving the provider lifetime.

`tiku_buffer_resize()` checks count-times-size overflow before calling the
provider. Zero bytes releases storage. Growth allocates replacement storage,
copies existing bytes and optionally zeroes newly added bytes. Failure preserves
the old pointer, size and contents. The provider must therefore have room for
both old and new allocations during resizing. Returned alignment is checked;
an invalid result is released and reported as failure. No operation falls back
to the system heap.

`tiku_buffer_borrow()` creates a span over caller-owned memory. It cannot resize.
Disposal detaches the span without freeing its memory. The backing owner must
outlive borrowers and deferred operations that reference it.
`tiku_buffer_dispose(NULL)` and disposal of an empty initialized buffer do
nothing. Disposal retains the allocation binding for subsequent reuse.

The bounded fixture in `platform/host-test/tiku_test_pool.*` takes disjoint
caller-supplied storage and allocation records. It records current and peak
payload bytes, live allocations, allocation attempts and invalid releases.
First-fit allocation scans a finite record array. Peak payload excludes padding
and metadata; capacity checks include padding. Fault injection can reject a
specified allocation call. Reset refuses live allocations, including queue
storage retained by deferred producer tickets.

Interface may include the allocator contract, but other Support headers remain
outside its dependency graph. Include and compiled-symbol checks enforce this
exception. The clock belongs to the application/service consumer, not to an
Interface model. Existing desktop constructors, layouts and default providers
remain unchanged. The new queue is opaque; its allocator ownership is private.

## Input queue and producer lifetime

`tiku_input_event_t` is the normalized input record. `tiku_event.h` also defines
its desktop compatibility name, `tiku_event_t`. A host that owns that name sets
`TIKU_INPUT_NO_EVENT_ALIAS=1` before including Interface headers. The normalized
record and logical key/modifier constants remain available; the host's event
type is unchanged. This selection is independent of displayless compilation.

`tiku_event_queue_create()` binds an allocator and a fixed capacity, allocating
the queue and its slots. If either allocation fails, creation returns NULL and
releases partial state. A queue copies complete `tiku_input_event_t` records.
Its producer and consumer calls are serialized by the caller; it is not an ISR
or concurrent-thread queue.

A successful take returns `TIKU_QUEUE_EVENT`. Empty input returns
`TIKU_QUEUE_EMPTY`. Overflow discards queued input, invalidates earlier producer
tickets and schedules `TIKU_QUEUE_CANCEL`. Until cancellation is consumed,
new input is refused. On cancellation the consumer must reset pressed keys,
pointer tracking and preedit state; otherwise it could retain a lost release.
The cancellation result does not modify the supplied event record.

Closing drops input, schedules cancellation and refuses new posts. Reopening
advances the generation and retains the cancellation until consumed. Generation
exhaustion refuses reopening; overflow at the final generation closes the queue.

Deferred producers acquire a zero-initialized `tiku_event_ticket_t`. A ticket
is noncopyable and retains the queue until posted or cancelled. Completion from
a closed queue or an older generation is dropped and releases the ticket.
Cancellation can be repeated on an empty ticket. An event rejected by queue
pressure still consumes its posting ticket.

Destroy requires a closed queue with no tickets. It returns failure and keeps
the queue pointer intact while busy. A stopped application's completion source
must cancel or complete its tickets before destruction. The queue does not
execute callbacks or own arbitrary application callback state; that state needs
an application-lifecycle owner with the same completion discipline.

## Time and deadlines

Initialize `tiku_clock_t` with a provider callback, its context and `last = 0`.
The provider returns microseconds and success, or failure. Its context must
outlive the clock. Calls are serialized by the owner.

`tiku_clock_now()` rejects decreasing readings and preserves output on failure.
`tiku_clock_after()` checks addition overflow and preserves the deadline on
failure. A successful reading can advance the clock's last observation even
when the requested deadline overflows. Compare a later successful reading
against the deadline with `now >= deadline`. Time values do not wrap.

## Checks and scope

`make FOUNDATION_PROFILE=portable-ui test test-tools` exercises bounded
allocation, ownership across contexts, growth failure, partial startup,
queue overflow, stale completions, busy teardown and clock errors. Mutation
fixtures remove these guards and must fail the same workloads. The host service
probe wraps heap entry points and checks that its workload makes no such calls.
This trap does not intercept internal calls inside a dynamically linked libc.

The S31 target additionally links `services.elf` with `-nostdlib`, the bounded
test provider and freestanding memory operations. Symbol checks require no
unresolved references, heap or native services. This ELF is a link probe;
it is not a bootable firmware image and does not establish hardware execution.

A local renderer, platform input adapter and application lifecycle consume these
contracts separately. No existing desktop font or public-layout type changes
are required by these services.
