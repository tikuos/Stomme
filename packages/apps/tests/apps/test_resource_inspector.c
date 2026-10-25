/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_resource_inspector.c - native proposal targeting and exported-menu boundaries.
 * SPDX-License-Identifier: Apache-2.0
 */
#define TIKU_APP_SO
#include "../../apps/resource-inspector/tiku_inspector.c"
#include <assert.h>
#include <unistd.h>
static unsigned frames;
/** @brief Open a native fixture canvas without a host display. */
static uint32_t opened(void *context, const char *title, int w, int h)
{
    (void)context;
    assert(!strcmp(title, "Resource Inspector") && w == 840 && h == 620);
    return 17;
}
/** @brief Check the complete logical native frame. */
static int frame(void *context, uint32_t window, const uint32_t *pixels, int w,
                 int h)
{
    (void)context;
    assert(window == 17 && pixels && w >= 760 && h >= 560);
    frames++;
    return 0;
}
/** @brief Exported menus contain no host mutation control. */
static int menus(void *context, uint32_t window, const tiku_menuset_t *menu)
{
    (void)context;
    (void)window;
    for (int i = 0; i < menu->nmenu; i++)
        for (int j = 0; j < menu->menu[i].nitem; j++) {
            int command = menu->menu[i].item[j].command;
            assert(command != RI_APPLY && command != RI_INVOKE &&
                   command != RI_CANCEL);
        }
    return 0;
}
int main(void)
{
    assert(!setenv("STOMME_RESOURCE_DIRECTORY",
                   "/tmp/stomme-inspector-missing-fixture", 1));
    tiku_app_services_t services = {
        .open = opened, .frame = frame, .menus = menus};
    void *state = NULL;
    assert(!start(&state, &services));
    ri_app_t *p = state;
    for (int i = 0;
         i < 1000 && (ri_transport_busy(p->transport) || !p->refresh_required);
         i++) {
        ri_collect(p);
        usleep(1000);
    }
    assert(!ri_transport_busy(p->transport) && p->refresh_required);
    p->description = tiku_msg_new(0);
    assert(p->description);
    assert(tiku_msg_add_string(p->description, "kind", "setting") &&
           tiku_msg_add_string(p->description, "availability", "available") &&
           tiku_msg_add_string(p->description, "path", "/settings/ui/theme") &&
           tiku_msg_add_int64(p->description, "change_scopes",
                              TIKU_RESOURCE_WRITE));
    p->caller.scopes = 63;
    p->refresh_required = 0;
    strcpy(p->connected, p->endpoint.text);
    load(p, 0, "/settings/ui/theme");
    assert(can_change(p, 0));
    load(p, 0, "/settings/ui/reduce-motion");
    assert(!can_change(p, 0));
    assert(!ri_action(p, RI_APPLY) && !ri_transport_busy(p->transport));
    assert(strstr(p->status, "Inspect the edited"));
    load(p, 0, "/settings/ui/theme");
    tiku_field_start(&p->endpoint, "/tmp/another-owner");
    assert(!can_change(p, 0));
    assert(!ri_action(p, RI_APPLY) && !ri_transport_busy(p->transport));
    tiku_field_start(&p->endpoint, p->connected);
    assert(can_change(p, 0));
    p->refresh_required = 1;
    assert(!can_change(p, 0));
    p->refresh_required = 0;
    p->caller.scopes = TIKU_RESOURCE_READ | TIKU_RESOURCE_CONTENT;
    assert(!can_change(p, 0));
    p->caller.scopes = 63;
    assert(!pick(p, p->id, RI_APPLY) && !ri_transport_busy(p->transport));
    p->focus = 2;
    load(p, 2, "{}");
    tiku_event_t enter = {.type = TIKU_EVENT_KEY_DOWN, .key = TIKU_KEY_RETURN};
    assert(!input(p, p->id, &enter));
    char *proposal = document(p, 2, 65536);
    assert(proposal && strchr(proposal, '\n') &&
           !ri_transport_busy(p->transport));
    free(proposal);
    load(p, 0, "/settings/ui/theme");
    tiku_event_t press = {
        .type = TIKU_EVENT_POINTER_DOWN, .button = 1, .x = 780, .y = 58};
    assert(!input(p, p->id, &press));
    assert(p->dialog.capture == RI_INSPECT);
    press.type = TIKU_EVENT_POINTER_UP;
    assert(!input(p, p->id, &press));
    assert(strstr(p->status, "Waiting for the native"));
    char long_path[TIKU_RESOURCE_PATH_MAX];
    long_path[0] = '/';
    memset(long_path + 1, 'a', sizeof long_path - 2);
    long_path[sizeof long_path - 1] = 0;
    load(p, 0, long_path);
    char *copy = document(p, 0, TIKU_RESOURCE_PATH_MAX - 1);
    assert(copy && !strcmp(copy, long_path));
    free(copy);
    assert(frames > 1);
    stop(p);
    puts(
        "PASS Resource Inspector: current target/grant, stale observations, native menu boundary and multiline editor input");
    return 0;
}
