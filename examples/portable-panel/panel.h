/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * panel.h - bounded control panel over portable input and presentation.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_PORTABLE_PANEL_H_
#define TIKU_PORTABLE_PANEL_H_
#include "tiku_framebuffer.h"
#include "tiku_event_queue.h"
#include "tiku_slider.h"
#include "tiku_press.h"

/** @brief Caller-owned panel state; children retain their allocator bindings. */
typedef struct {
    tiku_framebuffer_t *frame;
    tiku_event_queue_t *events;
    tiku_slider_t slider;
    tiku_press_t press;
    tiku_rect_t dirty;
    int recording;
} tiku_portable_panel_t;

/** @brief Initialize fresh state with an 800x480 surface and 16 input slots. */
int tiku_portable_panel_init(tiku_portable_panel_t *panel, tiku_allocator_t allocator);
/** @brief Consume at most budget queue results and update control state. */
size_t tiku_portable_panel_dispatch(tiku_portable_panel_t *panel, size_t budget);
/** @brief Repaint dirty pixels and submit; BUSY/error retains dirty state for retry. */
tiku_present_result_t tiku_portable_panel_render(tiku_portable_panel_t *panel,
                                                  tiku_presenter_t presenter);
/** @brief Release children; a pending presentation or ticket returns zero. */
int tiku_portable_panel_stop(tiku_portable_panel_t *panel);
#endif
