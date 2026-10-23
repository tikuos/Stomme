# Described panels

From the header of `kits/interface/tiku_form.h`, where a shorter statement now
stands.

A few lines of text say what controls there are, what each is called,
and -- the part that matters -- which PLACE in the namespace each one
shows or changes:

    title   Board one
    gauge   Temperature  /devices/board1/sys/temp  0 100
    toggle  Lamp         /devices/board1/led
    field   Name         /devices/board1/name
    button  Reboot       /devices/board1/reboot    1

The shell renders that, tracks it, reads the bound places to fill it
in, and writes to them when somebody works a control.  So a device
with NO interface code of its own has an interface: it publishes what
it has, in text, and the desktop is the program that draws it.

This is the whole thesis in one file.  The desktop IS the device's
interface; the device does not ship a second one, does not link a
toolkit, and does not care what the desktop looks like this year.
And because the description names PATHS, an interface for a board
reached over a serial line is written the same way as one for a
folder of files -- the namespace already made those the same kind of
thing.

Three rows take input.  `entry Label /path` is a field a person types
in: a click gives it the caret, Return writes the words, Escape puts
back what was there.  `slider Label /path lo hi` is a number set by
where the track is pressed.  `choice Label /path a|b|c` is one of a few
words, pressed through in order.  An older desktop skips all three,
counted, as it skips any row it does not know.

A `text` row is the words after it -- or, when those words are one
path and nothing else, whatever that place says, re-read like a gauge's,
so a board's status line moves without the panel being re-published.

WHAT IT DELIBERATELY IS NOT: a layout language.  There are no
coordinates, no sizes and no nesting, because a description that
carried them would be a description that has to be re-authored for
every screen it lands on -- and the reason to describe a panel rather
than draw one is that the drawing end knows things the describing end
cannot: the face, the theme, how much room there is.  Rows in the
order they were written is the whole of the arrangement.
