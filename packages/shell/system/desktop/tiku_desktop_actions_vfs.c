/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_desktop_actions_vfs.c - session requests and typed folder scripting.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_desktop_lifecycle_vfs_private.h"
#include <limits.h>
#include <unistd.h>
static const char *const verbs[] = {
    "TIKU_TRK_VERB_GET",    "TIKU_TRK_VERB_SET",    "TIKU_TRK_VERB_COUNT",
    "TIKU_TRK_VERB_CREATE", "TIKU_TRK_VERB_DELETE", "TIKU_TRK_VERB_EXECUTE"};
/** @brief Return a live focused folder or the first live folder. */
static folder_t *active_folder(desktop_t *app)
{
    folder_t *folder=folder_of(app,tiku_workspace_focused(app->desk));
    if(folder&&folder->win&&folder->win->open&&folder->view)return folder;
    for (int i = 0; i < app->folder_count; i++)
        if (app->folders[i].win && app->folders[i].win->open &&
            app->folders[i].view)
            return &app->folders[i];
    return NULL;
}
/** @brief Hash the complete ordered paths and selection flags for an index
 * precondition.
 */
static uint64_t view_order(folder_t *folder)
{
    uint64_t value=UINT64_C(1469598103934665603);
    for(int i=0;i<tiku_trk_poseview_count(folder->view);i++) {
        const tiku_trk_pose_t *pose = tiku_trk_poseview_at(folder->view, i);
        const unsigned char *p = (const unsigned char *)pose->model.path;
        while (*p) {
            value ^= *p++;
            value *= UINT64_C(1099511628211);
        }
        value ^= (unsigned)(pose->selected + 1);
        value *= UINT64_C(1099511628211);
    }
    return value?value:1;
}
/** @brief Copy one exact bounded text member. */
static int text_member(const tiku_msg_t *value, const char *key, char *out,
                       size_t capacity)
{
    tiku_msg_t *member = trk_value_find(value, key);
    const char *type = tiku_msg_find_string(member, "type", 0),
               *text = tiku_msg_find_string(member, "value", 0);
    int valid =
        type && !strcmp(type, "text") && text && strlen(text) < capacity;
    if (valid)
        snprintf(out, capacity, "%s", text);
    tiku_msg_free(member);
    return valid;
}
/** @brief Read an optional nonnegative native index. */
static int index_member(const tiku_msg_t *value,const char *key,int *out)
{
    tiku_msg_t *member = trk_value_find(value, key);
    int64_t number = 0;
    const char *type = tiku_msg_find_string(member, "type", 0);
    int valid = !member || (type && !strcmp(type, "integer") &&
                            tiku_msg_find_int64(member, "value", 0, &number) &&
                            number >= 0 && number <= INT_MAX);
    if(valid)*out=(int)number;
    tiku_msg_free(member);return valid;
}
/** @brief Validate member names and refuse duplicated command arguments. */
static int members(const tiku_msg_t *value)
{
    const char *keys[] = {"window",   "generation", "order_token",
                          "property", "specifier",  "index",
                          "end",      "path",       "paths"};
    unsigned seen = 0;
    for(int i=0;i<tiku_msg_count(value,"member");i++) {
        tiku_msg_t *member = tiku_msg_find_message(value, "member", i);
        const char *key = tiku_msg_find_string(member, "key", 0);
        int at;
        for(at=0;at<9;at++)if(key&&!strcmp(key,keys[at]))break;
        tiku_msg_free(member);if(at==9||(seen&(1u<<at)))return 0;seen|=1u<<at;
    }
    return (seen&31u)==31u;
}
/** @brief Decode an exact lifetime-bound native scripting request without
 * effects.
 */
