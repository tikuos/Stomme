# Sensor widgets

`tiku_sensor.h` provides caller-owned, bounded sample history.
`tiku_sensor_paint.h` provides a time-series chart, numeric readout, level meter
and sparkline. They do not open windows, poll a device, record files, or interpret
VFS paths. Feed them from an application's existing acquisition path.

## Sample history

```c
tiku_sample_t samples[600];
tiku_history_t history;
tiku_history_init(&history, samples, 600);
tiku_history_push(&history, timestamp_us, temperature, 1);
/* An explicitly missed measurement is a gap, not a zero reading. */
tiku_history_push(&history, next_timestamp_us, 0, 0);
```

Capacity is 1..4096 samples per history. The oldest sample is overwritten when
full; the kit never allocates memory. Timestamp units are microseconds from any
nonnegative monotonic origin shared by all plotted series. Older timestamps and
nonfinite valid values are rejected without changing the history. An equal
timestamp replaces the newest sample. Call `tiku_history_at` with a logical
index to read oldest-to-newest; pointers remain valid only until mutation.

`tiku_history_range` computes valid min/max in an inclusive time window.
`tiku_history_nearest` returns an actual sample, including an invalid sample:
inspectors must check validity and their acceptable distance from the requested
time. It does not interpolate missing data. Synchronize acquisition with painting
in the application's event loop; histories are not independently thread-safe.

`tiku_history_status` distinguishes No data, Live, Stale and Disconnected. A
missing latest sample, a future sample relative to `now`, or an expired age limit
is stale. Callers decide whether the source is connected and how old is too old.
Non-live readouts show `--`, not zero or an old value presented as current.

## Time-series chart

```c
tiku_sensor_plot_t plot = {
    .count = 1, .autoscale = 1,
    .from_us = window_start, .to_us = window_end,
    .gap_us = 500000, .unit = "Temperature (°C)",
    .threshold_on = 1, .threshold = 27
};
plot.trace[0] = (tiku_sensor_trace_t){
    .history = &history, .label = "Probe", .ink = TIKU_C_SELECT
};
tiku_sensor_plot_draw(&plot, surface, bounds);
```

Up to four traces share a Y axis and unit. Different physical quantities should
use separate plots. Fixed ranges use `min` and `max`; automatic ranges consider
only visible, valid samples in the window and expand flat traces. Hidden traces
do not influence scaling. Values outside a fixed range saturate at the edge.
Legend labels are numbered, and `dashed` adds a distinction beyond trace colour.
Callers choose colours appropriate to the active theme.

The X axis shows seconds relative to `to_us` (the right edge). Thresholds outside
the visible Y range are not drawn. Empty windows say No data. Invalid fixed
ranges say Invalid scale; small rectangles omit the chart interior. Chart
geometry is available through `tiku_sensor_plot_area` for hit testing; set
`cursor_on` and `cursor_us` to paint an inspection line. The application presents
the exact sample timestamp/value returned by `tiku_history_nearest`.

Both charts and sparklines stream screen-column summaries in O(retained samples)
with constant scratch space. Each column keeps first/last and min/max so a short
spike is not averaged away. Explicit missing samples break the line, including
within one column. `gap_us > 0` also breaks intervals longer than that limit;
zero disables this interval check. Retention and repaint rate remain explicit
application choices. A long gap is not filled with synthesized points.

## Readouts, meters and sparklines

- Readouts show a label, value/unit and status. Decimal precision is clamped to
  0..6. Desktop digits use a larger face when it fits; the board recorder uses
  its available bold face. Narrow fields elide text within their own bounds.
- Meters support horizontal or vertical fill, signed finite ranges, and an
  optional warning marker. Fill runs from the configured minimum to maximum;
  it is not a bipolar bar extending outward from zero. Values are clamped to the
  range. Unavailable readings have no fill; invalid ranges are labelled.
- Sparklines use the same history, gap handling and peak preservation as plots,
  with automatic scaling and no axes. Pair them with a readout/status label;
  historical data can remain useful when a source is disconnected.

## Pause is a view operation

The widgets do not stop acquisition. To freeze an exact view, initialize a
second caller-owned history, copy using `tiku_history_copy`, and bind the plot to
that snapshot while preserving its time window. The buffers must not overlap.
If the destination is smaller, only the newest samples fit. Resume by rebinding
to live history. This keeps a frozen view stable even after the live ring wraps.

## Demo and verification

Build with `make apps` and open **Sensors** from TikuDesktop's Applications
menu (the Sensor Monitor demo, `build/runtime/apps/sensors.so`), or run
`build/examples/tiku-example-sensors` with the desktop running.

The demo is explicitly simulated: it does not connect to or configure hardware.
It samples at most ten times per second, exposes 30/60-second windows,
automatic/fixed scaling, pause/resume, a second dashed trace, synthetic missing
samples and simulated disconnection. Click the chart for sample inspection.
Buttons use the shared dialog adapter, including Tab/Space and
release-to-activate pointer behavior. The Sensors menu offers the same actions.

`build/tests/test_sensor` verifies history bounds, snapshots, gaps, peaks,
scaling, clipping, light/dark/monochrome themes, display-list round trips and
demo behavior. It writes a preview to `/tmp/tiku-sensor-monitor.png`.
`make check-toolkit` checks behavior/painter compilation against board headers.
The painters compose existing display-list operations; no new opcodes are used.
Dense traces still consume display-list space: board apps must budget their
recording buffers and handle recording overflow normally. Compilation is checked;
this demo has not been deployed to physical boards.
