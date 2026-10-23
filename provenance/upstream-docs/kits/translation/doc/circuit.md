# TikuCircuit: schematic, drill and artwork preview

The circuit kit reads a **subset of KiCad `.kicad_sch` and EAGLE XML `.sch`
schematics, including multiple sheets**, **decimal-coordinate XNC/Excellon
drill drawings**, **Gerber layer stacks** and **Gerber jobs** into immutable
scenes, and draws them. Each reader supports the subset documented here; none
of them is a complete KiCad, EAGLE or Gerber viewer.

The TikuCircuit viewer built on the kit is described in
[apps/circuit/README.md](../../../apps/circuit/README.md).

## KiCad schematics

| Content | Current behaviour |
| --- | --- |
| Embedded symbols | Resolve `lib_id` or a `lib_name` override; reject missing or duplicate matching definitions |
| Units and body styles | Draw the selected unit/style and shared unit-zero/style-zero content |
| Symbol placement | Cardinal rotations and sheet-axis mirroring, with translation |
| Symbol properties | Sheet-space positions, symbol-relative direction, upright text and reflected alignment; DNP parts have a grey body and red cross |
| Pins | Line, inverted, clock and inverted-clock styles; supported name/number text, the name above and the number below a horizontal pin when the name offset is zero; hidden pins and library-level label visibility |
| Connections | Wires, buses, bus entries, junctions (with their own opaque colour) and no-connect markers |
| Graphics | Polylines, rectangles, circles, unfilled three-point circular arcs, solid strokes with their own opaque colour, and supported fills |
| Text | Local labels, ordinary text, visible instance properties, rotation and justification as KiCad 7 draws them, synthetic bold/italic |
| Global labels | Input, output, bidirectional, tri-state and passive tags; cardinal rotation; hidden intersheet references; literal visible properties |
| Hierarchy | Sheet outlines, name/file properties, pins and hierarchical labels; separate scenes and references for repeated child instances |
| Navigation | Native sheet list, previous/next sheet commands and search across loaded sheets |
| Inspection | Wrapped, scrollable component/pin details; copy all details; reference, value, library, unit/style, placement, DNP, BOM/PCB flags, footprint and datasheet text |
| Metadata | Known non-geometric fields, including UUIDs, instance bookkeeping and page information |

Unknown drawing features are **errors, not silently skipped objects**.
Unsupported features include coloured sheet backgrounds, coloured fills,
translucent colours, text colours, symbol properties and global or
hierarchical labels turned other than by right angles, images, text boxes,
inherited symbols, unsupported pin styles, alternate-pin selections, dashed
strokes, partial-arc fills, visible text variables, overbars,
superscripts/subscripts and multiline global tags.
Legacy KiCad `.sch` is not implemented. The project loader
refuses legacy root-level `symbol_instances` tables; save the project with
KiCad 7 or later before trying this preview.

The engine cannot open files itself. For linked sheets it calls a resolver
supplied by the application. That resolver permits regular files within the
selected root schematic's directory tree. It does not follow external library
paths, network resources or scripts. Unsupported content in a child makes that
sheet unavailable; unsupported content in the root refuses the entire import.

### Fidelity and evidence

Every imported schematic carries a persistent warning: text uses a substitute
system outline font, and the page frame is omitted. An object the file gives
no colour of its own is drawn in the viewer's theme. An opaque colour the file
gives a stroke, a junction or a sheet outline is drawn as given, as KiCad
draws it; the colours this preview cannot draw faithfully, listed above, are
refused. Text is turned as KiCad 7.0.11's SVG export turns it. Free text and
local labels read upward when the whole degrees of their angle are 90 or 270,
and left to right at any other angle, 180, -90 and 45 included, each with its
stored alignment. A symbol's own text is centred on the box its angle turns it
into. It reads across the sheet when the symbol is upright or upside down and
the text's angle is zero, or when the symbol is turned a quarter and the angle
is anything else, and upward otherwise. Text metrics and pin-label spacing are
approximations, not pixel-identical KiCad rendering. Missing glyphs cause
refusal, not omission. Font paths in a document are never opened.
Global tag widths enclose the substitute font's text, so their lengths can
differ from KiCad's. DNP crosses use visible body/pin extents, excluding pin
annotations and instance properties; curved-shape bounds remain conservative.
Hidden `${INTERSHEET_REFS}` properties are accepted. Visible generated
intersheet references are refused because project text-variable expansion is
not implemented. Footprint and datasheet metadata are plain text, never file
paths or links to follow.

