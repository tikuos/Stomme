# The display list

From the header of `kits/interface/tiku_dl.h`, where a shorter statement now
stands.

A session hands its window over as PIXELS: a frame copied, or a surface
shared when both ends are on one machine.  That is right for a machine
talking to itself and wrong for the thing this interface is being built
for.  A 372x302 window is 449,376 bytes; down a 115200 line that is
thirty-nine seconds, and the shared-surface path cannot help -- sharing
a surface means one machine, which a serial line denies.

So: a third way to say what a window looks like.  Not the pixels but
the CALLS -- twenty-one of them for that same window, around six
hundred bytes, which is the difference between a tenth of a second and
most of a minute.

It is deliberately NOT an app_server.  Nothing here draws a line or a
glyph on anybody's behalf: the commands are the ones the interface
already has, semantic where the interface is semantic -- "a button,
here, disabled, labelled this" -- so the stream is short because the
vocabulary is high, not because it was compressed.

And measuring stays where it is.  Every window in this interface lays
itself out by asking how wide a piece of text is; a stream that had to
ask the far end would pay a round trip per label, which is the mistake
that made remote interfaces a byword.  The face tables are small and
both ends carry them.

The recorded form IS the wire form -- appending a command writes the
bytes that will be sent -- so flattening is a copy and there is one
description of a command rather than two that must be kept agreeing.

  per command:  [u16 op][u16 length][payload]

The length is what lets a player step over an op it does not know, so
a newer end may draw with something an older one has never heard of
and the older one draws the rest.  Same bargain as tiku_msg, for the
same reason.
