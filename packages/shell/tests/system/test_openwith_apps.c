/* Only document-capable bundles belong in the Open With chooser. */
#include "tiku_trk_open_with_window.h"
#include "tiku_state.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    tiku_trk_apps_t apps = {0};
    tiku_trk_launch_add_document(&apps, "Editor", "/runner", "/editor.so", "text/plain");
    tiku_trk_launch_add_document(&apps, "Monitor", "/runner", "/monitor.so", "");
    tiku_trk_launch_add_document(&apps, "Reader", "/runner", "/reader.so", "text/html");
    tiku_trk_launch_add(&apps, "Legacy tool", "/bin/true", "");
    tiku_surface_t *surface = tiku_surface_new(1000, 680, 0);
    tiku_workspace_t *workspace = tiku_workspace_new(surface);
    tiku_store_t *store = tiku_store_memory_open();
    assert(surface && workspace && store);
    tiku_trk_open_with_window_t chooser;
    int dirty = 0;
    tiku_trk_open_with_window_init(&chooser, workspace, 1000, 680,
                                   &dirty, NULL, NULL, NULL, store);
    const char *types[] = {"text/plain"}, *paths[] = {"/example.txt"};
    tiku_trk_open_with_window_open(&chooser, &apps, types, 1, paths, 1);
    assert(chooser.count == 3 && chooser.window);
    assert(!strcmp(chooser.cand[0].app->name, "Editor"));
    for (int i = 0; i < chooser.count; i++)
        assert(strcmp(chooser.cand[i].app->name, "Monitor"));
    assert(!strcmp(chooser.cand[2].app->name, "Reader"));
    assert(chooser.cand[2].rel == TIKU_TRK_REL_NONE);
    tiku_workspace_free(workspace); tiku_surface_free(surface); tiku_store_free(store);
    puts("PASS: non-document utilities excluded; unmatched readers and legacy tools retained");
}
