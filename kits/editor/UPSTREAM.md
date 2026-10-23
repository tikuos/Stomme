# Scintilla material in the editor kit

The editor kit's document engine adapts a few algorithms from Scintilla.
This file records where they came from, what was taken, what was changed,
what was left out, and how each relevant upstream test is treated.
Nothing here is needed to build or test the kit: the adapted code and the
translated tests live in this repository.

## Origin

| Item | Value |
| --- | --- |
| Project | Scintilla, <https://www.scintilla.org/> |
| Release | 5.6.7 (`version.txt` reads `567`), released 28 September 2026 according to `doc/ScintillaHistory.html` |
| Archive | The release archive `scintilla567`, extracted; the copy had no version-control data |
| Tree identity | 266 files; `29c2950621fd24b52869a023d173481dd3936ac4c02a50950b364918af81cae6` |

The tree identity is computed inside the extracted `scintilla` directory:

```sh
find . -type f -print0 | LC_ALL=C sort -z | xargs -0 sha256sum | sha256sum
```

The files used, by SHA-256:

| File | Use | SHA-256 |
| --- | --- | --- |
| `License.txt` | copied as `LICENSE.Scintilla` | `ac32743bd464c837e481beae20df65a9207f84d3ff1912f6003000343e9c753d` |
| `src/SplitVector.h` | adapted | `6efa0ccdd2e59d9d1c364c8c8798d5378b7a2b1b8a734a79dfd6493455c83085` |
| `src/Partitioning.h` | adapted | `f2f938b2f5b94acf924308727d193afd5ab22be4805120442a183464408457ee` |
| `src/CellBuffer.cxx` | adapted | `1170c98261c43218e4733fcad781990a946cd793098920352191f2b46ccaaf66` |
| `src/CellBuffer.h` | reference | `a4156bec68eca11d9479737e37c7721bffcb4776889d4396d45b727aa80e3c20` |
| `src/Selection.cxx` | adapted | `256e87dd2b3381c08a9c340cb17806e09cb29c9bc56786ac238c68886a7c65f1` |
| `src/Selection.h` | reference | `c33abb1b50785ea7ab78fa3331a527837267092bd78b93d9dfac3dc979fd356c` |
| `src/UndoHistory.cxx` | reference | `767455b8a5a85ddb5a4bfb507dd9e39f80f19e65474f1b30ce48e1825537aac2` |
| `src/UndoHistory.h` | reference | `8e73fc5eded218252b2e5bd95a7e99462b6ecadd51f298e60bfe422ff5c861fc` |
| `src/Document.cxx` | reference | `6f10517c7d4acd94b70d210aa789373fa0c55fd7f0e14df9a43b4af622a8bd96` |
| `src/Document.h` | reference | `729715fd2c812d9b55e86a81572d4d8c59cbe794d86129ccc899b417133b5353` |
| `src/UniConversion.cxx` | reference | `161bbfe3fa0fbe140f92e9924fc6981f7ae9e16bc13e5033433b4c1882911254` |
| `src/PerLine.cxx` | reference | `138fbde84c8539ae57ff33e6f305ba5f73bb048c39c0736c87ab8f663171170b` |
| `test/unit/testSplitVector.cxx` | cases translated | `8682380e28cf1af81f64b600bcec89d5f3c781f79334e559791574e14344f27d` |
| `test/unit/testPartitioning.cxx` | cases translated | `6cdaf9de08b92733f06278cef596f8aba28b95608034ef7ce96d1045c2f5dba4` |
| `test/unit/testCellBuffer.cxx` | cases translated | `2be0be0e1721cf2d5d801349ac67c24a4b8a81e5cd0a1608177757e90dc26fcc` |
| `test/unit/testSelection.cxx` | cases translated | `c277296cb5d4ae4a1643d7609f9e8f43edcf837bf7e59735e4daaf58d81aad2b` |
| `test/unit/testDocument.cxx` | cases translated | `356dd0d972743ba19b51f099be19eb0c6fea0a171c38083817770575442c2d7c` |
| `test/unit/testUniConversion.cxx` | cases translated | `48abb7a5005f6878ca5bcebd41c190011657366925032a3b3669fa0d33c0694f` |
| `test/unit/testPerLine.cxx` | cases translated | `af1b379394df719245d7af8c52399d7a90294dc5ebad0fda10623e72fc04d566` |

