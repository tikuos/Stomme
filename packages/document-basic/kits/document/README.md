# Document Kit

Document Kit stores styled paragraphs and lets a host program edit them
without a window or event loop. It is a native C library, separate from the
plain-text Editor Kit.

The implemented API covers checked imports, named styles, immutable
snapshots, text replacement and formatting across paragraphs, paragraph
split/join, tracked positions, atomic edit groups, undo/redo and saved-state
tracking. It also supports paragraph formatting, editable named styles,
lists, hyperlinks, selection-format queries, styled fragments and incremental
literal search. A typed command catalog supplies availability, formatting
indicators and shared execution for host menus and shortcuts. Headless editing
sessions add independent selections and typing formats for multiple views;
document observers report published changes. The kit also supplies Unicode
segmentation, continuous layout, shared drawing/editing geometry and a
window-independent view controller. Document Formats package supplies native font
loading, glyph/selection/caret drawing and versioned `.tkrd` document
save/load. Rich View accepts shared keyboard, pointer and committed-text
events. TikuNotes embeds it for plain-text notes, retaining its existing
files. TikuWrite supplies a separate formatted-document application with
native saving, conflict checks and recovery. Single-section pagination feeds
page preview and PDF export. Embedded raster pictures support selection,
corner resizing, alternate text, history and native save/load. Structured
tables have adjustable column widths, nested cells, editing, native persistence
and shared screen/PDF geometry. Semantic heading navigation, hyperlink
occurrence scans and an interactive paragraph ruler are reusable kit features.
Full native accessibility and composition
adapters are not implemented. The Office viewers use their existing code.