static tiku_resource_error_t
script_request(struct desktop_lifecycle_vfs *owner, const char *path,
               const tiku_msg_t *value, folder_t **folder,
               tiku_trk_req_t *request, char storage[8][TIKU_PATH_MAX])
{
    char id[64], generation[32], order[32], property[32], specifier[32],
        expected[160];
    uint64_t parsed;
    int verb = -1;
    const char *properties[] = {"path", "entry", "selection"},
               *specifiers[] = {"direct", "index",    "range",
                                "path",   "previous", "next"};
    for (int i = 0; i < 6; i++) {
        snprintf(expected, sizeof expected, "/desktop/file-manager/actions/%s",
                 verbs[i]);
        if (!strcmp(path, expected))
            verb = i;
    }
    if(verb<0)return TIKU_RESOURCE_UNSUPPORTED;
    if (!members(value) || !text_member(value, "window", id, sizeof id) ||
        !text_member(value, "generation", generation, sizeof generation) ||
        !text_member(value, "order_token", order, sizeof order) ||
        !text_member(value, "property", property, sizeof property) ||
        !text_member(value, "specifier", specifier, sizeof specifier))
        return TIKU_RESOURCE_VALUE;
    tiku_window_t *window =
        tiku_trk_window_resources_find(owner->app->window_resources, id);
    *folder = window ? folder_of(owner->app, window) : NULL;
    if(!*folder||!(*folder)->view)return TIKU_RESOURCE_OWNER_LOST;
    if (tiku_resource_counter_parse(generation, &parsed) ||
        parsed != (uint64_t)tiku_trk_poseview_generation((*folder)->view))
        return TIKU_RESOURCE_STALE;
    if (tiku_resource_counter_parse(order, &parsed) ||
        parsed != view_order(*folder))
        return TIKU_RESOURCE_STALE;
    memset(request,0,sizeof *request);request->verb=(tiku_trk_verb_t)verb;
    int i;
    for (i = 0; i < 3 && strcmp(property, properties[i]); i++) {
    }
    if (i == 3)
        return TIKU_RESOURCE_VALUE;
    request->prop = (tiku_trk_prop_t)i;
    for (i = 0; i < 6 && strcmp(specifier, specifiers[i]); i++) {
    }
    if (i == 6)
        return TIKU_RESOURCE_VALUE;
    request->spec = (tiku_trk_spec_t)i;
    if (!index_member(value, "index", &request->a) ||
        !index_member(value, "end", &request->b))
        return TIKU_RESOURCE_VALUE;
    tiku_msg_t *single = trk_value_find(value, "path"),
               *index = trk_value_find(value, "index"),
               *end = trk_value_find(value, "end");
    int has_path=single!=NULL,has_index=index!=NULL,has_end=end!=NULL;
    tiku_msg_free(single);tiku_msg_free(index);tiku_msg_free(end);
    if(has_path) {
        if (!text_member(value, "path", storage[0], TIKU_PATH_MAX))
            return TIKU_RESOURCE_VALUE;
        request->path=storage[0];
    }
    if ((request->spec == TIKU_TRK_SPEC_INDEX && !has_index) ||
        (request->spec == TIKU_TRK_SPEC_RANGE && (!has_index || !has_end)))
        return TIKU_RESOURCE_VALUE;
    tiku_msg_t *paths=trk_value_find(value,"paths");
    if(paths) {
        const char *type = tiku_msg_find_string(paths, "type", 0);
        int n = tiku_msg_count(paths, "item");
        if (has_path || !type || strcmp(type, "array") || n > 8) {
            tiku_msg_free(paths);
            return TIKU_RESOURCE_VALUE;
        }
        for(i=0;i<n;i++) {
            tiku_msg_t *item = tiku_msg_find_message(paths, "item", i);
            const char *word = tiku_msg_find_string(item, "value", 0),
                       *kind = tiku_msg_find_string(item, "type", 0);
            if (!word || !kind || strcmp(kind, "text") ||
                strlen(word) >= TIKU_PATH_MAX) {
                tiku_msg_free(item);
                tiku_msg_free(paths);
                return TIKU_RESOURCE_VALUE;
            }
            snprintf(storage[i], TIKU_PATH_MAX, "%s", word);
            request->pathv[i] = storage[i];
            tiku_msg_free(item);
        }
        request->paths = request->pathv;
        request->npath = n;
        if (n)
            request->path = request->pathv[0];
        tiku_msg_free(paths);
    } else if (request->path) {
        request->pathv[0] = request->path;
        request->paths = request->pathv;
        request->npath = 1;
    }
    if ((request->spec == TIKU_TRK_SPEC_PATH ||
         request->spec == TIKU_TRK_SPEC_PREVIOUS ||
         request->spec == TIKU_TRK_SPEC_NEXT) &&
        !request->path)
        return TIKU_RESOURCE_VALUE;
    return 0;
}

