# Stomme

Shared native desktop libraries for graphics, controls, text, storage, codecs
and application processes. This checkout builds its libraries and SDK without
product application, shell or firmware source. TikuDesktop and KintsugiOS
select shared sources from this repository through explicit dependency pins.

The base SDK contains Support, Device, Interface, Storage, Translation,
Application, Media and Editor kits. Document and presentation providers are
separate packages; they are absent from the base SDK. Existing public `tiku_`
C names and application protocol identifiers are retained.

## Contribution rules

Stomme follows the TikuOS comment, prose and commit conventions. Read
[AGENTS.md](AGENTS.md), [comment-style.md](comment-style.md),
[prose-style.md](prose-style.md) and [the commit guide](hygiene/commit-style.md)
before changing source. Install the repository's commit hook after cloning:

```sh
git clone https://github.com/tikuos/Stomme.git
cd Stomme
sh hygiene/install_hooks.sh
```

The hook checks commit message shape and the configured author identity.
`make check test-tools` checks tracked comments, prose, dependencies, fonts,
build tools and commit hygiene. Stage new files before tracked-source checks.

## Build and validate

Linux builds require a C toolchain, Make, Python 3, pkg-config, X11 headers and
GIO development libraries. Darwin selects the Cocoa backend and native
frameworks; native Darwin execution is not established by Linux results.

```sh
make -j8
make check test-tools
make -j8 tests displayless check-link
make test
python3 tools/check_sdk.py build/sdk
python3 tests/integration/run_html_sdk.py --sdk build/sdk
```

`make` builds ordinary and position-independent archives and exports `build/sdk`.
`check-link` links the complete archive set to detect unresolved dependencies.
`displayless` compiles the portable models and runs document layout using
caller-supplied metrics, with no renderer or firmware checkout.

Use `BUILD=/path/to/output` to keep generated files outside the source tree.
The build records compiler flags and host selection and rebuilds objects when
that configuration changes. Each Make graph must have its own output directory.
`make clean` removes a directory carrying this project's build marker.

The font coverage data is checked in. Normal builds do not require Pillow or
rerendering fonts. Some regression tests require an installed TrueType face;
set `TIKU_TEST_FONT` to its readable file path when automatic lookup fails.

## Use the exported SDK

The SDK contains public headers, PIC archives, notices, `sdk.mk`, pkg-config
metadata and a provider manifest. It contains no implementation sources or
private headers. Copy it to an independent location and build the example:

```sh
make sdk
make -C examples/html-view SDK=/absolute/path/to/build/sdk
make -C examples/html-view SDK=/absolute/path/to/build/sdk smoke
```

The smoke entry paints an HTML page, checks selection/find and image resources,
and writes `examples/html-view/build/preview.png` without a running desktop.
The normal entry connects to a compatible desktop host. See the
[HTML view contract](docs/contracts/html.md) and [SDK contract](docs/contracts/sdk.md).

`make install-sdk SDK_PREFIX=/absolute/destination` copies an exported SDK to
a marked destination. SDK export and installation refuse to replace an
unrecognized directory or an SDK from another provider.

## Source ownership and configuration

`kits/` owns libraries and their public headers. `resources/` contains default
catalogs and sounds. Override `STRINGS_INPUT` and `SCORES_DIR` to select product
resources. `examples/` and `tests/` consume the public kit interfaces; they do
not provide application or desktop implementations to the libraries.

The include graph is checked by `tools/check_dependencies.py`. Shared kit code
cannot include product, document-provider or presentation-provider source.
Native host services stay behind their Interface Kit contracts.

## Provenance and notices

[provenance/sources.json](provenance/sources.json) records original repository
revisions, source paths and hashes. Original authorship and SPDX headers remain
in extracted source. Upstream product guides are retained under
`provenance/upstream-docs/`; their commands describe the original product trees.
Current standalone commands are documented here.

The SDK redistributes the applicable notices under `licenses/`, including
notices for baked font coverage. Kit-specific notices remain beside their
sources, including [the Editor Kit notice](kits/editor/LICENSE.Scintilla).

## Product adoption

Product builds set `FOUNDATION_ROOT` to this checkout and record its content
revision in `foundation.lock.json`. `tools/product_bridge.py` checks that pin,
creates a symlink source view, and selects shared kits, application packages and
shell sources with declared product providers and overrides. Product strings, sounds and icon registration
remain product inputs. `FOUNDATION_ALLOW_DIRTY=1` records an explicit local
source override in development metadata.

Run the adoption matrix with explicit checkout and result paths:

```sh
python3 tools/check_products.py --product /path/to/first-product \
    --product /path/to/second-product --output /path/to/results
```

The matrix builds each product serially, checks the recorded foundation
revision, runs the available desktop adoption suites, builds the relocated HTML
example and exercises the selected desktop scripts. It clears board-port
selection variables. Native macOS execution requires a macOS host.

The Darwin print backend imports Cocoa and PDFKit. Include those frameworks in
the standalone build's linker flags:

```sh
make LDFLAGS='-framework Cocoa -framework PDFKit'
```

## Document packages

The default build exports eight base kits with no document model or document
format adapters. `make DOCUMENT_PROVIDER=tiku-basic BUILD=build-basic` adds
the basic Document Kit and its matching Document Formats archive. Keep the
output directories separate when comparing configurations.

Document Formats supplies native persistence, HTML and Markdown conversion,
clipboard data, font shaping and PDF output. Its headers and model archives
are exported together. Kintsugi supplies its advanced document and presentation
providers from its product tree. Product profiles select one document provider;
Deck requires the advanced provider.

The base pkg-config descriptor links base kits. `tiku-document.pc` and
`tiku-document_formats.pc` select optional model and adapter dependencies.
Application catalogs name the kits their modules use. A missing required kit
is an error before compilation.

## Application source packages

`packages/apps` contains matching app implementations, Editor common files and
application tests. Products select packages in `foundation.product.json` and
supply their manifests, icons and application configuration. Both the desktop
build and the SDK-only application build compose those sources through
`tools/application_packages.py`. Exported app sources contain their required
common app libraries and product configuration and build without a foundation
checkout. Board apps and advanced document applications remain product-owned.

The optional [desktop shell package](packages/shell/README.md) supplies the common
file manager, Deskbar, runner and supervisor. Products select shell services and
provide identity, resource paths and defaults through their configuration header.

## Source maintenance

Products retain their configurations, resources, application catalogs and
provider implementations. Migrated implementations and matching tests live in
this repository. Edit shared files here and run `make check test-tools` before
updating a product pin. The content revision is printed by:

```sh
python3 tools/product_bridge.py --hash
```

Set that value as `source_sha256` in each adopting `foundation.lock.json` and
rebuild its desktop, SDK and applications. SDK identities change with shared
inputs, so rebuild detached applications against the new SDK as well.

Product builds generate `build/foundation/`. Its source, resource, application
and shell inventories record each selected file's owner. The build rejects a
migrated copy restored inside a product, an undeclared application dependency,
an unselected provider, or a kit include that reaches product source. Product
build rules consume the generated source lists; shared source lists are not
maintained again inside the product.

The font comparison tool runs from the checkout whose `build/tools/glyphdump`
it uses. Build that development executable with `make build/tools/glyphdump`.
It is excluded from the runtime archives. Regenerate shared font or display-list
inputs in this checkout and update the product pins after checking the results.

The [maintenance contract](docs/contracts/maintenance.md) defines shared-change
review, downstream qualification, release records and dependency rollback.