"Reference" means the file was read to understand behaviour and no code was
taken from it.

## Licence and notices

`LICENSE.Scintilla` is an unmodified copy of upstream `License.txt`. It is
a permissive licence: use, copying, modification and distribution are
allowed if the copyright notice and the permission notice appear in
supporting documentation.

Files carrying upstream-derived material say so in their header comment,
name the upstream file and its copyright line, and use the SPDX expression
`Apache-2.0 AND LicenseRef-Scintilla`, where `LicenseRef-Scintilla` means the
licence in `LICENSE.Scintilla`. Upstream files carry different copyright
years, so each header quotes its own source's line.

| Tiku file | Upstream material | Upstream copyright line |
| --- | --- | --- |
| `kits/editor/tiku_editdoc_buffer.c` | `src/SplitVector.h`; `RangeAddDelta` from `src/Partitioning.h` | Copyright 1998-2007 by Neil Hodgson |
| `kits/editor/tiku_editdoc_lines.c` | `src/Partitioning.h` | Copyright 1998-2007 by Neil Hodgson |
| | `src/CellBuffer.cxx` | Copyright 1998-2001 by Neil Hodgson |
| `kits/editor/tiku_editdoc_anchors.c` | `src/Selection.cxx`, the edit-position rule only | Copyright 2009 by Neil Hodgson |
| `kits/editor/tiku_editdoc_private.h` | structure layouts from `SplitVector.h` and `Partitioning.h` | Copyright 1998-2007 by Neil Hodgson |
| `kits/editor/tiku_codeview_rows.c` | `src/ContractionState.cxx`, display lines over line heights | Copyright 1998-2007 by Neil Hodgson |
| `tests/kits/test_editdoc_storage.c` | cases from `testSplitVector.cxx`, `testPartitioning.cxx`; `Partitioning::Check` | Neil Hodgson |
| `tests/kits/test_editdoc_lines.c` | cases from `testCellBuffer.cxx` | Neil Hodgson |
| `tests/kits/test_editdoc_history.c` | cases from `testCellBuffer.cxx`, `testDocument.cxx` | Neil Hodgson |
| `tests/kits/test_editdoc_marks.c` | a case from `testSelection.cxx` | Neil Hodgson |
| `tests/kits/test_editdoc_decorations.c` | the `LineMarkers` cases from `testPerLine.cxx` | Neil Hodgson |
| `tests/kits/test_editdoc.c` | cases from `testDocument.cxx`, `testCellBuffer.cxx`, `testUniConversion.cxx` | Neil Hodgson |

The upstream test files carry no copyright line of their own. They are part
of the same distribution and are covered by `License.txt`.

The remaining kit files are original Tiku code under Apache-2.0:
`tiku_editdoc.h`, `tiku_editdoc.c`, `tiku_editdoc_transaction.c`,
`tiku_editdoc_undo.c`, `tiku_editdoc_utf8.c`, `tiku_editdoc_alloc.c`, the
search's `tiku_edit_search.h` and `tiku_edit_search.c`, the line markers'
`tiku_edit_decorations.h` and `tiku_edit_decorations.c`, and the code view's
`tiku_codeview.h`, `tiku_codeview_private.h`, `tiku_codeview.c`,
`tiku_codeview_layout.c`, `tiku_codeview_paint.c`, `tiku_codeview_input.c`,
`tiku_codeview_edit.c`, `tiku_codeview_find.c`, `tiku_codeview_style.c`,
`tiku_codeview_assist.c`, `tiku_codeview_lines.c`,
`tiku_codeview_brace.c`, `tiku_codeview_notes.c` and
`tiku_codeview_folds.c`; the document's indicators and annotations in
`tiku_edit_notes.c`; and the tests `test_editdoc_model.c`,
`test_editdoc_faults.c`, `test_editdoc_notes.c` and every
`test_codeview_*.c`. The code view's word
moves follow the behaviour of `Document::NextWordStart` in
`src/Document.cxx`, and where a line marker goes when lines are added or
removed follows `LineVector::InsertLine` in `src/CellBuffer.cxx` and
`LineMarkers` in `src/PerLine.cxx`, all read as references.

**Packaging.** Editor, TikuBasic and TikuC link the kit. `tools/package.py`
copies the full `LICENSE.Scintilla` into each of their bundles, refuses a
package where one is missing or differs, and leaves `.a` files out of
packages. An About window may link to the licence but does not replace
it.

