# Shared applications

This package owns matching implementations of Calculator, Developer Calculator,
Reader, image/PDF viewers, Archive, Diff, Grab, Hex, Terminal, Circuit and Marker.
It also owns matching Editor files and shared application regression tests and
fixtures. Product manifests select the installed apps and their kit dependencies.

Each product supplies `apps/shared/tiku_app_product.h` with application identity,
icon registration and existing preference and data-file defaults. The composed
source view links package code alongside those product files. Source export
copies that composed app, its shared libraries and its configuration into an
independent tree built with an exported SDK. The exported source needs no
foundation or desktop checkout.

Editor retains its product Run integration and window implementation; matching
file, context, preference and search behavior uses package code. Notes retains
its product window/input behavior. Write and the Office viewer library retain
their basic and advanced variants. Board applications and Word remain in their
product trees.

The product application builds support `all`, `source`, `package` and `install`.
The source-origin manifest records the extracted files. Active application and
test origins are in `build/foundation/application-sources.json` in each product.
