#!/bin/sh
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
# Install the commit-message hook for this Stomme checkout.
set -eu

hygiene=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
root=$(CDPATH= cd -- "$hygiene/.." && pwd -P)
hooks="$hygiene/githooks"
top=$(git -C "$root" rev-parse --show-toplevel)
[ "$top" = "$root" ] || { echo "Stomme must have its own Git checkout" >&2; exit 1; }
[ -x "$hooks/commit-msg" ] && [ -x "$hygiene/check_commit_msg.py" ] || {
    echo "Stomme commit hook and checker must be executable" >&2
    exit 1
}
git -C "$root" config --local core.hooksPath "$hooks"
printf 'Stomme commit hooks: %s\n' "$hooks"