/** @brief Validate native session prompts and typed scripting arguments. */
static tiku_resource_error_t validate(void *opaque, const char *path,
                                      const tiku_msg_t *value, int invoke)
{
    struct desktop_lifecycle_vfs *owner = opaque;
    const char *type = tiku_msg_find_string(value, "type", 0);
    if(!invoke||!type||strcmp(type,"object"))return TIKU_RESOURCE_VALUE;
    if (owner->app->closing || alert_model(owner->app)->open)
        return TIKU_RESOURCE_BUSY;
    if (!strncmp(path, "/session/actions/", 17))
        return tiku_msg_count(value, "member") ? TIKU_RESOURCE_VALUE : 0;
    tiku_trk_req_t request;folder_t *folder;char storage[8][TIKU_PATH_MAX];
    return script_request(owner,path,value,&folder,&request,storage);
}
/** @brief Dispatch native scripting and session actions without bypassing
 * confirmation.
 */
static tiku_resource_error_t start(void *opaque, const char *job,
                                   const char *path, const tiku_msg_t *value,
                                   int invoke, tiku_resource_snapshot_t *out)
{
    struct desktop_lifecycle_vfs *owner = opaque;
    desktop_t *app = owner->app;
    tiku_msg_t *result = trk_value_container("object");
    tiku_resource_error_t error=validate(opaque,path,value,invoke);(void)job;
    if(error){tiku_msg_free(result);return error;}
    if(!strncmp(path,"/session/actions/",17)) {
        if (LIFE_TEXT(result, "action", path) ||
            LIFE_BOOL(result, "dispatched", 1)) {
            tiku_msg_free(result);
            return TIKU_RESOURCE_NO_MEMORY;
        }
        if (!strcmp(path, "/session/actions/restore"))
            tiku_trk_session_restore_folders(&app->session, NULL, 0, 0, 0, 0);
        else {
            tiku_trk_db_pick_t pick = {.what = TIKU_TRK_DB_QUIT};
            deskbar_pick_dispatch(app, &pick, 0, 0);
        }
    } else {
        tiku_trk_req_t request;
        folder_t *folder;
        char storage[8][TIKU_PATH_MAX];
        tiku_trk_rep_t *reply = calloc(1, sizeof *reply);
        if(!reply){tiku_msg_free(result);return TIKU_RESOURCE_NO_MEMORY;}
        error=script_request(owner,path,value,&folder,&request,storage);
        if(error){free(reply);tiku_msg_free(result);return error;}
        desktop_script_request(app, folder, &request, reply);
        tiku_msg_t *paths = trk_value_container("array");
        int bad = 0;
        bad |= LIFE_INT(result, "error", reply->error);
        bad |= LIFE_INT(result, "count", reply->count);
        for (int i = 0; i < reply->nresult; i++)
            bad |= trk_value_item(
                paths, tiku_resource_value_string(TIKU_RESOURCE_TEXT,
                                                  reply->result[i]));
        bad |= trk_value_member(result, "paths", paths);
        bad |= LIFE_BOOL(result, "truncated",
                         reply->nresult == 64 &&
                             tiku_trk_poseview_count(folder->view) > 64);
        free(reply);
        if (bad) {
            tiku_msg_free(result);
            return TIKU_RESOURCE_UNKNOWN_OUTCOME;
        }
    }
    desktop_damage_all(app);
    return trk_value_snapshot(out, result, "request_dispatched")
               ? TIKU_RESOURCE_UNKNOWN_OUTCOME
               : 0;
}
/** @brief Copy the currently scriptable folder identity and ordering
 * precondition.
 */
