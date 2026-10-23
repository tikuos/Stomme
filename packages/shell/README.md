# Shared desktop shell

This package owns the common file manager, Deskbar, desktop, application runner
and supervisor, with their matching system regression tests. Product profiles
select it with `"shell": "desktop"` in `foundation.product.json`.

Each product supplies `system/product/tiku_shell_product.h`. It declares names,
settings and resource paths, About text, initial file types and association
labels. Existing settings files retain their paths. Application manifests and
assets stay in the product. `TIKU_SHELL_APP_TYPES_MAX` declares the product's
handler capacity.

Three boolean constants select supported optional behavior:

| Constant | Enabled behavior |
| --- | --- |
| `TIKU_SHELL_BOARD_SERVICES` | Board namespace, channel and device integration |
| `TIKU_SHELL_REMOTE_SERVICES` | Remote preedit/caret, accessibility, links and native print routing |
| `TIKU_SHELL_NOTE_TABS` | Notes placement and automatic window tab hiding |

TikuDesktop enables board services and retains its board job, run, statistics,
preferences, tray and VFS implementations as product extensions. Kintsugi enables
remote services and Notes tabs and retains its host panel job implementation.
These choices preserve the products' existing behavior. The runner and
supervisor retain application ABI and SDK-identity checks.

Product builds compile `build/foundation/system`. Its `shell-sources.json`
records the selected source owner and hash. Matching tests are composed with
the application test view, and their ownership is recorded in
`application-sources.json`. Product `mk/system.mk` selects link lists and output
names. Production binaries link the script-refusal stub; development shells
link the test interpreter.
