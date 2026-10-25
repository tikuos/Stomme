# Application resources

The counter exports the optional `tiku_app_resources_v1` callback table and the
native application descriptor. Arrow keys and resource replacements call the
same controller. The setting accepts integers from zero through 1000 and lasts
for the application session.

Build against an exported SDK:

```sh
make SDK=/path/to/sdk all smoke
```

The native runner discovers the callback table when loading `counter.so`.
Resource transport uses the application's existing window connection. The
host assigns an opaque application namespace; callback paths are relative to
that namespace. The counter publishes `preferences/value`. The runner adds
identity, state, window, menu, control and command observations.

Catalog callbacks return borrowed relative paths and owned initial snapshots.
Capture callbacks return owned snapshots. Stream callbacks return owned byte
buffers. The runner copies descriptors before the next callback and releases
these returned values. Change callbacks run on the application's thread and
recheck native identities, selection preconditions, bounds and permissions.
A callback reports its native result and known effects; queuing a file worker
does not establish that its file was saved.

The optional retirement callback runs before the application descriptor's
`stop` callback. The host retires the provider when the native connection closes
or its generation changes. A replacement catalog creates a fresh provider
incarnation and retires earlier handles and pending operations.
