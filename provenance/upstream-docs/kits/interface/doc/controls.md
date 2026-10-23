# Shared dialog controls

Startup and Devices Preferences use these reusable controls. Behaviour lives
apart from painting and accepts `tiku_input_event_t` from `tiku_input.h`.
On the desktop, `tiku_event_t` is an alias. Board callers translate their input
into this record; the kernel's unrelated byte-sized `tiku_event_t` is unchanged.
Existing board applications are not automatically migrated to these adapters.

## Dialog focus and button tracking

Keep one `tiku_dialog_t` per dialog and initialize it with `tiku_dialog_init`.
Before drawing or handling input, rebuild the enabled controls in Tab order:

```c
tiku_dialog_begin(&dialog, ID_SAVE, ID_CANCEL);
tiku_dialog_add(&dialog, ID_NAME, name_rect, TIKU_DIALOG_INPUT, writable);
tiku_dialog_add(&dialog, ID_SAVE, save_rect, TIKU_DIALOG_BUTTON, dirty);
tiku_dialog_add(&dialog, ID_CANCEL, cancel_rect, TIKU_DIALOG_BUTTON, 1);
tiku_dialog_end(&dialog);
tiku_dialog_result_t result = tiku_dialog_event(&dialog, &event);
```

Use stable nonnegative IDs; `TIKU_DIALOG_NONE` means no action. There are at
most 64 controls; duplicate IDs or overflow return an error. Hidden controls
should be omitted and unavailable controls disabled. Tab/Shift-Tab wraps and
skips disabled controls. Return activates a focused button or the default;
Escape activates Cancel; Space activates a focused non-input control.
Repeated Return/Space does not activate a button repeatedly.

A control that disables itself hands the focus to the control beside it,
not to the first row of the panel, so the next Space cannot work a setting
the hand never aimed at.

Use `tiku_dialog_focused` and `tiku_dialog_pressed` for paint flags. Route
pointer move/up to the pressed dialog even outside its window: activation
requires release inside the original control. Send DEACTIVATED when focus is
lost or a modal takes over to cancel an unfinished press. The shell now does
this for both preferences windows.

## Dropdown

Initialize `tiku_choice_t`, add labels with enabled flags, then select the
current item. Capacity is 32 choices with labels shorter than 64 bytes;
overflow is refused, not silently truncated. Keep application values separately
and map `choice.selected` back to them.

Use `tiku_choice_draw` for the closed field and draw
`tiku_choice_draw_popup` last, over the completed panel. Both receive the same
anchor rectangle; the popup also takes its containing bounds. It opens above
or below, stays within those bounds, and shows up to eight rows. On a recording
surface, the popup restores clipping to the supplied bounds; desktop paint
restores the previous clip.

An open choice gets events before dialog shortcuts or hit testing. Route
outside clicks and wheel events to it too. Space/Return opens; arrows and
Home/End move the highlight; Return or a complete row click commits selection.
Escape/outside click cancels. Tab cancels and propagates to dialog navigation.
`TIKU_CHOICE_CHANGED` signals a local selection change, never permission to
write a device setting. Keep an explicit Apply/Save action.

## Text field input

Existing `tiku_field_t` and its small legacy API remain available. Rich editing
is opt-in through a separate `tiku_field_input_t` and `tiku_field_event`.
Call `tiku_field_input_init` after loading a different value to clear history
and pointer state. Dispatch input after the dialog, when it was not consumed;
pass whether this input has focus. Continue dispatching move/up while dragging.
Paint with `tiku_ui_textfield_scroll`, using the field's caret/selection and
the adapter's `scroll` offset.

The adapter supports pointer/Shift selection, Home/End, clipboard shortcuts,
and four undo plus four redo snapshots. History memory is opt-in and bounded.
Read-only fields can select/copy but cannot mutate. Inserts and pastes are
atomic: invalid UTF-8, denied characters or length overflow return
`TIKU_FIELD_REJECTED` without altering text or selection. Return/Escape report
COMMITTED/CANCELLED; the caller decides whether to close or save.

