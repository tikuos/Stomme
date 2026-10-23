# HTML document display

Interface Kit exposes HTML reading and layout through `tiku_doc.h` and drawing
through `tiku_doc_paint.h`. Reader and Help provide local document loading,
navigation, search, selection, bookmarks and image resources. Translation Kit's
`tiku_richmarkup_read_html` imports editable documents through a separate API.

## Parsing and structure

Element and attribute names ignore ASCII case. Quoted attribute values retain
`>` characters. Token whitespace includes space, tab, line feed, carriage return
and form feed. The first matching attribute supplies its value. Values are
bounded by document storage; exceeding that storage sets `TIKU_DOC_FULL_TEXT`.

Paragraphs, headings H1 through H6, unordered and ordered lists, definition
lists, preformatted blocks, quotations, horizontal rules, links, images and
table cells have document representations. `div`, `section`, `article`,
`header`, `footer`, `main`, `nav`, `aside`, `figure` and `figcaption` separate
blocks. Figures retain a bounded container range shared by their images and caption blocks.
Definition terms use bold text; descriptions indent their paragraphs.
Ordered lists accept signed decimal `start` and item `value` attributes.
Invalid or out-of-range numbers use the default. Numbering saturates at
`INT32_MAX`; it does not overflow.

`tiku_doc_heading_level` returns levels one through six. Levels four through
six use the existing level-three presentation. Reader's outline preserves
the source level for nesting and folding. The first heading supplies the
page title through its first text run.

Nonempty IDs on parsed elements target their current block, or the next block
when no block is active. Images and rules retain their own targets. Destinations
without a following block are dropped. Anchor resolution selects the first
matching ID and scrolls to the block; it does not expose an inline caret position.

Entities `amp`, `lt`, `gt`, `quot`, `apos` and `nbsp`, plus valid decimal and
hexadecimal numeric references with semicolons, are decoded within a ten-byte
reference-name limit. `nbsp` becomes an
ordinary space. Unknown names, zero, surrogate code points and out-of-range
numeric references retain their literal spelling. Preformatted content retains
whitespace; code and preformatted text still decode character references.

Comments and declarations are omitted. `head`, `title`, `script` and `style`
content is skipped through its matching closing name, ignoring ASCII case.
Unknown elements retain their text. A less-than character without a valid tag
start remains text. An unquoted new `<` ends an incomplete token; an
unterminated quoted token consumes the remaining input. Malformed nesting uses
bounded recovery and does not implement HTML5 tree construction.

## Storage and resources

The default parser uses caller-owned inline tables without heap allocation.
`tiku_doc_init_storage` accepts larger caller-owned tables. Capacity flags
report text, structure, nesting and layout limits. `tiku_doc_layout`, drawing
and hit testing share the document's line and piece geometry.

Image sizing and drawing use callbacks. Interface Kit does not read files,
decode images or fetch resources. Reader's local-resource rules and size limits
are described in [the Reader guide](../guides/tracker/document-reader.md).
Scripts do not execute. Image resources expose intrinsic dimensions through the
callback; document layout applies HTML/CSS sizing and fits the containing width.

Tables support bounded columns, column spans, row spans and caption blocks.
Nested tables remain outside the supported layout contract. Form submission, network browsing,
plugins and downloaded fonts are outside the display API.

## Text styles

Embedded stylesheets are collected before text parsing. Element, class, ID,
universal and compound selectors, plus comma-separated selector lists, match
elements. Element and property names ignore ASCII case; IDs and classes are
case-sensitive. Classes match complete whitespace-separated names. CSS escaped
identifiers, combinators, attribute selectors and pseudo-classes are unsupported.
Rules inside scripts, HTML comments and title text are not collected.

Text properties include `color`, `text-decoration`, `text-decoration-line`,
`font-family`, `font-size`, `font-weight`, `font-style`, `text-align`,
`line-height` and `white-space`. Colours accept `#rgb`, `#rrggbb` and the names black,
white, red, green, blue, yellow, gray, grey, orange, purple, lime, aqua, teal,
navy, maroon, silver, fuchsia and olive. Colour values `inherit` and `unset`
retain the parent foreground; `initial` uses the theme foreground.
Decorations accept underline, line-through, both together, or none. Ancestor
decorations continue across child text. CSS can remove a link
underline while retaining its keyboard focus frame. Elements `u`, `s`,
`strike` and `del` supply text decorations without CSS.

Precedence resolves each property by importance, specificity and source order.
Inline declarations have greater specificity than supported selectors.
`!important` is supported for these properties. Foregrounds inherit through
containers and restore at closing tags. Omitted paragraph, item, definition
item and heading closes restore their parent style at the next matching block.
Painted decorations and foregrounds use the existing piece geometry.