## What was adapted

### Gap storage: `tiku_editdoc_buffer.c` from `SplitVector.h`

Kept: the gap buffer itself; `GapTo`, which moves the gap with one
`memmove`; the growth rule in `RoomFor`, which doubles the growth step while
it is below one sixth of the size, starting at 8; `Insert`,
`InsertFromArray`, `ValueAt`, `SetValueAt`, `DeleteRange`, and `GetRange`
across the gap. `Partitioning::RangeAddDelta`, which adds a delta to a run of
line starts on both sides of the gap, is adapted here too, as
`tiku_ed_starts_add`.

Changed:

- C instead of a template, specialised twice: bytes for the text, and
  signed 64-bit positions for the line starts.
- Only an explicit reserve allocates. An insertion asserts that room was
  reserved, so a commit never allocates.
- Deleting everything keeps the storage. Upstream's `DeleteAll` frees it;
  here a commit may empty and refill a buffer without allocating.
- Every size calculation is checked for overflow, and failures are status
  returns, not exceptions.
- No pointer into the storage is handed out, so there is no `BufferPointer`.
  Readers copy a range or receive it in chunks.

### Line starts: `tiku_editdoc_lines.c` from `Partitioning.h`

Kept: line starts in a gap buffer, with a deferred shift after the last
edit (`stepPartition`, `stepLength`) moved forward by `ApplyStep` or back by
`BackStep`; an edit that lands earlier than the step by more than a tenth of
the partitions first applies the whole step. Also kept: `InsertPartition`,
`InsertPartitions`, `SetPartitionStartPosition`, `InsertText`,
`RemovePartition`, and the binary search of `PartitionFromPosition`.

Changed:

- Resetting keeps the storage, so a load or a full deletion can reset the
  index inside a commit.
- Room for new line starts is reserved before the commit, from a count of
  the CR and LF bytes the edit could create.

### CR, LF and CRLF: `tiku_editdoc_lines.c` from `CellBuffer.cxx`

Kept: the line bookkeeping of `BasicInsertString` and `BasicDeleteChars`,
including:

- inserting between a CR and an LF splits one line end into two;
- an inserted LF after an existing CR, or an inserted CR before an existing
  LF, joins them into one CRLF;
- line starts found in an insertion are gathered in blocks before they are
  added;
- a deletion that leaves a CR in front of an LF joins them;
- `LineEnd` steps back over a CRLF.

Changed:

- The Unicode line ends NEL, LS and PS are ordinary characters. Upstream
  treats them as line ends only when a document asks for it, and this
  engine never does.
- Not taken: style bytes, `PerLine` hooks, the UTF-16 and UTF-32 character
  indexes, undo, change history and the read-only flag. The document layer
  owns read-only and history.

### Selections: `tiku_editdoc_anchors.c` from `Selection.cxx`

Kept: `SelectionPosition::MoveForInsertDelete` and
`SelectionRange::MoveForInsertDelete`:

- A position before a change stays where it is.
- A position after the change shifts by the change's length.
- A position inside a deletion, or at its end, moves to the deletion's
  start.
- A position at an insertion point moves only for the end that starts a
  non-empty selection, so a selection keeps covering the text it selected.

Changed:

- There is no virtual space.
- A replacement is applied as a deletion followed by an insertion.
- After a transaction, a selection end left between a CR and its LF moves
  back to the CR. This is a Tiku rule.
- Each view owns its own selection. Anchors, views and listeners are Tiku
  code.

### Hidden lines: `tiku_codeview_rows.c` from `ContractionState.cxx`

Kept:

- Each line has a height in rows, and a line inside a folded region has
  none.
- The rows are a partition over the lines, so finding a line's first row
  is a read and finding the line of a row is the partition's binary search.
- Hiding or showing a line takes its height out of the partition or puts
  it back, and a new height changes the partition by the difference.
- Nothing is kept while every line is one row: the partition is made when
  lines wrap, annotations are shown or a line is folded, and dropped when
  none is.

Changed:

- Whether a line is hidden, and whether it heads a folded region, are a
  byte per line, not run styles; heights live only in the partition.
- Heights are counted when first needed: a line not yet counted keeps its
  last count, and a changed line takes one row until it is counted.
