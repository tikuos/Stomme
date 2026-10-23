# HTML View Contract

Interface Kit supplies a bounded offline HTML parser, layout model and reusable
view. Translation Kit supplies image decoding, including embedded PNG data
URIs. Applications provide resource resolution, navigation policy and external
link handling. The view performs no resource networking or script execution.

## Public interfaces

| Header | Responsibility |
| --- | --- |
| `tiku_doc.h` | Fixed-capacity parsing and layout, semantic blocks, conversion notices and resource callbacks |
| `tiku_doc_paint.h` | Painting parsed layout through supplied drawing callbacks |
| `tiku_html_document.h` | Owned source/model loading, growth budgets and transactional reflow |
| `tiku_html_text.h` | Visible text, measured selection geometry and lookup helpers |
| `tiku_html_view.h` | Owned/borrowed documents, pointer and keyboard input, scrolling, selection and find |
| `tiku_image.h` | Bounded embedded PNG inspection and decoding |

The supported profile includes headings, containers, paragraphs, lists, links,
anchors, figures and captions, embedded/local image callbacks, table cells with
row/column spans, inline styles and bounded embedded stylesheets. Styles include
font selection, text color/decorations, alignment, line height, whitespace,
backgrounds, margins, padding, borders and width constraints.

Conversion notices report capacity limits and unsupported or approximated
features. The parser remains bounded; this profile does not implement JavaScript,
external stylesheets, Flexbox, Grid, floats, positioned layout or nested tables.
Host applications decide which resource references and link targets to accept.

## Ownership and rendering

The fixed-capacity parser allocates no memory. The owned model and view use
explicit budgets and optional matched allocation callbacks. Failed replacement
or reflow retains the preceding usable model. Borrowed model lifetime remains
the caller's responsibility.

Text measurements and painting use the same resolved font face. Selection and
link geometry come from layout. The view supports light, dark and monochrome
themes and high-density surfaces. Resource callbacks inspect intrinsic image
dimensions and paint within the layout's fitted destination.

Displayless callers supply metrics and compile the portable model without
native rendering. `make displayless` also checks a zero-stylesheet-rule profile
with inline styles and conversion notices.

## Verification

`make test` runs the HTML parser, CSS, font, flow, box, image, table, ownership,
selection and paint suites. `tests/integration/run_html_sdk.py` builds the
exported example as a standalone process and loadable module, checks its ABI
descriptor and runs the headless paint/find/resource smoke. Its `--sdk` option
selects an exported SDK outside the source tree.
