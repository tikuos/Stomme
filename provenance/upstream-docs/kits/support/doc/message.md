# A message that describes itself

Why `tiku_msg.h` exists and where it parts from Be's BMessage.

Everything else on the wire here is a struct both ends were compiled
against: small and quick, and a hard contract.  That contract holds
while the two ends are one build.  It stops holding the moment a
device on the end of a cable is running last month's firmware, which
is the whole direction this interface is being built for.

So: a message carries its own shape.  A code saying what it is, and
under it any number of named fields, each with a type and one or more
values.  A reader asks for what it knows by name, and what it does not
know it steps over -- which is the version tolerance the fixed structs
cannot offer, and the reason this exists.

It is Be's BMessage in shape, and deliberately not in four places:

  - every value carries its length, so a reader can skip a type it has
    never heard of.  A BMessage reader that meets an unknown type is
    lost; this one is merely incurious.
  - the flat form is little-endian throughout, stated byte by byte, so
    a host and a device of different architectures agree.
  - the flat size can be asked for before writing, so the hot paths
    can flatten into a buffer they already own.
  - nothing here instantiates a class named in the data.  Be's
    archiving did, and a message that arrives over a wire is not
    something to hand a class name to.