static tiku_msg_t *folder_state(struct desktop_lifecycle_vfs *owner)
{
    tiku_msg_t *out = trk_value_container("object");
    folder_t *folder = active_folder(owner->app);
    char id[33];
    int bad = 0;
    int available =
        folder && !tiku_trk_window_resources_id(owner->app->window_resources,
                                                folder->win, id, sizeof id);
    bad|=LIFE_BOOL(out,"available",available);
    if (available) {
        bad |= LIFE_TEXT(out, "window", id);
        bad |= lifecycle_counter(out, "generation",
                                 tiku_trk_poseview_generation(folder->view));
        bad |= lifecycle_counter(out, "order_token", view_order(folder));
        bad |= LIFE_TEXT(out, "path", folder->path);
        bad |= LIFE_INT(out, "count", tiku_trk_poseview_count(folder->view));
    }
    if(bad){tiku_msg_free(out);return NULL;}return out;
}
/** @brief Register one action subtree and its optional observed state. */
static int register_actions(struct desktop_lifecycle_vfs *owner,int session)
{
    tiku_resource_initial_t initial[7] = {0};
    char paths[7][160];
    int count = session ? 3 : 7, error = 0;
    const char *names[]={"request-end","request-quit","restore"};
    tiku_resource_async_controller_t controller = {.validate = validate,
                                                   .start = start};
    for(int i=0;i<count;i++) {
        int state=!session&&i==6;
        if (state)
            snprintf(paths[i], sizeof paths[i], "/desktop/file-manager/state");
        else
            snprintf(paths[i], sizeof paths[i],
                     session ? "/session/actions/%s"
                             : "/desktop/file-manager/actions/%s",
                     session ? names[i] : verbs[i]);
        initial[i].spec = (tiku_resource_spec_t){
            .path = paths[i],
            .kind = state ? TIKU_RESOURCE_STATE : TIKU_RESOURCE_ACTION,
            .schema_version = 1,
            .schema = {.type = TIKU_RESOURCE_OBJECT},
            .read_scopes = TIKU_RESOURCE_READ,
            .change_scopes =
                state ? 0 : TIKU_RESOURCE_INVOKE | TIKU_RESOURCE_CROSS_APP,
            .sensitive = 1,
            .persistence = "none",
            .controller = session ? "native-session" : "native-file-manager"};
        if (trk_value_snapshot(&initial[i].initial,
                               state ? folder_state(owner)
                                     : trk_value_container("object"),
                               "observed"))
            error = -1;
    }
    if (!error)
        error = tiku_resource_register_async(
            owner->registry,
            session ? "native-session-actions" : "native-file-manager",
            session ? "/session/actions" : "/desktop/file-manager", NULL,
            initial, (size_t)count, &controller, owner,
            &owner->actions[session]);
    for(int i=0;i<count;i++)tiku_resource_snapshot_clear(&initial[i].initial);
    if(!error)owner->actions_live[session]=1;
    return error?-1:0;
}
int desktop_lifecycle_actions_open(struct desktop_lifecycle_vfs *owner)
{
    return register_actions(owner,0)||register_actions(owner,1)?-1:0;
}
void desktop_lifecycle_actions_close(struct desktop_lifecycle_vfs *owner)
{
    for (int i = 0; i < 2; i++)
        if (owner->actions_live[i]) {
            tiku_resource_unregister(owner->registry, &owner->actions[i]);
            owner->actions_live[i] = 0;
        }
}
void desktop_lifecycle_actions_poll(struct desktop_lifecycle_vfs *owner)
{
    tiku_resource_snapshot_t next={0};if(!owner->actions_live[0])return;
    if (!trk_value_snapshot(&next, folder_state(owner), "observed"))
        trk_value_publish(owner->registry, "/desktop/file-manager/state",
                          &next);
    tiku_resource_snapshot_clear(&next);
}
