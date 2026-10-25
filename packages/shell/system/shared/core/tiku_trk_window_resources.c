/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_window_resources.c - lifetime-checked window actions and
 * observations.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_trk_window_resources.h"
#include "tiku_trk_resource_values.h"
#include <stdlib.h>
#include <string.h>
#define TEXT(o, k, v)                                                         \
    trk_value_member(o, k, tiku_resource_value_string(TIKU_RESOURCE_TEXT, v))
#define NUMBER(o, k, v)                                                       \
    trk_value_member(o, k,                                                    \
                     tiku_resource_value_integer(TIKU_RESOURCE_INTEGER, v))
#define BOOL(o, k, v)                                                         \
    trk_value_member(                                                         \
        o, k, tiku_resource_value_integer(TIKU_RESOURCE_BOOLEAN, (v) != 0))
#define WINDOW_FIELDS 8
static const char *const fields[] = {"constraints", "focus",    "geometry",
                                     "identity",    "owner",    "title",
                                     "visibility",  "workspace"};
static const char *const actions[] = {"focus",  "maximize",      "minimize",
                                      "move",   "request-close", "resize",
                                      "restore"};
/** @brief One native slot held by a provider incarnation. */
typedef struct {
    struct tiku_trk_window_resources *owner;
    tiku_window_t *window;
    uint64_t generation;
    tiku_trk_window_source_t source;
    char id[TIKU_RESOURCE_ID_MAX], base[96];
    tiku_resource_stamp_t stamp;
} window_entry_t;
struct tiku_trk_window_resources {
    tiku_resource_registry_t *registry;
    tiku_workspace_t *workspace;
    int width, height;
    int (*source)(void *, tiku_window_t *, tiku_trk_window_source_t *);
    tiku_resource_error_t (*perform)(void *, tiku_window_t *,
                                     tiku_trk_window_action_t, int, int);
    void *context;
    char id[TIKU_RESOURCE_ID_MAX], base[96];
    tiku_resource_stamp_t stamp;
    window_entry_t entries[TIKU_WINDOW_MAX];
};

/** @brief Encode an unsigned native generation without signed conversion. */
static int counter(tiku_msg_t *value, const char *key, uint64_t number)
{
    char text[32]; snprintf(text,sizeof text,"%llu",(unsigned long long)number);
    return TEXT(value,key,text);
}

/** @brief Check native membership and both window and connection
 * incarnations.
 */
static int entry_live(window_entry_t *entry)
{
    tiku_trk_window_resources_t *owner = entry->owner;
    tiku_trk_window_source_t source = {0}; int i;
    for (i=0;i<tiku_workspace_count(owner->workspace);i++)
        if (tiku_workspace_at(owner->workspace,i)==entry->window) break;
    if (i == tiku_workspace_count(owner->workspace) || !entry->window->open ||
        entry->window->generation != entry->generation ||
        owner->source(owner->context, entry->window, &source))
        return 0;
    return source.connection == entry->source.connection &&
           source.generation == entry->source.generation &&
           source.local_id == entry->source.local_id &&
           !strcmp(source.kind, entry->source.kind);
}

/** @brief Copy one current native field without pointers or callback
 * addresses.
 */
