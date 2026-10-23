# Telling a line of a program apart

The reasoning behind `tiku_syntax.h`.

Given a line, this says which stretches of it are words the language
reserves, numbers, quoted text, and remarks the machine ignores.  It
paints nothing and knows nothing about colour: it hands back a table
of tiku_span_t, and a painter asks the theme what each meaning looks
like.  Which is why a highlighted window re-themes with everything
else instead of carrying a second palette of its own.

The one language it knows is TikuOS BASIC, and it knows it from the
interpreter's own tables rather than from a plausible-looking list --
basic/tiku_basic_token.inl's crunch table plus every
spelling the dispatchers match by hand.  Highlighting that lies is
worse than none: a word painted as reserved that the interpreter would
treat as a variable teaches the reader something false about their own
program, and they will believe it, because the machine said it.

Classification is line-local for BASIC, and that is a fact about the
language rather than a simplification: a remark runs to the end of its
line, a quoted string is closed by the line's end if the quote never
comes, and there is no continuation and no block comment.  Nothing
carries across, so a line can be told apart on its own -- which is
what lets a window classify only the lines it is showing, and never
re-scan a document to paint one row.

C cannot use that signature: a block remark opened on one line is
still open on the next, and no
amount of looking at the second line can tell you so.  What carries
is one small thing -- whether the line BEGAN inside a remark -- and it
is carried explicitly, in and out, by tiku_syntax_spans_on().  A
caller that colours C must therefore know where its lines start from,
which is a cost the language imposes and this file will not hide.
