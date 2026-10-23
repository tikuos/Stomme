/* Native Fonts panel: shared control geometry and live font-size changes. */
#include <assert.h>
#include "../../system/shared/ui/windows/tiku_trk_fonts_window.c"

void tiku_trk_deskbar_apply_settings(tiku_trk_deskbar_t *bar,
    const tiku_trk_deskbar_settings_t *settings)
{ (void)bar; (void)settings; }

static void click(tiku_trk_fonts_window_t *p, tiku_rect_t r, int complete)
{
    tiku_event_t ev = {.type = TIKU_EVENT_POINTER_DOWN, .button = 1,
        .x = r.x + r.w / 2, .y = r.y + r.h / 2};
    tiku_window_send(p->window, &ev);
    if (complete) { ev.type = TIKU_EVENT_POINTER_UP; tiku_window_send(p->window, &ev); }
}

int main(void)
{
    int dirty = 0;
    tiku_trk_fonts_window_t p;
    tiku_surface_t *s = tiku_surface_new(900, 700, TIKU_RGB(51,102,152));
    tiku_workspace_t *workspace = tiku_workspace_new(s);
    tiku_store_t *store = tiku_store_memory_open();
    tiku_trk_fonts_window_init(&p, workspace, NULL, store, 900, 700, &dirty);
    tiku_trk_fonts_window_open(&p); assert(p.window);
    int x = p.window->frame.x, y = p.window->frame.y;
    tiku_workspace_draw(workspace);
    font_layout_t l = layout(tiku_window_content(p.window));
    click(&p, l.plus, 0); assert(p.model.value.size == 12);
    tiku_event_t ev = {.type = TIKU_EVENT_POINTER_UP, .button = 1, .x = 0, .y = 0};
    tiku_window_send(p.window, &ev); assert(p.model.value.size == 12);
    click(&p, l.plus, 1); assert(p.model.value.size == 14);
    assert(p.window->frame.x == x && p.window->frame.y == y);
    l = layout(tiku_window_content(p.window));
    click(&p, l.revert, 1); assert(p.model.value.size == 12);
    for (int family = 0; family < 3; family++) for (int size = 10; size <= 16; size += 2) {
        p.model.value.family = family; p.model.value.size = size; apply(&p);
        tiku_workspace_draw(workspace); l = layout(tiku_window_content(p.window));
        assert(l.family.x + l.family.w < l.preview.x);
        assert(l.size.y + l.size.h < l.preview.y);
        assert(l.paper.h >= tiku_font_bold()->height + 38 + tiku_font_plain()->height);
        assert(l.help.y + l.help.h < l.done.y);
        assert(l.defaults.x + l.defaults.w < l.revert.x);
        assert(l.revert.x + l.revert.w < l.done.x);
        assert(l.done.y + l.done.h <= p.window->frame.y + p.window->frame.h);
        if (family == 0 && (size == 12 || size == 16))
            assert(!tiku_surface_png(s, size == 12 ? "/tmp/tiku-fonts.png" : "/tmp/tiku-fonts-large.png"));
    }
    activate(&p, DEFAULTS); tiku_workspace_draw(workspace);
    tiku_dialog_focus(&p.dialog, FAMILY);
    ev = (tiku_event_t){.type = TIKU_EVENT_KEY_DOWN, .key = TIKU_KEY_DOWN};
    tiku_window_send(p.window, &ev); assert(p.model.value.family == 1);
    /* Cmd-A on a one-at-a-time list is "select the first": the panel
     * must not read that as a family the hand chose. */
    ev = (tiku_event_t){.type = TIKU_EVENT_KEY_DOWN, .key = 'a',
                        .modifiers = TIKU_MOD_CMD};
    tiku_window_send(p.window, &ev); assert(p.model.value.family == 1);
    ev = (tiku_event_t){.type = TIKU_EVENT_KEY_DOWN, .key = TIKU_KEY_TAB}; tiku_window_send(p.window, &ev); assert(p.dialog.focus == MINUS);
    tiku_window_send(p.window, &ev); assert(p.dialog.focus == PLUS);
    ev.key = TIKU_KEY_RETURN; tiku_window_send(p.window, &ev); assert(p.model.value.size == 14);
    int height = p.window->frame.h;
    p.offer.count = TIKU_TRK_FONT_OFFER_MAX;
    for (int i = 3; i < p.offer.count; i++) snprintf(p.offer.name[i], 64, "Extra family %d", i);
    tiku_workspace_draw(workspace); assert(p.window->frame.h == height);
    ev = (tiku_event_t){.type = TIKU_EVENT_WHEEL, .dy = 4};
    tiku_window_send(p.window, &ev); assert(p.families.top > 0);
    /* Return reaches the panel's Done, as Escape reaches its cancel. */
    tiku_dialog_focus(&p.dialog, FAMILY);
    ev = (tiku_event_t){.type = TIKU_EVENT_KEY_DOWN, .key = TIKU_KEY_RETURN};
    tiku_window_send(p.window, &ev); assert(!p.window);
    tiku_trk_fonts_window_open(&p); assert(p.model.value.size == 14 && p.model.value.family == 1);
    activate(&p, DEFAULTS); activate(&p, DONE);
    tiku_workspace_free(workspace); tiku_surface_free(s); tiku_store_free(store);
    puts("PASS: Fonts layout, all font sizes/families, pointer release, keyboard, scrolling and persistence");
    return 0;
}
