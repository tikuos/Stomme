# Local resource client

`../stomme_resources.c` builds the desktop command client using Application Kit
and Support Kit public headers and libraries. It also serves as an SDK example
for discovery, validated mutations, watches, streams and receipt reconciliation.

From this directory:

```sh
make SDK=/absolute/path/to/relocated/sdk
./build/stomme-resources --directory /absolute/private/session list /
./build/stomme-resources --directory /absolute/private/session describe /settings/ui/theme
./build/stomme-resources --directory /absolute/private/session read /settings/ui/theme
```

The session directory is a host filesystem path. `/settings/ui/theme` is a path
inside the desktop resource namespace. See
[Resource Inspector and local clients](../../docs/guides/resource-inspector.md)
for endpoint discovery, typed proposals, watches and asynchronous results.
