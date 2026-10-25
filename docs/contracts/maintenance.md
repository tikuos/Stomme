# Shared source maintenance and releases

The foundation owns base kits, the optional basic Document provider, shared
application implementations and the common shell package. Products own their
resources, configuration headers, application catalogs, retained variants and
providers. The selected source inventories identify the owner of each input.
A shared defect is fixed in the foundation. A provider or product configuration
failure is fixed in that product. A failure involving both owners requires
both source changes and downstream checks before the dependency update.

## Review a shared change

Preserve the original redistribution notices and public C identifiers. Add a
regression that fails against the defective input before accepting its passing
result. Review dependency direction, public header closure, provider selection,
serialized data compatibility and effects on the other adopting product.

Run these checks in the foundation checkout:

```sh
make check test-tools
make -j6 tests displayless check-link
make test
make -j6 sdk
python3 tools/check_sdk.py build/sdk
python3 tests/integration/run_html_sdk.py --sdk build/sdk
```

Run the affected suites and sanitizers in each product. Changes to shared
integration contracts require both downstream products. Release qualification
also requires fresh source directories, production and core packages, runtime
and application checks, relocated SDK examples and desktop workflows. Run
system suites and desktop workflows serially across products because their
harnesses use shared temporary paths. Device-port execution requires a separate
request. Linux execution does not qualify Cocoa or Finder bundles; record native
macOS validation with its own host and toolchain identities.

Follow each repository's commit review requirements. Keep shared implementation
changes and product dependency updates reviewable as separate capabilities.
A change to a product provider does not transfer that provider into the base SDK.

## Record and update a dependency

`foundation.lock.json` identifies the foundation and its source-content SHA-256.
`foundation.product.json` selects providers, application packages, shell services
and explicit overrides. Print the dependency revision from the foundation:

```sh
python3 tools/product_bridge.py --hash
```

Put that value in each adopting lock's `source_sha256`, select the matching
checkout with `FOUNDATION_ROOT`, and build its consumers:

```sh
make -j6 FOUNDATION_ROOT=/path/to/foundation core sdk apps shells
make check-architecture check-shipping check-font check-tools lint
make check-package check-core check-app-build
python3 tests/integration/run_html_sdk.py
```

Record the previous lock, provider profile, foundation revision, product source
revision, source changes, compiler and linker versions, dependency versions,
build flags and commands. A Git commit identifies a checkout's committed state;
the content pin identifies the selected foundation implementation and build
inputs. An exported source directory has no Git revision. A directory inside
another checkout cannot claim the enclosing repository's commit. Source exports
can compose without Git installed. Development builds using
`FOUNDATION_ALLOW_DIRTY=1` record their differing pin and require qualification
before release.

A release record names the product, product source revision or immutable source
snapshot, foundation content pin, provider profile, host, architecture, ABI,
SDK identity and artifact hashes. Preserve the matching source export and
redistribution notices. Record local changes when a snapshot is not a published
Git checkout. Source-content pins do not include release ledgers or Git history.
Both product release records must name the same qualified foundation pin.

## Revert a dependency update

Keep the preceding compatible foundation source, product lock/profile and SDK
available. Save preferences and documents separately from build output. Stop
processes using the replaced runtime. Restore the preceding dependency source
and lock, restore any coupled product changes, and rebuild the desktop, SDK and
application modules. Replace marked generated packages through their package
commands. Do not remove user preference or document directories as part of a
dependency rollback.

SDK identities change with foundation inputs. Old modules cannot be mixed with
a newly built host, and new modules cannot be mixed with the preceding host.
The loader refuses mismatched identities before invoking module constructors.
Rebuild detached applications against the restored SDK or restore the matching
preceding binaries together. ABI equality alone does not permit a mixed SDK.

A compatible rollback preserves document formats, preference schemas and
application protocol contracts. Byte-preserving user-data checks and successful
reopening are part of the rehearsal. A serialized schema change requires an
explicit migration and downgrade policy; preserving file bytes does not prove
that an earlier implementation can read them. A rollback with unchanged data
formats does not establish compatibility with unrelated historical releases.


## Resource provider qualification

[Provider development](../guides/resource-providers.md),
[schema evolution](../guides/resource-schema-evolution.md),
[coverage and release qualification](../guides/resource-release.md) and
[troubleshooting](../guides/resource-troubleshooting.md) define the resource
service's development and maintenance procedures.