Native persistence is described in [Saving and loading documents](#saving-and-loading-documents).

## Build and run the example

Run these commands from `applications/`:

```sh
make build/examples/tiku_document_model
./build/examples/tiku_document_model
make test-document
make test-document-sanitize
make test-document-render
make test-document-files
make test-document-files-sanitize
make test-document-output
make test-document-write
make test-document-performance
make test-document-performance-sanitize
make test-document-tables
make build/examples/tiku_document_layout
./build/examples/tiku_document_layout build/examples/document-layout.png
```

The [complete example](../../examples/document/tiku_document_model.c)
loads a heading and a measurement, changes the measurement, makes the new
value bold, then undoes both changes together. It also pastes a new paragraph
and checks caret restoration through undo, edits a named style and paragraph
alignment, finds and replaces a measurement, and adds a list item and a link.
It then inspects mixed font sizes and copies the styled content to another
document. Finally, it queries a Bold command, applies it to both paragraphs,
and restores their formatting with one Undo command. A two-session example
checks independent typing formats and receives document-change callbacks.
It prints:

```text
Field notes
Temperature: 24 C
Dirty after edit: 1
Dirty after undo: 0
After multiline paste: 3 paragraphs
Undo restored the tracked caret
Body style: 14 pt; heading centered
Search replaced the reading with 25 C
Reading list item: 1; heading link stored
Selection has mixed font sizes: yes
Copied styled paragraphs, list and link into another document
Command catalog: 36; Bold state: mixed
One Undo restored both paragraphs after the Bold command
Two views: independent typing formats; 2 published changes
```

Include `tiku_richdoc.h` and link `build/lib/libtiku_document.a`.
`make kits` also builds `build/lib/pic/libtiku_document.a` for loadable apps.
The model example and dedicated suites run without X11, a desktop
shell, a board connection or a Rust runtime.

## Documents and snapshots

A document owns the current editable state. Use its mutation, reference
count and snapshot-creation functions on one owning thread. Retain the
document if another owner needs to keep it; release each retained reference.

A snapshot owns an immutable state. An edit does not change previously
captured snapshots. A worker may read and release a snapshot, and the
snapshot remains valid after the live document is released. Do not release
a snapshot while another thread is reading through that same handle; give
each worker its own snapshot or arrange shared lifetime in the host.

Paragraph text, run arrays and style names returned by snapshot queries are
borrowed. They remain valid until that snapshot is released. Copy them if
they must live longer. Import input is different: it is borrowed only for
the import call and copied before the call returns.

## Load paragraphs and styles

`tiku_richdoc_create(NULL, &doc)` creates a document with no unsaved changes with one empty
paragraph. Check the returned status before using `doc`. Creation failure
sets it to `NULL`.

`tiku_richdoc_import()` replaces the entire document as a file load. Supply
paragraphs, optional character runs and optional named styles. Success
clears undo/redo and marks the loaded state saved. Failure changes neither
the content nor its history. An empty source creates one empty paragraph.

Source style references are **one-based array indices**, with zero meaning
no style. Published styles and paragraphs instead have nonzero 64-bit IDs.
These IDs belong to one document; do not copy IDs between documents or
interpret them as array indices. A successful import assigns new IDs.
Paragraph-local edits retain paragraph IDs; undo restores the previous
state without reusing issued IDs for different content.

Named styles inherit from a style of the same category. The maximum chain
contains 64 styles, including the leaf. Cycles, missing parents, duplicate
names within a category and character styles with paragraph properties
are refused.

A character property's `present` bit decides whether it overrides an
inherited value. For example:

```c
tiku_rich_chars_t disable_bold = {
    .present = TIKU_RICH_BOLD,
    .bold = 0
};
```

Without `TIKU_RICH_BOLD`, the value of `bold` is ignored. The same distinction
allows explicit black text, zero spacing and normal baseline positioning.
Fields without a presence bit are canonicalized to zero on input.

Character properties resolve in this order, with later values overriding
earlier ones: paragraph style inheritance, character style inheritance,
paragraph direct character defaults, run property resets, then run direct
formatting. A run's `reset` mask clears inherited properties to absent before
its direct values apply. Joining paragraphs uses resets and direct overrides
where needed to preserve the former paragraph's character appearance without
assuming a particular layout font or size. Its character style ID survives;
the converted paragraph differences become direct run formatting.
Paragraph properties use paragraph style inheritance followed by direct
paragraph properties. Unspecified values remain absent; a layout provider
must supply its own final defaults.

## Text and positions

Text is length-delimited UTF-8 without embedded NUL. Raw paragraph input rejects
malformed sequences, CR and ASCII control characters other than TAB and LF.
It does not silently normalize Unicode text. U+FFFC requires a matching
inline-picture run; an unassociated object placeholder is refused.

Each paragraph is a separate object. LF inside a paragraph is a manual
line break; it does not create another paragraph. The plain-text extraction
function inserts LF between paragraphs as well, so extracted text cannot
preserve that structural distinction on its own. It is not a native rich
document format.

Positions identify the body, paragraph, byte offset and insertion affinity.
Validation checks that the offset falls between UTF-8 scalars. This is not
grapheme validation: for example, an offset before a combining mark may be
a valid raw model offset but an inappropriate interactive caret stop.
Use `tiku_richsnapshot_compare()` to compare positions in paragraph order,
not by the numeric value of their IDs.

## Tracked positions

Use `tiku_richanchor_create()` to register a position that follows edits.
An anchor is a small owner-thread object, not a snapshot. It retains the
document until `tiku_richanchor_release()`. Use two anchors for a selection's
ends, and separate anchors for each view's selection or a bookmark.

`TIKU_RICH_BEFORE` keeps an anchor before text inserted exactly at its
position; `TIKU_RICH_AFTER` moves it after that text. An anchor inside deleted
text collapses to the replacement's start or end according to affinity.
An anchor at a nonempty selection's end remains after the replacement,
including its pasted paragraphs, regardless of affinity.

`tiku_richanchor_get()` reads the current position. Call
`tiku_richanchor_set()` when the user moves the caret or changes a selection.
This does not edit the document or change its dirty state. Anchors can be
created and moved in a read-only document.

History records exact before/after positions for anchors present when the
edit commits. Undo can therefore restore an anchor inside deleted text,
including the original paragraph ID. If the anchor was manually moved,
or created after the edit, history maps its current position instead of
restoring an old selection. Such a map can collapse positions inside text
that no longer exists; it does not promise an exact undo/redo round trip for
a newly created or manually moved anchor.

Releasing and recreating an anchor gives it a new identity. Old history
cannot adopt that new anchor's position. A successful document import moves
live anchors to the new first paragraph at byte zero, preserving affinity.
There are no callbacks during mapping, and undo/redo allocate no storage.

## Edit and undo

A `tiku_rich_edit_t` addresses a byte range `[start, end)` in one paragraph.
`TIKU_RICH_REPLACE` replaces the range with UTF-8 text.
`TIKU_RICH_FORMAT` overlays direct character properties on the range.
`TIKU_RICH_SPLIT` splits at `start == end`: the leading paragraph keeps its
ID, the trailing paragraph receives a fresh ID, and both keep the original
paragraph style and direct properties. `TIKU_RICH_JOIN`, with zero offsets,
joins the named paragraph and its immediate successor. It retains the
leading ID and paragraph properties, and preserves surviving text's
character appearance. Splitting the last paragraph at its end creates an
empty paragraph. Joining the last paragraph is an invalid range.
Formatting an empty range does not change the document. Pending typing
attributes belong in the future view, not in the shared document.

Inserted text inherits the preceding run's stored character style and
direct properties. At offset zero, it uses paragraph defaults instead.
Hyperlinks have a separate boundary rule, described below.
The edit's explicit character attributes then override inherited values.
Surviving prefix and suffix runs keep their formatting. Equal adjacent
stored runs are merged after an edit.

Put one or more edits in a `tiku_rich_transaction_t` and call
`tiku_richdoc_apply()`. Offsets in each edit address the result of preceding
edits in that transaction. Supply the generation captured when preparing
the transaction to reject stale edits, or zero to accept the current state.
One successful transaction is one undo group. A transaction can contain at
most 1024 operations. Sessions can explicitly group compatible typing through
`tiku_richsession_type()`; direct transactions do not coalesce.

The implementation constructs replacement nodes and the history record
before publishing the result. Invalid ranges, bad text, resource limits
and allocation failures leave live content, history and generation
unchanged, including tracked positions. Undo and redo exchange retained
roots and map positions without allocating.
They still respect the document's read-only flag.

`TIKU_RICH_NO_CHANGE` means the operation made no publication: for example,
there was no undo entry, or each edit was individually a no-op. A group of
real edits that cancel each other can still create a history entry.

Edits copy the main paragraph pointer array and style array, but retain
unchanged paragraph nodes. There is no constant-time edit guarantee for a
very large paragraph array or a very large individual paragraph.

## Selections and plain text paste

Use `tiku_richdoc_replace_range()` to replace text across paragraphs or paste
plain text. It accepts ordered `tiku_rich_pos_t` endpoints and a generation
precondition. CRLF, CR and LF in its input become paragraph boundaries;
trailing line endings create empty paragraphs. This differs from raw
`TIKU_RICH_REPLACE`, where LF is a soft break within one paragraph.

For example, given valid `start` and `end` positions in the live document:

```c
const char pasted[] = "Temperature: 24 C\r\nHumidity: 40%";
tiku_rich_status_t status = tiku_richdoc_replace_range(
    doc, tiku_richdoc_generation(doc), start, end,
    pasted, sizeof pasted - 1);
```

Check `status` before updating application state. The replacement is one
undo group. The first selected paragraph survives; new paragraphs copy its
paragraph defaults. Surviving suffix text keeps its character formatting.
The first pasted segment uses normal insertion inheritance. Subsequent
segments start in new paragraphs and use those paragraphs' defaults.

`tiku_richdoc_format_range()` applies character overrides to a selection
across paragraphs as one undo group. Neither function changes the document
on invalid input, allocation failure, a stale generation or budget refusal.

These helpers expand into primitive operations under the same 1024-operation
limit. A paste uses one replacement, one join per removed paragraph boundary,
and two operations per inserted boundary. Oversized operations are refused
atomically; the helper does not split a paste into separately published edits.
The implementation joins selected paragraphs before replacement, so large
cross-paragraph selections can require repeated copying. This is not a
constant-time range-edit API.

## Paragraph formatting and named styles

Formatting commands use the same transaction and undo functions as text
edits. These are model changes; alignment and spacing become visible when
a layout/view consumer renders the document.

| Command | Values it uses | Effect |
| --- | --- | --- |
| `TIKU_RICH_PARAGRAPH_FORMAT` | `paragraph`, `props`, `chars`, `clear_props`, `clear_chars` | Change direct paragraph properties and its default character properties |
| `TIKU_RICH_PARAGRAPH_STYLE_SET` | `paragraph`, `style` | Assign a paragraph style ID, or remove the assignment with zero |
| `TIKU_RICH_CHARACTER_STYLE_SET` | `paragraph`, `start`, `end`, `style` | Assign a character style ID to selected text, or remove it with zero |
| `TIKU_RICH_FORMAT` | `paragraph`, `start`, `end`, `chars`, `clear_chars` | Clear selected direct/reset properties, then apply explicit values |
| `TIKU_RICH_STYLE_DEFINE` | `style`, `definition` | Create a named style when `style` is zero, or replace that style's definition |
| `TIKU_RICH_STYLE_REMOVE` | `style` | Remove an unused named style |

Zero-initialize edit records. Paragraph formatting and paragraph-style
assignment require `start` and `end` to be zero; they affect the whole
paragraph, including an empty paragraph. Character-style assignment uses
UTF-8 boundary offsets, just like direct formatting. It does not remove
existing direct formatting, and assigning a style of the wrong category
returns `TIKU_RICH_INVALID_STYLE`.

For example, to center a paragraph and add space after it:

```c
tiku_rich_edit_t edit = {
    .kind = TIKU_RICH_PARAGRAPH_FORMAT,
    .paragraph = paragraph.id,
    .props = {
        .present = TIKU_RICH_ALIGNMENT | TIKU_RICH_SPACING,
        .align = TIKU_RICH_CENTER,
        .before = 0,
        .after = 8
    }
};
tiku_rich_transaction_t change = {
    .generation = tiku_richsnapshot_generation(snapshot),
    .edits = &edit,
    .count = 1
};
tiku_rich_status_t status = tiku_richdoc_apply(doc, &change);
```

Lengths are document points. `TIKU_RICH_SPACING` sets both `before` and
`after`; `TIKU_RICH_INDENT` sets left, right and first-line indents together.
Properties whose presence bits are absent remain unchanged by a formatting
command. The clearing masks do something different: they remove direct
values so the named style can supply them again. If a property is both
cleared and present, the new explicit value wins.

To remove a paragraph's direct alignment, use `PARAGRAPH_FORMAT` with
`.clear_props = TIKU_RICH_ALIGNMENT` and no alignment presence bit.
To remove direct bold from a text range, use `FORMAT` with
`.clear_chars = TIKU_RICH_BOLD`. This does not force bold off: text may still
be bold because of its named style or paragraph defaults. To force it off,
supply the bold presence bit and `.bold = 0`.

Named-style definitions are **complete replacements**, unlike formatting
overlays. `tiku_rich_style_spec_t.parent` is a document-local style ID, not
the one-based index used during import. A definition keeps the style's ID
and category, but replaces its name, parent and properties. A property
omitted from the definition inherits from its parent. Updating a parent
affects descendants and their users when values are resolved; direct
overrides remain in effect. Old snapshots keep their original definitions.

To create a style, submit `STYLE_DEFINE` with `.style = 0`. A successful
definition appends a style with a fresh ID. Read the published style array
from a new snapshot to obtain that ID before assigning it. Do not predict
IDs. Styles cannot change category, form cycles, exceed 64 ancestors
including the leaf, or reuse a name within the same category. Editing a
parent also checks the resulting depth of its descendants.

Removing a style referenced by a paragraph, run or child style returns
`TIKU_RICH_INVALID_STYLE`. Reassign those users first; reassignment and
removal can belong to one transaction. History and older snapshots are not
live users: they retain their own definitions. Undo restores removed styles
and assignments. Each operation in a group must leave a valid candidate
document; temporary invalid references inside a transaction are not allowed.

## Headings and an outline

A heading is a paragraph with a level from 1 through 6. Its level says where
it belongs in the document's structure; it does not depend on its font size
or the name of its style. A large bold paragraph is not automatically a
heading. Level 0 means body text.

Set the level through the same command used for other paragraph properties:

```c
tiku_richcmd_t command = {
    .kind = TIKU_RICHCMD_PARAGRAPH_FORMAT,
    .value.paragraph.values = {
        .present = TIKU_RICH_OUTLINE,
        .outline = 2
    }
};
tiku_rich_status_t status = tiku_richsession_execute(session, 0, &command);
```

The command changes the selected paragraphs and creates one undo entry.
It does not change their appearance. A paragraph style can supply the level
alongside font and spacing properties. A direct level of 0 overrides an
inherited heading level. Clearing the direct `TIKU_RICH_OUTLINE` property
restores inheritance. Mixed-level selections set that bit in `mixed_props`.

Include `tiku_richoutline.h` to build a navigation list without creating a
window or a text layout:

```c
tiku_richoutline_t *outline = NULL;
status = tiku_richoutline_create(snapshot, &outline);
if (status == TIKU_RICH_OK) {
    do {
        status = tiku_richoutline_step(outline, 256);
        /* An interactive application schedules the next step on a later tick. */
    } while (status == TIKU_RICH_PENDING);
    if (status == TIKU_RICH_OK && tiku_richoutline_count(outline)) {
        tiku_rich_heading_t heading;
        status = tiku_richoutline_get(outline, 0, &heading);
        if (status == TIKU_RICH_OK)
            status = tiku_richsession_select(session,
                tiku_richoutline_generation(outline),
                heading.position, heading.position);
    }
    tiku_richoutline_release(outline);
}
```

The index retains its source, so the caller may release the snapshot after
creation. Labels are borrowed UTF-8 byte ranges, not C strings, and remain
valid until the index is released. Entries include headings in table cells,
in document order. Empty headings are included. Building the index does not
shape text. Its storage is charged to the document's shared memory limit;
release also cancels an unfinished scan. A failed scan keeps its completed
entries but reports the failure; it must not be presented as a complete list.

`tiku_richoutline_neighbor()` finds a heading before or after a source position
without wrapping. A caret inside a heading can navigate back to that heading's
start. After editing, rebuild the index: passing its old generation to session
selection returns `STALE` rather than navigating to outdated content.

Native files, recovery files and rich clipboard transfers preserve levels
through the required `outline-levels` feature. Files without heading properties
keep the earlier representation. Older readers refuse the required feature.
HTML uses `h1`–`h6`, and Markdown uses one through six `#` characters. Existing
native documents whose styles merely have heading-like names keep their
original meaning; assign a heading level explicitly to include them.

In TikuWrite, choose **Paragraph / Heading 1–6** to set both appearance and
level. **Body formatting** clears the level, while **Title formatting** remains
a visual title outside the outline. **View / Heading outline** opens the
sidebar. Click a heading to move the caret, or use Up/Down, Home/End and
Page Up/Page Down followed by Return. Escape returns focus to the editor.
The mouse wheel scrolls the list. Previous/Next heading works with the sidebar
closed. Navigation does not modify the document or add undo history.

## Bullets and numbered lists

List membership belongs to a paragraph; a marker is not part of its text.
An item can be ordinary text (`TIKU_RICH_LIST_NONE`), a bullet
(`TIKU_RICH_LIST_BULLET`) or a decimal number (`TIKU_RICH_LIST_DECIMAL`).
List settings are direct properties, separate from named paragraph styles.

For example, to make a paragraph a numbered item:

```c
tiku_rich_edit_t edit = {
    .kind = TIKU_RICH_LIST_SET,
    .paragraph = paragraph.id,
    .list = {
        .kind = TIKU_RICH_LIST_DECIMAL,
        .level = 0,
        .restart = 0
    }
};
```

Submit it in a transaction with `tiku_richdoc_apply()`. Both byte offsets
must be zero. `level` is zero for a top-level item and 1 through 8 for nested
items. A deeper level can exist without preceding parent items; the model
does not insert missing parents. Membership does not change paragraph
indents, spacing, character formatting or text. A future layout consumer
chooses marker appearance and placement from this metadata.

For a decimal item, `restart = 0` continues numbering at its level;
a positive value explicitly restarts at that number. Bullet items require
zero restart. To remove list membership, set `kind` to `LIST_NONE`.
The other list fields are then cleared.

`tiku_richsnapshot_list(snapshot, index, &list, &ordinal)` returns the
paragraph's membership and its decimal number, or zero for bullets and
ordinary paragraphs. It scans preceding paragraphs using these rules:

- Numbering starts at 1 unless an item has an explicit restart value.
- Nested items do not increment their parent-level counters.
- Returning to a shallower level clears deeper counters. For example,
  `1, nested 1, nested 2, 2, nested 1` is the resulting sequence.
- Changing between bullet and decimal at the same level clears that level's
  previous counter.
- An ordinary paragraph ends the sequence at all levels.

Ordinals use 64 bits, so continuation after the largest 32-bit restart
value does not wrap. Querying many paragraphs individually repeats the
preceding-paragraph scan; it is not a cached layout API.

Splitting a list paragraph keeps its membership and restart on the leading
item. The trailing item keeps the category and level but continues numbering.
Joining paragraphs keeps the leading paragraph's membership. Multiline
paste follows the same split rule. Undo restores the original membership
and restart values, and old snapshots retain their own numbering.

There are no named list definitions, alphabetic/Roman markers, custom bullet
glyphs or explicit sequence IDs connecting lists across ordinary paragraphs.
These limits do not affect the supported contiguous nested lists.

## Hyperlinks

### Navigate between linked passages

TikuWrite provides **View / Previous hyperlink** and **Next hyperlink**.
These select the linked passage and show its target in the status bar without
opening it. Navigation works in read-only documents and includes table cells.
It does not change the document or add undo history. There is no wrap at the
document ends. A selection change or document edit cancels an unfinished scan.

For other hosts, `tiku_richlinkscan.h` exposes the same bounded traversal:

```c
tiku_richlinkscan_t *scan = NULL;
status = tiku_richlinkscan_create(snapshot, caret, 1, &scan);
if (status == TIKU_RICH_OK) {
    status = tiku_richlinkscan_step(scan, 512);
    /* If PENDING, schedule another step on a later event-loop turn. */
    if (status == TIKU_RICH_OK) {
        tiku_rich_link_match_t match;
        tiku_richlinkscan_result(scan, &match);
        /* Use match.first/last at match.generation; copy target if needed. */
    }
    tiku_richlinkscan_release(scan);
}
```

The example releases the scan after one step; a real incremental controller
retains a pending scan until its next turn, or releases it to cancel. One
work unit inspects a run or paragraph boundary. Steps allocate no memory.
The scan retains immutable source content, so its target strings and positions
remain usable until release even if the document is edited or closed. A live
editor must still check the captured generation before moving its selection.

Forward traversal finds a link whose start is at or after the supplied
position. Backward traversal finds a start strictly before it. To move onward
from a selected occurrence, supply its end for Next or its start for Previous.
Adjacent runs with equal targets form one occurrence even when their styles
differ. A paragraph break or unlinked text separates occurrences.

Model link boundaries can fall inside a multi-codepoint character. The scan
returns those exact UTF-8 positions; a visual editor can expand them outward
with `tiku_richlayout_snap()`. TikuWrite does this and remembers the raw
occurrence separately, so two links within one grapheme can still be visited.
Previous/Next finds linked text. **Follow document link** resolves its
`#bookmark` target and moves the caret there. External-link activation is not
provided.

### Save a document location with a bookmark

A bookmark gives one text position a name. It is useful for returning to a
passage that is not a heading, such as a table cell or a paragraph you want to
revise. Unlike a temporary view anchor, it is part of the document: it survives
native save/reopen and recovery. It marks a point, not a selected range.

In TikuWrite, put the caret at the destination, then choose **View / Bookmarks /
Add or move bookmark**. Enter a name and choose Save. Using an existing name
moves that bookmark to the captured caret position. Choose **Go to bookmark**
to enter any name, or pick a name from the submenu's short list. The menu has
room for the first seven names; the name field reaches any saved bookmark.
**Remove bookmark** removes the name without deleting text.

Names are case-sensitive, with 1–63 UTF-8 bytes and no ASCII control characters.
For example, `Results` and `results` are different names. A document supports
up to 1,024 bookmarks. Adding, moving or removing a bookmark makes the document
unsaved and creates one Undo step. Navigating does neither. Read-only views can
navigate but cannot change bookmarks. If the text changes while an add/remove
panel is open, reopen the command; it will not apply an old location by mistake.

Other applications use `tiku_richbookmark.h` directly:

```c
tiku_rich_pos_t anchor, caret;
status = tiku_richsession_selection(session, &anchor, &caret);
if (status == TIKU_RICH_OK) {
    status = tiku_richdoc_bookmark_set(doc, tiku_richdoc_generation(doc),
                                       "Results", caret);
}
```

This records the caret end of a selection. The generation check prevents an
edit prepared against an older document from using stale paragraph IDs. A
host must also honor its session's read-only flag; the model API knows only
the document-wide restriction.

To look up a destination, retain a snapshot and copy out its position:

```c
tiku_richsnapshot_t *snapshot = NULL;
tiku_rich_pos_t destination;
status = tiku_richdoc_snapshot(doc, &snapshot);
if (status == TIKU_RICH_OK) {
    status = tiku_richsnapshot_find_bookmark(snapshot, "Results", &destination);
    /* Check status and the snapshot generation before navigating a live view. */
}
tiku_richsnapshot_release(snapshot);
```

Use `tiku_richsnapshot_bookmarks()` and `tiku_richsnapshot_bookmark()` to enumerate
copied names and positions in insertion order. `tiku_richdoc_bookmark_remove()`
removes one exact name. Missing names return `TIKU_RICH_NOT_FOUND`; updating a
name to the same position returns `TIKU_RICH_NO_CHANGE`. Lookup errors clear
their output. Mutations are owner-thread operations and are refused during
document notification. Snapshot queries do not allocate.

Bookmarks follow the same edit mapping as anchors. Inserting text at the point
leaves a BEFORE bookmark before the insertion and an AFTER bookmark after it.
Deleting surrounding text collapses the point to the replacement boundary.
Deleting its table cell maps it to a surviving paragraph. Undo restores its
original location, including a deleted cell; Redo reapplies the change.

```text
Text:       ab|cd          insert XYZ at the marked position
BEFORE:     ab|XYZcd
AFTER:      abXYZ|cd
```

To create a link, select its label and use **Format / Link** with a target such
as `#Results`. For names containing spaces or non-ASCII characters, percent
encode those bytes, for example `#Test%20results`. The kit's
`tiku_richsnapshot_resolve_link()` validates and decodes such a target, then
looks up the exact name. It does not normalize Unicode or resolve external
URLs. TikuWrite snaps the resulting point to a complete visual character
before selecting it. A missing or unsupported target leaves the selection
unchanged and reports a status message.

Native files and recovery preserve bookmark definitions. Rich clipboard
fragments copy link strings but not bookmark definitions: a pasted internal
link resolves only if the receiving document has the same name. HTML and
Markdown export omit bookmark definitions and report that loss. PDF does not
create named destinations; do not rely on internal links in exported PDFs.

### Store and edit link targets

Each character run can carry a link target separately from its named style
and direct formatting. Set a link on a paragraph-local range with:

```c
tiku_rich_edit_t edit = {
    .kind = TIKU_RICH_LINK_SET,
    .paragraph = paragraph.id,
    .start = 0,
    .end = paragraph.length,
    .link = "https://example.org/notes"
};
```

Use `NULL` or an empty string to remove a link. Assignment and removal keep
the selected text and its character formatting. They are undoable, accept
UTF-8 boundary offsets and do nothing on an empty range. For a link spanning
paragraphs, put one range operation per paragraph in the same transaction.
Imported runs accept the same `link` field and copy the input target.

Targets are NUL-terminated UTF-8 strings of at most 2048 bytes. Storage
validation accepts `http:`, `https:`, `mailto:` and `#fragment` targets;
HTTP(S) uses the `://` form. Unknown schemes and relative file paths return
`TIKU_RICH_UNSUPPORTED`. Malformed UTF-8, empty HTTP(S) destinations, raw ASCII
whitespace/control characters, backslashes, incomplete percent escapes and
percent-encoded ASCII controls are refused. `%20` is allowed for spaces in a
path. `tiku_rich_link_validate()` exposes the same check without allocating.

This is storage validation, not a complete URL parser or authorization to
navigate. The kit does not launch a browser, open a file, fetch a resource,
resolve DNS or check whether a fragment target exists. The host must apply
its own navigation policy and authorization before activating a target.
Use `tiku_richsnapshot_resolve_link()` for named destinations within a snapshot.
Automatic mouse-click activation and external browser launching are separate
host features; TikuWrite uses an explicit menu command for document targets.

Snapshot run queries expose a borrowed `link` pointer, or `NULL` when there
is no link. Keep the snapshot alive while reading it, or copy the string.
Edits and history share immutable, reference-counted target storage. Old
snapshots retain their targets after links are removed or the document
closes. Each new imported target or assigned target is budgeted; the kit
does not maintain a global URL cache.

Typing follows explicit boundary rules:

- Insertion strictly inside a linked range inherits its target, including
  at a formatting-run boundary within that same target.
- Insertion at the beginning or end of the linked range does not extend it.
- Replacing nonempty text that is uniformly linked preserves that target.
  Replacing a mixture of targets or linked/unlinked text does not.

Formatting a link or clearing direct formatting does not unlink it.
Splitting and joining preserve surviving linked text; adjacent runs merge
only when their stored formatting and target values match. No blue color or
underline is written into the text model automatically.

## Find and replace text

Literal search works on a snapshot, not the changing live document. It is
case-sensitive, compares the supplied UTF-8 bytes, and returns nonoverlapping
matches inside an explicit half-open range. It does not perform Unicode
normalization, case folding, whole-word matching, regex matching or automatic
wrapping. LF matches either a soft line break or a paragraph boundary, just
as in plain-text extraction; use LF, not CRLF, in the search term.

Create a search with valid `first` and `last` snapshot positions:

```c
tiku_richsearch_t *search = NULL;
tiku_rich_status_t status = tiku_richsearch_create(
    snapshot, first, last, "Temperature", 11, &search);
```

Check creation before stepping. The search copies the nonempty search term
and retains the snapshot's underlying state. The original snapshot and
live document may be released without invalidating it. A search handle
belongs to one caller at a time; separate searches can run independently.

```c
tiku_rich_match_t match;
status = tiku_richsearch_next(search, 4096, &match);
```

Handle the result as follows:

- `TIKU_RICH_OK`: `match.first` and `match.last` describe the next match.
- `TIKU_RICH_PENDING`: call again with the same handle to continue. A GUI
  should schedule the next step so it can process other events.
- `TIKU_RICH_NOT_FOUND`: the scope is exhausted, including after the last
  successful match.

The work budget counts byte comparisons, including failed-prefix fallback,
not just consumed bytes. Stepping allocates no memory. Search construction
builds a linear-time index of the search term; scanning is linear overall
and does not create a flattened copy of the document. Term storage and its
index count against the document's aggregate allocation limit. A zero work
budget is invalid and does not advance the search.

To replace a returned match in the live document:

```c
status = tiku_richdoc_replace_range(doc, match.generation,
    match.first, match.last, "Reading", 7);
```

The captured generation makes an intervening edit return `TIKU_RICH_STALE`.
After a successful replacement, restart the search on a new snapshot before
replacing further results. There is no replace-all command that silently
reuses stale offsets. Release or cancel with
`tiku_richsearch_release(search)`, including when closing a Find panel.

## Inspect selected formatting

A toolbar needs to distinguish bold text, non-bold text and a selection that
contains both. Use `tiku_richsnapshot_selection()` with an ordered, half-open
range from the same snapshot:

```c
tiku_rich_selection_t state;
tiku_rich_status_t status = tiku_richsnapshot_selection(
    snapshot, first, last, &state);
if (status == TIKU_RICH_OK) {
    if (state.mixed_chars & TIKU_RICH_BOLD) {
        /* Show the control's mixed state. */
    } else if (state.chars.present & TIKU_RICH_BOLD) {
        /* state.chars.bold is the common explicit value, including false. */
    } else {
        /* No selected text specifies bold; use the host's default. */
    }
}
```

`mixed_chars` uses character-property bits, and `mixed_props` uses paragraph
property groups. A differing value is cleared in `chars` or `props`; its mixed
bit tells you why. Uniform absence has neither a presence bit nor a mixed bit.
Absent bold and explicit `bold = 0` are different, because the former can
inherit the layout provider's default. `mixed` separately reports differing
paragraph styles, character styles, list settings and hyperlink targets.
A common link is borrowed from the snapshot; do not keep its pointer after
releasing that snapshot.

Character queries sample selected text runs. Paragraph properties include
each touched paragraph, except an ending paragraph selected only up to byte
zero. If only paragraph separators are selected, character values come from
the first paragraph's defaults. At an empty selection, character formatting
comes from the preceding run, or paragraph defaults at byte zero. Hyperlink
inheritance still follows the link's boundary rules. These queries allocate
no memory and can run on an immutable snapshot after its document closes.

This API reports stored formatting, not a view's pending typing format or
whether an application command is enabled. For command availability, use
the command API below; a read-only document still has queryable formatting.

## Use shared commands for menus and shortcuts

Include `tiku_richcmd.h` and link the same Document Kit library. The command
API uses the document's existing edit and history machinery. It provides
36 commands, which can be enumerated with `tiku_richcmd_count()` and
`tiku_richcmd_at()`. Each catalog entry has a typed identity, stable text key,
English label, required payload kind and selection scope. A host can translate
the label using the stable key. The catalog does not create menus or widgets.

| Commands | Behavior |
| --- | --- |
| Replace Text, Delete Selection, Paragraph Break | Replace the selected range; plain-text line endings create paragraphs |
| Paste Styled Content | Paste an in-process fragment through the same transfer API described below |
| Undo, Redo | Exchange one existing history group |
| Bold, Italic, Underline, Strikethrough | Toggle the selected text using its uniform or mixed state |
| Character Formatting, Clear Character Overrides | Apply values or clear run overrides; clearing retains named styles, links and paragraph defaults |
| Align Left/Center/Right/Justify, Paragraph Formatting | Change touched paragraphs, including the caret's paragraph |
| Paragraph Style, Character Style | Assign document-local named styles; ID zero removes the assignment |
| List Settings | Set membership and level; restart the first selected item and continue subsequent items |
| Set Link, Remove Link | Change link targets without opening them |
| Define Style, Remove Style | Create/update a named definition, or remove one that has no live users |

Supply the host's ordered selection and captured generation:

```c
tiku_richcmd_context_t context = {
    .generation = tiku_richsnapshot_generation(snapshot),
    .first = first,
    .last = last,
    .read_only = 0
};
tiku_richcmd_state_t state;
tiku_rich_status_t status = tiku_richcmd_query(
    doc, &context, TIKU_RICHCMD_BOLD, &state);
```

A successful query means that `state` is available, not that the command is
enabled. If `state.enabled` is false, `state.status` gives the refusal code
and `state.reason` gives a static English explanation. For example, a stale
generation needs a refreshed selection; an empty undo history has nothing
to undo. Queries allocate no memory and do not change the document.

For Bold, Italic, Underline and Strikethrough, `state.check` distinguishes
explicit on, explicit off, mixed values and an unspecified property. Align
commands have equivalent indicators. Link commands report on, off or mixed
targets. Commands without a checkbox return `CHECK_NONE`; use the selection
query for the individual values in a font, paragraph, style or list dialog.
An unspecified property is not an explicit false. The host may display a
layout default, but that default is not stored formatting.

Execute through the same API from a menu, toolbar or keyboard handler:

```c
if (status == TIKU_RICH_OK && state.enabled) {
    const tiku_richcmd_t command = {.kind = TIKU_RICHCMD_BOLD};
    status = tiku_richcmd_execute(doc, &context, &command);
}
```

Execution rechecks availability. A Bold selection that is uniformly and
explicitly on becomes explicitly off. Off, unspecified or mixed selections
become explicitly on. Formatting a range that crosses paragraphs is one
transaction and one undo group. Other commands use the payload member listed
in the catalog, for example:

```c
const tiku_richcmd_t command = {
    .kind = TIKU_RICHCMD_CHARACTER_FORMAT,
    .value.character = {
        .values = {.present = TIKU_RICH_SIZE, .size = 14},
        .clear = TIKU_RICH_HIGHLIGHT
    }
};
status = tiku_richcmd_execute(doc, &context, &command);
```

This sets the selected text to 14 points and removes its direct highlight
override/reset, allowing inherited highlighting to apply. It does not alter
paragraph defaults or named style definitions. Payloads are borrowed for the
call. Availability queries do not validate them: invalid values, unknown style
IDs, unsupported link schemes or memory limits can still refuse execution.
Refusal preserves content, history, IDs and tracked positions.

Paragraph commands exclude an ending paragraph selected only up to byte zero.
Character commands skip empty text slices; selecting only paragraph separators
does not format characters. A caret can receive paragraph formatting, but
character-format commands require selected text. Pending formatting for future
typing belongs to the session API below, not these stateless model commands.

The context's `read_only` flag adds a restriction for one host without making
other hosts read-only. It is not an authorization boundary: a caller holding
the document can still use its lower-level mutation API. Document read-only
state is always enforced. Undo, Redo, Define Style and Remove Style ignore
selection endpoints but still check permissions and generation.

Commands do not own or collapse a host selection. Existing tracked anchors
follow edits; the host decides where its caret goes after a successful command.
Use a fresh generation for a subsequent command, including Undo. Queries report
the current generation so a toolbar cache can be keyed by generation, selection
and permission state. Mutation and command queries belong to the document's
owner thread; this API does not install callbacks or start jobs.
Successful document changes do notify registered document observers.

Character and paragraph command expansion is limited to 1,024 affected
operations, checked before allocation. Plain replacement also counts its joins
and splits. The state query refuses a range that cannot fit even without a
text payload; the payload can require additional operations at execution.
Rich-fragment paste retains its separate paragraph-array path and limits.
These are model commands, not system clipboard actions, search dialogs,
file operations, navigation or link activation.

## Give each editor view a session

Include `tiku_richsession.h`. A session holds a directional selection and the
formatting to apply when that view next types. It does not draw text, move by
graphemes or create a window. Use one session for each editing view that shares
a document. A read-only view can also use a session to track its selection.

```c
tiku_richsession_t *left = NULL, *right = NULL;
tiku_rich_status_t status = tiku_richsession_create(doc, &left);
if (status == TIKU_RICH_OK)
    status = tiku_richsession_create(doc, &right);
if (status != TIKU_RICH_OK) {
    tiku_richsession_destroy(left);
    return status;
}
```

New sessions start at the beginning of the document. Each owns two tracked
anchors and retains the document through them. Destroying one session does not
close another session or invalidate snapshots. After successful construction,
destroy both sessions when their views close:

```c
tiku_richsession_destroy(right);
tiku_richsession_destroy(left);
```

Set a selection with `tiku_richsession_select(session, generation, anchor,
caret)`. The anchor is the end where selection began; the caret is the moving
end. Either direction is accepted. Positions use document-local paragraph IDs
and UTF-8 byte boundaries, as described above. This API checks byte boundaries,
not grapheme boundaries. Passing the same position twice creates a caret.
`tiku_richsession_selection()` returns endpoints in that same directional order.
Selection changes allocate nothing, clear pending formatting and leave content,
dirty state and history unchanged. Invalid or stale requests change nothing.

### Format the next text without changing existing text

At a caret, Bold, Italic, Underline, Strikethrough, Character Formatting,
Clear Character Overrides and Character Style affect only that session's
pending typing attributes. For example:

```c
const tiku_richcmd_t bold = {.kind = TIKU_RICHCMD_BOLD};
status = tiku_richsession_execute(left, 0, &bold);
if (status == TIKU_RICH_OK) {
    const tiku_richcmd_t text = {
        .kind = TIKU_RICHCMD_REPLACE_TEXT,
        .value.text = {"Hello", 5}
    };
    status = tiku_richsession_execute(left, 0, &text);
}
```

On a new empty document, this inserts bold text. The Bold command alone does
not mark the document dirty, add a history entry or notify document observers.
Another view's pending attributes are unchanged. With selected text, the same
command formats that text in the document and creates one undo group.

Pending attributes override the normal insertion inheritance: paragraph
defaults at byte zero, otherwise the preceding run. Clear Character Overrides
clears inherited run overrides and reset bits but retains the named character
style, paragraph defaults and normal link-boundary behavior. Character Style
uses a document-local style ID; zero removes that assignment. Link commands
still require selected text. They do not set a pending link target.

Use `tiku_richsession_query()` for menu and toolbar states, and
`tiku_richsession_format()` for individual values. Both include pending
attributes at a caret and allocate no memory. Query keys must include local
selection/format/permission changes, not just document generation: toggling
Bold at a caret does not advance the document generation.

Pending attributes persist across text insertion and paragraph breaks. An
explicit selection change or a successful formatting, deletion, style-edit or
rich-paste command clears them. Undo/Redo restore the recorded attributes.
Import clears them in every session. Removing a named style also clears
any pending reference to it; undoing that removal does not recreate another
session's discarded pending reference. Direct document edits do not use
session-local typing attributes.

### Selection and undo rules

Text replacement, Delete Selection, Paragraph Break and rich paste collapse
the initiating selection after the inserted content. Formatting retains its
directional selection. Rich paste keeps the fragment's formatting rather than
applying the session's typing attributes.

| Operation | Initiating session | Other sessions |
| --- | --- | --- |
| Edit | Receives the command's selection and typing state | Positions follow the edit's maps; local typing choices remain |
| Undo/redo | Restores the recorded selection and typing state, even after navigation | Unmoved anchors restore exactly; moved/new anchors follow inverse/forward maps |
| Import | Selection moves to the new document start; typing state clears | Same reset |

Undo and Redo refer to document-wide history. Invoking Undo in the right view
can therefore restore a selection in the left view if the left view made
that edit. Closing the left view does not keep it alive through history, and
opening a new view does not reuse its identity.

Publication is atomic: allocation failures, limits, stale generations and
other refusals leave document history, selections and pending attributes
unchanged. A `NO_CHANGE` result also keeps the current selection. History
captures the initiating selection even for formatting edits without position
maps. Each successful document command is one undo group. Use
`tiku_richsession_type()` to group compatible adjacent insertions; use
`execute()` for paste and indivisible composition commits. Undo and Redo
allocate no memory.

Typing groups end on selection changes, formatting, paragraph breaks,
other edits, save checkpoints and explicit `tiku_richsession_break_undo()`.
Call the explicit break function on focus loss, composition start or a host
typing timeout. No clock is read inside the kit. Grouping is conservative:
creating, moving or removing other anchors can also prevent a merge.
Replacing selected text does not merge with subsequent insertion. A merged
group keeps the original before-state, final after-state and both views'
correct anchor bindings. Failure does not partially merge the previous entry.

`tiku_richsession_set_read_only()` adds a local editing restriction. Selection
and formatting queries remain available, but document edits and pending-format
commands are refused. A read-only session does not make other sessions
read-only. Global document read-only state applies to them all.

### React to selection changes

Register a callback on a session when your host needs to refresh a toolbar,
status bar or accessibility description after the caret moves:

```c
static void selection_changed(void *context, tiku_richsession_t *session,
                              const tiku_rich_selection_change_t *change)
{
    struct editor_host *host = context;
    (void)session;
    (void)change;
    host->refresh_controls = 1;
}

/* One observer per session. Replacing it does not send an initial event. */
tiku_richsession_observe_selection(session, selection_changed, host);
/* Before releasing the host's context, remove the observer or session. */
tiku_richsession_observe_selection(session, NULL, NULL);
```

The event contains the old and new anchor/caret positions in their original
direction, plus the current document generation. Moving the caret does not
edit the document or advance that generation. Changes to either endpoint or
its affinity send an event; selecting the same values again does not.

You also receive events when an edit, Undo, Redo or import changes your
session's endpoints. Each session observes its own selection. Document-change
callbacks run first, followed by affected selection observers. The event's
previous positions describe the old state and may no longer be valid in the
current document. Read current content using the new positions.

Registration and delivery allocate no memory. The context stays yours and
must outlive registration. Callbacks can inspect content and selection, but
must defer document/session mutations, destruction and observer replacement
until after delivery. Schedule command-availability queries for afterward
too: they report BUSY during callbacks. Pending typing-format changes and
cell-rectangle shape changes are not selection endpoint events. This API
does not provide native screen-reader integration.

## Observe published document changes

Include `tiku_richwatch.h` and register a callback with
`tiku_richwatch_create(doc, callback, context, &watch)`. The callback receives
the document plus the change kind, content generation, stable save-state
token, originating session ID (zero for programmatic changes), and dirty flag.
Undo/Redo report the original edit's session ID, even if that session closed.

Notifications cover content edits, Undo, Redo, import, changed save checkpoints,
changed read-only state and explicit removal of nonempty history. Metadata
changes do not advance content generation. Import emits one import notification,
not a second notification for the history it clears. Failed/no-change edits
and local selection or pending-format changes emit no document events.
Selection observers, described above, handle endpoint changes separately.

Callbacks run synchronously, newest subscription first, on the document owner
thread **after** content, history, anchors and session state have been updated.
Delivery itself allocates nothing. The callback may read formatting, inspect
selections, capture a snapshot or schedule a repaint. Its event pointer lasts
only for the callback; copy any values needed later.

Do not edit the document, move a session's selection or destroy subscriptions
inside a callback. Status-returning mutation calls return `TIKU_RICH_BUSY`;
void document/anchor release, read-only setter and history-clear calls do
nothing there. Snapshot release remains permitted.
Defer those actions until after notification. Command queries report BUSY
during delivery; queue toolbar-availability refreshes for afterward. Reads of
content, selection and formatting remain available.

A subscription retains its document. Keep its callback context alive until
`tiku_richwatch_destroy(watch)` succeeds. Destruction outside notification
guarantees no further callbacks. The limit is 1,024 subscriptions per document;
their storage counts toward the aggregate allocation ceiling. Sessions and
subscriptions are owner-thread objects, not worker-thread listeners.

## Copy and paste styled content

A fragment contains selected text, paragraph properties, lists, links and
the named styles needed to interpret them. It can be pasted into the same
document or another document in the same process. The model operation does
not access a clipboard. Document Formats package can encode the fragment for transfer
to another process, as described below.

```c
tiku_richfragment_t *fragment = NULL;
tiku_rich_status_t status = tiku_richfragment_create(
    source_snapshot, source_first, source_last, &fragment);
if (status == TIKU_RICH_OK) {
    status = tiku_richdoc_paste_fragment(destination,
        tiku_richsnapshot_generation(destination_snapshot),
        destination_first, destination_last, fragment);
}
tiku_richfragment_release(fragment);
```

Endpoints must belong to their respective snapshots. Paste checks the live
destination generation; if it changed, refresh the destination selection
before retrying. Check the paste result before treating a transfer as a move
or deleting source content. The API does not make changes to two documents
atomic.

Paste follows these rules:

- Copied characters keep their resolved formatting and link targets. Named
  character-style references are remapped to destination IDs. Direct values
  and resets are added only where needed to preserve character appearance
  under a different paragraph's defaults.
- A destination paragraph with a surviving prefix keeps its paragraph
  settings. A trailing paragraph with a surviving suffix keeps its settings.
  If prefix and suffix end up in one paragraph, the prefix takes precedence.
  Complete imported paragraphs keep their source settings and list membership.
- A style with the same category, name, mapped parent and stored properties
  is reused. A conflicting imported name gets a suffix such as
  `Body (paste 1)`. Existing destination styles are not overwritten. Repeating
  the paste reuses a matching renamed definition.
- The first output paragraph keeps the first destination paragraph's ID.
  Further output paragraphs and newly imported styles receive fresh
  destination IDs. Numeric IDs from the source are not portable references.
- A selected paragraph separator remains a separator, even when no character
  text is selected. An empty fragment deletes the destination selection;
  pasting it at an empty selection returns `NO_CHANGE`.
- Successful paste is one undo group. Tracked positions follow the entire
  replacement. Any refusal leaves content, IDs, history and positions intact.

Captured text and run arrays are copied without retaining the source root
or unselected paragraphs. Only referenced styles and their ancestor chains
are included. Link strings are reference-counted with the source allocation
owner. The fragment can outlive both the source document and source snapshot;
a custom allocator context must also remain alive that long. Use
`tiku_richfragment_snapshot()` to inspect a fragment with the ordinary
snapshot query functions. Its IDs describe fragment content, and its state
token is zero: it cannot mark a document saved.

Paste allocates imported content and link strings through the destination's
allocator. After paste returns, the fragment can be released without affecting
the destination. Retain the fragment to paste it again. Fragment handles have
one release obligation; arrange their lifetime before sharing across workers.

Capture and paste are synchronous and count against their respective owners'
memory budgets. Nonempty rich paste builds a replacement paragraph array
directly, so it does not have the 1,024-primitive-operation limit of the plain
range helpers. Document size, run, style, aggregate-memory and history limits
still apply. Empty-fragment deletion uses the plain range helper and its
operation limit. There is no serialized or untrusted fragment decoder here.

## Save state

Generation increases on import, edit, undo and redo. A state token identifies
the document contents held by a snapshot and is restored by undo/redo.
Use `tiku_richdoc_dirty()` for unsaved-change state, not a generation
comparison.

For a worker save, capture a snapshot on the document thread and save that
snapshot's contents. Once storage reports success, call
`tiku_richdoc_mark_saved(doc, snapshot)` on the document thread. If the user
edited meanwhile, the newer document remains dirty. Undoing to the saved
state clears its unsaved changes. A snapshot from a different document is refused, as is
a fragment snapshot, even if it was captured from this document.
The host must serialize save completion notifications for a destination;
this model does not coordinate multiple overlapping file writes.

## Memory limits

Zero-valued limit fields select defaults:

| Limit | Default |
| --- | --- |
| UTF-8 paragraph text | 64 MiB |
| Paragraph count | 250,000 |
| Formatting runs | 1,000,000 |
| Named styles | 4,096 |
| Live tracked positions | 4,096 |
| Aggregate allocated storage | 512 MiB |
| Maximum extra storage for undo and redo | 64 MiB |

The aggregate includes allocation headers, document metadata, snapshots,
search handles and their term indexes, fragments and captured styles,
sessions, subscriptions, retained history and unpublished candidate edits.
Imports and edits may
need room for the old and new states at the same time. A custom allocator
must provide both allocation and release hooks, aligned storage and a
context that outlives surviving snapshots, searches and fragments. Its hooks
can run on a worker; allocator hooks must not reenter the kit.

History has a separate budget for the additional storage needed to undo and
redo. Unchanged paragraphs, shared links, table structures and shared picture
pixels count once, even when several document versions refer to them.
Different allocations with identical contents still count separately. The
count includes allocation headers, history records, position maps and captured
anchor positions; it excludes the storage needed for one current document.

Undo can make a much smaller document current. For example, undoing a picture
insertion moves the picture into redo history. The allocator therefore checks
the largest extra cost among the states that Undo and Redo can reach. This
reservation stays unchanged while moving through history. It is the unique
storage of the current and retained document versions, plus their history
records, minus the smallest complete version's unique storage.

For example, suppose two versions share 100 KiB. The smaller version has
another 2 KiB and the larger one another 6 KiB. With 1 KiB of history records,
the union occupies 109 KiB. The history reservation is 7 KiB: 109 minus the
smaller version's 102 KiB. When the larger version is current, the additional
history storage is only 3 KiB. Undo makes the smaller version current, raising
that cost to 7 KiB without allocating anything.

| Query | What it reports |
| --- | --- |
| `tiku_richdoc_memory(doc)` | Actual kit allocation charges, including snapshots, layouts and temporary work |
| `tiku_richdoc_history_memory(doc)` | Largest additional history cost across reachable versions; checked against `history_bytes` |
| `tiku_richdoc_history_retained_memory(doc)` | Additional history storage relative to the version that is current now |

Do not add these values: history allocations are already part of the total.
External snapshots are not subtracted from the history counts. Clearing
history can therefore free less than the retained-memory query reports when
a snapshot or another retained reader still needs some of the same data.

Publication keeps the longest newest-first sequence of whole undo groups that
fits, removes older groups, and clears redo. If even the new edit's two states
and history record do not fit, the edit fails without changing the document
or discarding history. Candidate allocation can also fail at the aggregate
ceiling before pruning. Undo and Redo still allocate no storage.
`tiku_richdoc_clear_history()` changes neither current content nor saved state.

Accounting runs on the document's owning thread and allocates no scratch
buffers. It visits the candidate versions and retained root/paragraph arrays;
shared paragraph payloads are traversed only once per census. The cost still
grows with document size and the number of retained versions. It is not a
constant-time or asynchronously stepped operation.

## Transfer a styled fragment between processes

Include `tiku_richclip.h` from Document Formats package to encode a captured fragment.
This preserves selected paragraphs, their properties and lists, referenced
named styles and their parents, links, and inline pictures. It does not copy
unselected paragraph text or unused style definitions. A selection within
one table cell can be captured; a rectangle of cells cannot.

```c
unsigned char *payload = NULL;
size_t payload_bytes = 0;
tiku_richio_status_t result = tiku_richclip_write(
    fragment, NULL, &payload, &payload_bytes);
if (result == TIKU_RICHIO_OK) {
    /* Send exactly payload_bytes as TIKU_RICHCLIP_MIME binary data. */
}
free(payload);
```

The receiver decodes the bytes into a new fragment, then uses the ordinary
paste API. Decoding does not edit any existing document:

```c
tiku_richfragment_t *received = NULL;
tiku_richio_status_t result = tiku_richclip_read(
    payload, payload_bytes, NULL, &received);
if (result == TIKU_RICHIO_OK) {
    tiku_rich_status_t pasted = tiku_richdoc_paste_fragment(
        destination, expected_generation, first, last, received);
    /* Check pasted before reporting success. */
    (void)pasted;
}
tiku_richfragment_release(received);
```

The decoded fragment owns its content. You can discard the incoming byte
buffer immediately after decoding, and release the fragment after paste.
Pasted styles and pictures belong to the destination's allocation context.
Custom allocator contexts supplied in `options.document` must remain valid
until the decoded fragment and any snapshots of it are released.

The complete payload is limited to 16 MiB. Both directions default to a
64 MiB codec scratch budget; decoding also defaults to a separate 64 MiB
model budget. `options.xml_bytes` can lower the XML limit. Scratch limits
also bound the simultaneous XML and envelope buffers when encoding.
The supplied input buffer and an existing source document are outside those
codec budgets. The decoder's temporary document and captured fragment share
its model budget. Limits can cause a transfer to fail even when the source
document is valid.

Malformed input, unsupported features, size limits, allocation failures and
cancellation return an error with cleared outputs. Cancellation is polled
while the native codec works and before/after fragment capture; capture itself
is synchronous. This is not a measured cancellation-latency guarantee.
Paper settings in the encoded snapshot do not replace the destination's paper
settings during paste.

This codec does not own the clipboard. Application Kit's `tiku_clip_rpc.h`
provides a separate typed clipboard protocol, used by TikuWrite to publish
rich data and a plain-text fallback together. A matching desktop confirms
publication before TikuWrite removes a Cut selection. Paste reads a fresh
snapshot; changed documents/selections and timed-out replies are refused.
Do not pass binary data to `clip_set()` or treat it as NUL-terminated text.
Other desktop applications receive fallback text, with `[Picture]` for
pictures. Older hosts retain the text-only path and refuse picture Copy/Cut.
With a multi-format-capable desktop, TikuWrite also publishes HTML for native
applications. Formatting, links, lists, nested tables and embedded PNG pictures
use the existing HTML exporter. The plain, native and HTML forms together must
fit 16 MiB. Copy conversion is synchronous and may take time for large pictures.
Rectangular cell transfer is described below.

### Copy and paste with native applications

On Linux/X11, the native adapter advertises `text/html` alongside text. On
macOS, it uses the HTML pasteboard type. TikuWrite prefers native Tiku fragments
inside the desktop; external selections use HTML when offered, otherwise plain
text. Other TikuDesktop applications still see plain text through their usual
clipboard service. The Cocoa implementation has an isolated pasteboard test
target, but still needs execution on macOS before release qualification.

HTML paste accepts formatted paragraphs, links, lists, embedded PNG pictures,
or one table with optional nested tables. It does not fetch remote pictures,
run scripts, or open link targets. The status bar reports markup conversion
losses. A mixed HTML selection containing text outside a table, multiple
top-level tables, or flat text pasted into a selected cell rectangle is refused
without changing the document. Plain-text TSV remains available for cells.
Malformed rich data is refused rather than pasted as an unexpected fallback.
An empty imported paragraph does not delete the current selection.
Choose **Edit / Paste as plain text** to use the plain fallback explicitly:
Ctrl+Shift+V, or Command+Shift+V on macOS. It discards source formatting and
pictures, keeping readable picture placeholders when supplied by the source.
For a cell rectangle, it reads tab-separated text. An HTML-only selection
without plain text leaves the destination unchanged.

Native transfers keep both representations until complete and check ownership
before publishing. A failed or stale transfer cannot apply a partly received
HTML document. A new plain-text copy removes preceding HTML. The macOS backend
uses the first pasteboard item; multi-item joins, RTF and non-UTF-8 HTML are not
supported. See [the clipboard contract](../interface/doc/clipboard.md) for the
multi-format API and version-1/version-2 compatibility rules.

## Contracts and remaining work

The [behavior contract](../../docs/contracts/document.md) maps the public
guarantees to tests. [References](REFERENCES.md) records the design source
and Unicode baseline.

Notes and TikuWrite supply window hosts for the
window-independent view. Full OpenType shaping, bidi, preedit composition,
merged cells, floating images, multiple sections,
headers/footers, footnotes,
editable Office
interchange, RTF clipboard interchange and native printing remain outside the
implemented scope. Single-section pages and PDF export do not complete those
larger features.

## Insert and edit a picture

A picture is part of a paragraph. It moves with the surrounding text and
occupies one selectable object, even though its UTF-8 marker uses three bytes.
The document owns the pixels: changing or deleting the original image file
does not change a saved document.

For a host with an existing editing session, this inserts a small picture at
the caret, or replaces a selection within one paragraph:

```c
const uint32_t pixels[4] = {
    0xffff0000, 0x8000ff00,
    0xff0000ff, 0x00000000
};
tiku_rich_image_t picture = {
    .pixel_width = 2, .pixel_height = 2, .pixels = pixels,
    .width = 96, .height = 96,
    .alt = "Four colour samples, including transparency"
};
tiku_richcmd_t command = {.kind = TIKU_RICHCMD_IMAGE_INSERT};
command.value.image = &picture;
tiku_rich_status_t status =
    tiku_richsession_execute(session, 0, &command);
if (status != TIKU_RICH_OK) {
    /* Report the refusal; the document and selection remain unchanged. */
}
```

Pixels use straight-alpha `0xAARRGGBB`, one row after another. Pixel dimensions
describe the source; `width` and `height` describe its printed size in points.
One point is 1/72 inch. The insertion copies pixels and alternate text before
returning. The caller may then release the source buffer.

Click a picture in Rich View to select it. Drag its bottom-right grip to
resize while keeping its aspect ratio. The outline previews the new size;
releasing the pointer publishes one edit, so one Undo restores the old size.
Deactivation, reflow or an intervening document edit cancels that drag.
Read-only sessions allow selection but do not offer a resize grip.

For programmatic changes, select exactly the picture and execute
`TIKU_RICHCMD_IMAGE_SET` with a complete image descriptor. A descriptor copied
from a live snapshot can change dimensions or `alt` while keeping `pixels`.
Such revisions share the immutable pixel buffer. Keep the snapshot alive
until the call returns. Neither metadata edits nor failed operations move
the selection.

Each image is limited to 4,194,304 decoded pixels. Display dimensions must be
finite and between 1 and 14,400 points. Alternate text is valid UTF-8 of at
most 1,023 bytes. These limits do not replace the document's aggregate memory
budget. Deleting a picture releases its pixels when undo history, snapshots
and fragments no longer retain them.

Checked import accepts an `image` on a run only when that run covers exactly
one `U+FFFC` marker. A marker without an image, or an image on ordinary text,
is refused. Plain-text insertion cannot manufacture picture records. Internal
styled fragments retain pictures, and cross-document paste copies their
resources into the destination allocation budget. TikuWrite uses typed
clipboard transfer for pictures when its desktop supports it. A host offering
only the legacy text clipboard refuses picture Copy/Cut without deleting
anything.

Native `.tkrd` documents with pictures declare the required `inline-images`
feature. Each image run stores dimensions and escaped alternate text in its
XML child. The first occurrence of a pixel payload stores endian-independent
hexadecimal ARGB pixels. Repeated identical pixels use a backward reference
and add the required `image-references` feature. Display size and alternate
text remain independent for each occurrence. Pixel matching checks dimensions
and exact bytes, not a hash alone or the original filename. The package
does not depend on an external path or image decoder when reopening. The XML
size and scratch-memory limits still apply; raw pixels make large pictures
substantially larger than their original JPEG or PNG files. Readers without
the declared required features must refuse the document, not discard pictures.
This reduces native file size, including recovery files and rich clipboard
payloads. It does not deduplicate the live model's separately imported image
allocations or convert raw pixels to compressed PNG/JPEG data.

The shared preview and PDF output include pictures and transparency. External
hyperlinks on pictures are retained in the PDF over the picture rectangle. PDF
export permits at most 16 Mi pixels summed over picture occurrences, in
addition to its output-byte limit. A picture whose line is taller than the
printable page is refused by pagination; resize it or change the paper setup.

In TikuWrite, choose **Format / Insert picture** for PNG, JPEG or BMP files.
The file is decoded on a worker and inserted only if the target document and
selection still match. Initial size fits the printable area where possible.
**Picture width** changes width while preserving the ratio; **Picture
alternate text** edits up to 255 UTF-8 bytes in the application's text field.
Longer imported alternate text is preserved, and that field refuses to
truncate it. Plain-text export substitutes `[Image: alternate text]` for each
picture; native saving and PDF export preserve the picture itself.

## Tables and cells

A table is a document block, not text padded with spaces. It has a rectangular
grid of cells. Each cell contains paragraphs and can contain another table.
That structure keeps two neighboring cells separate when their text wraps or
the user presses Backspace at the start of a cell.

```text
Document body
├── Paragraph: Sensor report
├── Table: 1 row, 2 columns
│   ├── Cell
│   │   ├── Paragraph: Temperature
│   │   └── Paragraph: degrees C
│   └── Cell
│       └── Paragraph: 24.5
└── Paragraph: End of report
```

### Insert and remove a table

Place the session caret in a paragraph, with no selection. This command
inserts a table **after the whole paragraph**, not at its current byte offset:

```c
tiku_richcmd_t command = {
    .kind = TIKU_RICHCMD_TABLE_INSERT,
    .value.table = {
        .rows = 2, .columns = 3,
        .width = 0, .padding = 4, .border = 1,
        .color = 0xff707070
    }
};
tiku_rich_status_t status =
    tiku_richsession_execute(session, 0, &command);
if (status != TIKU_RICH_OK) {
    /* Report the refusal; the document and selection remain unchanged. */
}
```

Width, padding and border thickness are in points. A zero width fills the
available space. Columns start at equal widths; a row grows to fit its tallest
cell. Layout can expand the requested width to fit padding, nested tables or
embedded pictures. A table wider than the view sets the layout overflow
warning; it does not collapse its cells or invalidate the document. TikuWrite
can zoom out to show a wider table.

Insertion creates one empty paragraph per cell, selects the first cell and
adds a paragraph after the table. Inside a cell, insertion creates a nested
table. Each insertion is one undo group. Undo restores the original caret;
Redo selects the first cell again.

Use `TIKU_RICHCMD_TABLE_REMOVE` with an empty selection in a cell to delete its
immediate containing table. There is no parameter. The command deletes that
table's contents and any nested tables. Undo restores them. If removal would
leave the document or enclosing cell without content, the model creates one
empty paragraph there so it remains editable.

In TikuWrite, choose **Table / Insert table**, enter a size such as `2 x 3`,
and apply it. Enter creates another paragraph in the cell. Tab moves to the
next cell; Shift+Tab moves back. From the final cell, Tab moves to the paragraph
after the table when one exists. **Table / Delete table** removes the table
containing the caret.

### Add or delete rows and columns

Place the caret in a cell, then use the Table menu to insert a row above or
below it, insert a column to its left or right, or delete its row or column.
These actions affect the immediate containing table. For a nested table,
place the caret in the outer cell's own paragraph to edit the outer table.

Each action is one undoable change. New cells start with an empty paragraph.
Surviving cells keep their text, formatting, pictures, nested tables and IDs.
Deleting a row or column also deletes everything inside its cells; Undo
restores that content. The last row or column cannot be deleted this way.
Use **Delete table** to remove the whole table.

The corresponding session commands have no payload:

```c
tiku_richcmd_t command = {.kind = TIKU_RICHCMD_ROW_AFTER};
tiku_rich_status_t status =
    tiku_richsession_execute(session, 0, &command);
```

The six identities are `ROW_BEFORE`, `ROW_AFTER`, `ROW_REMOVE`,
`COLUMN_BEFORE`, `COLUMN_AFTER` and `COLUMN_REMOVE`, each prefixed with
`TIKU_RICHCMD_`. Query availability before enabling a menu item. A selection
must be collapsed to a caret inside a cell; insertion also respects the
table dimension and document resource limits.

After insertion, the initiating caret moves to the new row or column, keeping
its other coordinate. After deletion, it moves to the next cell at the same
coordinate, or the preceding cell if the deleted axis was last. Other views'
carets in surviving paragraphs keep their exact byte offsets. Carets in
deleted content move to the start of a nearby surviving cell. Undo restores
recorded caret positions; newly created carets also remain valid through undo.

For an edit addressed by table ID rather than a caret, use the primitive API:

```c
tiku_rich_edit_t edit = {
    .kind = TIKU_RICH_TABLE_GRID,
    .object = table_id,
    .grid = {.operation = TIKU_RICH_COLUMN_INSERT, .index = 0}
};
tiku_rich_transaction_t transaction = {0, &edit, 1};
tiku_rich_status_t status = tiku_richdoc_apply(doc, &transaction);
```

`index` is zero-based. Inserting at the current row/column count appends;
deletion requires an existing index. A grid edit must be the only primitive
in its transaction. No file-format extension is needed: native saves record
the resulting table structure.

### Change column widths and table appearance

In TikuWrite, place the caret in a cell and choose a command from **Table**:

- **Table size and padding** sets the outer width and the space inside each
  cell, in points. Width `0` fits the available space; a positive width is
  between 1 and 14,400 points. Padding is between 0 and 72 points.
- **Column width** sets the current column's percentage of the table width.
  Enter 1–99 for a table with several columns, or 100 for a single column.
  Other columns keep their proportions relative to one another.
- **Equal column widths** gives every column the same share again.
- **Table borders** sets thickness from 0 to 12 points and a `#RRGGBB` colour.
  Thickness `0` hides the borders. `#AARRGGBB` also accepts an alpha value.

These settings apply to the immediate containing table and can be undone.
Applying a panel after editing the document or moving to another column is
refused; reopen the settings for the intended table. Large pictures, padding
or nested tables may force the table wider than its requested size. The
column proportions remain unchanged when that happens.

The kit stores **relative weights**, not percentages or pixel widths. For
example, weights `{3, 2}` divide the outer table width into 60% and 40%.
All-zero weights select equal columns. Otherwise, every active column needs
a finite weight from 0.001 to 1,000; entries beyond `columns` must be zero.
Weights include the column's padding and border. The array is stored by value
and is covered by document and history memory budgets.

Read the current table before changing it, so other settings are preserved:

```c
tiku_richsnapshot_t *snapshot;
tiku_rich_block_t table;
unsigned column;
tiku_rich_status_t status = tiku_richdoc_snapshot(doc, &snapshot);
if (status == TIKU_RICH_OK) {
    status = tiku_richsnapshot_table(snapshot, caret.paragraph,
                                    &table, &column);
    uint64_t generation = tiku_richsnapshot_generation(snapshot);
    tiku_richsnapshot_release(snapshot);
    if (status == TIKU_RICH_OK && table.table.columns == 2) {
        tiku_richcmd_t command = {
            .kind = TIKU_RICHCMD_TABLE_SET,
            .value.table = table.table
        };
        command.value.table.weights[0] = 3;
        command.value.table.weights[1] = 2;
        status = tiku_richsession_execute(session, generation, &command);
    }
}
```

Here `caret` must be the session's current collapsed selection. The lookup
returns `TIKU_RICH_INVALID_RANGE` outside a table. `TABLE_SET` replaces the
properties but cannot change row or column counts; use the grid commands for
that. A no-op returns `TIKU_RICH_NO_CHANGE`. For edits addressed by table ID,
use `TIKU_RICH_TABLE_SET`, `.object = table.id` and `.table = table.table` in
the primitive transaction API. Property edits leave paragraph IDs, text and
caret offsets unchanged.

Inserting a column gives it the mean of the existing weights and retains the
other weights. Deleting a column removes its weight. In both cases the shares
are recalculated over the remaining columns; changing rows keeps widths
unchanged. Undo restores the earlier geometry.

### Resize columns with the mouse

In TikuWrite, move the pointer over an internal column boundary. A horizontal
resize cursor indicates that it can move. Drag to position the blue guide,
then release to change the two adjacent columns. The table's width and other
columns stay unchanged, and the view does not jump back to the text caret.
Undo restores the previous widths in one step.
Escape cancels the drag. You can also resize a table whose borders are hidden.

The divider stops before either column becomes too narrow for its padding,
pictures or nested tables. Content in every row contributes to this limit.
The text can wrap as a column narrows. This is different from the numeric
**Column width** command, which redistributes space across the other columns.

Kit hosts pass pointer events to `tiku_richview_event()` in viewport-local
pixels, including move and release events outside the viewport during capture.
`tiku_richview_divider_at()` tests a four-pixel region around each divider for
cursor feedback. `tiku_richview_divider()` returns the active guide; both return
geometry in document points. The shared font view paints that guide. A custom
renderer must apply zoom and scroll to it, just as it does to cell rectangles.
The lower-level `tiku_richlayout_divider()` accepts document points and an
explicit hit tolerance. Nested-table dividers take priority over outer ones.

Dragging records the document generation and selection revisions. Edits,
selection changes, read-only mode, reflow, scrolling and deactivation invalidate
the drag; keyboard and committed-text input end it before being processed.
Mouse movement changes only the guide. Release applies one `TABLE_SET`
transaction. Allocation failure leaves widths and history unchanged.

### Shade a cell

Place the caret in a cell and choose **Table / Cell shading**. Enter a colour
such as `#DAEEFF`, or `#80DAEEFF` for partial transparency. **Clear cell shading**
removes it; entering `none` in the panel has the same effect. The fill covers
the cell behind its text and pictures, even when table borders are hidden.
It appears in editing, preview and PDF and is stored in native files.

Use this command from another host with an empty selection inside the cell:

```c
tiku_richcmd_t command = {
    .kind = TIKU_RICHCMD_CELL_SET,
    .value.color = 0xffdaeeff
};
tiku_rich_status_t status = tiku_richsession_execute(session, 0, &command);
```

Colours are straight-alpha ARGB: the high byte is opacity, followed by red,
green and blue. Zero removes the fill. `tiku_richsnapshot_cell()` resolves a
paragraph's immediate containing cell and exposes its `background`. For an
ID-addressed transaction, use `TIKU_RICH_CELL_SET`, `.object = cell.id` and
`.background = colour`. Imports can set `background` on CELL records only.
The command changes no text or identities and supports undo. Row/column
changes keep shading with surviving cells; new cells start without shading.
The TikuWrite panel refuses Apply if its cell or document has changed.

### Select and edit a rectangle of cells

A cell selection is separate from a text selection. It identifies a table,
a zero-based starting row and column, and the number of rows and columns.
It can therefore select a column without also selecting text from the cells
beside it. The selection belongs to a Rich View, not to the saved document.
Two views can select different cells in the same document.

For example, select the first two rows and three columns in a known table:

```c
tiku_rich_cells_t cells = {
    .table = table_id,
    .row = 0, .column = 0,
    .rows = 2, .columns = 3
};
tiku_rich_status_t status =
    tiku_richview_select_cells(view, expected_generation, &cells);
if (status == TIKU_RICH_OK) {
    tiku_richcmd_t command = {
        .kind = TIKU_RICHCMD_CELL_SET,
        .value.color = 0xffddeeff
    };
    status = tiku_richview_execute(view, expected_generation, &command);
}
```

Check the returned status before reporting success. The rectangle must fit
in the table. A nonzero generation must match the current document; zero
accepts the current state. Invalid ranges and failed edits leave the
document and selection unchanged. Shading is one undoable change, including
when the rectangle contains all 4,096 permitted cells. It changes the direct
cells' backgrounds, not separately stored backgrounds in nested tables.

Use `tiku_richview_query()` and `tiku_richview_execute()` for a host's typed
commands when cell selection is enabled. They handle ordinary text commands
through the view's session. With cells selected, CELL_SET applies shading,
DELETE clears contents, REPLACE_TEXT pastes tabular text, and undo/redo use
document history. Other commands
are refused instead of acting on the underlying text caret. Calling the
lower-level session API directly does not interpret a view's cell rectangle.

`tiku_richview_cells_clear()` removes text and inline pictures from selected
cells, including paragraphs in nested tables. It retains paragraph breaks,
paragraph properties, cell shading and the table structure. Clearing more
than 1,024 nonempty paragraphs at once returns LIMIT without changing any
of them; select a smaller range. Clearing already empty cells is a no-op.
Both clearing and shading retain the selection on success. Undo/redo and
other document edits clear it. Moving the text selection through the session
also invalidates the cell selection. Resizing and scrolling preserve it.

The shared input handler supports Alt/Option-drag to select cells and
Shift-click to extend an existing cell selection. Nested tables are selected
independently; extending a range stays in its original table. With cells
selected, Shift+arrow keys move the selection's far end by one cell.
Shift+Home/End reaches the first/last column of that row;
Ctrl+Shift+Home/End (Command on macOS) reaches the first/last cell of the
table. The starting cell stays fixed, so reversing direction shrinks the
rectangle and can extend it past its starting cell. Keyboard movement
scrolls the moving cell into view vertically. A cell taller than the viewport
is shown from its top. These operations also work in read-only views.
Escape returns
to text editing. Delete or Backspace clears selected contents. Typing and
text formatting are refused while cells are selected. The host still owns
clipboard transport, window controls and repaint scheduling.

For a toolbar or other input adapter, call
`tiku_richview_cells_extend(view, TIKU_RICH_MOVE_DOWN)`, for example.
It requires a current cell selection and completed layout. A rectangle set
with `select_cells()` starts at its top-left cell, with the bottom-right
cell as its moving endpoint. A pointer selection keeps the two cells in
the order they were selected. Shading and clearing preserve that direction.

To scroll while dragging, keep delivering captured pointer moves and the
primary-button release, even outside the editor's bounds. Also call the
view's tick function from the host event loop:

```c
/* now_us comes from the host's monotonic clock. */
tiku_rich_status_t status = tiku_richview_tick(view, now_us);
if (status == TIKU_RICH_OK) request_repaint();
```

The view does not create a timer or thread. Continue ticking when the pointer
is stationary: holding it above or below the viewport scrolls through the
same table. The first tick establishes the clock; subsequent ticks move at
a speed based on the pointer's distance outside the view. A long pause is
capped at 100 ms of scrolling per tick. Backward time resets the clock,
and negative time returns INVALID. Horizontal dragging chooses the nearest
column; it does not introduce horizontal scrolling.

Pointer release, keyboard input, wheel scrolling, deactivation, reflow and
viewport-size changes end the drag. Content changes or external session
movement cannot continue an old drag. TikuWrite supplies the tick and
pointer capture routing; another host must supply its own event-loop calls.

`tiku_richview_cells_rectangles()` supplies one document-coordinate highlight
rectangle per selected row. The shared font painter uses these rectangles,
including zoom and scrolling, without tinting nested cells a second time.
No selection highlight appears in printed output or saved files.

For a headless application, include `tiku_richcells.h`. Use
`tiku_richsnapshot_cells()` to resolve two direct-cell IDs into a rectangle,
and `tiku_richsnapshot_cell_at()` to enumerate its members. Both endpoints
must belong to the same immediate table. Apply a `TIKU_RICH_CELLS_SET` edit
with its `cells` and `background` fields to shade without creating a view.
`tiku_richdoc_cells_clear()` similarly clears contents without a view.

`tiku_richsnapshot_cells_text()` exports quoted tab-separated text. Query
the required size with NULL/0, then provide a buffer with one extra byte for
the final NUL. Cells are separated by tabs and rows by LF. Each cell is
quoted; quotes inside its text are doubled. Paragraph breaks remain inside
the quoted field. A short buffer is left untouched. Pictures and nested
tables are refused rather than silently omitted. This exports text, not
formatting or editable table structure.

In TikuWrite, **Table / Select cell**, **Select row**, **Select column** and
**Select table** provide alternatives to dragging. Cell shading and Clear
cell shading apply to the rectangle; **Clear cell contents** keeps the grid.
Copy preserves rich cell contents when the desktop supports typed clipboard
messages; other applications receive quoted tab-separated text. Cut waits
for publication confirmation, then clears the contents without removing the
cells. Paste follows the shape rules below. Press Escape to resume ordinary
text editing. Older hosts retain text-only cell Copy/Paste and refuse Cut.

### Copy formatted cells

TikuWrite-to-TikuWrite cell transfer preserves paragraph formatting, named
styles, lists, links, pictures and cell shading. Select one starting cell to
paste a copied rectangle down and right, or select a rectangle of exactly
the same size. The destination table must have enough room; paste does not
insert rows, change column widths or remove cells. Undo restores the whole
replacement in one step. Nested tables are copied with their contents,
column proportions, borders and repeated headers. Existing nested content
in the destination cells is replaced, not combined with the pasted content.

Applications using Document Kit directly can transfer from a snapshot:

```c
/* target_view already has a cell selection. source_cells describes the
   cells in source_snapshot; its table ID belongs to that snapshot. */
tiku_rich_status_t status = tiku_richview_cells_paste(
    target_view, source_snapshot, &source_cells);
```

The snapshot stays caller-owned and can come from the destination document;
overlapping self-paste reads the captured contents. The model-only equivalent
is `tiku_richdoc_cells_paste(doc, generation, target, snapshot, source, out)`.
It copies resources into the destination allocator, preserves cell identities,
and publishes once. A failure leaves content, identities, history and tracked
positions unchanged. Capacity is checked for the complete replacement, while
temporary storage and retained history still count against their budgets.

For cross-process transfer, `tiku_richcellclip_write()` and
`tiku_richcellclip_read()` in `tiku_richclip.h` use the separate
`application/x-tiku-richcells` MIME type. The versioned envelope contains only
selected cells and referenced styles, not unselected source text. The decoder
returns an owned document and its rectangle; take a snapshot for paste, then
release both when finished. Packet and allocation limits match the paragraph
codec. `tiku_richsnapshot_cells_fallback()` supplies quoted TSV with
`[Picture]` placeholders. Nested paragraphs become separate lines inside
their outer cell's quoted field. This fallback loses formatting, image
pixels and nested table structure; the rich representation retains them.

TikuWrite refuses a damaged recognized rich payload without substituting its
fallback. Cut/Paste replies are ignored if the document, caret or selected
rectangle changed while the request was pending.

### Paste cells as a new table

Place the text caret in a paragraph and paste a copied cell rectangle.
TikuWrite inserts a new table at the caret. If the caret is in the middle of
a paragraph, the text before it stays above the table and the remaining text
stays below. Their formatting is retained. The inserted cells keep their
formatting, pictures, nested tables and shading. The new table retains the
source padding, border, selected column proportions and repeated-header rows.
If only some columns were copied, a fixed table width is reduced to those
columns' share. An automatic width remains automatic. Headers outside the
selected rows are not included.

This operation requires an empty text selection. It does not replace selected
text. A caret inside a cell inserts a nested table. The resulting hierarchy
must fit within eight table levels; otherwise nothing is pasted. A selected
cell rectangle still uses the replacement behavior above.
Undo removes the entire inserted table and restores the original paragraph in
one step, including tracked cursor positions. The new grid is selected after
insertion; press Escape to resume text editing.

Other applications can call `tiku_richview_cells_insert(view, snapshot, cells)`.
The model-only API is `tiku_richdoc_cells_insert(doc, generation, at, snapshot,
cells, out)`, where `at` is a valid paragraph position and optional `out`
receives the inserted grid. The source snapshot stays caller-owned. The whole
edit is prepared before publication; allocation, capacity and history refusals
leave the document and its identities unchanged.

### Paste text into a table

Select one cell as the starting point, then paste tab-separated text.
The pasted block extends down and right within the existing table. It does
not add rows or columns. If you select several cells, the clipboard must
have exactly the same number of rows and columns; values are not repeated
to fill a larger selection. Shape mismatches leave the document unchanged.

Tabs separate cells and unquoted line endings separate rows. Quoted fields
can contain tabs and paragraph breaks; double a quote inside a quoted field
to include it literally. LF, CRLF and CR line endings are accepted and
normalized to LF. One final row ending is ignored. Missing closing quotes,
text after a closing quote, unequal row widths, invalid UTF-8 and embedded
NUL bytes are refused. A quote inside an unquoted field is literal.

For example, this pastes a two-row, two-column block. Its first cell contains
two paragraphs:

```c
const char text[] = "\"Temperature\ndegrees C\"\t24.5\nHumidity\t61 percent";
tiku_rich_status_t status =
    tiku_richview_cells_paste_text(view, text, sizeof text - 1);
```

The view must already have a cell selection. On success, it selects the
affected rectangle. Hosts using typed commands can pass the same bytes in
`TIKU_RICHCMD_REPLACE_TEXT` to `tiku_richview_execute()`. Normal typing remains
disabled while a cell rectangle is selected.

For headless use, call `tiku_richdoc_cells_paste_text()` with a generation,
target rectangle and byte length. Its optional final argument receives the
affected rectangle; it is cleared on refusal. A zero generation accepts the
current document. Passing a NULL pointer with a zero byte length explicitly
replaces one cell with empty text; TikuWrite treats an empty system clipboard
as no action.

Paste replaces the target cells' text and inline pictures. Cell shading and
table geometry stay unchanged. New paragraphs inherit the first paragraph's
defaults in their target cell, including its named style; character-run formatting
and links are not imported. A target cell containing a nested table is
refused rather than flattened. These are the plain-text path's rules; rich
cell paste also imports formatting, pictures and cell shading.

The replacement is one undoable transaction. Undo restores the previous
paragraphs, formatting, pictures and tracked positions. Parsing, shape
checks and operation counting finish before any change is published.
Pasting uses at most 1,024 edit operations: each nonempty old paragraph needs
a clear, each extra old paragraph a join, each nonempty new paragraph an
insertion, and each new paragraph break a split. For example, replacing
512 nonempty single-paragraph cells with nonempty text uses 1,024 operations.
Larger operations and resource failures are refused without a partial paste.
Document text, paragraph, run, memory and history limits also apply.

### Inspect or import structure

`tiku_richsnapshot_paragraphs()` and `tiku_richsnapshot_paragraph()` include
paragraphs inside cells in depth-first order. Paragraph text APIs therefore
still work, but a paragraph index alone does not describe ownership.
Use `tiku_richsnapshot_blocks()` and `tiku_richsnapshot_block()` to inspect the
paragraph/table/cell hierarchy. Each node has a stable ID and a parent ID;
top-level nodes have the document body as their parent.
`tiku_richsnapshot_container()` returns a paragraph's parent. Positions still
use the document body ID and paragraph ID, including when the paragraph is
inside a cell.

For import, `tiku_rich_source_t.blocks` is a depth-first array of
`tiku_rich_block_source_t`. Its parent references are **one-based array indices**,
not live IDs; zero means the document body. Paragraph references are zero-based
indices into `source.paragraphs`, and must occur exactly once in ascending
order. Each table has `rows * columns` cell children, listed row by row. A cell
must have at least one paragraph or table. Setting `blocks = NULL` and
`block_count = 0` retains the paragraph-only import format.

Limits are 256 rows, 64 columns and 4,096 cells per table; eight nested tables;
and 65,536 structural nodes per document. Aggregate allocation, text, history
and codec budgets also apply. Layout refuses geometry wider than one million
points. Invalid imports do not replace the open document.

### Editing boundaries and files

Typing, formatting, paragraph splits, joins and styled fragments work within
a cell. A text replacement or rich-fragment operation cannot cross a cell or
table boundary; it is refused without changing content. Use the table command
to remove a complete table. TikuWrite refuses a text selection that crosses
cell boundaries; select a cell rectangle for rectangular Copy/Cut/Paste.
Plain-text Copy/export can flatten text into paragraphs, but neither
preserves the grid. The rich cell clipboard preserves cell contents while
leaving destination table geometry unchanged.

Native `.tkrd` files preserve table structure and require the `tables` feature.
Readers that do not support it must refuse the file. Screen rendering, page
preview and PDF export use the same retained cell rectangles and text layout.
Lines are stored in document order, which is not strictly increasing vertical
order when columns sit side by side. Use the layout hit-test API instead of
binary-searching table lines by their vertical coordinate.

### Long tables and repeated headers

A table that fits on one printed page stays together. If it does not fit in
the space left on the current page, it starts on the next page. Longer tables
continue across pages, breaking between complete rows when they fit. A row
that is taller than the available page area splits between text lines inside
its cells. Preview and PDF use the same fragment positions, borders, shading
and content.

Set `tiku_rich_table_t.header_rows` to the number of leading rows to repeat on
continuation pages. Zero, the default, means no repeated headers. For example,
set it to 1 for a table whose first row contains column labels. To change an
existing table, copy its properties with `tiku_richsnapshot_table()`, change
`header_rows`, then apply `TABLE_SET` or its typed command. The count cannot
exceed the table's row count. The change supports undo like other table
properties. TikuWrite exposes it as **Table / Repeat header rows**.

Inserting a row inside the header prefix makes the new row a header too;
deleting a header reduces the count. Inserting immediately after the last
header adds a body row. Column edits leave the header count unchanged.

Complete leading headers repeat above the continuing body. Each cell advances
independently: a short cell can finish on the first page while its neighbour
continues. Its empty continuation still gets the row's borders and shading.
Each fragment repeats cell padding and draws a closed border. Nested tables
can continue too, including their own repeated headers. A picture stays with
its entire text line; pagination does not cut the image into strips.
Within a cell, keep-lines and keep-next groups move together when they fit
its fragment's available height. Oversized groups split and set
`TIKU_RICH_PAGE_KEEP_BROKEN`. Keep groups stop at nested-table boundaries.

```text
Source row                 First page              Second page
┌──────────┬──────────┐    ┌──────────┬──────────┐  ┌──────────┬──────────┐
│ A        │ 1        │    │ A        │ 1        │  │ C        │          │
│ B        │ 2        │    │ B        │ 2        │  │ D        │          │
│ C        │          │    └──────────┴──────────┘  └──────────┴──────────┘
│ D        │          │
└──────────┴──────────┘
```

Tall-row fragments use dedicated pages; content after that row starts on a
new page. This can leave unused space on the final continuation. Cell line
heights, padding, enclosing cells and repeated headers must leave room for
progress. A single line or header group that cannot fit returns
`TIKU_RICH_LIMIT`, as does a table wider than the printable area. Reduce a
large picture or header, or choose larger paper before retrying. An explicit
page break inside any cell returns `TIKU_RICH_UNSUPPORTED`. Failed pagination
publishes no partial page count. These are layout operations: they do not
split the stored table, change its text or create undo entries.

Ordinary `tiku_rich_page_t` pages describe two ranges of retained layout lines:

| Range | Source lines | Position on paper |
| --- | --- | --- |
| Main content | `first_line`, `line_count` | source y − `source_top` + top margin |
| Repeated headers, if any | `header_first`, `header_count` | source y − `header_top` + top margin |

On a tall-row page, `line_count` is zero and `piece_count` is nonzero. Read its
ordered pieces with `tiku_richpages_piece(pages, page_index, piece_index, &piece)`:

| Piece kind | Rendering |
| --- | --- |
| `TIKU_RICH_PAGE_TEXT` | Draw the specified lines without their original cell boxes, subtracting `source_top` from their y coordinates. |
| `TIKU_RICH_PAGE_RANGE` | Draw the lines and unchanged cell boxes at or below `depth`; ancestor boxes use separate relocated pieces. |
| `TIKU_RICH_PAGE_CELL` | Draw `cell.box` at its already relocated position, with its fill and borders. |

Piece coordinates are relative to the printable area's top-left corner. Add
the page margins when drawing. Paint the repeated outer header first, then
the pieces in order. A parent cell's background appears before its children.
Body lines occur once across ordinary ranges and pieces. Header ranges can
revisit earlier lines; they do not duplicate content in the document.
`source_top` already includes space for those headers: do not add their height
again when rendering the main content. Draw headers before the main range,
using the page's printable area. The shared preview and PDF helpers handle
ranges and pieces for you. Each layout line's `paragraph_index` identifies its paragraph in the
retained snapshot, including when revisiting a header.

Fragment cursors and pieces count against the document's allocation budget.
There are at most 1,000,000 pieces and 5,000 pages. Stepping advances grid
indexing or a line/cell operation; it is a work budget, not a wall-clock
deadline. Releasing a pending job releases its fragments and retained source.
Cell merge/split remains outside the implemented table-editing operations.

## Unicode boundaries

`tiku_richtext_boundaries()` implements the Unicode 17 default extended
grapheme and word boundaries. Supply one output flag byte per input byte,
plus an endpoint byte. `TIKU_RICH_GRAPHEME` marks a caret boundary and
`TIKU_RICH_WORD` marks a default word boundary. Continuation bytes have zero
flags. Invalid UTF-8 or insufficient capacity leaves the output unchanged.
The function allocates no memory and does not normalize the input.

For example, `a` followed by a combining acute accent occupies three UTF-8
bytes but one grapheme. Layout navigation moves from byte zero to byte three,
not into the accent. A shaped ligature can span several graphemes; a provider
must return its interior caret positions too.

The checked-in conformance files contain 766 grapheme and 1,944 word cases.
Default word boundaries do not provide dictionary segmentation or
language-specific tailoring. Whole-word search uses these boundaries as
described below.

## Continuous layout

Create a job from an immutable snapshot and an explicit font provider:

```c
tiku_richlayout_options_t options = {
    .width = 400,                 /* document points */
    .leading = 2,
    .provider = my_provider
};
tiku_richlayout_t *layout = NULL;
tiku_rich_status_t status = tiku_richlayout_create(snapshot, &options, &layout);
if (status == TIKU_RICH_OK) {
    /* Call once per event-loop turn, not in a busy loop on the UI thread. */
    status = tiku_richlayout_step(layout, 256);
}
```

`PENDING` means the job has more work. `OK` makes its geometry queryable;
other statuses remain until the job is released. Releasing a pending job
cancels it. The layout retains its snapshot root and provider independently
of the caller. Geometry contains no pointers into provider scratch arrays.

The layout handles paragraph spacing, indents, left/center/right/justified
alignment, mixed-size baselines, list labels, uniform tab stops and soft
newlines. Wrapping uses spaces, hyphens, CJK opportunities and grapheme
fallback. It is not a full Unicode line-breaking implementation. Keep rules
are stored but do not affect this continuous, non-paginated view.

The default limits are 128 MiB of layout storage and one million visual
lines. Storage also counts against the document allocation owner's aggregate
budget. Array growth must fit at its temporary peak. One provider call sees
at most 4,096 input bytes and can return at most 32 glyphs and 34 caret
positions. A grapheme larger than the input budget is refused, not split.
A format boundary inside a grapheme uses the leading format and sets
`TIKU_RICH_LAYOUT_CLUSTER_STYLE`; stored formatting remains unchanged.

Provider output is checked for byte boundaries, complete caret coverage,
finite metrics, monotonic caret positions and bounded counts. The current
geometry profile is left-to-right. Providers must flag unsupported shaping;
an arbitrary cmap lookup is not evidence of Arabic or Indic shaping support.

`line()`, `cluster()` and `glyph()` expose the retained geometry.
`hit()`, `caret()`, `selection()` and `move()` use those same clusters.
At a soft-wrap boundary, BEFORE selects the preceding visual line and AFTER
selects the following line. A non-grapheme caret is refused. The model's raw
editing API still accepts valid scalar boundaries; use view navigation for
user-facing caret movement.

### Reuse unchanged paragraphs after an edit

A completed layout can supply geometry for a new document snapshot:

```c
tiku_richlayout_t *next = NULL;
tiku_rich_status_t status = tiku_richlayout_create_reusing(
    snapshot, &options, previous, &next);
if (status == TIKU_RICH_OK) {
    tiku_richlayout_release(previous);
    previous = NULL;
    /* Advance next from the event loop, as with an ordinary layout job. */
}
```

The new job retains `previous` if it can use it. It releases that reference
when it completes, fails or is cancelled. Completed jobs do not retain a chain
of earlier layouts. A NULL, pending, failed or incompatible previous layout
gives a fresh layout. `tiku_richlayout_reused(next)` reports how many
paragraphs have been copied; it is a diagnostic count, not a time measurement.

Reuse requires the same immutable paragraph object and allocation owner,
unchanged named-style formatting/inheritance and defaults, and the same font provider
and font generation. The available paragraph width and actual list label
must also match. Changing style formatting invalidates the cache conservatively,
even for paragraphs that do not use that style; renaming alone does not.
Changing a font provider's results
without advancing its generation violates this contract.

Matching paragraphs retain their wrapping, glyphs and grapheme caret
positions. Their coordinates are translated to the new position, and table
boxes and row heights are rebuilt. A changed paragraph before a table can
therefore move the table without reshaping its unchanged cells. Changing a
column width reflows affected cells. List numbering is recomputed before
reuse, so inserting an item cannot leave the following labels stale.

This optimization still copies geometry and visits the document structure;
it does not make reflow constant-time or avoid all allocation. Copying is
incremental, and the pending job still exposes no geometry. Each job keeps
its own `memory_bytes` ceiling. Keeping the previous layout temporarily also
uses the document owner's aggregate budget. Standalone callers can release
the old geometry and retry without a cache after a memory refusal. The view
controller does this once automatically.

## Native font drawing

Document Formats package's `tiku_richfont` loads owned TrueType/OpenType faces through
the existing native font reader. `tiku_richfont_system()` discovers the
requested family's four variants. `tiku_richfont_open()` accepts explicit
paths and collection indices. File inputs are limited to 32 MiB each and
128 MiB together; transient file copies and parsed-font overhead are separate
from document allocation accounting. Font calls on one set must be serialized.

`tiku_richfont_provider()` supplies layout callbacks. The built-in provider
maps graphemes to cmap glyphs and advances; it does not implement GSUB/GPOS,
kerning or bidi. Combining sequences and joining scripts report SHAPING.
Substituted faces, absent requested variants and missing glyphs report
FALLBACK. It does not synthesize bold or italic. A caller can supply a more
capable provider without replacing the model or geometry queries.

`tiku_richfont_draw()` draws retained glyph IDs, list labels, highlights,
underlines and strikeout into a clipped, scaled Tiku surface. It accepts a
line range and limits each call to 65,536 visible clusters. Native raster size
is capped at 512 pixels per glyph. A failed draw may leave partial pixels;
draw into a frame surface before publishing it. Glyph drawing marks a display
list incomplete, so applications must use raster frames or tiles for transport.
The headless PNG example demonstrates that path without opening a window.

## View controller

`tiku_richview` owns an editing session, document subscription, layout job and
viewport state. Multiple views can share a document with independent width,
zoom, scroll and selections. It does not own a native window, clipboard,
file path, recovery data or font-selection UI.

Use `viewport()` after a content-area resize and `step()` from the event loop.
An edit automatically invalidates each observing view. `layout()` returns NULL
while geometry is stale, pending or refused, including when another observer
queries it before the view's notification callback runs. Do not edit from
outdated hit-test coordinates. `reflow()` retries after a resource refusal.

Views automatically reuse compatible paragraphs from their last completed
layout. This applies to TikuWrite and other applications using the view,
including Notes. An edit during unfinished reflow discards that pending job.
If retained old geometry causes a memory refusal, the view drops both jobs
and schedules one fresh attempt; a second refusal remains visible to the host.

`point()` accepts coordinates relative to the content area in logical pixels;
the view applies its zoom and scroll. Extend mode preserves the selection
anchor for shift-click and dragging. `move()` supplies grapheme Left/Right,
visual Up/Down, Home/End and document Start/Finish. Consecutive vertical moves
keep the preferred horizontal coordinate; an external session selection
change discards it. `reveal()` scrolls the caret into view.

Borrow `session()` for formatting, typed text, paste and undo/redo. Do not
destroy that borrowed session. The host supplies clipboard transport, redraw
scheduling, selection painting and a visible explanation for layout or shaping
limitations. The shared input adapter below provides common key bindings and
committed-text assembly.

## Add a paragraph ruler

Include `tiku_richruler.h` when an editor needs indentation controls. The
Document Kit supplies the measurements and edits; the host draws its ruler.
TikuWrite exposes this through **View / Paragraph ruler**.

The top marker controls the first line. The lower-left marker controls the
other lines and moves the first line by the same amount. The lower-right
marker controls the right text boundary. Measurements use document points
(72 points per inch), independent of zoom. These are paragraph indents in the
continuous editing view, not the paper margins used by page preview.

```c
tiku_rich_ruler_t original, preview;
status = tiku_richruler_read(view, &original);
if (status == TIKU_RICH_OK) {
    /* Convert a viewport-local pointer coordinate to document points. */
    double x = pointer_x / tiku_richview_zoom(view);
    status = tiku_richruler_preview(&original, TIKU_RICH_RULER_FIRST,
                                   x, &preview);
    if (status == TIKU_RICH_OK) {
        /* Draw preview.position while dragging; apply only on release. */
        status = tiku_richruler_apply(view, &original,
                                      TIKU_RICH_RULER_FIRST, x);
    }
}
```

Keep the original read result unchanged throughout a drag. Previewing does
not allocate, modify the document or create undo history. Discarding the
preview cancels the operation. Applying uses the original capture, checks it
against the current view, and changes the selected paragraphs in one undo
step. A document edit, selection change or width/zoom change can invalidate
the capture. Check the returned status rather than applying an old position
to a new selection. The capture is valid only during that view's lifetime.

The ruler includes list-label spacing and cell padding from actual layout.
Alignment does not move its markers: centered text still has separate left
and right indentation boundaries. Each moved marker preserves the other
indentation components in each selected paragraph. For example, moving the
right edge retains different first-line indents in a mixed selection. A
changed paragraph receives direct indentation values; its named style and
list membership remain attached, but later changes to that style's indentation
no longer affect those direct values. Undo restores the previous inheritance.

The `mixed` bits identify markers whose positions differ across paragraphs;
`position` shows the first selected paragraph. TikuWrite draws those markers
hollow. A drag sets that marker's position for the selection. Limits keep the
first and subsequent line starts inside the containing text area with at least
one point before the right edge. Unsupported selections spanning different
cells, rectangular cell selections, or more than 1,024 paragraphs do not
produce an editable ruler. Read-only views still expose measurements, with
the `enabled` mask cleared. Layout must finish before measurements are read.

In TikuWrite, drag a marker to preview it and release to commit. Escape cancels;
resizing, switching documents, deactivation and unrelated commands also end
capture. A normal drag rounds to whole points; Option/Alt permits fractional
positions. With the ruler focused, Up/Down chooses a marker, Left/Right moves
it one point, and Shift changes the step to ten points. Home/End uses the
marker's permitted limits. Return or Escape returns focus to the document.
The status bar reports the selected marker's position. Hiding the ruler does
not change the document.

### Measure native-font layout and painting

Run `make test-document-performance` for repeatable 250-, 1,000- and
2,000-paragraph workloads. This test uses a real installed TrueType face,
not the fixed-width provider used by headless geometry tests. Set
`TIKU_TEST_FONT=/path/to/font.ttf` to select the face. No display server or
board is required.

The CSV output records initial layout, edits near the beginning/middle/end,
undo and a width change. It reports total reflow time, the longest 4,096-unit
layout call, the average of five viewport paints, reused paragraphs and peak
model/layout allocation bytes. The peak includes simultaneously live growth
buffers and retained layouts, but excludes font storage, raster allocations
and the benchmark's own buffers. It is not process RSS. Reflow measurements
exclude the independent reference layout.

Separate model-only samples make 200 edits at each size, report total and
maximum edit times, history reservation and current extra history storage,
then undo and redo every edit. They check that navigation allocates nothing
and that clearing history releases exactly the reported extra bytes when no
external readers retain those versions. These samples do not include layout
or painting time.

For each edit, the test compares every resulting line and the pixels of a
scrolled viewport with a fresh uncached layout. It checks undo and complete
allocator cleanup. Cache lookup has a bounded probe count, so an unchanged
paragraph can be laid out again after a hash collision; these fixtures require
at least 99% of the unchanged paragraphs to be reused, not an unconditional
cache hit for each one. A width change must invalidate that reuse.

Timing is reported, not used as a machine-dependent pass/fail threshold.
Use repeated normal-build runs on the target machine to assess responsiveness.
The sanitizer target checks the same workload for memory errors; its timings
are not comparable with release builds. These fixtures cover short Latin
paragraphs and lists, not table-heavy documents, large pictures, complex-script
shaping or native macOS behavior.

## Shared input handling

After translating pointer coordinates into the content area's coordinates,
pass its input to `tiku_richview_event()`:

```c
int handled = 0;
tiku_rich_status_t status = tiku_richview_event(view, event, &handled);
```

The adapter handles these operations through the existing session and command
APIs:

| Input | Operation |
| --- | --- |
| Text key | Insert text using pending formatting and typing undo groups |
| Return; Tab | Insert a paragraph break; insert a tab |
| Backspace; Delete | Delete the selection, neighboring grapheme or paragraph separator |
| Arrows; Home/End | Move through the shared layout; Shift extends selection |
| Command+Home/End | Move to the start/end of the document |
| Command+A | Select the entire text body |
| Command+B/I/U | Toggle bold, italic or underline through typed commands |
| Command+Z; Command+Shift+Z or Y | Undo; redo |
| Primary pointer press/drag | Place or extend the selection |
| Wheel | Scroll vertically using the desktop's wheel direction |
| Deactivation | Cancel an incomplete commit and end a typing undo group |

“Command” means the SDK's logical modifier, mapped to the host's standard
shortcut modifier. File actions, clipboard actions, Option-modified keys,
Shift+Tab and unsupported keys remain unhandled for the application to process.
Resize events also remain with the host, which knows the content area's size.

Check the result as well as `handled`. An operation can be recognized but
refused, for example because the view is read-only. `PENDING` has two uses:

- For navigation, pointer hit testing or deletion, layout must finish first.
  Keep and retry that input after bounded `step()` calls; do not report an
  edit as applied when it was deferred.
- For `TEXT_INPUT`, the adapter is waiting for the remaining pieces of the
  same commit. Send the subsequent pieces, not the first piece again.

The input ABI supports at most 1,023 committed UTF-8 bytes. No partial commit
changes the document. Bad ordering, malformed UTF-8, missing pieces or an
explicit rejection discard the pending commit. If the document generation or
either selection revision changes during the transfer, its final event returns
`STALE` and inserts nothing. This handles **committed text**, not an input
method's preedit display or candidate-window positioning.

`tiku_richview_delete(view, backward)` and `tiku_richview_select_all(view)` are
also available to menus and context actions. Deletion uses complete grapheme
boundaries from current layout. Deleting at a paragraph boundary joins the two
paragraphs. Undo restores the initiating view's original caret, rather than
leaving a temporary deletion selection. A failed allocation changes neither
the text nor that selection.

## Saving and loading documents

Document Formats package's `tiku_richio.h` provides native document persistence. The
file extension is `.tkrd`; its MIME type is `application/x-tiku-richdoc`.
The file is a ZIP containing `document.xml`. It is not HTML or an Office file.
The codec preserves the paragraph model's text, named styles and inheritance,
explicit formatting overrides, reset masks, paragraph defaults, lists and links.
It does not store selection, scroll, layout coordinates, undo history or
document-local IDs.

### Save an immutable snapshot

Capture a snapshot on the document's owning thread. A worker can save it while
the owner continues editing:

```c
tiku_richsnapshot_t *snapshot = NULL;
if (tiku_richdoc_snapshot(doc, &snapshot) != TIKU_RICH_OK) {
    /* Report failure; the document is unchanged. */
    return;
}

/* Run blocking file I/O on a worker in a graphical application. */
tiku_richio_status_t saved = tiku_richio_save(path, snapshot, NULL);

/* Return to the document's owning thread before changing saved state. */
if (saved == TIKU_RICHIO_OK) {
    (void)tiku_richdoc_mark_saved(doc, snapshot);
}
tiku_richsnapshot_release(snapshot);
```

Mark the **snapshot that was written**, not a snapshot captured after the
worker finishes. If edits occurred during the save, the document remains dirty.
The codec never marks a document saved by itself.

Saving converts the snapshot before opening an output file. It then writes to
a temporary file beside the destination, flushes and syncs it, renames it over
the destination and syncs the directory. An existing regular file keeps its
permissions. A symbolic-link destination is refused.

Conversion failure, cancellation during conversion/output or a short write
before replacement leaves the old destination intact. An I/O failure after
rename can mean that replacement succeeded but directory synchronization failed.
Report that uncertainty and keep the document dirty; do not silently retry over
an external edit. The save API does not detect external modifications. An
application must perform conflict detection before requesting replacement.

### Load into a separate document

```c
tiku_richdoc_t *loaded = NULL;
tiku_richio_status_t status = tiku_richio_load(path, NULL, &loaded);
if (status != TIKU_RICHIO_OK) {
    /* Display tiku_richio_strerror(status); keep the current document. */
    return;
}
/* loaded is a new, clean document. Attach its views after accepting it. */
```

The loader checks the package and builds temporary import data. It creates a
new model through the public checked importer; it does not reach into private
model structures or replace an application's active document. The caller owns
the returned handle and releases it with `tiku_richdoc_release()`.

For in-memory conversion and inspection, use `tiku_richio_write_xml()` and
`tiku_richio_read_xml()`. Encoded XML is malloc-owned and NUL-terminated; the
reported length excludes that terminator. Free it with `free()`. Failed writes
return a NULL buffer and zero length; failed loads return a NULL document.

The complete [file example](../../examples/document/tiku_document_file.c)
writes a styled sensor report, reopens it and prints its text:

```sh
make build/examples/tiku_document_file
./build/examples/tiku_document_file build/examples/sensor-report.tkrd
```

That example replaces the explicitly named output file. It does not change
Tracker associations or the existing Notes and Office applications.

### Native schema 1

The `bookmarks` extension stores one `<bookmark name="..." paragraph="..."
byte="..." after="..."/>` per named point. `paragraph` is a zero-based index in
the file's depth-first `body` order, not a live paragraph ID. `byte` is a UTF-8
scalar boundary within that paragraph; `after` is 0 or 1. The reader assigns
fresh paragraph IDs and resolves the indices during checked import. Duplicate
names, missing attributes, invalid indices and invalid boundaries are refused.
The container follows optional table structure and must be declared as a
required feature. Files without bookmarks have no container or feature token.

The root is `tiku-richdoc` with `version="1"`. Its required features always
include `paragraphs styles lists links`. Custom paper settings add `page`;
paragraph or style page-break properties add `page-breaks`. Pictures add
`inline-images`; repeated pixel payloads also add `image-references`.
Tables add `tables`; explicit column weights also add
`table-columns`; shaded cells add `cell-shading`; repeated table headers add
`table-headers`. Saved points add `bookmarks`. Tokens can be
reordered but cannot be repeated. Unknown versions, features, attributes and
elements are refused. An older reader therefore refuses a document that needs
an unsupported extension rather than dropping its page settings.

The tree has this structure. Sibling order is significant:

```text
tiku-richdoc
├── page [present when required includes page]
├── styles
│   └── style [zero or more]
│       ├── chars
│       └── para
├── body
│   └── p [one or more, including cell paragraphs in depth-first order]
│       ├── chars
│       ├── para
│       ├── text
│       └── runs
│           └── run [zero or more]
│               ├── chars
│               └── image [present for an inline picture]
├── structure [present when required includes tables]
│   └── node [depth-first paragraph/table/cell records]
└── bookmarks [present when required includes bookmarks]
    └── bookmark [one to 1,024 named point records]
        ├── column [ordered weights, only for tables with explicit weights]
        ├── header [after columns, only for tables with repeated headers]
        └── fill [one, only for a shaded cell]
```

| Element | Required attributes |
| --- | --- |
| `style` | `id`, `name`, `kind`, `parent` |
| `p` | `style`, `list`, `level`, `restart` |
| `run` | `bytes`, `style`, `reset`, `link` |
| `chars` | `mask`, `bold`, `italic`, `underline`, `strike`, `size-f32`, `font`, `foreground`, `highlight`, `baseline` |
| `para` | `mask`, `align`, `left-f32`, `right-f32`, `first-f32`, `before-f32`, `after-f32`, `keep-next`, `keep-lines` |
| `page` | `width-f32`, `height-f32`, `top-f32`, `right-f32`, `bottom-f32`, `left-f32` |
| `image` | `pixel-width`, `pixel-height`, `width-f32`, `height-f32`, `alt`; optional `ref` with `image-references` |
| `node` | `kind`, `parent`, `paragraph`, `rows`, `columns`, `width-f32`, `padding-f32`, `border-f32`, `color` |
| `column` | `weight-f32` |
| `header` | `rows` (leading header count, 1 through the table's row count) |
| `fill` | `color` (unsigned decimal ARGB) |

`cell-shading` requires `tables`. A shaded CELL node contains one `fill`
child; unshaded cells have none. Other node kinds cannot carry fills. Missing
feature declarations, extra children and invalid colours are refused. Files
without shading keep the earlier structure format; older readers must refuse
the required extension rather than silently removing colours.

With `page-breaks` required, every `para` also requires `break-before` (0 or 1).
Without it, the attribute and its mask bit are rejected. The `page` element
precedes `styles`. Without `page`, the document uses A4 and 54-point margins.

With `image-references` required, an image can replace its pixel text with
`ref="N"`, where N is a one-based image occurrence in preceding body runs.
The feature also requires `inline-images`. Referenced images must have the
same pixel dimensions. A reference element has no child content, but keeps
its own display dimensions and alternate text. Zero, self, forward and
dimension-mismatched references are damaged input. Backward reference chains
are allowed and resolved without recursive traversal. The writer refers
directly to the first matching payload. Files with no repeated pixels keep
the earlier inline-image representation and need no additional feature.

With `tables` required, `structure` follows `body`. Its node indices and
paragraph references use the import conventions described in
[Tables and cells](#tables-and-cells). Document-local IDs are not serialized.
Missing structure, invalid ownership and duplicate paragraph references are
refused. Without the feature, a structure element is also refused.

With `table-columns` required, a table node may contain exactly `columns`
ordered `column` children. Each stores one positive weight as exact binary32
hexadecimal. No column children means equal widths. Missing or extra weights,
invalid values, column children on other node kinds, and column children
without the feature are refused. The extension also requires `tables`.
Readers supporting only the older table schema must refuse weighted documents.

The `table-headers` extension also requires `tables`. A table with a nonzero
header count contains one `header rows="N"` child after any column children.
Without that child, the count is zero. Headers do not require column weights.
Missing feature declarations, zero or out-of-range counts, duplicate headers,
and headers on other node kinds are refused. Older readers must reject the
required extension rather than silently dropping repeated headings.

Other than the root attributes, `styles`, `body`, `text` and `runs` have no
attributes. Style IDs are consecutive one-based indices in file order; zero
means no parent or style reference. Duplicate or missing indices are rejected.
Paragraph-style kind is 0; character-style kind is 1. Runs count decoded UTF-8
bytes, cover the whole paragraph and cannot split a scalar. An empty paragraph
has no runs.

Masks use the public model's presence/reset bits. Boolean values are 0 or 1;
colors are unsigned decimal `0xAARRGGBB` values. Alignment and list kind use
their public enum values. Baseline uses 0 for subscript, 1 for normal and 2 for
superscript. Properties outside a presence mask must have their empty/default
values. References, inheritance, links, list levels and property ranges also
pass through the model's validator.

Attributes ending in `-f32` contain exactly eight lowercase hexadecimal digits
representing IEEE-754 binary32 bits. This keeps finite point values exact and
independent of the process's decimal locale. For example, 12 points is
`41400000`; 12.125 points is `41420000`. NaN and infinity are rejected. The
format defines bits, not a native-endian dump of a C structure.

Names, font families, link targets and paragraph text use UTF-8 with XML
escaping. There is one additional string-encoding step: `%` is written as
`%25`, and U+FFFE/U+FFFF are written as `%EF%BF%BE`/`%EF%BF%BF`. These two
scalars are accepted by the document model but cannot occur directly in XML
1.0. The decoder reverses `%HH` byte escapes after XML entity decoding, then
the model validates the resulting strings. This preserves accepted text
without generating invalid XML or replacing characters. Run lengths refer to
the resulting text, not its escaped XML length.

Schema 1 accepts namespace-free UTF-8 XML, an optional UTF-8 BOM and either no
declaration or the writer's exact UTF-8 declaration. It refuses DTDs, CDATA,
comments, processing instructions, undeclared entities and malformed character
references. These restrictions apply to native documents only; the existing
general XML/Office readers retain their broader input support.

The package contains exactly one regular part named `document.xml` with that
case. Stored and deflate input are supported; output uses stored ZIP32 entries.
Extra parts, including duplicate main parts, are refused. Inline pictures
store hexadecimal ARGB pixels and optional backward references inside
`document.xml`; separate compressed media parts are unsupported. Local names/methods/flags
must agree with the ZIP directory, and entry data must pass its CRC and size
checks. Paths are not extracted onto the filesystem.

### Resource limits and cancellation

Pass `tiku_richio_options_t` to change conversion ceilings or supply a
cancellation callback. Zero-valued byte limits select defaults.

| Resource | Limit |
| --- | --- |
| XML bytes | 64 MiB by default; configurable up to 512 MiB |
| Conversion scratch memory | 256 MiB by default; half for the XML tree and half for import staging |
| ZIP directory | 4 KiB, checked before directory allocation |
| Package bytes | Configured XML limit plus 4 KiB |
| XML nodes | 2,000,000 |
| Model structure | At most 4,096 styles, 250,000 paragraphs and 1,000,000 runs, also subject to model limits |

The XML buffer, archive machinery and resulting document have separate
allocations; the scratch limit is not a total process-memory ceiling. Model
limits can be supplied through `options.document`. Large XML can reach the
tree-memory ceiling before the XML-byte ceiling. Applications should report
`LIMIT` as a resource refusal, not as a damaged file.

Cancellation is polled during conversion and around package output; it cannot
interrupt a blocking filesystem operation already in progress. Inputs remain
borrowed during the call and must not be modified by cancellation callbacks.
Use a worker and an application-owned cancellation flag for graphical apps.

`tiku_zip_write.h` also exposes the stored ZIP writer for other Document Formats package
consumers. It validates up to 1,024 unique portable ASCII paths and ZIP32 sizes
before emitting bytes. A stream write failure can leave partial output; use
the Storage Kit's atomic-file helper when replacing a file.

## Embedding a painted view

The view controller is independent of windows. Document Formats package supplies
`tiku_richfont_draw_view()` to paint it into a UI surface. The same finished
layout determines glyph positions, pointer hits, selection rectangles and the
caret. The painter draws the visible lines; it does not run layout or change
the selection.

```c
/* Run on the view's owner thread, after view creation. */
tiku_rect_t body = {8, 8, width - 16, height - 16};
tiku_rich_status_t status;

status = tiku_richview_viewport(view, body.w, body.h, 1.0);
if (status == TIKU_RICH_OK)
    status = tiku_richview_step(view, 8192);

if (status == TIKU_RICH_OK) {
    tiku_fill(surface, body, 0xffee8c);
    status = tiku_richfont_draw_view(fonts, view, surface, body,
                                    0x704b7fcc, 0x202020, focused);
    /* Publish the completed frame only after OK. */
}
/* PENDING: schedule another step, without using old geometry for input. */
```

Use the same font set that supplied the view's layout provider. `body` uses
logical surface pixels. Its size must match the viewport dimensions supplied
to the view. Scroll is applied by the painter, and zoom comes from the view.
The painter intersects the existing surface clip with `body` and restores it
before returning. Selection colour is an ARGB tint over the drawn text; it
remains visible on highlighted text. The RGB caret colour is used only for a
collapsed selection when `focused` is nonzero.

Pointer coordinates passed to `tiku_richview_event()` are relative to `body`,
so subtract its origin from window coordinates. Keep input in arrival order
while layout is pending: applying typing ahead of a queued click would insert
text at the wrong position. A bounded host queue must report overflow and
cancel an incomplete text transfer; do not join pieces across missing events.

For multiple clicks, the host tracks press times and positions. Call
`tiku_richview_select_at(view, x, y, 0)` to select a Unicode word segment, or
pass `1` to select the wrapped visual line. Both require current geometry.
Word selection intersects word and grapheme boundaries, so it does not cut a
combining character sequence. Allocation refusal preserves the previous
selection. Dictionary-based word segmentation is not supplied.

### Plain-text clipboard

`tiku_richsession_text()` returns selected plain text without allocating. It
joins selected paragraphs with LF, handles either selection direction and
does not change selection or undo history. Query the length first, then
supply space for that many bytes plus a terminating NUL. A short buffer is
left unchanged. `tiku_richsession_range()` gives the ordered endpoints when a
host needs them; `selection()` still returns anchor/caret order.

Clipboard publication belongs to the application host. For Cut, publish the
complete selected text and check success **before** executing the typed
`TIKU_RICHCMD_DELETE` command. A failed publication must not remove text. Paste
uses `TIKU_RICHCMD_REPLACE_TEXT`, so the entire paste is one undo group.
Notes consumes only the text fallback, not the rich fragment representation.

### TikuNotes as an embedding example

The source under `apps/notes/` uses the public document, view and font APIs:

| File | Responsibility |
| --- | --- |
| `tiku_notes.c` | Application lifecycle, windows, resizing and quiet-save scheduling |
| `tiku_notes_input.c` | Ordered input, multiple clicks, Edit actions and plain clipboard |
| `tiku_notes_view.c` | Shared-view painting, titles and published menus |
| `tiku_notes_file.c` | Checked note loading and worker-thread atomic saves |

Notes keeps its existing `.txt` format: the first line names the paper colour
and importance, followed by LF-terminated text. It does not write `.tkrd`
sidecars or offer formatting that would disappear when the file is reopened.
The shared view provides wrapping, Unicode grapheme deletion, selection,
undo/redo and committed-text input. The Edit menu adds Cut, Copy, Paste and
Select all. Window resizing and maximizing update the viewport before the
next layout is used.

Each save worker receives an immutable snapshot, a copied filename and copied
header values. Only the owner thread collects the result and marks that
snapshot saved. Text or header changes made during the write remain dirty.
Failed saves leave the note open with a retry message. Closing a dirty note
keeps a window alive until its latest state is saved; editing it cancels the
pending close. Startup refuses malformed UTF-8, symbolic links, nonregular
files and files beyond its explicit resource ceilings without overwriting
them. It can hold 24 notes, with 4 MiB of paragraph text per note plus
paragraph separators, a 128 MiB aggregate model/layout budget per note, and
a 16 MiB history budget. These are resource limits, not recommended note sizes.

Saving runs off the event thread. Startup file loading is synchronous and
bounded. On host shutdown the application drains accepted queued input and
waits for outstanding writes. If saving still fails, it attempts a private
`/tmp/tiku-notes-recovery-XXXXXX` plain-text file and logs that filename. This
is best-effort emergency recovery, not a recovery browser or a crash journal;
temporary files can be removed by the host. Normal note saves do not yet
detect external edits or offer a conflict-resolution dialog.

The native font provider's shaping limitations still apply. Notes shows a
warning for text needing unsupported shaping; using the view does not add
OpenType positioning or bidirectional layout.

## Whole-word search

Use `tiku_richsearch_create_options()` with
`TIKU_RICH_SEARCH_WHOLE_WORD` to require a whole-word match:

```c
tiku_richsearch_t *search = NULL;
tiku_rich_status_t status = tiku_richsearch_create_options(
    snapshot, first, last, "sensor", 6,
    TIKU_RICH_SEARCH_WHOLE_WORD, &search);
```

The rest of the search lifecycle is unchanged. Step with a work budget and
release the handle on completion or cancellation. This option prepares word
and grapheme boundaries incrementally before scanning. Its boundary array
and paragraph offsets are charged to the document's aggregate memory budget.
Preparation and scanning perform no further allocation.

Both ends of a match must be Unicode word boundaries and grapheme boundaries
in the complete paragraph. Restricting the search scope does not turn an
interior letter into a word boundary. For example, `cat` does not match inside
`scatter`, and a base letter does not match separately from its combining
accent. Rejected partial-word candidates do not hide later overlapping
whole-word candidates. Matching remains case-sensitive, without normalization
or dictionary segmentation.

## Paper, page breaks and PDF

The document stores one `tiku_rich_page_setup_t`. Dimensions and margins are
points (72 points per inch). An all-zero setup means A4 with 54-point margins.
Explicit dimensions must be finite and between 72 and 14,400 points; margins
must be nonnegative and leave at least 36 points of content in each direction.

Change paper through an ordinary transaction, so it participates in saved
state, snapshots, undo and redo:

```c
tiku_rich_edit_t edit = {
    .kind = TIKU_RICH_PAGE_SETUP,
    .page = {612, 792, 36, 36, 36, 36}
};
tiku_rich_transaction_t transaction = {0, &edit, 1};
tiku_rich_status_t status = tiku_richdoc_apply(doc, &transaction);
```

The example selects Letter paper with half-inch margins. Check `status`
before treating it as applied. `tiku_richsnapshot_page()` reads the setup
without exposing mutable document storage.

Paragraph properties can include `TIKU_RICH_PAGE_BREAK` with
`break_before = 1`. Named paragraph styles can supply it too. An explicit
false overrides an inherited break; clearing the property restores
inheritance. Splitting a paragraph keeps its break on the first part and
turns it off on the new trailing paragraph. Joining retains the leading
paragraph's properties. These operations support undo.

### Paginate an immutable snapshot

Create a `tiku_richpages_t` with the snapshot and a layout configuration.
Pagination replaces the layout width with paper width minus left/right
margins. It retains the provider and other layout settings. Then call
`tiku_richpages_step(pages, work)` until the result is no longer `PENDING`.
Release the job to cancel it or to free completed geometry.

```text
snapshot + font provider
          |
          v
line layout at printable paper width
          |
          v
keep groups + explicit page breaks
          |
          v
page ranges + cell fragments -> raster preview
          |
          +------------------> vector PDF
```

`tiku_richpages_count()` returns zero until the entire job succeeds. Each
ordinary page describes consecutive source lines. Its vertical transform is
`page.top + line.y - source_top`; its horizontal origin is `page.left`.
Tall-row pages use the ordered pieces described under
[Long tables and repeated headers](#long-tables-and-repeated-headers).
Keep-lines and keep-next groups fit whole where possible. Explicit page breaks
take priority over keep-next. Oversized groups split between lines and set
`TIKU_RICH_PAGE_KEEP_BROKEN`. A line taller than the printable height fails
with `LIMIT`. The default and maximum page count is 5,000; callers can choose
a lower ceiling. Page arrays are charged to the retained document allocator.

`tiku_richoutput_page()` paints a completed page into a surface and restores
its clip. `tiku_richoutput_pdf()` emits a malloc-owned PDF from the same
completed page mapping and glyph positions. Supply the same native font
provider that constructed the layout. PDF embeds retained font bytes rather
than reopening their paths during output. It supports selectable Unicode
text and external links, subject to the provider's existing shaping limits.
Check both layout and page warnings; neither API adds bidi or complex-script
shaping. PDF is not tagged PDF/UA.

PDF generation is a blocking worker operation with cancellation between
lines. Its ceilings are 250,000 clusters, 500,000 glyphs and 64 MiB of final
output. Do not use a mutable native font set concurrently for drawing and
export. TikuWrite creates a separate worker font set using the same installed
family profile; replacing installed fonts between preview and export can
change their metrics.

## TikuWrite as a document application

Build with `make runtime` and launch **Documents / TikuWrite**. Its independent
bundle is `build/runtime/apps/write/TikuWrite.so`. Tracker recognizes `.tkrd`
and File Associations lists it as a TikuWrite document; existing text and
Office associations are unchanged. The
[application help](../../assets/help/apps/write.html) describes its controls.

| Source | Responsibility |
| --- | --- |
| `tiku_write.c`, `tiku_write_view.c` | Window lifecycle, controls and menus |
| `tiku_write_input.c` | Ordered input and incremental reflow |
| `tiku_write_format.c`, `tiku_write_styles.c` | Character, paragraph, list and named-style controls |
| `tiku_write_search.c` | Find/replace and bounded atomic Replace All |
| `tiku_write_actions.c` | Clipboard, file panels and confirmed continuations |
| `tiku_write_file.c` | Isolated load/save/export workers and file versions |
| `tiku_write_recovery.c` | Private checkpoint storage and explicit recovery |
| `tiku_write_pages.c` | Paper controls and incremental preview |
| `tiku_write_picture.c`, `tiku_write_table.c`, `tiku_write_table_props.c`, `tiku_write_cell.c` | Picture properties, tables, column widths and cell shading |

The Table menu groups structural table commands. Named-style controls and a
bounded set of style shortcuts are under **Format / Paragraph styles**.
**Apply paragraph style** accepts an exact name for styles not shown among
the shortcuts. Menu publication respects the existing six-menu, 28-entry
transport limits; this does not change the application ABI.

### File safety

Open decodes into a separate document. Adoption occurs only after a new view
can be allocated and the visible document's generation still matches the
load request. Failed, damaged or stale opens leave the current document intact.

Saving captures an immutable snapshot. The worker writes beside the target,
flushes and syncs it, checks the target again, renames, then syncs its directory.
The expected version includes device/inode, size, permissions, modification
and change timestamps, and a full SHA-256 digest. Symlink and nonregular
destinations are refused. New files are private (0600); existing permissions
are retained. The guarded writer does not change the process umask.

Conflict detection is cooperative, not a filesystem compare-and-swap. A
noncooperating writer can still change a path between the final check and
rename. After replacement, TikuWrite checks the resulting file against the
bytes it wrote. Directory-sync failure or an unexpected replacement is
reported as uncertain, and the document remains dirty. A successful save
marks its captured state, not edits made while the worker was running.

The app checkpoints unsaved work after a quiet period. Each process holds a
lock on its private recovery session. Fresh launches remain blank and offer
the newest available inactive draft through **File / Recover last draft**.
Recovery opens a copy rather than acquiring the original save path. The old
checkpoint remains available; the app does not delete another session's draft
when a recovered copy is saved. Checkpoints are best-effort, not a crash journal.

### Text import and export

`tiku_richio_read_text()` imports UTF-8 using checked model import, independently
of the interactive paste transaction limit. CRLF, CR and LF become paragraph
separators; one initial UTF-8 BOM is removed. A final newline creates an empty
last paragraph. Invalid UTF-8, NUL, configured memory limits and model limits
are reported rather than silently truncating input. The default model limits
are 64 MiB of text and 250,000 paragraphs. The codec scratch limit also applies.

TikuWrite imports `.txt` as an unsaved copy. Saving requires a native `.tkrd`
path. Exporting text, HTML, Markdown or PDF never marks the native document saved. Text export
omits formatting and list markers; it is not a lossless native save.

### Open HTML and Markdown for editing

Choose **File / Open** and select an `.html`, `.htm`, `.md` or `.markdown`
file. TikuWrite opens a converted copy. **Save** asks for a `.tkrd` filename,
so it cannot overwrite the source markup by accident. The status bar lists
conversion differences. Keep the source file if you need its original syntax
or browser appearance. The Open picker starts in your home directory, or beside
the current native document.

The same conversion is available to other applications without a window:

```c
#include "tiku_richmarkup.h"
#include <string.h>

const char *html = "<h1>Experiment</h1><p>A <b>useful</b> result.</p>";
tiku_richdoc_t *doc = NULL;
unsigned notices = 0;
tiku_richio_status_t status = tiku_richmarkup_read_html(
    html, strlen(html), NULL, &doc, &notices);
if (status == TIKU_RICHIO_OK) {
    char report[256];
    tiku_richmarkup_report(notices, report, sizeof report);
    /* Attach doc to a Rich View, or edit it with Document Kit commands. */
    tiku_richdoc_release(doc);
}
```

On success, the document owns its content. You may discard the HTML input
immediately. On failure, `doc` is NULL and no partially converted document is
returned. Pass `tiku_richio_options_t` to set input, staging and model limits
or provide a cancellation callback. A host application should run conversion
on a worker, as TikuWrite does.

HTML import supports paragraphs, six heading levels, bold/italic/underline,
strike-through, subscript/superscript, links, numbered/bulleted lists, code,
quotes, line breaks and rectangular nested tables. Supported inline CSS
includes text alignment, indents, spacing, font family/size, colours and keep
rules. Headings become editable named styles with semantic heading levels.
Table cells retain their text and nested structure; unsupported table geometry
is reported.

This is a document importer, not a browser. Stylesheets, script behavior,
page geometry and arbitrary CSS are not reproduced. Scripts and active
objects are omitted. No URLs or local resource paths are fetched. Embedded
`data:image/png;base64,` pictures are decoded and copied into the document.
Their alternate text, enclosing link and display size are retained. CSS
width/height accepts point or pixel lengths; HTML width/height accepts integer
pixels. A single dimension preserves the picture's aspect ratio. An unspecified
size uses 0.75 points per pixel, with a one-point minimum reported as a style
adjustment.

External pictures and unsupported variants use `[Image: alternate text]`
labels and an image notice. The importer refuses malformed embedded PNG data
and pictures exceeding the pixel or memory limits. Decode scratch space and
previously imported pictures share the import budget. Cancellation is checked
before and after decoding each PNG, not within the decoder's inflate loop.
The supported profile excludes 16-bit PNGs and non-palette `tRNS` transparency;
these use reported alt-text substitution. TikuWrite's own HTML exports use
8-bit RGBA PNGs, including alpha, which can be reopened as editable pictures.

Merged cells and irregular grids are refused rather than reshaped
silently. The notice bitmask distinguishes style, content, picture, structure,
syntax and page-layout differences.

For Markdown, call `tiku_richmarkup_read_markdown()` with the same arguments.
It reads the documentation subset: headings, emphasis, inline links, lists,
quotes, fenced code and pipe tables. Six heading levels and written decimal
list values are retained. Raw HTML is text; unsupported Markdown extensions
can remain literal syntax. A syntax-subset notice is always returned. This
does not claim CommonMark compliance.

### Markdown export

**File / Export Markdown** writes a `.md` copy without marking your native
document saved. The reusable function is
`tiku_richmarkup_write_markdown(snapshot, options, &bytes, &length, &notices)`.
Free the successful output with `free()`. Failure clears the output pointer,
length and notice bits.

The exporter retains semantic heading levels 1 through 6, list
levels and numbering, bold, italic and links. Markdown cannot express the
whole document model: table content becomes separate paragraphs, pictures
become alternate text, and colours, precise spacing, paper settings and named
style inheritance are not retained. Empty paragraph structure can also differ.
The conversion notices identify these differences. Keep `.tkrd` as the
editable source; Markdown is an interchange copy, not a lossless backup.

### HTML export

Use **File / Export HTML** in TikuWrite to create a browser-readable `.html`
copy. The reusable exporter lives in Document Formats package, not in TikuWrite:

```c
#include "tiku_richhtml.h"
#include <stdlib.h>

/* Create snapshot on the document's owning thread before handing it off. */
unsigned char *html = NULL;
size_t length = 0;
unsigned notices = 0;
tiku_richio_options_t options = {.xml_bytes = 8u * 1024u * 1024u};
tiku_richio_status_t status = tiku_richhtml_write(
    snapshot, &options, &html, &length, &notices);
if (status == TIKU_RICHIO_OK) {
    /* Write exactly length bytes and show any notices to the user. */
}
free(html); /* Also valid after failure, which sets html to NULL. */
```

The exporter reads an immutable snapshot; it does not open files or change
document state. The returned buffer is UTF-8 and NUL-terminated. `length`
does not include the terminator. Use it from a worker for larger documents.
The existing codec options supply cancellation and size limits:
`xml_bytes` caps the HTML output at 64 MiB by default, and the output must
also fit within half of `memory_bytes`, including its terminator. Counting
precedes the single output allocation. Failure clears the buffer, length
and notice bits, including cancellation during the second pass.

HTML preserves resolved character formatting, paragraph indents and spacing,
links, list levels and numbering, nested table structure, column proportions,
borders and cell shading. Empty paragraphs keep their text size. Repeated
table headers become HTML header groups. Page dimensions, margins and keep
rules become CSS print hints; browser support determines their effect.
Text and link attributes are HTML-escaped. Font-family names are separately
CSS-escaped, so punctuation in a font name cannot introduce HTML or CSS rules.
The output contains no scripts or externally loaded resources.

Pictures are embedded as PNG data URLs, retaining straight alpha, RGB values,
display dimensions in points and escaped alternate text. Links on pictures
remain links. The HTML file is self-contained; no image directory or original
source file is required. `tiku_png_rgba_stream()` in Document Formats package emits
PNG through a callback using a fixed-size buffer and no heap allocations.
`tiku_png_rgba_size()` gives its exact encoded size without reading pixels,
so the HTML counting pass can reject oversized output before allocating it.

This streaming encoder uses uncompressed DEFLATE blocks. After base64
encoding, picture data takes roughly 5.34 bytes per pixel, plus row and
format overhead. Repeated pictures are embedded at each occurrence. The
64 MiB default HTML limit includes picture data. Exceeding it fails the
whole export; it does not replace pictures with text or modify the destination.
Cancellation is checked between chunks of at most 4,096 raw image bytes.

Check the returned notice bits before describing the export as equivalent
to the original:

| Notice | What changes |
| --- | --- |
| `TIKU_RICHHTML_PAGINATION` | Always set: the browser controls fonts, wrapping and page layout. |
| `TIKU_RICHHTML_STYLES` | Named styles are resolved to inline CSS; style names and inheritance are not exported. |
| `TIKU_RICHHTML_IMAGES` | Reserved for compatibility; not emitted because pictures are embedded. |

This is an export profile, not reversible HTML editing or a rich clipboard
format. TikuWrite reports the differences in its status bar. It uses the same
snapshot worker, atomic replacement and external-change checks as native
saving, but does not change the native path, save marker or recovery data.
Keep the `.tkrd` source. Use PDF when you need TikuWrite's page layout.