- Each view owns its rows. Fold regions are read from the lines' text by
  the rules (`tiku_codeview_folds.c`), not kept as fold levels.
- Fold display texts are not kept; a folded line shows a fixed mark.

### Reference only

- `UndoHistory` informed which typing joins into one undo step and how the
  save point behaves. The history code is Tiku's own:
  - records of replacements;
  - a byte budget;
  - state identities that are never reused;
  - failure-atomic undo and redo.
- `Document` informed which deletions join into one undo step: consecutive
  Backspace and Delete presses, in either direction.
- `UniConversion` was compared with the UTF-8 validator, which is Tiku's
  own. It refuses NUL, overlong forms, surrogates and anything above
  U+10FFFF. It accepts noncharacters, which upstream flags.
- `PerLine`'s `LineMarkers` and `CellBuffer`'s `LineVector::InsertLine`
  informed where a line marker goes: lines inserted at a marked line's
  start push the marker down with the line's text, and a removed line's
  markers join the line before. The markers are Tiku code, kept as
  positions at their lines' starts and moved with each replacement.

## Left out

These upstream features are not in the kit. Adding one means updating this
file first.

| Feature | Reason |
| --- | --- |
| Style bytes, `RunStyles`, `Decoration` | Colouring stays with `tiku_syntax`; indicators are Tiku code (`tiku_edit_notes.c`), kept as positions |
| `PerLine` levels, states, annotations | Fold regions are read from the text by the rules; annotations and line markers are Tiku code (`tiku_edit_notes.c`, `tiku_edit_decorations.c`) |
| `SparseVector`, `UniqueString` | Fold display texts are not kept |
| UTF-16 and UTF-32 character indexes | No consumer; positions are byte offsets |
| `ChangeHistory` | Not needed for save state |
| Undo collection switch, container actions, nested groups, tentative undo | A command builds one transaction; there is no preedit history |
| Unicode line ends | Only LF, CR and CRLF end lines |
| DBCS and other code pages | Documents are UTF-8 |
| Virtual space, multiple and rectangular selections | They come with multiple carets |
| Selection serialisation | No consumer |
| `RESearch`, Lexilla | Regex is a separate later decision; syntax stays with `tiku_syntax` |
| `Document` as an object, watchers, `Editor`, `EditView`, `PositionCache`, platform layers | The widget, layout and painting are Tiku code |

## Dependencies

The adapted files depend on the C standard library (`stddef.h`,
`stdint.h`, `string.h`, `assert.h`) and on each other, nothing else. No C++
runtime and no upstream header is needed.

## Test map

Every relevant upstream `TEST_CASE` and `SECTION` is classified:

| Class | Meaning |
| --- | --- |
| Unchanged | The same inputs and assertions, run against the adapted code |
| Adapted | The case identity is kept and a changed Tiku rule is tested |
| Excluded | The feature it tests was left out, for the reason given |
| Deferred | It waits for the feature that uses it; it does not count as coverage |

Each section runs on a fresh fixture, as Catch runs it. A translated case
carries its upstream name as its check label, such as
`SplitVector/InsertOne`, so a failure names the case it came from.

These translated cases are regression evidence, not an independent oracle.
Independent checks run alongside them:

- `test_editdoc_model.c` compares the engine with a plain model after every
  generated operation.
- `test_editdoc_faults.c` fails every allocation in turn.
- `test_editdoc_lines.c:exhaustive` tries every edit of short texts over
  `a`, CR and LF.

### `testSplitVector.cxx`

The Tiku tests are in `tests/kits/test_editdoc_storage.c:split_vector`, run
against the 64-bit position store (upstream uses `SplitVector<int>`).