The parser accepts modern revisions from `20211123`, but this is not a claim
of complete version compatibility. The authored fixtures use `20230121`
and were independently accepted by **KiCad CLI 7.0.11**. Its SVG export supplies
fixed expected coordinates for normal, rotated and mirrored asymmetric
symbols, hierarchy markers, repeated child units/references, global tag
directions, DNP crosses and property alignment under all cardinal rotations
and both mirror axes. Other accepted revisions display an additional untested-version
warning. No KiCad 6/8/9/10 corpus has been qualified.

See [fixtures and oracle coordinates](../../../tests/fixtures/circuit/README.md).
KiCad is a development comparison tool, not a runtime or test dependency. The
decoder was written from the public [schematic specification](https://dev-docs.kicad.org/en/file-formats/sexpr-schematic/),
[common syntax](https://dev-docs.kicad.org/en/file-formats/sexpr-intro/) and
[symbol-library specification](https://dev-docs.kicad.org/en/file-formats/sexpr-symbol-lib/).
The authored fixture contains no third-party symbol library or customer design.

This preview is for inspection, not electrical or manufacturing verification.

## EAGLE XML schematics

The importer resolves each part through its embedded library, deviceset, gate
and symbol, and makes each sheet a scene of its own. An object's description
gives its library, gate, device, pin name and connected package pads.

The reader accepts EAGLE XML version strings 6.x–9.x. The authored regression
fixture declares 9.6.2 and validates against CadSoft's published 7.7 DTD. This
checks XML structure, not rendering compatibility with every EAGLE release.
Geometry tests independently calculate mirrored/rotated gate coordinates;
there is not yet a qualified corpus of EAGLE-generated reference images.

| Content | Supported behaviour |
| --- | --- |
| Symbols | Embedded libraries, devicesets, multiple gates, placed instances, pin-to-pad connections |
| Placement | Translation, rotation, mirroring; sheet-positioned detached (smashed) attributes |
| Pins | Point/short/middle/long lengths, normal/inverted/clock/inverted-clock styles, pin/pad visibility |
| Connections | Nets, buses, wires, junctions and ordinary labels; pin references validated against definitions |
| Graphics | Straight and curved continuous round-ended wires, circles, rectangles, straight-edged solid polygons and holes |
| Text | Rotation/spin, nine alignments, readable mirrored glyphs, NAME/PART/VALUE/GATE, custom attributes, SHEET and DRAWING_NAME substitutions |
| Frames | Rectangular borders, optional sides, signed row/column numbering; theme-based spacing |

Text uses a substitute outline font; colours, line presentation and frame
spacing use the viewer's theme, not EAGLE's configured layer palette. The
preview displays imported schematic geometry without applying EAGLE layer
visibility settings. These differences are not electrical verification.
Binary pre-XML EAGLE, `.brd` boards, hierarchical modules, active assembly
variants, generated cross-reference labels, dimensions, dashed/flat-ended
wires and curved or hatched polygons are explicitly refused. Unknown drawing
elements are errors, not silently omitted objects. Export an ordinary default
assembly schematic or simplify the reported feature in the source CAD tool.

The XML parser never loads a DTD, external entity, library path or network
resource. The standard `eagle.dtd` declaration is inert. Only valid UTF-8,
the five XML entities and valid numeric character references are admitted;
duplicate attributes, other entity declarations and malformed input fail.
The shared XML reader's stricter mode is opt-in, preserving existing clients.
Limits are 8 MiB of source, 32 MiB of conservatively charged XML allocations,
65,536 XML nodes, 64 sheets and 64 MiB of retained scenes/fonts. The normal
per-scene object/point limits also apply. Cancellation and allocation failures
leave the caller's output and the application's current drawing unchanged.

## Drill drawings

The reader puts holes and routed slots in one scene and needs no font. Each
object's description gives its tool number, diameter, source position, line
number and declared attributes, and tool names such as `T01` can be found.

The reader implements a bounded subset, not arbitrary Excellon compatibility:

| Input | Supported behaviour |
| --- | --- |
| Envelope | `M48`, `METRIC` or `INCH`, `%` or `M95`, `M30`; optional `FMAT,2` |
| Tools | Unique `T1C1.0` declarations after units; tool numbers 1–255; round tools |
| Coordinates | Absolute `X` then `Y`; explicit decimal points; omitted axes reuse the current point, but the first point needs both |
| Drilling | `G05`, tool selection, coordinate hits and explicit start/end `G85` slots |
| Routing | `G00` rapid positioning, `M15` tool down, `G01` straight cuts, `M16`/`M17` tool up |
| Circular routing | `G02` clockwise and `G03` counterclockwise, with `A` radius and a sweep of at most 180 degrees |
| Attributes | `; #@! TF`, `TA`, `TO` and `TD`; tool attributes are captured at tool declaration, object attributes at each emitted operation |

Units and origin come from the file; there is no automatic translation or
alignment with another file. Scene distances use millimetres. Source Y points
up, whereas screen Y points down. Inspection positions retain the source axes
and are labelled in millimetres. Tool diameter determines the filled circle or
round-ended route width. Plating is shown only when an attribute declares it;
it is not guessed from the filename or tool number. Overlapping holes and
routes are painted with one opaque colour, not interpreted as copper layers.

A nonzero coordinate without a decimal point is refused unless the file has
the tested KiCad `FORMAT={-:-/ absolute / metric / decimal}` declaration
(or its `inch` equivalent). Zero alone is unambiguous. XNC itself permits
integer coordinates, but the viewer deliberately requires this extra evidence
when reading files that could instead contain suppressed-zero Excellon values.
Export explicit decimals to avoid this ambiguity. Fixed-point format selection,
zero suppression, incremental positioning, I/J arcs, repeats, compensation and
machine commands are not implemented. Unknown commands refuse the entire file;
they are not skipped. TikuCircuit never sends these commands to hardware.

Drill limits are 8 MiB of input, 262,144 lines, 1,024 bytes per line, 4,096
objects, eight attributes per scope, 63-byte attribute names, 191-byte values
and 4,096 bytes of combined details per object. Numbers allow at most 16 digits,
eight fractional digits, and coordinates within 10,000 mm of the source origin.
Geometry bounds must also fit the scene limits. ASCII only; no exponent notation.
Commands use uppercase and one command per line. A drill preview always warns
that copper and board outlines are absent. It is not a manufacturing check.

The decoder follows the public
[XNC specification](https://www.ucamco.com/files/downloads/file_en/452/xnc-format-specification-revision-2021-11_en.pdf)
with the listed KiCad Excellon extensions. The authored board fixture was
exported using KiCad CLI 7.0.11 in both slot and route modes. Tests check those
coordinates, plus independently calculated circular routes, inches, metadata
lifetimes, invalid states, resource limits, cancellation and allocation failure.
KiCad is not required at runtime.

## Gerber artwork

The reader decodes one artwork layer. It infers no layer function from a file
name: only the file's own attributes say what the layer is. The source origin
is kept, units are converted to millimetres, and source Y is reversed for
screen coordinates.

### Implemented subset

| Feature | What the current reader accepts |
| --- | --- |
| Format | `FSLAX...Y...`, matching X/Y formats, 1–6 integer and 1–6 fractional digits; leading-zero omission and absolute coordinates |
| Units | One `MOMM` or `MOIN` declaration; no mid-file unit changes |
| Apertures | `ADD` definitions numbered 10–255; circular, rectangular, obround and 3–12-sided regular polygon flashes |
| Holes | One centred circular hole strictly inside a standard aperture; transparent, not a clear operation |
| Tracks | Linear draws and G75 circular interpolation with a solid circular aperture; full circles and bounded coordinate-rounding differences allowed |
| Regions | Multiple closed contours, G75 curves and validated coincident cut-ins for holes; at most 1,024 edges per contour after tessellation |
| Aperture macros | Restricted expressions, variables, positive/negative exposure, primitive rotation; circles, vectors, rectangles, outlines, regular polygons, thermals and historical moirés |
| Step-and-repeat | Explicit SR blocks, Y-then-X expansion, original dark/clear order; bounded nested blocks |
| Polarity | Ordered `LPD` and `LPC` operations on this layer's coverage |
| Metadata | Bounded TF/TA/TO/TD attribute scopes; aperture attributes captured at definition, region attributes at construction |
| Commands | G04 comments, G01/G02/G03 modes, D01/D02/D03 operations, modal coordinates, G36/G37, G75, G90, M02; identity LMN/LR0/LS1 |

Coordinates use the declared fixed-point format; they are not parsed as
ordinary decimals. A first move or flash needs X and Y. Later operations may
reuse an omitted axis. A track needs an explicitly selected interpolation
mode. Numeric parameters use decimal points and are locale-independent.
The fractional formats below six digits are supported historical variants,
not a claim that they meet the latest writer recommendations.

Nonidentity aperture transforms, aperture blocks, trailing-zero
omission, incremental coordinates, G74 arcs, rectangular holes and
noncircular/holed track apertures are refused. A zero-size aperture (C,0, a
zero-sized R, O or P, or a macro whose primitives cover nothing) may be
defined and selected, as KiCad does before its regions; a flash or draw with
it makes no object. Arc radius differences
are bounded by the larger of floating-point tolerance and two source coordinate
quanta, capped at 0.01 mm for the latter. The radius changes linearly with angle
between the declared endpoints. Region curves are tessellated at a target
chord error of 0.00001 mm before contour validation; large curves may exceed the
edge budget. Separate contours are combined by coverage, not an even-odd hole
rule. Repeated line endpoints are accepted as no-ops. One parameter per
`%...*%` block is required, except for `AM` macro bodies. Unknown commands
fail the entire import rather than leaving missing artwork in the view.

### Macros, repeats and cut-ins

An `AM` definition describes an aperture; `AD` supplies its parameters and a
flash places it on the layer. Supported primitive codes are 1, 4, 5, 7, 20
and 21, plus historical 2, 6 and 22. Expressions support parentheses, unary
signs, addition, subtraction, `x` multiplication and division. Undefined
variables start at zero; assigning a variable twice or dividing by zero is
an error. Macro expressions are interpreted locally, never run as host code.

Each macro flash first builds its own coverage. Negative-exposure primitives
remove only that macro's coverage, leaving earlier layer artwork untouched.
The complete flash is then applied using the layer's current dark/clear
polarity. Picking uses the same isolated shape: a transparent macro hole can
expose an earlier object. Details identify the aperture and macro name.

`SRX2Y3I10J5` makes two columns and three rows at 10 mm and 5 mm spacing in a
metric file. `%SR*%` closes the block. Expansion preserves operation order,
including clear operations, macro flashes and regions. Displacements appear
in copied objects' details. After closing a block, a new explicit position is
required. This reader requires explicit closing blocks; historical implicit
reset idioms are not accepted. Nested blocks are an extension, limited to
eight levels. A block may request at most 4,096 repetitions, and the final
scene must still fit 4,096 objects and its point/storage budgets.

A cut-in travels from an outer boundary to a hole and back over exactly the
same segment in the opposite direction. The validator checks complete,
axis-aligned coincident pairs, one bridge orientation per contour, separate
nonintersecting loops, nesting and opposite hole winding. Bridges must lie
inside the filled area. Multiple holes are supported. Diagonal or partially
overlapping bridges, arbitrary crossings and ambiguous touching contours are
refused, not guessed. Validation uses bounded quadratic work and checks
cancellation. Curves are tessellated before validation. Macro outlines use
the same checks before primitive rotation. Outline vertices equal to the one
before are dropped first, as KiCad's FreePoly macros repeat their start; an
outline whose vertices lie on one line, a vector line of zero length or width
and any zero-size primitive add nothing.

Macro limits: 64 definitions, 64 KiB of definition text, 1,024 variables,
256 statements per instantiation, expression depth 24 and 32,768 expression
atoms. Outlines have at most 1,024 edges. Instantiated aperture geometry has
a separate 16 MiB budget; each flash is also conservatively charged to the
final scene. Large expansions fail atomically. Rendering an isolated macro
uses a temporary viewport and coverage buffer, released after that flash.
A macro aperture's bounds are held to the same 1,000 mm as a standard
aperture. Thermal arcs are cut into chords within 0.00001 mm of the curve, at
most 500 per arc; a larger thermal's chords stay within 1.3e-6 of its radius.

### Holes and clear operations

An aperture hole leaves earlier artwork untouched. A clear operation removes
earlier coverage in the area of its shape. These are different operations:
placing a ring on top of a filled pad does not punch out the pad's centre.

The renderer builds an initially empty coverage buffer for the layer, applies
operations in order, and then adds its display colour. It does not implement
clear areas by painting the window's background. The antialiased buffer uses
one additional byte per viewport pixel. Each layer is composited independently
when displaying a stack.
The `.FilePolarity` attribute is retained as metadata, not interpreted as a
command to invert an unbounded canvas.

### Inspection, limits and evidence

Apertures such as `D10` can be found by name. An object's description gives
its operation polarity first, then its dimensions, hole size, position and
declared attributes. Picking follows the final operation order: a cleared
interior is not picked as the dark shape it previously covered, and a
transparent aperture hole can expose an earlier object. This is geometric
inspection, not netlist analysis.

The reader accepts printable ASCII plus line breaks, at most 8 MiB of input,
262,144 commands and 1,024 bytes per command. Attribute limits match the drill
reader. The shared scene limits still apply: 4,096 objects, 65,536 points and
16 MiB of accounted scene storage. Aperture dimensions, including a macro
aperture's bounds, are limited to 1,000 mm. Extremely large or complex artwork
is refused, not truncated.

The implementation follows the public
[Gerber layer specification](https://www.ucamco.com/files/downloads/file_en/554/gerber-layer-format-specification-revision-2026-05_en.pdf)
for this subset. KiCad CLI 7.0.11 exported the authored four-pad board used for
coordinate comparisons. Separate analytic fixtures check holes, clear areas,
polygon and obround pads, arc direction and full circles. Tests also exercise
atomic failure, allocation failure, cancellation and malformed inputs. This
is limited fixture evidence, not qualification against a manufacturing corpus.

An artwork scene draws in one display colour, and its warning states that it
is one artwork layer without manufacturing verification.

## Linked sheets

A physical-file key detects cycles, including hard-link aliases. Reusing a
child file in sibling sheets is allowed. Each occurrence gets its own scene:
references and units come from the corresponding UUID instance path, not from
the child file's default Reference property. The project-name bucket is used
when it matches. A renamed root can use one uniquely matching stored bucket;
ambiguous matches are refused. Child symbols without a matching modern
instance table are unavailable rather than assigned guessed references.

Syntax is cached per physical file for the duration of import. Repeated reads
are compared byte-for-byte; detected changes are not combined with the cached
version. This still is not an atomic snapshot of all project files.

## Layer stacks and Gerber jobs

Hidden and zero-opacity layers are left out of find, picking and the stack's
bounds.

Every layer uses the same millimetre coordinates and source origin. There is
no automatic alignment, mirroring of bottom layers, or inference from filenames.
If exports use different origins, correct that in the CAD application. Clear
operations remove coverage only within their own layer. A transparent area
reveals layers behind it. Drill holes and routes are coloured overlays, not
operations that cut holes in copper. Keep drills at the front to see them.

The job loader reads `FilesAttributes` and resolves its paths within the selected job's
directory tree using the same confinement rules as schematic dependencies.
Entries retain their listed order as display order, not inferred physical
stack order. `FileFunction`, `FilePolarity` and `FileFormat` are shown as declared
metadata. Formats Gerber, XNC and NC are supported; an absent format is detected
from content. A format/content conflict, or a file over its own limits (such
as a contour over 1,024 edges or more than 4,096 objects), marks that entry
unavailable. Other job metadata is syntax-checked but not interpreted. Job attributes are not
verified against file attributes, and negative FilePolarity does not invert
the canvas. This is not a manufacturing stack-up validator.

An unavailable or unsupported file stays in the list with `!`, an error and
an incomplete-stack warning. Selecting that row displays its path and error.
The job's 64 MiB budgets, allocation failure and cancellation reject the
entire new stack, and the error names the entry that caused it. The
collection of file reads is not an atomic filesystem snapshot.

The stack supports 32 entries, each labelled in at most 255 bytes
(`TIKU_CIRCUIT_LABEL_BYTES` with the terminating zero). Job JSON is limited to
1 MiB, 24 levels,
32 members per object, 32,768 values and 4,095 decoded bytes per string.
An entry keeps Path up to 4,095 bytes, FileFunction up to 191 and
FilePolarity and FileFormat up to 31; a longer value is refused.
Duplicate keys, malformed UTF-8 and malformed JSON are refused. A stack has
separate conservative 64 MiB source and retained-scene budgets, in addition
to each file's 8 MiB and each scene's existing limits. These are not RSS caps:
temporary decode data, old snapshots and render buffers require extra memory.

## Embedding the engine

The public API is `tiku_circuit.h`. Its decoder takes bounded document bytes
and optional caller-supplied TrueType bytes. It copies what it needs; input
buffers need not remain alive after decoding.

```c
tiku_circuit_t *replacement = NULL;
tiku_circuit_diagnostic_t diagnostic;
tiku_circuit_import_t options = {
    .font_data = font_bytes,
    .font_bytes = font_length,
    .cancel = cancelled, /* optional callback on the importing thread */
    .ctx = job
};
tiku_circuit_err_t error = tiku_circuit_kicad(
    source_bytes, source_length, &options, &replacement, &diagnostic);
if (error != TIKU_CIRCUIT_OK) {
    /* replacement is still NULL. Keep the current document. */
    report(diagnostic.message, diagnostic.line, diagnostic.column);
    return;
}
tiku_circuit_view_t view;
tiku_image_t image = {0};
if (tiku_circuit_fit(replacement, 800, 600, &view)) {
    error = tiku_circuit_render(replacement, &view, cancelled, job, &image);
    if (error == TIKU_CIRCUIT_OK)
        show_preview(&image, tiku_circuit_warning(replacement));
}
tiku_image_free(&image);
tiku_circuit_release(replacement);
```

Here `cancelled`, `report` and `show_preview` are caller-provided functions,
not additional kit APIs. The preview function must copy pixels if it needs
them after this example frees the image. A real UI publishes results on its
UI thread, not on the importing worker.

For a drill file, call `tiku_circuit_drill()` instead of
`tiku_circuit_kicad()` with the same byte buffer, output and diagnostic
arguments. The font fields are ignored; `options` may be NULL when cancellation
is unnecessary. Rendering, picking, finding and scene lifetime are unchanged.
Use `tiku_circuit_gerber()` with the same arguments for one artwork layer.
It also needs no font. Its scene retains ordered coverage operations, so use
the common renderer rather than drawing its shapes independently.

Include `tiku_circuit_stack.h` to combine artwork/drill scenes with
`tiku_circuit_stack()`. Pass front-to-back `tiku_circuit_layer_t` descriptors.
It copies strings and retains child scenes; the caller still releases its own
references. Nested stacks and text scenes are refused. To change a layer,
copy descriptors, change the desired fields and construct another stack.
Existing render workers can finish safely with the old retained snapshot.
Object indices enumerate visible, nonzero-opacity layers in front-to-back
order; they change when the stack changes. `tiku_circuit_object_layer()` maps
an index back to its layer. Do not retain a selection index across replacement.

`tiku_circuit_job_open()` takes the same resolver contract as project loading
and returns an ordinary reference-counted stack. Check
`tiku_circuit_stack_incomplete()` and each borrowed layer's diagnostic even
after success. The embedding application must enforce path confinement.

For multi-sheet documents, include `tiku_circuit_project.h` and call
`tiku_circuit_project_open()` for KiCad. Supply `read` and `release` callbacks through
`tiku_circuit_resolver_t`. The engine calls `release` after every `read`, even
on failure. Return a stable physical-file key and a path for resolving future
relatives. The application, not this kit, must enforce filesystem policy.

For EAGLE, the same project header declares
`tiku_circuit_eagle(data, bytes, path, options, &project, &diagnostic)`.
It decodes all sheets from one byte buffer, so it needs no resolver. `path`
labels the source; the kit does not open it. Supply font bytes through the
import options for text. Every sheet must decode successfully or the output
remains untouched. Browse/render the returned scenes through the same project
and scene APIs below, and free the project in the same way.

`tiku_circuit_project_count()` includes unavailable entries. Inspect each
`tiku_circuit_project_sheet()` for its name, path, parent, depth, page, instance
path and diagnostic. `tiku_circuit_project_scene()` returns NULL for an
unavailable sheet. A successful open can still have
`tiku_circuit_project_incomplete()` set: embedding applications must show it.
Root failures and budget/cancellation errors leave the output untouched.

Free a project with `tiku_circuit_project_free()`. Its scenes, sheet descriptors
and strings are borrowed. Retain a scene separately if it must outlive the
project; descriptors and their strings cannot outlive it.

Scenes are immutable after decoding. Retain one before handing it to another
worker and release it afterwards. Object strings and warnings are borrowed
from that retained scene. `tiku_circuit_find()` searches from an index without
wrapping and returns `SIZE_MAX` for no match; the app adds wrapping.
`tiku_circuit_details(scene, index)` borrows a plain-text description until the
scene is released. It returns an empty string for absent metadata or an invalid
index. This text is untrusted document data: render it as text, not HTML, shell
input or an automatically followed URL. Search covers names, values and drawn
text, not every field in this description.

Fit, zoom, world-coordinate conversion and selection share the same
millimetre-to-pixel transform. Hit testing uses path distances and filled
interiors, or a transformed text box. Track arcs retain their parametric
representation until viewport-dependent tessellation. Gerber region curves
are tessellated during import for contour validation. Arc bounds conservatively
include the entire circle at the larger endpoint radius.

Rendering requires an empty output image. Success transfers pixels to the
caller; failure leaves the output untouched. Free pixels with
`tiku_image_free()`. Cancellation must be thread-safe and remain true once
requested. An embedding app must keep calling `tiku_work_collect()` after
destroying a native controller until cancelled jobs release their references.

## Resource limits

| Resource | Current limit |
| --- | --- |
| Document / supplied font | 8 MiB each |
| Hierarchy | 64 sheet instances including the root; 16 child levels below it |
| Project import | 64 MiB accounting budget for project records, source cache, syntax, scenes, fonts and link workspaces; conservative scratch reservations can refuse an import earlier |
| Syntax | 262,144 nodes; 128 nesting levels; 4,096 decoded bytes per token |
| Interpretation | 2,000,000 checked semantic operations |
| Scene | 4,096 drawing objects; 65,536 stored points; 16 MiB accounted storage excluding the separately bounded font |
| Object names / values | 255 UTF-8 bytes each |
| Inspection metadata | 4,096 bytes per object, charged against scene/project budgets; an overlong description is refused, not truncated |
| Source coordinates | Finite, within 10,000 mm of the origin |
| Stroke width | At most 1,000 mm; a zero KiCad width uses the default |
| Viewport | 4,194,304 pixels (`TIKU_CIRCUIT_RENDER_PIXELS`); at most 8,192 pixels per axis (`TIKU_CIRCUIT_RENDER_SIDE`) |
| Zoom | 0.01–4,096 pixels/mm |
| View translation | 45,064,192 pixels, shared by fit, zoom and render: room to bring any in-range point, stroke included, to any viewport pixel at the largest scale |
| Stroke at the current zoom | Geometry past the viewport is clipped before rasterising; a stroke reaching past the viewport is refused as a limit when its width in pixels plus the viewport's larger side exceeds 1,000,000 |

These are admission limits, not maximum RSS. The hierarchy importer reserves
worst-case space for a bounded read, syntax parse or scene decode before that
step. It then accounts for retained data. Font-library structures, allocator
overhead and viewport rendering have separate costs. Import temporarily holds input,
syntax, a replacement scene, font and first image. The old scene and image
also remain alive during replacement. Limit errors never publish partial
drawings.

The vector rasterizer under the renderer is shared with PDF and independent of
it. Circuit paths use strict subdivision and round caps and joins, without
PDF's minimum display width for thin strokes.

## Not implemented

Broader KiCad feature and version coverage, generated intersheet references
and overbars, broader EAGLE coverage, Gerber aperture transforms and blocks,
fixed-point drill format selection, manual layer alignment and recent files
are not implemented. The Open command implies none of them.

## Viewer inspection tools

TikuCircuit supplies two-point distance measurements in mm, inches and mils,
layer appearance controls, PNG viewport export and per-document presentation
settings. These belong to the application, not the parsers: a measurement
does not add geometry, and changing a layer creates a new immutable stack.
The old scene remains valid for an in-flight render.

PNG export copies the completed viewport, adds a measurement if visible and
complete, and includes an inspection-preview label and warnings. It uses the
image kit's stream encoder with the storage kit's atomic file writer. It is
not a Gerber exporter or fabrication output. Saved settings only style layers
already loaded by the normal confined resolver; they never introduce paths to
load. See the [application guide](../../../apps/circuit/README.md) for controls,
storage location and limits.
