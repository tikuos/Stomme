# The conductor channel

Why the desktop is driven from outside it, from the header of
`tiku_conduct.h`.

A scripted driver compiled INTO the shell can see everything and so is
tempting to grow without limit -- and then it ships, or it does not ship
and half the product's behaviour can only be proven by a binary nobody
runs.  This is the other arrangement: the desktop offers a stated
automation surface, and the driver is an ordinary process on the far end
of a stream.

The surface is deliberately thin, and it is the whole of it:

  INJECT   one input event, exactly as the display would have delivered it
  QUERY    one pixel, or one NAMED string the shell already knows about

Everything a script wants -- that a window is titled so, that a menu
offers such a row, that this pixel is the dusk amber -- is composed by
the DRIVER out of those two.  Nothing about scripts, waits, expectations
or files exists on this side of the wire; that vocabulary lives in the
driver, out of the product entirely.

The channel is off unless the shell was asked for it (-conduct), because
a device that can be puppeted by anything that reaches its socket is a
device with a hole in it.  The framing is the session's: little-endian
[u32 type][u32 length][payload] over any stream -- a local socket now, a
serial link to a real board later, and that is what putting the driver
outside is for.

The external driver accepts `text <UTF-8>` for a committed-text input, using
bounded codepoint-aligned BEGIN/END events through INJECT. `key` delivers a
physical key press/release and does not synthesize text. This distinction lets
packaged Rich View editors be tested without relying on a native keyboard
backend to convert injected key presses. Invalid UTF-8 or refused transport
fails the script. Text uses the driver's bounded single-line, word-split grammar.

The named fact `menu-point:<label>` returns the current Deskbar menu row's
screen coordinates, or an empty result when no visible row matches. The external
driver's `menu-pick` queries those coordinates and injects a pointer gesture.
Its `expect-file` checks a regular file in the driver's filesystem. Both compose
existing input/query operations; the production shell contains no script parser.