| Case | Class | Note |
| --- | --- | --- |
| CompileCopying SplitVector / CopyingMoving | Excluded | C++ copy and move construction; the C store has only init and free |
| CompileCopying SplitVector / MoveOnly | Excluded | Same |
| SplitVector / IsEmptyInitially | Unchanged | |
| SplitVector / InsertOne | Unchanged | |
| SplitVector / Insertion | Unchanged | |
| SplitVector / InsertionString | Adapted | The same insertions and deletion, of distinct numbers instead of strings, checked against an array |
| SplitVector / InsertionPattern | Unchanged | |
| SplitVector / EnsureLength | Excluded | No `EnsureLength`; lengths change only by insertion and deletion |
| SplitVector / InsertFromArray | Unchanged | |
| SplitVector / InsertEmpty | Excluded | It returns a pointer into the storage, which the C store never does |
| SplitVector / SetValue | Unchanged | |
| SplitVector / Indexing | Unchanged | |
| SplitVector / Fill | Unchanged | |
| SplitVector / DeleteOne | Unchanged | |
| SplitVector / DeleteRange | Unchanged | |
| SplitVector / DeleteAll | Adapted | Also checks that the storage is kept |
| SplitVector / GetRange | Unchanged | |
| SplitVector / GetRangeOverGap | Unchanged | |
| SplitVector / ReplaceUp | Unchanged | |
| SplitVector / ReplaceDown | Unchanged | |
| SplitVector / BufferPointer | Excluded | No pointer into the storage is handed out |
| SplitVector / DeleteBackAndForth | Unchanged | |
| SplitVector / GrowSize | Excluded | The growth step is internal, with no setter |
| SplitVector / OutsideBounds | Unchanged | |

### `testPartitioning.cxx`

The Tiku tests are in `tests/kits/test_editdoc_storage.c:partitioning`.
Upstream's `Check` is translated as `partitions_ok` and called where
upstream calls it.

| Case | Class | Note |
| --- | --- | --- |
| CompileCopying Partitioning / CopyingMoving | Excluded | C++ copy and move construction |
| Partitioning / IsEmptyInitially | Unchanged | |
| Partitioning / SimpleInsert | Unchanged | |
| Partitioning / TwoPartitions | Unchanged | |
| Partitioning / MoveStart | Unchanged | |
| Partitioning / InsertAgain | Unchanged | |
| Partitioning / InsertMultiple | Unchanged | |
| Partitioning / InsertMultipleWithCast | Adapted | One element type, so the cast variant is the same call |
| Partitioning / InsertReversed | Unchanged | |
| Partitioning / InverseSearch | Unchanged | |
| Partitioning / DeletePartition | Unchanged | |
| Partitioning / DeleteAll | Unchanged | |
| Partitioning / TestBackwards | Unchanged | |
| Partitioning / TestMany | Unchanged | |

### `testCellBuffer.cxx`

| Case | Class | Tiku test | Note |
| --- | --- | --- | --- |
| ScrapStack / Push | Excluded | | Upstream's undo payload store; a Tiku record owns its bytes |
| CellBuffer / InsertOneLine | Adapted | `test_editdoc_lines.c:cell_buffer`, `test_editdoc_history.c:upstream_cell_buffer` | Lines on the text layer, undo on the document; `startSequence` and the returned pointer have no counterpart |
| CellBuffer / InsertTwoLines | Adapted | Same | Same |
| CellBuffer / LineEnds | Adapted | `test_editdoc_lines.c:cell_buffer` | LF, CRLF and CR as upstream; NEL and LS stay inside one line |
| CellBuffer / UndoOff | Excluded | | History cannot be switched off; its byte budget bounds it |
| CellBuffer / UndoRedo | Adapted | `test_editdoc_history.c:upstream_cell_buffer` | `DeleteUndoHistory` becomes a load, the one operation that clears history |
| CellBuffer / LineEndTypes | Excluded | | There is no line-end-type setting |
| CellBuffer / ReadOnly | Adapted | `test_editdoc.c:upstream_document` | The insertion is refused with `TIKU_EDIT_READ_ONLY` rather than ignored |
| ScaledVector / ScalingUp | Excluded | | Upstream packs undo actions into scaled integers |
| UndoHistory / Basics | Adapted | `test_editdoc_history.c:upstream_undo_history` | The saved state stands in for the save point |
| UndoHistory / EnsureTruncationAfterUndo | Adapted | Same | The action count becomes "can redo" and a step count |
| UndoHistory / Coalesce | Adapted | `test_editdoc_history.c:typing` | Joining needs the typing flag and the same view |
| UndoHistory / SimpleContainer | Excluded | | No container actions |
| UndoHistory / CoalesceContainer | Excluded | | No container actions |
| UndoHistory / Grouping | Adapted | `test_editdoc_history.c:upstream_undo_history` | A group is one transaction of non-overlapping replacements |
| UndoHistory / DeepGroup | Excluded | | No nested groups; a command builds one transaction |
| UndoHistory / Tentative | Excluded | | No preedit history; a committed composition is an ordinary transaction |
| UndoActions / Basics | Excluded | | Upstream's action storage |
| CharacterIndex / Setup | Excluded | | No UTF-16 or UTF-32 index |
| CharacterIndex / Insertion | Excluded | | Same |
| CharacterIndex / Deletion | Excluded | | Same |
| CharacterIndex / Insert Complex | Excluded | | Same |
| CharacterIndex / Delete Multiple lines | Excluded | | Same |
| CharacterIndex / Delete Complex | Excluded | | Same |
| CharacterIndex / Insert separates new line bytes | Adapted | `test_editdoc_lines.c:cell_buffer` | The line starts it implies, without the index |
| ChangeHistory / all 10 sections | Excluded | | No change history |
| CellBufferWithChangeHistory / StraightUndoRedoSaveRevertRedo | Excluded | | No change history |
| CellBufferWithChangeHistory / Detached | Excluded | | No change history |
| CellBufferLoadUndoHistory / Basics | Excluded | | History is not saved or loaded |
| CellBufferLoadUndoHistory / Detached | Excluded | | Same |
| CellBufferLong / Random | Adapted | `test_editdoc_lines.c:random_runs` | Upstream's random insertions and deletions, then a second run with CR, LF and a two-byte character, lines checked after each step |

