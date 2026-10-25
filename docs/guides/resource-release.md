# Resource coverage and release qualification

A release qualifies a finite inventory of public settings, state and actions.
Keep the agreed inventory immutable. A closure ledger records each inventory ID,
owner, products, effective path or template, actual kind, access mode, persistence,
applicability, implementation binding and evidence. Dynamic paths identify their
instance discovery rule. Record kind refinements and derived read-only fields
with their writable native owner or collection children.

A row is closed by its native binding and applicable verification. An internal
command sentinel is excluded with its original reason. A missing implementation,
editable setting or failing check remains open. Product-disabled or host-absent
capabilities retain explicit policy and availability; they do not establish that
the operation ran on another host. Family workflows exercise named payloads and
states. They do not prove every payload or conditional GUI state.

Coverage inventories and qualification ledgers remain outside published source.
Published source carries contracts, provider APIs, checks, fixtures and guides.
A local release record includes an inventory checksum, immutable source manifest,
repository heads and local diffs, content pins, SDK identities, compiler/linker
and dependency versions, flags, commands, exits, skips and artifact hashes.

## Fresh source qualification

Create separate source snapshots and empty output directories for the foundation
and both products. Include staged implementation changes in a local snapshot;
a checkout's HEAD alone does not identify those bytes. Resolve the selected
foundation content hash with `tools/product_bridge.py --hash`. Set that hash in
the isolated product locks and build with `FOUNDATION_ALLOW_DIRTY=0` to check
strict dependency adoption. Do not rewrite a working product's published gitlink
as part of an unpublished rehearsal.

Run the foundation checks, kit suites, displayless/link checks and SDK export:

```sh
make -j6 all tests resource-client displayless check-link
make check test-tools test
python3 tools/check_sdk.py build/sdk
python3 tests/integration/run_html_sdk.py --sdk build/sdk
```

Run each product's full build and tests, architecture/shipping/lint gates,
packages, SDK checks and independent app matrix. Use the selected foundation
checkout consistently, and run native system/UI workflows serially across
products. The product's `make test` includes native host suites; a missing display
server must be reported as a skip or supplied in the native host lane.

```sh
make -j6 FOUNDATION_ROOT=/path/to/foundation FOUNDATION_ALLOW_DIRTY=0 all sdk
make FOUNDATION_ROOT=/path/to/foundation FOUNDATION_ALLOW_DIRTY=0 test
make FOUNDATION_ROOT=/path/to/foundation FOUNDATION_ALLOW_DIRTY=0 lint check-shipping check-package check-core
python3 tools/test_app_build.py --integration --build build
```

SDK/source-view tools receive `STOMME_SOURCE_PRODUCT` and `STOMME_SOURCE_VIEW`
when qualifying a nondefault selected tree. Run affected sanitizers and the native
resource workflows for preferences, shell state, windows, lifecycle, application
models, host services, product devices and Resource Inspector. Record concurrency,
overflow, crash/retirement, allocation, disk failure, stale identities and
shutdown/cancellation evidence. Confirm that defect guards reject defective
inputs before using passing results.

## Dependency rehearsal and host lanes

Preserve a preceding compatible source/profile, SDK and runtime. Demonstrate a
shared provider change adopted by both products at matching strict pins, then
restore the preceding compatible dependency with its matching modules. Verify
user-data bytes and reopen documents through native owners before and after the
rollback. The [maintenance contract](../contracts/maintenance.md) defines the
adoption and restoration procedure.

Linux headless conductor and native X11 results are separate evidence from
Cocoa/Finder results. A macOS lane checks Cocoa builds, SDK and app loading,
Finder bundles, native clipboard, accessibility permission/dispatch, printing,
volumes, displays, package launch and shutdown. Physical printer, removable-volume
and board execution require explicit authorization and recorded device identity.
An unexecuted lane remains unexecuted in the release record. Publication status
records whether the shared change and downstream pins were committed and pushed.