Navigation/deletion respects UTF-8 codepoints, not grapheme clusters. Full IME
composition and native accessibility integration remain separate work. The X11
host now bridges this shared store to the [host text clipboard](clipboard.md),
including the desktop's existing application-session mirroring.

## Preferences layout, property tables and display text

`tiku_preferences_layout.h` supplies an etched group's font-aware interior and
the common footer pattern: a leading utility action and a right-aligned group
of primary/cancel actions. `tiku_preferences_actions_width` reports the minimum
width; `tiku_preferences_actions` returns success only when the supplied bounds
fit. On insufficient space its output rectangles are empty, never overlapping
click targets. Button widths use the same text measurement and padding as native
buttons. Both Startup and Devices Preferences use these helpers.

`tiku_properties.h` / `tiku_properties_paint.h` provide a bounded two-column
property table, now used for Devices Preferences' current values:

```c
tiku_properties_t properties;
tiku_properties_init(&properties);
tiku_properties_add(&properties, "Device settings", "", TIKU_PROPERTY_HEADING);
tiku_properties_add(&properties, "CPU clock", current_rate, 0);
tiku_properties_add(&properties, "Radio power", "Unavailable",
                    TIKU_PROPERTY_UNAVAILABLE);
properties.selected = 1;
tiku_properties_draw(&properties, surface, table_rect, focused_row);
```

Tables hold at most 32 borrowed label/value pairs; keep the strings alive through
drawing. Headings span both columns and are not selectable. Unavailable values
are subdued but remain selectable so the application can explain them. Read-only
values are ordinary rows; write permissions belong to the editor, not the table.
The label column is measured from all labels and capped at half the inner width.
Labels and values are ellipsized independently.

Only complete visible rows are painted and registered for input. Set `row_h`
for a custom row height, otherwise the current font determines it. The `top`
index and `tiku_properties_select` support scrolling/revealing a selected row.
Use `tiku_properties_controls` between dialog begin/end to register visible rows
in keyboard order with stable `base + row` IDs. It refuses duplicate IDs or
insufficient capacity without partially modifying the dialog. It does not add
a scrollbar or choose an application's scrolling/key policy automatically.

`tiku_text_layout.h` handles measured line elision and streaming word wrapping
without allocation. Valid UTF-8 codepoints are never split; malformed bytes
are displayed as `?`. Explicit newlines and blank lines are honored, long words
break at codepoint boundaries, and the last available line marks omitted text.
The marker is `...`, which all baked faces can render. Each output line uses at
most 256 bytes including its terminator. A box too narrow for the marker stays
empty. This is not bidirectional, grapheme-aware or full typography layout.

`tiku_text_line_draw` and `tiku_text_block_draw` apply that behavior using existing
display-list text/clip operations. Desktop painting intersects and restores the
caller's clip. On board recording surfaces these helpers reset clipping after
drawing; callers maintaining an outer clip must reapply it after the helper,
as with existing recording-side painters. No new wire opcode is required.

## Verification

From `applications`:

```sh
make check-toolkit build/tests/test_controls build/tests/test_preferences_kit build/tests/test_startup build/tests/test_deviceprefs
build/tests/test_controls
build/tests/test_preferences_kit
build/tests/test_startup
build/tests/test_deviceprefs
```

These tests use synthetic input and mock devices. They cover focus traversal,
press/release cancellation, popup commit/cancel and bounds, UTF-8 insertion,
atomic rejection, pointer selection, read-only editing, history, and preferences
integration. No physical device is written or restarted.

`test_preferences_kit` also exercises all four baked font sizes, tiny bounds,
buffer limits, explicit blank lines, malformed UTF-8, table headings and focus,
and pixel-identical display-list replay in light/dark themes. Preview images
are written to `/tmp/tiku-properties-light.png` and `-dark.png`.
