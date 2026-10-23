# Host clipboard

The X11 host connects the kit's shared text clipboard to the surrounding
desktop's CLIPBOARD selection. Copy/cut in a shared field or an application
using the desktop clipboard service publishes text; paste imports external
text. It also offers and imports UTF-8 `text/html`. TikuBasic's existing
clipboard service receives the plain-text fallback. Applications that implement
a separate private clipboard are not automatically migrated.

The bridge lives entirely in `tiku_x11_clipboard.c` behind `tiku_host_poll`;
there are no X11 includes in portable field models or desktop shell code.
Board applications still use the existing desktop service/protocol. No board
firmware change is needed, and no device VFS data is written by this bridge.

## Input and failure handling

Imports are asynchronous. Activation and explicit Ctrl-V or Ctrl-Shift-V
request fresh content;
primary-button clicks refresh with a short 250 ms cache so a menu-driven paste
has current text too. The event that initiated the request and later native
input stay queued while selection/property replies are processed. Timers and
the desktop loop keep running. A one-second inactivity timeout and five-second
total transfer limit release input when a peer stalls or sends data too slowly;
this is not a blocking wait loop.

Only an explicit paste or an activation holds input back; the refresh a
plain click arms runs in the background, so a selection owner that never
answers cannot still the desk. A refresh nobody asked for keeps a failure
to itself: the text that was there stands and no error is raised.
A subsequent explicit paste joins a pending refresh and requires its result;
if that refresh fails, it clears the stale cache before releasing Paste.

`TIKU_EVENT_CLIPBOARD` precedes the deferred input. Its `key` contains a
`TIKU_CLIPBOARD_*` result. The desktop mirrors imported text to remote sessions
before forwarding their paste event; embedded clients read the shared store.
KintsugiDesktop and KintsugiTracker show transfer errors in their status area.

Imports never take host ownership. Only a new local copy does that, with a real
X server timestamp. A local copy also cancels an outstanding import, preventing
a late response from overwriting it. Each import has its own temporary hidden
requestor window so stale replies cannot complete a later transfer. Transfer
resources are released on completion, timeout, replacement, and host shutdown.

The store is bounded to 16 MiB of text and optional binary data combined.
Complete imports are validated atomically; oversized, malformed or embedded-NUL text is
refused. Latin-1 conversion also checks the expanded UTF-8 size. On transfer
failure the local text cache becomes empty instead of exposing stale text;
the external owner's clipboard is unchanged. Empty text pastes are no-ops in
both field APIs and TikuBasic's service path, preserving the user's selection.

The legacy field API also inserts nonempty Paste text atomically as UTF-8.
Forbidden characters or capacity overflow reject the paste without deleting the
selection; they are no longer silently filtered or truncated into another name.
`tiku_clipboard_import` itself leaves the store unchanged on failure; it is the
host bridge that deliberately invalidates stale external-cache contents.

## Protocol and limits