static tiku_msg_t *window_value(window_entry_t *entry, int field)
{
    tiku_trk_window_resources_t *owner = entry->owner;
    tiku_window_t *window = entry->window;
    tiku_msg_t *value=trk_value_container("object"); int bad=0,i;
    if (field==0) {
        bad |= NUMBER(value, "minimum_width", 180);
        bad |= NUMBER(value, "minimum_height", 90);
        bad |= NUMBER(value, "maximum_width", 8192);
        bad |= NUMBER(value, "maximum_height", 8192);
        bad |= BOOL(value, "resizable", window->resizable);
        bad |= BOOL(value, "zoomable", window->zoomable);
        bad|=BOOL(value,"plain",window->plain);
        for (i = 0; i < TIKU_TRK_WINDOW_ACTIONS; i++)
            bad |= BOOL(value, actions[i], entry->source.actions & (1u << i));
    } else if (field == 1)
        bad |= BOOL(value, "focused",
                    window == tiku_workspace_focused(owner->workspace));
    else if (field==2) {
        bad |= NUMBER(value, "x", window->frame.x);
        bad |= NUMBER(value, "y", window->frame.y);
        bad |= NUMBER(value, "width", window->frame.w);
        bad |= NUMBER(value, "height", window->frame.h);
        bad|=BOOL(value,"maximized",window->zoom_valid);
    } else if (field==3) {
        bad |= TEXT(value, "id", entry->id);
        bad |= counter(value, "generation", entry->generation);
        bad |= TEXT(value, "session",
                    tiku_resource_registry_session(owner->registry));
        bad |= TEXT(value, "workspace", owner->id);
    } else if (field==4) {
        bad |= TEXT(value, "kind", entry->source.kind);
        bad |= TEXT(value, "name", entry->source.name);
        bad |= counter(value, "connection", entry->source.connection);
        bad |= counter(value, "generation", entry->source.generation);
        bad|=NUMBER(value,"local_id",entry->source.local_id);
    } else if (field==5) bad|=TEXT(value,"text",window->title);
    else if (field==6) {
        bad |= BOOL(value, "open", window->open);
        bad |= BOOL(value, "minimized", window->minimized);
        bad |= BOOL(value, "hidden", window->hidden);
        bad |= BOOL(value, "visible", !window->hidden && !window->minimized);
    } else bad|=TEXT(value,"id",owner->id);
    if(bad){tiku_msg_free(value);return NULL;}return value;
}

/** @brief Match an exact action child of this provider. */
static int action_index(window_entry_t *entry, const char *path)
{
    char expected[160];int i;
    for(i=0;i<TIKU_TRK_WINDOW_ACTIONS;i++) {
        snprintf(expected, sizeof expected, "%s/actions/%s", entry->base,
                 actions[i]);
        if(!strcmp(path,expected))return i;
    }
    return -1;
}

/** @brief Validate an exact pair of bounded integer members. */
static int argument_pair(const tiku_msg_t *value, const char *first,
                         const char *second, int minimum, int maximum, int *a,
                         int *b)
{
    tiku_msg_t *left = trk_value_find(value, first),
               *right = trk_value_find(value, second);
    int64_t x = 0, y = 0;
    const char *lt = tiku_msg_find_string(left, "type", 0),
               *rt = tiku_msg_find_string(right, "type", 0);
    int valid = tiku_msg_count(value, "member") == 2 && lt && rt &&
                !strcmp(lt, "integer") && !strcmp(rt, "integer") &&
                tiku_msg_find_int64(left, "value", 0, &x) &&
                tiku_msg_find_int64(right, "value", 0, &y) && x >= minimum &&
                x <= maximum && y >= minimum && y <= maximum;
    tiku_msg_free(left);
    tiku_msg_free(right);
    if (valid) {
        *a = (int)x;
        *b = (int)y;
    }
    return valid;
}

/** @brief Revalidate lifetime, native capabilities and complete command
 * arguments.
 */
static tiku_resource_error_t window_validate(void *opaque, const char *path,
                                             const tiku_msg_t *value,
                                             int invoke)
{
    window_entry_t *entry = opaque;
    tiku_trk_window_source_t source = {0};
    int index = action_index(entry, path), a, b;
    const char *type=tiku_msg_find_string(value,"type",0);
    if(!entry_live(entry))return TIKU_RESOURCE_OWNER_LOST;
    if(!invoke || index<0)return TIKU_RESOURCE_UNSUPPORTED;
    if (entry->owner->source(entry->owner->context, entry->window, &source))
        return TIKU_RESOURCE_OWNER_LOST;
    if(!(source.actions&(1u<<index)))return TIKU_RESOURCE_UNSUPPORTED;
    if (tiku_workspace_tracking(entry->owner->workspace))
        return TIKU_RESOURCE_BUSY;
    if(!type || strcmp(type,"object"))return TIKU_RESOURCE_VALUE;
    if (index == TIKU_TRK_WINDOW_MOVE)
        return argument_pair(value, "x", "y", -32768, 32768, &a, &b)
                   ? 0
                   : TIKU_RESOURCE_VALUE;
    if(index==TIKU_TRK_WINDOW_RESIZE) {
        if (!argument_pair(value, "width", "height", 90, 8192, &a, &b) ||
            a < 180)
            return TIKU_RESOURCE_VALUE;
    } else if(tiku_msg_count(value,"member"))return TIKU_RESOURCE_VALUE;
    return TIKU_RESOURCE_OK;
}

