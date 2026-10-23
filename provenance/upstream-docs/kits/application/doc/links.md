# Explicit native link opening

`tiku_app_open_link` sends a bounded external URI and request ID. The host
checks the requesting session's live focused window and consumes one host
keyboard/pointer/menu gesture no older than two seconds. Focus changes clear
that gesture. Opening is never an automatic side effect of importing content.

The target profile is shortest-form UTF-8 HTTP, HTTPS and mailto, at most 2048
bytes. Local fragments, unsupported schemes, malformed percent escapes,
controls and backslashes refuse. Local Write bookmarks remain local.

The native launcher receives the URI as one argv element without a shell.
Dispatch runs on a worker with a five-second child timeout. The reply reports
queue acceptance; it does not assert that the browser loaded a remote page.
Write and Deck ignore uncorrelated replies and time out old hosts after two
seconds. Link opening does not alter document content or history.

Tests inject dispatch/spawn backends; qualification does not open remote URLs.