The ten ChangeHistory sections are Start, Some Space, An insert, A delete,
"Insert, delete, and undo", Deletes, Deletes 101, Deletes Stack, Delete
Contiguous Backward, and Delete Contiguous Forward.

### `testSelection.cxx`

| Case | Class | Tiku test | Note |
| --- | --- | --- | --- |
| SelectionPosition / SelectionPosition | Excluded | | No invalid-position value or virtual space; a view starts as an empty caret at 0 (MRK-008) |
| SelectionPosition / Comparison | Excluded | | Virtual space ordering; positions are integers |
| SelectionPosition / Add | Excluded | | Virtual space |
| SelectionPosition / MoveForInsertDelete | Adapted | `test_editdoc_marks.c:selection_upstream` | The steps without virtual space; the sequence restarts where the first virtual step was |
| SelectionPosition / Serialization | Excluded | | No serialisation |
| SelectionSegment / SelectionSegment | Excluded | | No invalid value, no segment type |
| SelectionRange / SelectionRange | Excluded | | No invalid value |
| SelectionRange / Serialization | Excluded | | No serialisation |
| SelectionRange / Intersect | Deferred | | Painting a selection line by line, with the editing widget |
| SelectionRange / StartEndSet | Deferred | | Multiple carets and overlap normalisation |
| Selection / Selection | Deferred | | A view holding several selections comes with multiple carets |
| Selection / Serialization | Excluded | | No serialisation |
| Selection / SerializationMultiple | Excluded | | No serialisation |
| Selection / SerializationRectangular | Excluded | | No serialisation |

### `testDocument.cxx`

