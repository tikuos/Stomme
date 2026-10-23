# HTML view SDK example

This application uses the exported Interface and Translation Kit APIs. The view
loads HTML from memory, displays styled text, tables and a figure, selects and
copies text, finds matches, scrolls and delivers links through a host callback.
Its image callbacks inspect and decode an embedded PNG without resource I/O.

Build the SDK at the repository root, then build this directory:

```sh
make sdk
make -C examples/html-view SDK=/absolute/path/to/build/sdk
make -C examples/html-view SDK=/absolute/path/to/build/sdk smoke
```

`build/htmlview` runs as a desktop client. `build/htmlview.so` exports the
application descriptor for a host. The smoke command requires no desktop and
writes `build/preview.png`. Copy this directory and an exported SDK to separate
locations and pass the relocated SDK path to build without repository sources.