/** @brief Dispatch one validated operation; success confirms dispatch, not
 * app consent.
 */
static tiku_resource_error_t window_start(void *opaque, const char *job,
                                          const char *path,
                                          const tiku_msg_t *value, int invoke,
                                          tiku_resource_snapshot_t *out)
{
    window_entry_t *entry = opaque;
    tiku_resource_error_t error = window_validate(opaque, path, value, invoke);
    int index=action_index(entry,path),a=0,b=0;tiku_msg_t *result;(void)job;
    if(error)return error;
    result=trk_value_container("object");
    if (TEXT(result, "window", entry->id) ||
        TEXT(result, "action", actions[index]) ||
        counter(result, "generation", entry->generation) ||
        BOOL(result, "dispatched", 1)) {
        tiku_msg_free(result);return TIKU_RESOURCE_NO_MEMORY;
    }
    if (trk_value_snapshot(out, result, "request_dispatched"))
        return TIKU_RESOURCE_NO_MEMORY;
    if (index == TIKU_TRK_WINDOW_MOVE)
        argument_pair(value, "x", "y", -32768, 32768, &a, &b);
    if (index == TIKU_TRK_WINDOW_RESIZE)
        argument_pair(value, "width", "height", 90, 8192, &a, &b);
    error = entry->owner->perform(entry->owner->context, entry->window,
                                  (tiku_trk_window_action_t)index, a, b);
    if(error)tiku_resource_snapshot_clear(out);
    return error;
}

/** @brief Retire a window incarnation and its queued jobs before forgetting
 * its slot.
 */
static void entry_close(window_entry_t *entry)
{
    if (entry->window)
        tiku_resource_unregister(entry->owner->registry, &entry->stamp);
    memset(entry,0,sizeof *entry);
}

/** @brief Register all observations and action descriptors for a native
 * window.
 */
static int entry_open(tiku_trk_window_resources_t *owner,
                      window_entry_t *entry, tiku_window_t *window)
{
    tiku_resource_initial_t initial[WINDOW_FIELDS+TIKU_TRK_WINDOW_ACTIONS]={0};
    char paths[WINDOW_FIELDS + TIKU_TRK_WINDOW_ACTIONS][160], key[64];
    int i, error = 0;
    tiku_resource_async_controller_t controller = {.validate = window_validate,
                                                   .start = window_start};
    entry->owner = owner;
    entry->window = window;
    entry->generation = window->generation;
    if (owner->source(owner->context, window, &entry->source) ||
        tiku_resource_new_id(entry->id))
        goto fail;
    snprintf(entry->base, sizeof entry->base, "/windows/%s", entry->id);
    snprintf(key, sizeof key, "window-%s", entry->id);
    for(i=0;i<WINDOW_FIELDS+TIKU_TRK_WINDOW_ACTIONS;i++) {
        int action=i>=WINDOW_FIELDS;
        snprintf(paths[i], sizeof paths[i], action ? "%s/actions/%s" : "%s/%s",
                 entry->base, action ? actions[i - WINDOW_FIELDS] : fields[i]);
        initial[i].spec = (tiku_resource_spec_t){
            .path = paths[i],
            .kind = action ? TIKU_RESOURCE_ACTION : TIKU_RESOURCE_STATE,
            .schema_version = 1,
            .schema = {.type = TIKU_RESOURCE_OBJECT},
            .read_scopes = TIKU_RESOURCE_READ,
            .change_scopes =
                action ? TIKU_RESOURCE_INVOKE | TIKU_RESOURCE_CROSS_APP : 0,
            .sensitive = 1,
            .persistence = "none",
            .controller = "window-manager"};
        if (trk_value_snapshot(&initial[i].initial,
                               action ? trk_value_container("object")
                                      : window_value(entry, i),
                               "observed")) {
            error = -1;
            break;
        }
    }
    if (!error)
        error = tiku_resource_register_async(
            owner->registry, key, entry->base, NULL, initial,
            WINDOW_FIELDS + TIKU_TRK_WINDOW_ACTIONS, &controller, entry,
            &entry->stamp);
    for (i = 0; i < WINDOW_FIELDS + TIKU_TRK_WINDOW_ACTIONS; i++)
        tiku_resource_snapshot_clear(&initial[i].initial);
    if(!error)return 0;
fail:memset(entry,0,sizeof *entry);return -1;
}

