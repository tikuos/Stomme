# Portable rendering and presentation

The portable profile selects the existing 32-bit graphics primitives, theme,
control drawing, slider/tab paint and frozen-font text. Raster operations use
the same arithmetic as the desktop. UTF-8 decoding and glyph coverage blending
are shared with the desktop font implementation; portable glyph lookup uses the
profile's plain/bold 12 px subsets.

The selection supports scale one. Dynamic fonts, display-list recording, PNG
file output, desktop surface constructors, the vector painter and outline
readers are excluded. Call `tiku_framebuffer_create()` or
`tiku_framebuffer_borrow()` to supply pixels. Desktop and remote board recording
remain selected by their existing builds. `TIKU_PORTABLE_UI` selects local
painting without the display-list branches; it does not change surface layout.

## Ownership

`tiku_framebuffer_t` is an opaque allocator-bound owner. Its `tiku_surface_t`
is a borrowed drawing view; the public surface layout remains unchanged. Owned
pixel allocations and the owner retain their allocation contexts. A borrowed
pixel buffer is detached on destruction and remains the caller's responsibility.
Its storage must outlive the framebuffer and accepted presentation work.

Dimensions are fixed at creation, from 1 to 32767 in each direction, subject to
checked byte count and allocator capacity. Pixels are packed 32-bit words at
scale one, with stride `width * sizeof(tiku_rgb_t)`. The format is XRGB8888:
`0x00RRGGBB`, with the high byte unused. Primitive drawing rectangles follow the
existing graphics contract: their coordinates and endpoints must be representable
as `int`. The framebuffer acquisition API clips damage with 64-bit arithmetic.

The ownership sequence is:

1. `tiku_framebuffer_begin()` acquires an idle buffer and clips optional damage
   to its bounds. Empty damage or a busy buffer returns NULL.
2. The caller draws through the borrowed surface without modifying its geometry,
   scale, recorder or pixel pointer.
3. `tiku_framebuffer_present()` calls the provider with immutable pixels, format,
   byte length, stride, damage, owner identity and a submission token.
4. `TIKU_PRESENT_OK` transfers read ownership until matching completion. BUSY
   or ERROR retains drawing ownership for retry or abandonment.
5. `tiku_framebuffer_complete()` releases an accepted submission. It rejects stale
   tokens and invalid completion results. Completion errors are observable through
   `tiku_framebuffer_result()`.

The provider cannot complete from inside its submit callback. Calls are serialized
by the owner thread. During an accepted submission the caller must not access a
retained drawing pointer. Begin, abandonment and destruction refuse an in-flight
buffer. The provider must neither retain a rejected view nor access a completed
one. Cache maintenance and display/DMA completion belong to the platform provider.

Abandonment returns drawing ownership without submitting; it retains pixel
contents. Destruction succeeds only when idle, keeping the pointer intact on
busy failure. Failed construction releases partial allocations. Framebuffer
resizing is outside this interface; a consumer can create a replacement after
retiring the previous owner.

## Memory presentation fixture

`platform/host-test/tiku_memory_presenter.*` is a delayed provider over
caller-supplied RGB565 output storage. It keeps accepted XRGB8888 pixels until
`finish()` converts the damaged region and completes the identified owner.
A failed conversion completes with ERROR. Input/output row sizes, byte lengths,
alignment and damage are checked before writing. Conversion takes the high
5/6/5 channel bits and writes little-endian RGB565, leaving row padding and
undamaged pixels untouched. Input and output ranges must be disjoint.

RGB565 conversion remains a provider operation. Shared controls draw in 32-bit
color; a TikuOS adapter can implement conversion and DMA behind the same
presentation contract. The host fixture makes no hardware cache or power claim.

## Panel example

```sh
make FOUNDATION_PROFILE=portable-ui portable-panel
build/portable-ui-host-test/portable-panel panel.ppm
make FOUNDATION_PROFILE=portable-ui test
```

`examples/portable-panel/panel.c` provides an 800×480 scene with a slider,
reset button and checkbox. The checkbox changes example state; it does not
control audio hardware. The panel consumes at most a caller-specified number
of input results per dispatch. Queue cancellation clears slider tracking and
pressed controls. Drawing reuses the surface with accumulated damage. Rejected
submission abandons drawing ownership and preserves damage for retry; accepted
submission keeps the buffer unavailable until completion.

The example uses an injected bounded pool and a delayed memory presenter.
`main.c` is the host harness; its optional PPM output uses host file I/O.
Rendering and presentation require no heap calls beyond the injected provider.
The reference conversion and selected drawing path allocate no scratch buffers
during repaint; fixed-size local arrays remain part of stack accounting.

The tests compare the rendered fixture with pixels recorded from the desktop
revision named in `tests/portable/painting.json`. They also exercise primary-color
conversion, clipped alpha composition, padded strides, allocation failures,
queue pressure, stale completion, busy/error submission and repeated repaint.
Faulted implementations that permit early buffer reuse or stale completion fail
the same tests. These checks establish host behavior and target compilation;
they do not establish S31 display performance or combined radio/display operation.
