# Shared Kit SDK Contract

The base SDK exports eight kits in linker order: Editor, Application,
Translation, Interface, Storage, Device, Media and Support. It exports no
document or presentation provider. Stomme retains the machine identity
`desktop-foundation` for SDK and product lock compatibility; its central
definition is `foundation.json`.

## Artifacts and compatibility

`sdk.json` records the foundation identity, base provider, host, operating
system, architecture, application ABI, library inventory and kit source hash.
`sdk.mk` locates headers and libraries relative to itself. The pkg-config file
uses its installed location as its prefix. Neither requires a source checkout.

Archives are position-independent static libraries. Rebuild an application
when its kit implementations change. Matching ABI numbers alone do not imply
matching APIs, feature negotiation or document record layouts. Product SDKs
must pair any added provider's headers and implementation explicitly.

Exported headers have a complete quoted include graph. Headers named
`*_private.h` and headers that reach private or missing headers are excluded.
No application, shell, firmware or implementation sources are copied into the
SDK. System development headers and libraries named in the exported flags are
host dependencies.

## Publication and installation

SDK replacement requires a marker with this foundation identity. An unknown
directory, symbolic-link output or unfinished previous export is refused.
Export stages complete artifacts before replacing the destination and restores
the preceding SDK if publication fails.

`make sdk` exports the selected archives. `make install-sdk SDK_PREFIX=...`
copies that export. `tools/check_sdk.py` checks ownership, provider exclusion,
header closure, archive inventory and compilation of the public header set.
The relocated HTML example additionally links a standalone process and module,
loads its ABI descriptor and paints without a desktop server.
