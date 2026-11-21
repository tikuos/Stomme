# Portable SDK

The `portable-ui` SDK contains selected C headers, static archives, font notices,
a capability and ABI manifest, a Make include and a standalone integrity checker.
It exports local painting, caller-owned window workspaces, Deskbar painting,
text fields and static application contracts. Native allocation, desktop
process, system clipboard, remote-storage and document implementations are excluded.

Build and exercise the host SDK:

```sh
make FOUNDATION_PROFILE=portable-ui check-sdk
python3 tools/check_sdk.py build/portable-ui-host-test/sdk
```

Build the S31 SDK and link its relocated examples:

```sh
make FOUNDATION_PROFILE=portable-ui FOUNDATION_TARGET=s31 \
    CROSS_COMPILE=/path/to/riscv32-esp-elf- check-sdk
python3 tools/check_sdk.py build/portable-ui-s31/sdk \
    --cc /path/to/riscv32-esp-elf-gcc
```

`SDK_DIR` selects the export destination; it defaults to `$(BUILD)/sdk`.
Export replaces a directory marked with this foundation's SDK identity and
refuses an unowned destination. The profile build still uses separate output
directories for each target/configuration. The S31 executable is a link probe,
not a bootable firmware image. The host executable runs the same provider and
application workload.

## Consumer selection

A consumer selects the target and compiler, supplies its required capabilities,
and includes the exported `sdk.mk`. For example:

```make
FOUNDATION_TARGET := host-test
CC := cc
SDK_REQUIRED_CAPABILITIES := local-rendering static-applications paged-storage-read
include /opt/stomme-portable/sdk.mk

app: app.c
	$(CC) $(SDK_CFLAGS) app.c $(SDK_LIBRARIES) -o $@
```

For installation paths containing spaces, set `SDK_ROOT` to the complete path
before including `sdk.mk`, or pass it as a Make command-line variable. Escape
spaces in the include filename. The exported include and archive arguments are
quoted. Relocated integration tests exercise a path containing a space.

`SDK_CFLAGS` supplies the local-rendering selection, include path, generated ABI
header and target ISA/ABI flags. `SDK_LIBRARIES` supplies the archives in a link
group. Consumers provide their own optimization flags and platform runtime.
The S31 example links the toolchain's memory/string routines and compiler
arithmetic library with `-nostdlib`; it provides no syscall stubs or system heap.
Other toolchains require their own complete-link qualification.

The manifest records these capabilities:

- `control-models`, `local-rendering`, `font-subset`.
- `allocator-contexts`, `bounded-input`, `monotonic-time`.
- `static-applications`, `paged-storage-read`, `word-calculator`.
- `text-field-provider`, `text-field-events`, `word-checksum-input`.
- `paged-storage-mutation`, `paged-storage-replacement`, `paged-storage-info`.
- `paged-storage-directories`, `paged-storage-subtrees`,
  `paged-storage-volume-features`, `paged-storage-tree-copy`.
- `window-workspace`, `deskbar-paint`.

The control archive's `text-metrics` requirement is supplied by the renderer's
font provider when the complete SDK is linked. Allocation, presentation, input,
clock and storage adapters remain consumer-owned bindings. A capability says
which contract and implementation are supplied, not that hardware is attached.
Writable storage operations require a consumer-supplied provider with the
corresponding capabilities and caller authorization.

Public headers retain shared declarations that are also used by desktop builds.
`sdk.json` lists the selected `public_symbols`; declarations alone do not imply
that an unselected implementation is available. For example, owning surfaces
use `tiku_framebuffer_create()`, while native surface constructors and display
list recording are outside this SDK. `TIKU_DISPLAYLESS` is rejected by the SDK
header because its exported graphics headers describe local painting.

## Artifact and ABI identity

`profile.json` records source/header hashes, font inputs, build-tool inputs,
compiler version, target, ISA, ABI, compile flags and selected capabilities.
Their digest identifies the compiled profile. A source or configuration change
updates the build prerequisite before archives are produced.

Each archive member carries that identity in a nonallocated ELF section. Export
checks the member inventory and identity, checks source hashes against the build
record, and copies the corresponding headers. An old member cannot pass by
being inserted into a newly timestamped archive. The SDK manifest records hashes
of exported files and the member inventory. Re-exporting the same build produces
the same file inventory and hashes. Debug-bearing archives can retain build-path
information; relocation does not require those source paths to exist.

The installed checker verifies file inventory, checksums, build identity,
required capabilities and compiler target without reading the source checkout.
The generated header checks machine ABI macros, enum width and structure packing.
It retains a reference to a revision-specific archive symbol, so linking headers
against a different revision fails even under section garbage collection or LTO.
The reference uses the compiler's `retain` attribute. Default enum and structure
layout are required; the host profile uses its compiler's default machine ABI.

GNU LTO builds use fat objects so the exporter can inspect object provenance.
An LTO SDK requires its recorded compiler version. The qualification runner roots
all exported symbols for the complete-profile link; LTO cannot omit an unused
export and conceal its runtime dependencies. The selected-example link separately
uses section garbage collection and reports the symbols it retains.

## Runtime checks and measurements

`check-sdk` copies the SDK and example/provider source into a relocated directory.
Its compiler commands use the copied files, exported headers and archives; no
foundation source include paths are supplied. The fixture exercises an 800×480
panel, delayed presentation, paged browsing, shutdown during a read and the
shared word calculator. A separate host scene must match the desktop pixel
reference. The S31 counterpart is compiled and linked without execution.

The runner records selected-example and complete-profile maps, section sizes,
compiler arithmetic helpers and symbol inventories. It checks defined as well
as unresolved symbols for heap and native-service dependencies. In the S31 link,
this includes statically linked libc and compiler helpers. The host's dynamically
linked libc internals are outside that inspection.

Portable tabs copy labels with bounded byte truncation. Portable stepper painting
formats integers with a bounded decimal conversion. These paths avoid pulling a
formatted-I/O runtime into the SDK; the desktop implementations keep their
formatting calls. Fixtures compare truncation, zero-capacity buffers, signed
extremes and ordinary values against libc formatting, and remove guards to check
that the fixtures fail.

Section totals describe the standalone consumer and runtime. Its fixed arena and
RGB565 output are test-provider reservations, not foundation static RAM. Product
linker placement, board stack high water, cache ownership and combined radio,
media and display load require downstream qualification.