`tiku_doc_notices` reports unsupported CSS, syntax recovery and CSS resource
limits separately from document capacity flags. Unsupported declarations and
at-rules are omitted while text remains available. Each read allows 64 selector
rules and 32 KiB of combined stylesheet and inline declaration source by
default, and at most 1,048,576 rule-match checks. Capacity macros accept 0 through
256 rules and 1 through 1,048,576 source bytes. Zero selector-rule capacity
retains inline declarations. These buffers remain
fixed and the parser allocates no heap storage. Public run/document structures
include style metadata; consumers rebuild against the same headers.

Inline CSS decodes the supported HTML character references before tokenizing
property names, values, quotes and declaration separators. Decoding streams
through a 512-byte declaration buffer; oversized declarations set the CSS limit
notice and following declarations remain eligible. The source-byte budget counts
the original attribute bytes. Stylesheet text retains HTML raw-text semantics.

External stylesheets, colour functions, alpha colours and unsupported decoration
values report conversion notices.

## Document fonts

Font families accept `system-ui` for the current desktop family/source, `Sans`
and `sans-serif`, `Serif` and `serif`, and `Mono` and `monospace`. Fallback lists
select the first recognized available family, including quoted names. An
unavailable list reports unsupported conversion and retains the inherited font.
Documents do not select a global desktop family or load font files.

Sizes accept decimal `px`, `em`, `rem` and percent values. Absolute values range
from 1 through 512 px. Relative factors range from 1/64 through 64; nesting
bounds the root-size factor at 64 and the pixel offset at 512 px. Decimal input
retains four fractional digits. `inherit` and `unset` retain the parent's
computed value; `initial` and `medium` use the Reader's body size. Logical
root-size factors and pixel offsets are stored in runs, then resolved during
layout. Parsing and text-only search do not access desktop font state.

The proportional size ladder is 10, 12, 14, 16, 20, 24, 28 and 32 px. The larger
sizes use the existing high-resolution baked faces as logical fonts. Monospace
uses odd sizes from 9 through 23 px and their doubled sizes from 18 through
46 px. Requests round to an integer pixel size, then use the largest available
size no greater than that request, or the smallest available size below the
ladder. Weights 100 through 500 map to regular; 600 through 900 map to bold.
`normal`, `bold`, `bolder` and `lighter` select those available weights. Monospace
supports italic and bold italic; proportional italic/oblique uses the upright
cut. `TIKU_DOC_FONT_FALLBACK` reports size/cut approximation and is recomputed
on reflow. CSS conversion notices remain attached to the parsed document.

Semantic heading, bold, emphasis, code and definition defaults apply before
an element's CSS declarations. Explicit CSS overrides those defaults. Runs,
list markers, painting, link hits and Reader selection share the resolved face
and its metrics. Layout tracks font family/source changes while temporary
Reader size changes retain the font configuration token. Public measurement
handles are distinct from the face IDs chosen by the display-list recorder;
faces it cannot encode use the existing pixel fallback.

Displayless callers keep fixed-capacity, allocation-free parsing. Their
measurement table supplies the available plain, bold or mono face, and styled
font requests report a font fallback. Public document/run layouts include font
metadata and a wider face handle; applications and SDK consumers rebuild
against the same headers.

## Text flow

`text-align` accepts left, center, right, start and end, plus inherit, unset
and initial. Start and end map to left and right in this left-to-right flow
profile. Justified and bidirectional paragraph layout are unsupported. Alignment
inherits into blocks and table cells. Decoded images contribute their measured
width to alignment. Each line uses its available width after
indentation; overflowing lines retain their starting position. Inline alignment
does not change the enclosing paragraph. Painting, hit testing, selection and
link focus use the aligned line and piece coordinates.

`line-height` accepts normal, a positive unitless factor, px, em, rem and percent,
plus inherit, unset and initial. Unitless factors inherit as factors of each
descendant's logical font size. Em and percent lengths compute against the
element's logical font size, then inherit as lengths. Rem lengths use the
Reader's body size. The numeric limits and precision match font-size values.
Logical length expressions retain the root-size factor and pixel offset, so
Reader size changes recompute their advance. A block's own line height supplies
a minimum for its text lines, including empty preformatted lines; inline runs can
increase that height. Extra leading is split above and below the glyphs.

Lines retain at least their glyph ascent and descent and at most 4096 pixels
of requested height per run. A request below those metrics or above that bound
sets `TIKU_DOC_FLOW_FALLBACK`. Mixed font ascents and descents can increase the
line's total height. This notice is recomputed on reflow. Overlapping line boxes
are outside the supported profile.

