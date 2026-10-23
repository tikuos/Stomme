# Document Kit references

## Kit boundaries

Document Kit supplies the document model and window-independent editing
controllers. Document Formats supplies file conversion, font providers and
output adapters. The Editor Kit supplies the plain-text programming model.

## Unicode baseline

The selected baseline for segmentation and future line-breaking/bidi data
is [Unicode 17.0.0](https://www.unicode.org/versions/Unicode17.0.0/), using
the matching [versioned UCD](https://www.unicode.org/Public/17.0.0/ucd/).
This is a pinned baseline, not a request to follow the latest release.

Segmentation implements the default extended-grapheme and word rules in
[UAX 29 revision 47](https://www.unicode.org/reports/tr29/tr29-47.html).
`tools/gen_richtext_unicode.py` reads the pinned GraphemeBreakProperty,
WordBreakProperty, DerivedCoreProperties and emoji-data files. Their hashes
are recorded in `tiku_richtext_data.inl`. Generated tables and official test
vectors carry the [Unicode license](LICENSE.Unicode).

The exact GraphemeBreakTest and WordBreakTest files are checked in under
`tests/fixtures/document/unicode17/`; tests do not download data. The runtime
has no Unicode-library dependency. Layout wrapping is a limited policy,
not UAX 14 conformance. Neither segmentation nor the basic cmap font provider
establishes bidi, OpenType shaping or script-coverage conformance.

## Implementation boundaries

The self-contained HTML picture exporter follows the
[PNG Third Edition specification](https://www.w3.org/TR/png-3/) for RGBA
scanlines and chunk framing, and
[RFC 1951 section 3.2.4](https://www.rfc-editor.org/rfc/rfc1951.html#section-3.2.4)
for stored DEFLATE blocks. These are format references; the callback encoder
is local C code and introduces no image-codec dependency.

The [kit guide](README.md) describes the public API and implemented behavior.