/** @brief Copy a current workspace field, including its stacking-order window
 * identities.
 */
static tiku_msg_t *workspace_value(tiku_trk_window_resources_t *owner,int field)
{
    tiku_msg_t *value=trk_value_container("object");int bad=0,i,j;
    if(field==0)bad|=BOOL(value,"active",1);
    else if(field==1){
        int registered = 0;
        for (i = 0; i < TIKU_WINDOW_MAX; i++)
            if (owner->entries[i].window)
                registered++;
        bad |= NUMBER(value, "registered_windows", registered);
        bad |= BOOL(value, "complete",
                    registered == tiku_workspace_count(owner->workspace));
        bad |= NUMBER(value, "width", owner->width);
        bad |= NUMBER(value, "height", owner->height);
        bad |= NUMBER(value, "index", 0);
        bad |= NUMBER(value, "workspace_count", 1);
        bad |= BOOL(value, "switch_supported", 0);
    } else if (field == 2) {
        bad |= TEXT(value, "id", owner->id);
        bad |= TEXT(value, "session",
                    tiku_resource_registry_session(owner->registry));
    } else
        for (i = 0; i < tiku_workspace_count(owner->workspace); i++) {
            tiku_window_t *window = tiku_workspace_at(owner->workspace, i);
            char key[24];
            for (j = 0; j < TIKU_WINDOW_MAX; j++)
                if (owner->entries[j].window == window &&
                    entry_live(&owner->entries[j]))
                    break;
            if (j == TIKU_WINDOW_MAX) {
                tiku_msg_free(value);
                return NULL;
            }
            snprintf(key, sizeof key, "%d", i);
            bad |= TEXT(value, key, owner->entries[j].id);
        }
    if(bad){tiku_msg_free(value);return NULL;}return value;
}

/** @brief Retain the last complete observation when capture or publication
 * fails.
 */
static void publish_value(tiku_trk_window_resources_t *owner, const char *path,
                          tiku_msg_t *value)
{
    tiku_resource_snapshot_t next={0};
    if (trk_value_snapshot(&next, value, "observed") ||
        trk_value_publish(owner->registry, path, &next)) {
        tiku_resource_caller_t caller = {.authenticated = 1, .scopes = 63};
        tiku_resource_snapshot_t previous = {0};
        strcpy(caller.principal,"00000000000000000000000000000001");
        if(!tiku_resource_read(owner->registry,&caller,path,&previous)) {
            strcpy(previous.apply_state, "last_good_unavailable");
            trk_value_publish(owner->registry, path, &previous);
        }
        tiku_resource_snapshot_clear(&previous);
    }
    tiku_resource_snapshot_clear(&next);
}

void tiku_trk_window_resources_poll(tiku_trk_window_resources_t *owner)
{
    int i, j;
    char path[160];
    static const char *const names[] = {"active", "arrangement", "identity",
                                        "windows"};
    if(!owner)return;
    for (i = 0; i < TIKU_WINDOW_MAX; i++)
        if (owner->entries[i].window && !entry_live(&owner->entries[i]))
            entry_close(&owner->entries[i]);
    for(i=0;i<tiku_workspace_count(owner->workspace);i++) {
        tiku_window_t *window=tiku_workspace_at(owner->workspace,i);
        for(j=0;j<TIKU_WINDOW_MAX;j++)if(owner->entries[j].window==window)break;
        if(j<TIKU_WINDOW_MAX)continue;
        for(j=0;j<TIKU_WINDOW_MAX;j++)if(!owner->entries[j].window)break;
        if(j<TIKU_WINDOW_MAX)entry_open(owner,&owner->entries[j],window);
    }
    for(i=0;i<TIKU_WINDOW_MAX;i++)if(owner->entries[i].window) {
            window_entry_t *entry = &owner->entries[i];
            tiku_trk_window_source_t source = {0};
            if (!owner->source(owner->context, entry->window, &source))
                entry->source = source;
            for (j = 0; j < WINDOW_FIELDS; j++) {
                snprintf(path, sizeof path, "%s/%s", entry->base, fields[j]);
                publish_value(owner, path, window_value(entry, j));
            }
    }
    for(i=0;i<4;i++) {
        snprintf(path,sizeof path,"%s/%s",owner->base,names[i]);
        publish_value(owner,path,workspace_value(owner,i));
    }
}

