# One namespace over several backends

Why `tiku_mount.h` exists and the three things it must do that a plain
forwarder would not.

A shell held one backend, chosen at startup and fixed for the life of
the process: two devices meant two desktops, and a file could not be
dragged from one to the other because there was no name that meant
both.  This is that name.  Backends are attached at a path prefix, and
every operation is routed to the longest prefix that claims it:

  /                     the local files
  /devices/board1       one TikuOS device
  /devices/board2       another

It is itself a backend, so nothing above it knows it exists -- a view
that asks its questions of tiku_backend_ops_t keeps asking exactly
those questions.  That is the whole reason the seam was worth having.

Three things it must do that a plain forwarder would not:

- Rewrite paths both ways.  A backend mounted at /devices/board1 is
  asked about /sys/mem and answers with a model saying /sys/mem; the
  caller must be told /devices/board1/sys/mem, or the next call it
  makes with that path goes to the wrong backend.  The model's backend
  pointer is rewritten to the router for the same reason.

- Synthesise the ancestors.  Nothing serves /devices; it exists
  because something is mounted under it.  A path that is only an
  ancestor of a mount is a directory whose children are the mounts
  beneath it, and it is listed and stat-ed as one.

- Keep identities apart.  Two backends will both call something inode
  12345.  Ids leaving here carry the mount in their top byte, so a
  change notice names one node rather than two, and path_of_id knows
  who to ask.
