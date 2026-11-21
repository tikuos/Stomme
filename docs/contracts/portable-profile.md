# Portable control profile

`FOUNDATION_PROFILE=portable-ui` selects bounded Interface models, allocator and queue services, and
frozen font data. The default remains `desktop`. The portable profile includes local control drawing and presentation contracts.
It includes static application lifecycle, bounded storage requests and shared
integer calculator logic. Caller-owned window workspaces and shared Deskbar
painting provide the desktop composition. Native allocation and process
adapters are excluded from the portable archives.
`profiles/portable-ui.json` declares the selected sources, required metrics
provider and C runtime symbols. Existing model headers and layouts are retained.

Build and exercise the Linux host probe:

```sh
make FOUNDATION_PROFILE=portable-ui test test-tools
```

Cross-compile the models and data for the S31 ABI without a firmware checkout:

```sh
make FOUNDATION_PROFILE=portable-ui FOUNDATION_TARGET=s31 \
    CROSS_COMPILE=/path/to/riscv32-esp-elf-
```

The S31 target uses `-march=rv32imafc_zicsr_zifencei -mabi=ilp32f`.
It builds archives and a freestanding service link probe, then inspects their
dependencies. `check-sdk` exports the SDK and links relocated consumers without
running target code. `host-test` requires a Linux compiler. Native macOS desktop
selection remains in the desktop build.

Outputs default to `build/portable-ui-<target>`. Use separate `BUILD`
directories for concurrent configurations. `profile.json` records the target,
compiler, flags and source selection. Portable configuration performs no
`uname`, X11, GIO, Cocoa or pthread discovery. A desktop host or document
provider supplied to this profile is rejected.

Application and storage ownership is defined in
[portable-applications.md](portable-applications.md).

The control archive requires the `tiku_measure_*` provider listed in its
manifest. The host probe supplies fixed metrics and links the complete control
archive. Text-entry fields use caller-provided clipboard operations. The document parser is excluded.
The heap-owning `tiku_textview_t` is also excluded. Text-input staging and the
selected control models operate on caller-owned records.

## Font data

`profiles/portable-fonts.json` selects plain and bold UI faces at 12 px from the
checked-in desktop header. Selection is printable ASCII (32–126) and Latin-1
(160–255), with `?` as the replacement glyph. Symbols outside these ranges
use that glyph. No outline readers are linked. A consumer that needs other
widget symbols must declare an expanded font selection or draw vector icons.

`tools/subset_fonts.py` copies glyph metrics and coverage bytes and repacks
coverage offsets. It verifies the source hash and declared byte counts.
`fonts.json` records the manifest, source, generator and output hashes, and the
output directory carries `LICENSE.DejaVu`. The desktop header is not rewritten.
Generation needs Python's standard library; installed fonts are not consulted.

The two faces contain 382 records and 41,536 coverage bytes. Records occupy
6,112 bytes, for 47,648 bytes before descriptors and alignment. Generated data
is compiled as `tiku_font_subset_data.o`, separate from lookup code, so a
product linker script can place it in its selected flash region.

`tiku_font_subset_face()` accepts face 0 or 1 and size 12; other requests return
NULL. `tiku_font_subset_glyph()` uses `?` for missing codepoints and returns
NULL for an invalid face. Font pointers and glyph data are immutable and have
program lifetime. Existing desktop font APIs keep their source selection.

## Dependency checks

`tools/check_portable.py` checks the selected include closure and object
symbols against the manifest. It rejects direct heap and native service
references and unselected font tables. The host probe check also examines
symbol definitions, including those resolved from helper archives.
`dependencies.json`, `link-dependencies.json` and the probe link map record
the inspected closure. The tests include direct and resolved indirect heap
calls, missing metrics, native dependencies, extra fonts and corrupted glyphs.

Object checks describe the selected sources and their direct dependencies.
The [portable services](portable-services.md) supply an injected allocator and
a heap-trapped workload. The [portable SDK](portable-sdk.md) adds relocated
consumers and complete S31 links that inspect static libc and compiler helpers.
These links do not establish hardware execution.

The [rendering contract](portable-rendering.md) describes scale-one drawing,
framebuffer ownership, the delayed memory presenter and the panel example.
