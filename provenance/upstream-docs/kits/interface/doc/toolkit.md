# Interface toolkit

The interface kit supplies controls, drawing operations and host adapters.

`dl_ops.txt` defines display-list operation numbers and payload fields.
`gendl.py` generates `tiku_dl_ops.h` for the reader in `tiku_dl.c`.
Run `make dl` after changing the table and `make check-dl` to verify the header.

Control state and input handling live in `tiku_<name>.c`; painting lives in
`tiku_<name>_paint.c`. Text measurement uses `tiku_measure.h`.
`kits/tools/check_kits.py` checks kit dependencies and the separation between
behavior and painting. `make check-toolkit` runs that check and `check-dl`.

The shared [dialog, dropdown and field-input controls](controls.md),
[text-input adapter](text-input.md) and [sensor widgets](sensors.md) compose
existing drawing operations. Add control sources to `mk/kits.mk` and register
behavior files in `kits/tools/check_kits.py`.

### Short interaction animations

`tiku_motion.h` supplies desktop-side timing, an expanding icon outline,
and popup fade helpers. It does not own an event loop or delay an action.
Start a `tiku_motion_t` with a monotonic microsecond timestamp, then call
`tiku_motion_progress()` when drawing. A zero timestamp disables motion;
completion, cancellation, or a backwards timestamp makes the state idle.

Repaint the underlying content before drawing an outline. Request another
frame only while `motion.active` is true, and include the old outline bounds
when replacing or cancelling it. The final repaint removes the previous
outline. `tiku_motion_outline_bounds()` includes its maximum expansion.
Popup fades capture only the intersection of the popup and the repaint
clip. If capture fails, the menu still draws and accepts input immediately.

KintsugiDesktop and KintsugiTracker play a 170 ms outline when an icon is opened,
and KintsugiDesktop fades its menus in over 100 ms. Selection alone does not
animate. **Settings > KintsugiDesktop preferences > Reduce motion** turns these
effects and the workspace's window transitions off, and keeps hover
highlights and pressed controls. The host draws these effects.