Vertical layout has a `TIKU_DOC_HEIGHT_MAX` coordinate budget of `INT32_MAX/4`,
with 16384 pixels reserved before that boundary for trailing space and a
capacity notice. Reaching the limit sets `TIKU_DOC_FULL_HEIGHT`, stops further
text layout and retains parsed text. `tiku_doc_full_first` reports layout height
as the limiting resource. A size change recomputes the limit and can expose more
of a document with relative line heights.

`white-space` accepts normal, pre-wrap, pre and nowrap, plus inherit, unset
and initial. Normal and nowrap collapse HTML whitespace; nowrap keeps its runs
on the current line. Pre and pre-wrap retain spaces and newlines, normalize
carriage-return sequences to line feeds and expand each tab to four spaces.
Pre-wrap fits words and preserved spaces within the content width and splits
overlong words between code points. Pre retains explicit line breaks and does
not split words. Preserved trailing spaces contribute to geometry and selection.
Unwrapped lines retain their text and width; drawing clips to the viewport.
The view has no horizontal scrollbar. Preformatted elements default to pre-wrap
and monospace; CSS can override their whitespace policy independently of font.

## Block boxes and backgrounds

Block containers and styled block elements retain descendant ranges and physical
border boxes. Backgrounds paint before text and remain visible when the first
line of a container has scrolled above the viewport. Inline colour backgrounds
paint under their pieces before selection and text. `background` accepts a
colour or transparent; background images, gradients and alpha colours are
unsupported.

Margins and padding accept one through four values and physical side properties.
Lengths accept nonnegative px through 4096, em/rem factors through 64 and percent
through 100, with four fractional digits. Zero may omit its unit. Width and
max-width accept those units; width accepts auto and max-width accepts none.
Explicit inherit and initial/unset values resolve per property. Box values do
not inherit by default. Em uses the element font; rem uses the Reader body size;
percent uses the containing content width. Automatic left/right margins consume
remaining width, with two automatic margins centering a constrained box. Margins
add between boxes; this profile does not collapse margins or accept negative
lengths. Oversized resolved edges and widths fit the available width and report
flow approximation.

Borders accept a uniform width, solid/none style and RGB colour, with border,
border-width, border-style and border-color declarations. The default width is
3 px; the default colour follows the foreground. Per-side borders, rounded
corners and other line styles are unsupported. Inline box padding, borders and
width are unsupported; inline backgrounds remain available. Image sizing uses
its own dimension contract; image margins, padding and borders report unsupported
conversion. A decorated figure can enclose an image.

Display accepts block, inline and none. Block gives an inline element a container;
inline removes paragraph/container/list flow boundaries. Native headings, table
structure, images and rules retain their block representation for an inline
request and report unsupported conversion. None omits the subtree's text, links,
anchors and image resources. Scopes beyond the fixed nesting budget retain
inherited visibility and report the depth limit.

## Image resources and captions

Unsigned HTML width/height attributes accept 1 through 4096 pixels. CSS width,
max-width and height override those hints, including explicit auto values.
Width and max-width accept the box units; height accepts px/em/rem. A single
dimension scales the intrinsic aspect ratio; two dimensions form an
aspect-preserving bounding box. Explicit sizes can enlarge an image. Content
width and max-width then constrain it. The layout accepts positive intrinsic
dimensions up to 32768 each and caps displayed height at 4096 pixels, reporting
flow approximation for refused or capped dimensions. Missing/refused resources
retain alternate text. Figures retain image and caption parent relationships;
captions remain separate text blocks.

Translation Kit exposes `tiku_image_data_png_size` for allocation-free header
inspection and `tiku_image_data_png` for decoding. Both accept an explicit byte
length, a base64 PNG MIME prefix, whitespace and canonical padded base64. They
bound the URI at 1 MiB, dimensions at 2048 each and decoded pixels at one
megapixel. Decoding failure leaves caller output untouched. Neither entry point
reads a file or opens a network resource. Reader supports these embedded PNGs
alongside confined local images. Its 1 MiB image-metadata budget includes owned
source references and its 16 MiB pixel cache evicts older decoded images.
Transparent pixels blend over the already painted document background.

## Tables

Tables retain separate container ranges and captions. Cells share column
measurements across the table. Content widths, padding, uniform borders and
cell width preferences contribute to column sizing; width preferences share
remaining space with other columns and can shrink at narrow widths. Cells
paint backgrounds, borders and inherited row backgrounds over their full box.
Their text alignment and line height use the inner padded content interval.
The default row rule spans the table's content width; an explicit none/zero
border suppresses it.

