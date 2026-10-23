/* Desktop column records and state-backed cells stay independent. */
#include "../../system/shared/ui/poseview/tiku_trk_poseview_private.h"
#include <assert.h>
static tiku_store_t *memory;
static tiku_store_t *state(tiku_backend_t *b) { (void)b; return memory; }
static int list(tiku_backend_t *b, const char *path,
                tiku_entry_fn cb, void *ctx)
{ (void)b; (void)path; (void)cb; (void)ctx; return 0; }
int main(void)
{
    tiku_backend_ops_t ops = {.state_store = state, .list = list};
    tiku_backend_t backend = {.ops = &ops};
    memory = tiku_store_memory_open(); assert(memory);
    tiku_trk_poseview_t *v = tiku_trk_poseview_new(&backend, "/sample");
    assert(v);
    v->columns_dirty = 1;
    assert(tiku_trk_poseview_save_columns(v) > 0);
    unsigned char before[4096], after[4096];
    int n = tiku_state_read(memory, "/sample", TIKU_ATTR_COLUMNS, before, sizeof before);
    assert(n > 0);
    tiku_trk_poseview_set_state_slot(v, "desktop");
    v->columns[0].width += 20; v->columns_dirty = 1;
    assert(tiku_trk_poseview_save_columns(v) > 0);
    int m = tiku_state_read(memory, "/sample", TIKU_ATTR_COLUMNS, after, sizeof after);
    int fails = n != m || memcmp(before, after, n) != 0;
    puts(fails ? "FAIL D-032" : "PASS D-032");
    tiku_trk_pose_t p = {0}; char text[128];
    strcpy(p.model.path, "/sample/.Trash/item");
    tiku_state_write(memory, p.model.path, TIKU_ATTR_ORIGINAL, "/old", 5);
    assert(strcmp(tiku_trk_poseview_cell_text(v, &p, TIKU_TRK_COL_ORIGINAL,
                  text, sizeof text), "/old") == 0);
    tiku_state_write(memory, p.model.path, TIKU_ATTR_ORIGINAL, "/new", 5);
    int stale = strcmp(tiku_trk_poseview_cell_text(v, &p, TIKU_TRK_COL_ORIGINAL,
                       text, sizeof text), "/new") != 0;
    puts(stale ? "FAIL D-034" : "PASS D-034");
    tiku_model_t models[2] = {0};
    models[0].kind = models[1].kind = TIKU_KIND_FILE;
    models[0].node_id = 1; models[1].node_id = 2;
    strcpy(models[0].path, "/sample/a"); strcpy(models[0].name, "a");
    strcpy(models[1].path, "/secondary/b"); strcpy(models[1].name, "b");
    assert(tiku_trk_poseview_set_models(v, models, 2) == 2);
    tiku_node_event_t event = {.op = TIKU_EV_MOVED, .model = models[1]};
    strcpy(event.from, "/secondary/b"); strcpy(event.path, "/secondary/c");
    strcpy(event.dir, "/secondary"); strcpy(event.model.path, event.path);
    strcpy(event.model.name, "c");
    tiku_trk_poseview_notify(v, &event);
    int mixed = tiku_trk_poseview_count(v) != 2;
    event.op = TIKU_EV_CREATED; event.model.node_id = 3;
    strcpy(event.path, "/secondary/d"); strcpy(event.model.path, event.path);
    strcpy(event.model.name, "d");
    tiku_trk_poseview_notify(v, &event);
    mixed |= tiku_trk_poseview_count(v) != 3;
    puts(mixed ? "FAIL D-040" : "PASS D-040"); fails += mixed;
    assert(tiku_trk_poseview_set_results(v, NULL, 0) == 0);
    assert(tiku_trk_poseview_set_models(v, models, 2) == 2);
    tiku_trk_poseview_free(v); tiku_store_free(memory);
    return !!(fails || stale);
}
