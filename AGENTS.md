# AGENTS.md

Stomme follows the TikuOS comment, prose and commit conventions.

This repository owns shared kit implementations, source checks and SDK export.
Shared application implementations and matching tests are under `packages/apps`;
product manifests, application data and retained variants stay product-owned.
The optional basic document provider is under `packages/document-basic`.
Common shell implementations and matching tests are under `packages/shell`.
Selected source copies must not be restored inside a product. Matching kit
tests and fixtures are selected from `tests/` through the product source view.
Product shell configuration, application inventories, advanced document and
presentation providers, and board extensions belong to their products.

## Dependencies

`tools/check_dependencies.py` defines the allowed kit include graph. Keep
Interface models independent of drawing and native host headers so they compile
with `TIKU_DISPLAYLESS`. Logical event names and metrics are shared contracts;
product adapters translate physical or firmware events to them.

SDK export selects the kits in `foundation.json`, paired public headers and
PIC archives. Keep private headers and product providers out of the base SDK.
Retain public C names and negotiated protocol behavior during extraction.

## Checks

Run `make check test-tools`, the kit suites relevant to the change, and
`make displayless check-link`. SDK changes require `tools/check_sdk.py` and the
relocated example test in `tests/integration/run_html_sdk.py`. Use separate
output directories for different builds. Do not modify C or header inputs
while a compiler is using that source tree.

`comment-style.md` and `prose-style.md` define source and documentation style.
Stage added source before running tracked-source lint. New regression guards
must reject the failing input they cover before their passing result is used.
Keep original author, copyright and redistribution notices with extracted code.

## Commits

Commit one capability at a time as the repository's configured identity.
Use `Area: what changed` within 72 characters and at most three plain English
body bullets. `hygiene/commit-style.md` defines the message and review contract.
Run `sh hygiene/install_hooks.sh` after cloning to install the local commit hook.
The hook rejects malformed messages, author overrides and a missing checker.
Present the subject, author and body and wait for approval before committing. Push when requested. Preserve unrelated local changes. Do not use
author overrides, assistant trailers or `--no-verify`.

Local plans, measurements and result ledgers remain outside published source.
The local `kintsugi/` and `temp/` directories are ignored.
The source-origin manifest under `provenance/` is part of the source distribution.
