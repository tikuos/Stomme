# HVIF: what the parser and rasteriser leave out

The format is reimplemented from Haiku's `src/libs/icon/flat_icon/` and
`src/libs/icon/{IconRenderer,style,shape,transformer}/` (Haiku,
haiku-os.org, MIT licensed).

Deliberately not implemented.  Each of these consumes its bytes so the
stream stays in sync, then degrades rather than failing:
  - CONTOUR (21), AFFINE (20) and PERSPECTIVE (22) transformers.  Zero
    occurrences in the Tracker set; AFFINE and PERSPECTIVE have zero in
    the whole Haiku tree.  Payload is skipped and the shape draws with
    its own matrix only.
  - SHAPE_FLAG_HINTING.  Cosmetic vertex snapping; unset in every
    Tracker icon.  Axis-aligned edges stay soft at 16 px.
  - Gamma-correct blending.  Haiku composites through a 2.2 LUT; without
    it antialiased edges read slightly heavier on a light panel.
  - The compound rasteriser.  Shapes composite back to front instead of
    resolving per scanline, which leaves a faint seam where two opaque
    shapes share an edge.
  - Miter and bevel joins.  The stroker unions round joins under the
    nonzero rule; at the 1-2 px stroke widths this artwork uses the
    difference is invisible.
GRADIENT_FLAG_16_BIT_COLORS is rejected outright: Haiku never writes it
and cannot read it, so guessing a layout would desynchronise the stream.
