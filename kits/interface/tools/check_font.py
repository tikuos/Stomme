#!/usr/bin/env python3
#
# TikuOS kits -- the foundation TikuDesktop and TikuTracker
# both stand on.
#
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
#
# check_font.py - the baked font was made by the generator beside it.
#
# The bake is committed, because making it needs PIL and whichever faces
# the machine has; what can be checked anywhere is that genfont.py has not
# changed since the bake was made.  A generator edited without a
# regeneration is a header that no longer says what the code says.
#
# SPDX-License-Identifier: Apache-2.0

import hashlib
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
GENERATOR = os.path.join(HERE, "genfont.py")
BAKE = os.path.join(os.path.dirname(HERE), "tiku_font_data.h")


def main():
    with open(GENERATOR, "rb") as f:
        now = hashlib.sha256(f.read()).hexdigest()
    with open(BAKE, encoding="utf-8") as f:
        head = f.read(4096)
    m = re.search(r"GENERATOR sha256 ([0-9a-f]{64})", head)
    if not m:
        print("tiku_font_data.h carries no generator stamp: run make font")
        return 1
    if m.group(1) != now:
        print("tiku_font_data.h was baked by an earlier genfont.py "
              "(%s.., now %s..): run make font" % (m.group(1)[:12], now[:12]))
        return 1
    print("the baked font matches its generator (%s..)" % now[:12])
    return 0


if __name__ == "__main__":
    sys.exit(main())