tiku_trk_window_resources_t *tiku_trk_window_resources_open(
    tiku_resource_registry_t *registry, tiku_workspace_t *workspace, int width,
    int height,
    int (*source)(void *, tiku_window_t *, tiku_trk_window_source_t *),
    tiku_resource_error_t (*perform)(void *, tiku_window_t *,
                                     tiku_trk_window_action_t, int, int),
    void *context)
{
    tiku_trk_window_resources_t *owner;
    tiku_resource_initial_t initial[4] = {0};
    char paths[4][160];
    int i, error = 0;
    static const char *const names[] = {"active", "arrangement", "identity",
                                        "windows"};
    if(!registry||!workspace||!source||!perform||width<1||height<1)return NULL;
    owner=calloc(1,sizeof *owner);if(!owner)return NULL;
    owner->registry = registry;
    owner->workspace = workspace;
    owner->width = width;
    owner->height = height;
    owner->source=source;owner->perform=perform;owner->context=context;
    if(tiku_resource_new_id(owner->id)){free(owner);return NULL;}
    snprintf(owner->base,sizeof owner->base,"/workspaces/%s",owner->id);
    for(i=0;i<4;i++) {
        snprintf(paths[i],sizeof paths[i],"%s/%s",owner->base,names[i]);
        initial[i].spec =
            (tiku_resource_spec_t){.path = paths[i],
                                   .kind = TIKU_RESOURCE_STATE,
                                   .schema_version = 1,
                                   .schema = {.type = TIKU_RESOURCE_OBJECT},
                                   .read_scopes = TIKU_RESOURCE_READ,
                                   .sensitive = 1,
                                   .persistence = "none",
                                   .controller = "workspace-manager"};
        if (trk_value_snapshot(&initial[i].initial,
                               trk_value_container("object"), "loading"))
            error = -1;
    }
    if (!error)
        error = tiku_resource_register(registry, "workspace-manager",
                                       "/workspaces", NULL, initial, 4, NULL,
                                       NULL, &owner->stamp);
    for(i=0;i<4;i++)tiku_resource_snapshot_clear(&initial[i].initial);
    if (error) {
        free(owner);
        return NULL;
    }
    tiku_trk_window_resources_poll(owner);
    return owner;
}

void tiku_trk_window_resources_close(tiku_trk_window_resources_t *owner)
{
    int i;if(!owner)return;
    for (i = 0; i < TIKU_WINDOW_MAX; i++)
        if (owner->entries[i].window)
            entry_close(&owner->entries[i]);
    tiku_resource_unregister(owner->registry,&owner->stamp);free(owner);
}

tiku_window_t *
tiku_trk_window_resources_find(tiku_trk_window_resources_t *owner,
                               const char *id)
{
    if(!owner||!id)return NULL;
    for (int i = 0; i < TIKU_WINDOW_MAX; i++)
        if (owner->entries[i].window && !strcmp(owner->entries[i].id, id) &&
            entry_live(&owner->entries[i]))
            return owner->entries[i].window;
    return NULL;
}
int tiku_trk_window_resources_id(tiku_trk_window_resources_t *owner,
                                 tiku_window_t *window, char *out,
                                 size_t capacity)
{
    if(!owner||!window||!out||capacity<33)return -1;
    for (int i = 0; i < TIKU_WINDOW_MAX; i++)
        if (owner->entries[i].window == window &&
            entry_live(&owner->entries[i])) {
            strcpy(out, owner->entries[i].id);
            return 0;
        }
    return -1;
}
