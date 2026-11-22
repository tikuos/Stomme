# Shared desktop shell

This package owns the common file manager, Deskbar, desktop, application runner
and supervisor, with their matching system regression tests. Product profiles
select it with `"shell": "desktop"` in `foundation.product.json`.

Each product supplies `system/product/tiku_shell_product.h`. It declares names,
settings and resource paths, About text, initial file types and association
labels. Existing settings files retain their paths. Application manifests and
assets stay in the product. `TIKU_SHELL_APP_TYPES_MAX` declares the product's
handler capacity.

`TIKU_SHELL_SCREEN_W` and `TIKU_SHELL_SCREEN_H` set the desktop surface in
pixels; without them it is 1000 by 680.

Four boolean constants select supported optional behavior:

| Constant | Enabled behavior |
| --- | --- |
| `TIKU_SHELL_BOARD_SERVICES` | Board namespace, channel and device integration |
| `TIKU_SHELL_REMOTE_SERVICES` | Remote preedit/caret, accessibility, links and native print routing |
| `TIKU_SHELL_NOTE_TABS` | Notes placement and automatic window tab hiding |
| `TIKU_SHELL_TOUCH` | The 16 px face at first start, a folder count line and scroll bar sized from the face, and the lens for presses on small controls |

TikuDesktop enables board services and retains its board job, run, statistics,
preferences, tray and VFS implementations as product extensions. Kintsugi enables
remote services and Notes tabs. TikuSurface enables touch with its panel's
touch mode.
These choices preserve the products' existing behavior. The runner and
supervisor retain application ABI and SDK-identity checks.

A desktop built without board services takes the host panel jobs from
`system/desktop/tiku_desktop_panel_jobs.c`, extracted from Kintsugi, and
carries no copy of its own.

Two more constants default to a desktop computer's behavior:

| Constant | Default | Meaning |
| --- | --- | --- |
| `TIKU_SHELL_RESOURCE_PUBLICATION` | 1 | Publish the desktop, its menus and windows, the Deskbar, the session and host observations as resources for other processes; at 0 only the native settings are registered |
| `TIKU_SHELL_EVENT_TIME` | 0 | At 1 the platform's input events carry their time on the `tiku_trk_now()` clock, and a press in a folder pairs with the press before by event time |

A build may define smaller values for the sizes the shell and kits allocate:
`TIKU_TRK_POSES_MAX` (entries one folder view holds, 4096),
`TIKU_TRK_NAV_DEPTH` (navigation menu levels, 8),
`TIKU_TRK_NATIVE_ENTRIES_MAX` (1800), `TIKU_TRK_RESOURCE_PROVIDERS` (512),
`TIKU_TRK_KEPT_PIXELS_BYTES` (folder windows' kept rows, unbounded),
`TIKU_WINDOW_CACHE_BYTES` (window content caches, 64 MiB),
`TIKU_EVENTS_MAX` (notices per batch, 128), `TIKU_REMOTE_SESSIONS` (8) and
`TIKU_STATE_UPDATE_BYTES` (4 MiB). `TIKU_SHELL_POLL_SCALE` multiplies the
settings providers' poll intervals and the resource discovery's refresh. A
platform without job threads defines `TIKU_WORK_INLINE` as 1, and
`kits/support/tiku_work_inline.c` runs the jobs on the loop thread.

`TIKU_SHELL_DAMAGE_CHECK` at 1 draws every bounded frame again in full at the
same clock and compares the two, and compares a turn that took input but drew
no frame with a full frame. Damage put off to the next frame is not counted.
At exit the desktop prints how many comparisons it made and how many found
pixels that differ; each of those prints the box of the differing pixels.

Product builds compile `build/foundation/system`. Its `shell-sources.json`
records the selected source owner and hash. Matching tests are composed with
the application test view, and their ownership is recorded in
`application-sources.json`. Product `mk/system.mk` selects link lists and output
names. Production binaries link the script-refusal stub; development shells
link the test interpreter.