| Case | Class | Tiku test | Note |
| --- | --- | --- | --- |
| Document / InsertOneLine | Adapted | `test_editdoc.c:upstream_document` | `LineStartPosition` has no counterpart |
| Document / Style | Excluded | | No style bytes |
| Document / SearchInLatin | Deferred | | Document search, with the Editor's Find |
| Document / SearchInBothSegments | Deferred | | Same |
| Document / InsensitiveSearchInLatin | Deferred | | Same; ASCII case folding over UTF-8 |
| Document / InsensitiveSearchIn1252 | Excluded | | Windows-1252 |
| Document / Search2InLatin | Deferred | | Document search |
| Document / SearchInUTF8 | Deferred | | Document search |
| Document / InsensitiveSearchInUTF8 | Deferred | | Its ASCII letters only; non-ASCII case folding is excluded |
| Document / SearchInShiftJIS | Excluded | | DBCS |
| Document / InsensitiveSearchInShiftJIS | Excluded | | DBCS |
| Document / GetCharacterAndWidth DBCS | Excluded | | DBCS |
| Document / NextPosition Valid DBCS | Excluded | | DBCS |
| Document / RegexSearchAndSubstitution | Excluded | | Regex |
| Document / RegexAssertion | Excluded | | Regex |
| Document / RegexContextualAssertion | Excluded | | Regex |
| Document / RESearchMovePositionOutsideCharUTF8 | Excluded | | Regex |
| Document / RESearchMovePositionOutsideCharDBCS | Excluded | | Regex, DBCS |
| Document / BraceMatch | Deferred | | Bracket matching |
| Document / BraceMatch DBCS | Excluded | | DBCS |
| DocumentUndo / CheckDeleteForwards | Adapted | `test_editdoc_history.c:upstream_document_undo` | See below |
| DocumentUndo / CheckDeleteBackwards | Adapted | Same | See below |
| DocumentUndo / CheckBothWays | Adapted | Same | See below |
| DocumentUndo / CheckInsert | Adapted | Same | See below |
| DocumentUndo / CheckGrouped | Adapted | Same | The group is one transaction; its container action is excluded |
| Words / WordsInText | Deferred | | Word selection and movement, with the editing widget |
| Words / WordsAtEnds | Deferred | | Same |
| SafeSegment / Short, ASCII, UTF-8, UTF-8 Character Fragments, UTF-8 Combining Characters | Deferred | | Splitting long lines for layout, with the editing widget |
| SafeSegment / DBCS Shift-JIS | Excluded | | DBCS |
| DiscardLastCombinedCharacter / all 8 sections | Deferred | | The same layout splitting |
| PerLine / LineMarkers / Initial, AddDelete, MarkerNext | Adapted | `test_editdoc_decorations.c:upstream_line_markers` | Marker numbers are kinds and markers are added to lines of a document; the per-line handle and number queries (`HandleFromLine`, `NumberFromLine`) have no counterpart |
| PerLine / LineMarkers / MergeMarkers, InsertRemoveLine | Adapted | Same | Lines are joined, inserted and removed as edits of the text, which is how markers meet them here |
| PerLine / LineAnnotation | Deferred | | Annotations |

In the DocumentUndo cases, one undo still restores the whole joined step
and empties the history. The caret is where the Tiku rule differs.
Upstream's `Undo` returns the end of the reinserted text, and Scintilla's
editor puts the caret there. Here undo restores the selection the view had
before the step, so the caret returns to where the typing began:

| Case | Upstream caret | Tiku caret |
| --- | --- | --- |
| CheckDeleteForwards | 4 | 1 |
| CheckDeleteBackwards | 6 | 6 |
| CheckBothWays | 7 | 5 |
| CheckInsert | 1 | 1 |
| CheckGrouped | 3 | 1 |

### `testUniConversion.cxx`

| Case | Class | Tiku test | Note |
| --- | --- | --- | --- |
| UTF16Length / all 8 sections | Excluded | | No UTF-16 API |
| UniConversion / UnicodeFromUTF8, 6 sections | Excluded | | The interface kit decodes UTF-8 for display |
| UniConversion / UTF16FromUTF8, 8 sections | Excluded | | No UTF-16 API |
| UniConversion / UTF32FromUTF8, 8 sections | Excluded | | No UTF-32 API |
| UTF8Classify / all 24 sections | Adapted | `test_editdoc.c:utf8_classify` | Each byte string is accepted or refused whole |

For UTF8Classify, upstream flags U+FDD0, U+FFFE, U+FFFF and U+1FFFF as
invalid because they are noncharacters. The document accepts them: they
are well-formed UTF-8, and the text rule refuses only malformed UTF-8 and
NUL.

### Other upstream test files

These cover modules this kit has not adapted. Their cases are classified
one by one when the module is adapted.

| File | Class | When |
| --- | --- | --- |
| `testCharClassify.cxx` | Deferred | Word rules, if the widget needs them |
| `testCharacterCategoryMap.cxx` | Excluded | No Unicode categories |
| `testContractionState.cxx` | Deferred | Folding |
| `testDecoration.cxx` | Deferred | Diagnostic underlines |
| `testGeometry.cxx` | Excluded | Geometry is Tiku code |
| `testPerLine.cxx` | Adapted, Deferred | `LineMarkers` adapted; annotations and fold levels wait for their features |
| `testRESearch.cxx` | Excluded | Regex is a separate later decision |
| `testRunStyles.cxx` | Deferred | Indicators |
| `testSparseVector.cxx` | Deferred | Folding |

## Reviewing a new upstream release

Upstream fixes do not apply automatically. For a new release:

1. Compare the SHA-256 of each file in the Origin table.
2. For each changed file, read the upstream diff and decide whether it
   touches an adapted routine or a translated case.
3. Adapt the change here, update the hashes and this map, and run
   `make test`.