The bridge follows the X.Org [selection transfer conventions](https://www.x.org/releases/current/doc/xorg-docs/icccm/icccm.html#Peer-to-Peer_Communication_by_Means_of_Selections):

- Negotiates a bounded TARGETS list, then requests UTF8_STRING, falling back
  to STRING (ISO Latin-1). Owners that refuse TARGETS retain the text path.
- If the owner advertises `text/html`, stages the fallback and reads HTML
  before publishing either. HTML-only selections have an empty fallback.
- Receives direct properties and INCR chunks, validating type and capacity.
- Serves TARGETS, TIMESTAMP, UTF8_STRING, TEXT, representable STRING text and
  `text/html` when available. Large HTML uses the same INCR mechanism as text.
- Refuses unsupported targets and unrepresentable Latin-1 conversions.
- Catches X errors from vanished requestors instead of terminating the host.

PRIMARY/middle-click selection, file-copy operations, standalone image formats,
MULTIPLE conversions, and CLIPBOARD_MANAGER/SAVE_TARGETS persistence are not
implemented. Tracker's file clipboard remains independent. Without a clipboard
manager that has already saved the text, a local copy can disappear when its
owning desktop process exits. This is an X11 bridge, not a native Wayland,
browser, IME or accessibility implementation. The bridge runs on the UI thread;
its short X error-handler traps are not a general multithreaded Xlib facility.

## Typed clipboard inside KintsugiDesktop

The shared store can hold four binary MIME representations alongside its UTF-8
fallback. `tiku_clipboard_import_typed()` validates the text, MIME and combined
size, copies both inputs, and publishes one new stamp. Allocation failure
preserves the preceding pair. A successful text-only import or legacy set
clears the preceding binary representation, even if the text is unchanged.
Binary bytes are opaque to the store; the consuming kit validates the format.
`tiku_clipboard_import_formats()` publishes the complete ordered set and
`tiku_clipboard_find()` looks up an exact MIME token. Duplicate names and a
combined size above 16 MiB are refused. The fallback is separate from this list.
`tiku_clipboard_data()` still returns the first binary representation for old
callers. Text-only replacement clears every binary representation.

Application Kit's `tiku_clip_rpc.h` carries version-1 PROBE, GET and SET
messages through existing application messages without changing the service
ABI. Each has a positive 64-bit request ID. GET replies contain one snapshot
of text, MIME and data. SET acknowledges only after the store owns both
representations. Reply allocation failure occurs before publication;
failure to queue an acknowledgment can leave a published copy without a
reply. The application must not delete a Cut source in that case.

PROBE replies additionally advertise `formats=4`. A client that sees this may
use a version-2 SET with repeated, paired `mime`/`data` fields, in preference
order. All representations must be accepted before acknowledgment. Version-1 GET returns the first representation and fallback; old clients do
not receive unexpected duplicate fields. Old version-1 SET remains supported.
Do not send version 2 to a host that has not advertised multi-format support.

PROBE also advertises `read_format=1`. A client that receives it may send
`tiku_clip_request_format()`, a version-3 GET with one `wanted` field. Supported
values are `text/html`, `text/rtf` and `application/rtf`. Reads return the exact
held representation and the fallback in an owned message, without changing the
store or its stamp. A `text/rtf` request accepts a held `application/rtf` alias.
A missing representation returns fallback text without MIME/data. An exact
consumer must refuse that result, rather than silently pasting another format.
Duplicate or invalid wanted fields, and version-3 publication/probe operations,
refuse. This read does not fetch additional representations from a native host.


TikuWrite probes support once at startup. A matching reply enables typed
Copy/Cut/Paste; older hosts retain the legacy text path. The app matches
request IDs and operations, checks content generation and directional
selection before Cut/Paste, and abandons unanswered requests after five
seconds. Adopting another document cancels pending application work. Copy
does not require the source selection to remain active after capture.
Malformed rich data is refused, not silently pasted as fallback text.

TikuWrite uses `application/x-tiku-richfragment` for styled paragraphs and
pictures. Text-only applications receive the fallback through the unchanged
clipboard mirror; rich clients explicitly GET data rather than broadcasting
binary content to every process. On capable hosts TikuWrite also includes HTML
with resolved formatting and embedded PNG pictures. It is generated from the
captured selection, not the entire open document. The X11 and Cocoa adapters
exchange the HTML and fallback; native Tiku envelopes remain desktop-local.
The sum of plain, native and HTML payloads must fit the same 16 MiB ceiling.
Conversion is bounded but synchronous; large selections can delay Copy.

External HTML paste uses the checked markup importer, with separate 64 MiB
scratch/model budgets. Active content is omitted with a conversion notice;
external resources are not fetched. TikuWrite accepts formatted paragraphs
and one complete table, including nested tables. A mixed selection containing
ordinary text outside a table or multiple top-level tables is refused without
changing the destination. A flat fragment cannot replace a cell rectangle.
Empty imported text is a no-op. Malformed supported rich data is not silently
replaced with its plain fallback. Edit / Paste as plain text explicitly uses
the fallback, ignoring rich formats. Its shortcut is Ctrl-Shift-V (Command-
Shift-V on macOS); it also supports TSV into selected cells. An HTML-only
selection has no plain fallback, so this command does nothing for that selection.
The X11 adapter can retain RTF when that representation is selected during
host import. The Cocoa adapter retains its declared text/HTML profile.
Non-UTF-8 HTML remains unsupported.

## Native macOS adapter

Cocoa publishes text and HTML in one `NSPasteboardItem`, using
`NSPasteboardTypeString` and `NSPasteboardTypeHTML`. Import reads both from the
first item, checks the pasteboard change counter, validates the combined size,
and updates the local store once. Embedded NUL in plain text is refused.
This does not use the general pasteboard in tests: the dedicated Cocoa lane
uses a uniquely named pasteboard. Native macOS execution is still required to
qualify that backend; Linux tests do not exercise AppKit.
Failed background reads are not recorded as imported: an explicit paste retries
the same change and reports failure rather than pasting the preceding contents.

## Verification

From `applications`:

```sh
make build/tests/test_clipboard_text build/tests/test_client_wake
build/tests/test_clipboard_text
build/tests/test_client_wake
make test-host-clipboard
make test-host-clipboard-sanitize
make test-host-clipboard-cocoa # on a native macOS build
```

The X11 targets start a private Xvfb display if installed, otherwise a nested
Xephyr window, and terminates only that test server afterwards. It does not use
the user's actual clipboard. Xephyr requires an existing DISPLAY; Xvfb permits
headless testing. Running `test_host_clipboard` without an explicit
`TIKU_CLIPBOARD_TEST_DISPLAY` reports SKIP, so ordinary test runs cannot silently
overwrite a user's clipboard.

Coverage includes direct UTF-8, Latin-1, multibyte characters split across INCR
chunks, exact size boundaries, conversion expansion, empty/invalid text,
unsupported formats, silent/dead peers, stale responses, no ownership echo,
independence from PRIMARY, and clipboard-before-input ordering. The client
runtime test verifies that a remote application's first paste sees the new
UTF-8 clipboard through its existing service.
HTML cases additionally cover direct and INCR import/export, HTML-only owners,
atomic text/HTML staging, wrong property types, combined-size refusal, owner
disappearance, a stalled HTML transfer and clearing stale HTML on a text copy.
Both Ctrl-V and Ctrl-Shift-V wait for the clipboard result before application
input is delivered. `test_clipboard_offer` injects allocation failure while
preparing either native representation and checks that the preceding offer
remains complete; its sanitizer run also checks leaks.


## Explicit Write representations

Edit / Paste special and the selection context sheet expose source formatting,
Merge Formatting, plain text, HTML and RTF choices. Exact HTML/RTF buttons
require negotiated read-format support. Missing representations, malformed
content, changed selections/generations and read-only destinations refuse
without editing. Existing source conversion limits determine which formats
were published; a refused RTF conversion does not create an RTF representation.

The native Write test uses acknowledged desktop protocol messages and a private
X11 display. It selects ordinary text with both representations, enters the
context sheet, pastes HTML and RTF separately and undoes each at 320×240.