Column spans reserve bounded columns. Row spans accept 1 through 64 rows and
reserve covered columns in following rows. Layout shares a spanning cell's
height across its rows and clips its span to the table's row range. Unsupported
span values and occupancy recovery set `TIKU_DOC_TABLE_FALLBACK`. Excess columns
retain the existing folded-column flag. Cell text wraps within its content box;
pre/nowrap cell requests use wrapping with a table conversion notice. At widths
below the column minimum, cells stack vertically with a table notice. A glyph
wider than the remaining content interval is clipped to that interval. Text
and links remain available through the document model.

## Owned models and reusable view

`tiku_html_document.h` owns bounded model loading and transactional layout-table
growth. Its default limits are 8 MiB of source and 64 MiB of model storage,
including inline tables. Optional parse adapters support application format
conversion without importing an application into Interface Kit. Custom
allocate/zero/release callbacks form a complete matched allocator. A failed
replacement/growth retains the previous model. Reflow copies font, flow, box,
image, table, anchor and conversion metadata while preserving indices.

`tiku_html_view.h` loads HTML from memory and accepts an optional default
stylesheet. Input strings are copied into computed model data and need not
outlive loading. Owned views allocate their model, flattened UTF-8 text and run
mapping within the model-memory budget; transactional replacement temporarily
retains the old page. `tiku_html_view_bind` borrows those records from a host and
releases none of them. Borrowed buffers and a supplied callback context must
outlive the binding. The fixed-capacity `tiku_doc` parser remains allocation-free.

The view supplies resize/reflow, clamped vertical scrolling, pointer selection,
copy, ASCII-case-insensitive find with wrap/reveal, keyboard link traversal and
activation, and image/link callbacks. The host routes keyboard events to the
focused view and may provide its own scrollbar and find controls. Anchor links
scroll within the document; non-anchor links are delivered to the host callback.
The view performs no resource I/O. Image callbacks supply intrinsic dimensions
and draw into the host's current surface. A standalone example is in
`examples/html-view/`; it builds against a relocated SDK without Reader sources.

Selection uses the theme's selection background and text ink. Text segments
are drawn once at their measured boundaries, including partial selections on
explicit HTML foreground/background colours. Find highlights retain the current
match frame. Shared text helpers select the pointed table column rather than
the first line at that vertical coordinate. Reader, Help and generated reports
use the shared model, view painting and text helpers while retaining their
navigation, search, outline, localization and bookmark controllers.

## Checks

`test_doc_html` covers token boundaries, skipped-content boundaries, attributes,
entities, containers, headings, definitions, numbering, anchors, truncated inputs
and the authored fixtures in `tests/fixtures/html/`. It runs under ASan/UBSan
with leak checks through `check-sanitize`. `test_help_reader` checks six-level
outline folding, search and anchor navigation alongside the existing Help
workflow. `test_doc`, `test_doc_large`, `test_doc_paint`, Markdown and Reader
suites cover the existing layout and application contracts.
`test_doc_css` checks the text cascade, inheritance, decoration pixels, limits
and truncated buffers normally and under ASan/UBSan. `test_reader_allocations`
checks that reflow growth retains style metadata and conversion notices.

`test_doc_fonts` checks CSS font metrics, relative units, inheritance, heading
and marker overrides, narrow reflow, links and a pixel comparison against the
selected face. The font provider and document pipeline run under ASan/UBSan.
Help also checks pointer drag/copy at styled Serif and monospace positions.
`test_doc_flow` checks inherited alignment, line-height units, block minima,
empty lines, whitespace modes, narrow reflow, truncated input, link hits and
pixel placement normally and under ASan/UBSan. Help checks drag/copy on centered
and right-aligned text with extra line spacing. Allocation-failure checks retain
run whitespace and block alignment/line-height metadata after reflow growth.

`test_doc_boxes` covers nested range geometry, spacing, width constraints,
background/border pixels, scrolled backgrounds, display, nesting limits and
every prefix of styled input. `test_doc_images` covers dimension precedence,
aspect ratio, narrow containers, captions and refused callbacks.
`test_image_data` covers authored PNG pixels, MIME/padding validation, truncated
byte ranges, encoded limits and output preservation. Reader checks embedded PNG
loading, long embedded references and alternate text for refused formats.
`test_doc_tables` covers cell boxes, row/column spans, captions, width preferences,
row styles, adjacent tables, link hits and constrained painting.

`test_html_view` checks public ownership, allocation failures, transactional
replacement, table selection/copy, link callbacks, find, scrolling, borrowed
lifetimes and selection pixels in light, dark and monochrome themes. Its 72
family/theme/size/width combinations check 2x surface geometry and link hits;
native font-source changes invalidate cached layout.
`tests/integration/run_html_sdk.py` builds the standalone example and module
against a relocated source-free SDK, loads its ABI descriptor and runs its
headless paint/find/resource smoke. Native macOS execution is a separate host
check; Linux evidence does not establish a native macOS result.
