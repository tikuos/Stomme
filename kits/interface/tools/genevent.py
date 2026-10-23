#!/usr/bin/env python3
"""The desktop's key and modifier names, copied for the board.

    genevent.py kits/interface/tiku_event.h > board/gui/tiku_event.h

A board's behaviour files name keys by the desktop's names but carry
the kernel's own event record, so only the names cross.
"""
import re
import sys

src = open(sys.argv[1]).read()
defs = [l for l in src.splitlines() if re.match(r"#define TIKU_(KEY|MOD)_", l)]
sys.stdout.write("""/*
 * TikuGUI -- a board's interface on the desktop.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_event.h - the desktop's key and modifier names, for the board.
 *
 * The kit's behaviour files name keys and modifiers by these; the
 * event record itself is the kernel's on a board, so only the names
 * come across.  Generated from kits/interface/tiku_event.h by
 * kits/interface/tools/genevent.py; `make check-dl` proves it current.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_EVENT_BOARD_
#define TIKU_EVENT_BOARD_

""" + "\n".join(defs) + "\n\n#endif /* TIKU_EVENT_BOARD_ */\n")
