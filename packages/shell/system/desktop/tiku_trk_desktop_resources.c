/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_desktop_resources.c - copied live desktop, Deskbar and session observations.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_trk_desktop_private.h"
#include "tiku_trk_resource_values.h"
#include "tiku_state_update.h"
#include "tiku_trk_native_record.h"
#include <unistd.h>

#define INTEGER(object, key, value) trk_value_member(object, key, tiku_resource_value_integer(TIKU_RESOURCE_INTEGER, value))
#define BOOLEAN(object, key, value) trk_value_member(object, key, tiku_resource_value_integer(TIKU_RESOURCE_BOOLEAN, (value) != 0))
#define TEXT(object, key, value) trk_value_member(object, key, tiku_resource_value_string(TIKU_RESOURCE_TEXT, value))

static const tiku_trk_observation_t desktop_observations[] = {
    {"/desktop/backdrop",0,1}, {"/desktop/icon-layout",1,1}, {"/desktop/selection",1,1}, {"/desktop/menus",1,1}
};
static const tiku_trk_observation_t deskbar_observations[] = {
    {"/deskbar/clock",0,0}, {"/deskbar/dock",0,0}, {"/deskbar/frame",0,0},
    {"/deskbar/replicants",1,1}, {"/deskbar/teams",1,1}, {"/deskbar/tray",1,1}
};
static const tiku_trk_observation_t session_observations[] = {
    {"/session/identity",0,1}, {"/session/restore-policy",0,1}, {"/session/startup",1,1},
    {"/session/state",0,0}, {"/session/windows",1,1}
};

/** @brief Copy native rectangle fields without window addresses. */
static tiku_msg_t *frame_value(tiku_rect_t frame)
{
    tiku_msg_t *out = trk_value_container("object");
    if (INTEGER(out,"x",frame.x) || INTEGER(out,"y",frame.y) || INTEGER(out,"width",frame.w) || INTEGER(out,"height",frame.h)) {
        tiku_msg_free(out); return NULL;
    }
    return out;
}

/** @brief Copy a window's live identity and geometry without exporting its callbacks. */
static tiku_msg_t *window_value(tiku_window_t *window, int focused)
{
    tiku_msg_t *out = frame_value(window->frame); char generation[32];
    snprintf(generation,sizeof generation,"%llu",(unsigned long long)window->generation);
    if (TEXT(out,"generation",generation) || TEXT(out,"title",window->title) || INTEGER(out,"z",window->z) ||
        BOOLEAN(out,"minimized",window->minimized) || BOOLEAN(out,"focused",focused) || BOOLEAN(out,"resizable",window->resizable)) {
        tiku_msg_free(out); return NULL;
    }
    return out;
}

/** @brief Walk a bounded menu tree and return one owned row, or NULL off the end. */
static tiku_msg_t *menu_row(const tiku_menu_t *menu, int wanted, int *cursor, int depth, const char *scope)
{
    int i;
    if (!menu || depth >= TIKU_MENU_DEPTH_MAX) return NULL;
    for (i = 0; i < menu->count; i++) {
        const tiku_menu_item_t *entry = &menu->item[i]; tiku_msg_t *out;
        if ((*cursor)++ == wanted) {
            out = trk_value_container("object");
            if (TEXT(out,"scope",scope) || TEXT(out,"menu",menu->title) || TEXT(out,"label",entry->label) ||
                INTEGER(out,"depth",depth) || INTEGER(out,"row",i) || INTEGER(out,"command",entry->command) ||
                INTEGER(out,"shortcut",(unsigned char)entry->shortcut) || INTEGER(out,"modifiers",entry->mods) ||
                BOOLEAN(out,"enabled",entry->enabled) || BOOLEAN(out,"marked",entry->marked) || BOOLEAN(out,"separator",entry->separator)) {
                tiku_msg_free(out); return NULL;
            }
            return out;
        }
        out = menu_row(entry->submenu,wanted,cursor,depth+1,scope); if (out) return out;
    }
    return NULL;
}

/** @brief Copy a shell menu or one focused application's published menu row. */
static tiku_msg_t *menus_value(desktop_t *app, int wanted, int *count)
{
    const tiku_menu_t *roots[] = {&app->menus.file,&app->menus.window,&app->menus.attributes,app->popup};
    const char *scopes[] = {"folder-file","folder-window","folder-attributes","popup"};
    int cursor = 0, i, j; tiku_window_t *focus = tiku_workspace_focused(app->desk);
    for (i = 0; i < 4; i++) { tiku_msg_t *row = menu_row(roots[i],wanted,&cursor,0,scopes[i]); if (row) return row; }
    if (focus && focus->has_menus) for (i = 0; i < focus->menus.nmenu; i++) {
        const tiku_menu_list_t *menu = &focus->menus.menu[i];
        for (j = 0; j < menu->nitem; j++) if (cursor++ == wanted) {
            const tiku_menu_entry_t *entry = &menu->item[j]; tiku_msg_t *out = trk_value_container("object");
            if (TEXT(out,"scope","focused-window") || TEXT(out,"menu",menu->title) || TEXT(out,"label",entry->label) ||
                INTEGER(out,"depth",entry->level) || INTEGER(out,"row",j) || INTEGER(out,"command",entry->command) ||
                INTEGER(out,"shortcut",(unsigned char)entry->sc) || INTEGER(out,"modifiers",entry->mods) ||
                BOOLEAN(out,"enabled",entry->enabled) || BOOLEAN(out,"marked",entry->marked) || BOOLEAN(out,"separator",entry->separator)) {
                tiku_msg_free(out); return NULL;
            }
            return out;
        }
    }
    if (count) *count = cursor;
    return NULL;
}

/** @brief Count complete stream rows from the current native owners. */
static int observed_count(void *opaque, const char *path)
{
    desktop_t *app = opaque;
    tiku_trk_folder_window_t *folder = tiku_trk_desktop_view_folder(&app->desktop_view);
    if (!strcmp(path,"/session/startup")) return desktop_startup_count(app)+1;
    if (!strcmp(path,"/deskbar/tray")) return tiku_trk_deskbar_trayicons(app->deskbar,-1,NULL);
    if (!strcmp(path,"/deskbar/replicants")) {
        int count = 0; tiku_trk_replicant_t value;
        while (!tiku_trk_deskbar_replicant_at(app->deskbar,count,&value)) count++;
        return count;
    }
    if (!strcmp(path,"/desktop/menus")) { int count = 0; menus_value(app,-1,&count); return count; }
    if (!strcmp(path,"/session/windows")) return tiku_workspace_count(app->desk);
    if (!strcmp(path,"/deskbar/teams")) return tiku_trk_deskbar_team_count(app->deskbar);
    if (!folder || !folder->view) return 0;
    if (!strcmp(path,"/desktop/selection")) {
        int i, count = 0;
        for (i = 0; i < tiku_trk_poseview_total_count(folder->view); i++) if (tiku_trk_poseview_entry(folder->view,i)->selected) count++;
        return count;
    }
    return tiku_trk_poseview_total_count(folder->view);
}

/** @brief Copy one icon's public model and unscaled arrangement. */
static tiku_msg_t *pose_value(const tiku_trk_pose_t *pose)
{
    tiku_msg_t *out = trk_value_container("object"); char identity[32];
    snprintf(identity,sizeof identity,"%llu",(unsigned long long)pose->model.node_id);
    if (TEXT(out,"name",pose->model.name) || TEXT(out,"path",pose->model.path) || TEXT(out,"node_id",identity) ||
        INTEGER(out,"x",pose->loc_x) || INTEGER(out,"y",pose->loc_y) || BOOLEAN(out,"placed",pose->placed) ||
        BOOLEAN(out,"selected",pose->selected) || BOOLEAN(out,"invisible",pose->invisible) || BOOLEAN(out,"broken",pose->broken)) {
        tiku_msg_free(out); return NULL;
    }
    return out;
}

/** @brief Copy a running application's published window state and process identifiers. */
static tiku_msg_t *team_value(desktop_t *app, int row)
{
    const char *name = tiku_trk_deskbar_team_name(app->deskbar,row);
    int team = name ? tiku_trk_deskbar_find_team(app->deskbar,name) : -1, active = 0, i, count, bad = 0;
    const tiku_trk_dbwin_t *windows = NULL; long pids[16];
    tiku_msg_t *out = trk_value_container("object"), *items = trk_value_container("array"), *processes = trk_value_container("array");
    if (team < 0) { tiku_msg_free(out); tiku_msg_free(items); tiku_msg_free(processes); return NULL; }
    count = tiku_trk_deskbar_team_windows(app->deskbar,team,&windows,&active);
    bad |= TEXT(out,"name",name); bad |= INTEGER(out,"row",row); bad |= BOOLEAN(out,"active",active);
    bad |= BOOLEAN(out,"expanded",tiku_trk_deskbar_team_expanded(app->deskbar,team));
    bad |= TEXT(out,"icon",tiku_trk_deskbar_team_icon(app->deskbar,team));
    for (i = 0; i < count; i++) {
        tiku_msg_t *item = trk_value_container("object");
        bad |= TEXT(item,"title",windows[i].title); bad |= INTEGER(item,"state",windows[i].state); bad |= BOOLEAN(item,"current",windows[i].current);
        bad |= trk_value_item(items,item);
    }
    count = tiku_trk_deskbar_team_pids(app->deskbar,team,pids,16);
    for (i = 0; i < count; i++) bad |= trk_value_item(processes,tiku_resource_value_integer(TIKU_RESOURCE_INTEGER,pids[i]));
    bad |= trk_value_member(out,"windows",items); bad |= trk_value_member(out,"pids",processes);
    if (bad) { tiku_msg_free(out); return NULL; } return out;
}

/** @brief Copy tray entries through the Deskbar's native inspection functions. */
static tiku_msg_t *tray_value(desktop_t *app, int replicants, int index)
{
    int bad = 0; tiku_msg_t *out = trk_value_container("object");
    if (replicants) {
        tiku_trk_replicant_t value; char identity[32];
        if (tiku_trk_deskbar_replicant_at(app->deskbar,index,&value)) { tiku_msg_free(out); return NULL; }
        snprintf(identity,sizeof identity,"%llu",(unsigned long long)value.node_id);
        bad |= INTEGER(out,"id",value.id); bad |= TEXT(out,"node_id",identity); bad |= TEXT(out,"label",value.label);
        bad |= TEXT(out,"path",value.path); bad |= TEXT(out,"value",value.value); bad |= BOOLEAN(out,"valid",value.valid);
        bad |= INTEGER(out,"height",value.height); bad |= BOOLEAN(out,"visible",tiku_trk_deskbar_show_replicants(app->deskbar));
    } else {
        tiku_trk_trayicon_t value = {0}; char owner[32];
        if (index < 0 || index >= tiku_trk_deskbar_trayicons(app->deskbar,-1,NULL)) { tiku_msg_free(out); return NULL; }
        tiku_trk_deskbar_trayicons(app->deskbar,index,&value); snprintf(owner,sizeof owner,"%llu",(unsigned long long)value.owner);
        bad |= TEXT(out,"owner",owner); bad |= TEXT(out,"id",value.id); bad |= TEXT(out,"icon",value.icon);
        bad |= TEXT(out,"text",value.text); bad |= TEXT(out,"tip",value.tip);
    }
    if (bad) { tiku_msg_free(out); return NULL; } return out;
}

/** @brief Capture a live owner value without filesystem reads or mutation. */
static tiku_msg_t *observed_value(void *opaque, const char *path, int index)
{
    desktop_t *app = opaque; int bad = 0;
    tiku_trk_folder_window_t *folder = tiku_trk_desktop_view_folder(&app->desktop_view);
    tiku_msg_t *out;
    if (!strcmp(path,"/desktop/menus")) return menus_value(app,index,NULL);
    if (!strcmp(path,"/session/windows")) {
        tiku_window_t *window = tiku_workspace_at(app->desk,index);
        return window ? window_value(window,window == tiku_workspace_focused(app->desk)) : NULL;
    }
    if (!strcmp(path,"/deskbar/teams")) return team_value(app,index);
    if (!strcmp(path,"/desktop/icon-layout") || !strcmp(path,"/desktop/selection")) {
        int i, selected = !strcmp(path,"/desktop/selection");
        if (!folder || !folder->view) return NULL;
        for (i = 0; i < tiku_trk_poseview_total_count(folder->view); i++) {
            const tiku_trk_pose_t *pose = tiku_trk_poseview_entry(folder->view,i);
            if ((!selected || pose->selected) && index-- == 0) return pose_value(pose);
        }
        return NULL;
    }
    if (!strcmp(path,"/session/startup")) return desktop_startup_observe(app,index);
    if (!strcmp(path,"/session/restore-policy")) return desktop_startup_observe(app,-1);
    if (!strcmp(path,"/deskbar/replicants")) return tray_value(app,1,index);
    if (!strcmp(path,"/deskbar/tray")) return tray_value(app,0,index);
    if (!strcmp(path,"/deskbar/frame")) return frame_value(tiku_trk_deskbar_frame(app->deskbar));
    out = trk_value_container("object");
    if (!strcmp(path,"/desktop/backdrop")) {
        bad |= BOOLEAN(out,"drawn",folder && folder->bg_drawn);
        bad |= TEXT(out,"path",folder && folder->bg_drawn ? folder->bg_shown : "");
        bad |= INTEGER(out,"width",folder && folder->bg_drawn ? folder->bg_image.w : 0);
        bad |= INTEGER(out,"height",folder && folder->bg_drawn ? folder->bg_image.h : 0);
        bad |= INTEGER(out,"color",TIKU_C_BACKDROP);
        bad |= INTEGER(out,"icon_size",folder && folder->view ? tiku_trk_poseview_icon_size(folder->view) : 0);
    } else if (!strcmp(path,"/deskbar/clock")) {
        char stamp[256]; tiku_trk_deskbar_clock_stamp(app->deskbar,stamp,sizeof stamp);
        bad |= TEXT(out,"stamp",stamp); bad |= BOOLEAN(out,"visible",tiku_trk_deskbar_clock_visible(app->deskbar));
        bad |= BOOLEAN(out,"calendar_open",tiku_trk_deskbar_calendar_open(app->deskbar));
    } else if (!strcmp(path,"/deskbar/dock")) {
        bad |= INTEGER(out,"dock",tiku_trk_deskbar_dock(app->deskbar)); bad |= INTEGER(out,"width",tiku_trk_deskbar_width(app->deskbar));
        bad |= BOOLEAN(out,"vertical",tiku_trk_deskbar_vertical(tiku_trk_deskbar_dock(app->deskbar)));
    } else if (!strcmp(path,"/session/identity")) {
        tiku_resource_registry_t *registry = tiku_trk_resource_session_registry(app->resource_session);
        bad |= TEXT(out,"incarnation",tiku_resource_registry_session(registry)); bad |= TEXT(out,"product",TIKU_SHELL_DESKTOP_NAME);
        bad |= INTEGER(out,"uid",geteuid()); bad |= INTEGER(out,"pid",getpid());
    } else if (!strcmp(path,"/session/state")) {
        bad |= BOOLEAN(out,"running",app->running); bad |= INTEGER(out,"open_windows",tiku_workspace_count(app->desk));
        bad |= INTEGER(out,"folder_slots",app->folder_count); bad |= BOOLEAN(out,"persistent",tiku_store_persistent(app->store));
        bad |= BOOLEAN(out,"safe_mode_at_boot",app->resource_safe_mode);
    } else bad = 1;
    if (bad) { tiku_msg_free(out); return NULL; } return out;
}

/** @brief Register native cache keys through their existing state-store owner. */
static int cached_record(const char *node, const char *attribute, const void *bytes, size_t length, void *context)
{
    desktop_t *app = context; (void)bytes; (void)length;
    const char *const names[] = {TIKU_ATTR_COLUMNS,NULL,TIKU_ATTR_DESK_COLUMNS,TIKU_ATTR_DESK_VIEW_STATE,NULL,TIKU_ATTR_DESKBAR_PREFS_FRAME,
        TIKU_ATTR_DISKS_COLUMNS,TIKU_ATTR_DISKS_VIEW_STATE,TIKU_ATTR_DOCK,TIKU_ATTR_FRAME,TIKU_ATTR_ORIGINAL,TIKU_ATTR_POSE_INFO,
        TIKU_ATTR_REPLICANTS,TIKU_ATTR_SESSION,NULL,TIKU_ATTR_VIEW_STATE};
    int i;
    for (i = 0; i < TIKU_TRK_NATIVE_COUNT; i++) if (names[i] && !strcmp(attribute,names[i]))
        tiku_trk_native_settings_add(app->native_settings,i,app->store,node);
    return 0;
}

/** @brief Add loaded folders and poses without querying the filesystem. */
static void native_view_records(desktop_t *app, tiku_trk_folder_window_t *folder, int desktop)
{
    tiku_store_t *store; int i;
    if (!folder || !folder->view || !app->backend || !app->backend->ops->state_store) return;
    store = app->backend->ops->state_store(app->backend);
    if (!store) return;
    if (desktop) {
        const char *node = app->desktop_prefs_window.node;
        tiku_trk_native_settings_add(app->native_settings,TIKU_TRK_NATIVE_DESK_COLUMNS,store,node);
        tiku_trk_native_settings_add(app->native_settings,TIKU_TRK_NATIVE_DESK_VIEW,store,node);
        tiku_trk_native_settings_add(app->native_settings,TIKU_TRK_NATIVE_DISKS_COLUMNS,store,node);
        tiku_trk_native_settings_add(app->native_settings,TIKU_TRK_NATIVE_DISKS_VIEW,store,node);
    } else if (!folder->qname[0]) {
        tiku_trk_native_settings_add(app->native_settings,TIKU_TRK_NATIVE_FRAME,app->store,folder->path);
        if (strcmp(folder->path,"/")) {
            tiku_trk_native_settings_add(app->native_settings,TIKU_TRK_NATIVE_COLUMNS,store,folder->path);
            tiku_trk_native_settings_add(app->native_settings,TIKU_TRK_NATIVE_VIEW,store,folder->path);
        }
    }
    for (i = 0; i < tiku_trk_poseview_total_count(folder->view); i++) {
        const tiku_trk_pose_t *pose = tiku_trk_poseview_entry(folder->view,i);
        tiku_trk_native_settings_add(app->native_settings,TIKU_TRK_NATIVE_POSE,store,pose->model.path);
        tiku_trk_native_settings_add(app->native_settings,TIKU_TRK_NATIVE_ORIGINAL,store,pose->model.path);
    }
}

/** @brief Read one already validated native integer for a live owner. */
static int native_integer(const tiku_msg_t *value, const char *key)
{
    tiku_msg_t *member = trk_value_find(value,key); int64_t number = 0;
    tiku_msg_find_int64(member,"value",0,&number); tiku_msg_free(member); return (int)number;
}

/** @brief Apply committed records through loaded view and session owners without I/O. */
static void native_record_saved(void *context, tiku_trk_native_family_t family, const char *node, const tiku_msg_t *value)
{
    desktop_t *app = context; unsigned char bytes[8192]; size_t length; int i;
    static const char *const attributes[] = {TIKU_ATTR_COLUMNS,NULL,TIKU_ATTR_DESK_COLUMNS,TIKU_ATTR_DESK_VIEW_STATE,NULL,NULL,
        TIKU_ATTR_DISKS_COLUMNS,TIKU_ATTR_DISKS_VIEW_STATE,NULL,NULL,NULL,TIKU_ATTR_POSE_INFO,NULL,NULL,NULL,TIKU_ATTR_VIEW_STATE};
    if (family == TIKU_TRK_NATIVE_SESSION) app->session.preserve_restore = 1;
    else if (family == TIKU_TRK_NATIVE_DOCK) {
        int present = tiku_msg_count(value,"member") != 0;
        tiku_trk_deskbar_set_dock(app->deskbar,present ? native_integer(value,"dock") : tiku_trk_deskbar_dock(NULL));
        tiku_trk_deskbar_set_width(app->deskbar,present ? native_integer(value,"width") : tiku_trk_deskbar_width(NULL));
    } else if (family == TIKU_TRK_NATIVE_DESKBAR_FRAME && tiku_msg_count(value,"member") && app->deskbar_prefs_window.window) {
        tiku_window_t *window = app->deskbar_prefs_window.window;
        window->frame = tiku_trk_deskbar_prefs_frame(1,native_integer(value,"x"),native_integer(value,"y"),DESK_W,DESK_H,window->frame.w,window->frame.h);
    } else if (family == TIKU_TRK_NATIVE_FRAME && tiku_msg_count(value,"member")) {
        tiku_trk_frame_t frame = {native_integer(value,"x"),native_integer(value,"y"),native_integer(value,"width"),native_integer(value,"height")};
        tiku_trk_frame_clamp(&frame,DESK_W,DESK_H);
        for (i = 0; i < app->folder_count; i++) if (app->folders[i].win && app->folders[i].win->open && !strcmp(app->folders[i].state_path,node)) {
            app->folders[i].win->frame = (tiku_rect_t){frame.x,frame.y,frame.w,frame.h}; app->folders[i].opened_with = frame; app->folders[i].had_frame = 1;
            tiku_trk_folder_window_forget(&app->folders[i]);
        }
    } else if (attributes[family]) {
        tiku_trk_native_record_kind_t kind = family == TIKU_TRK_NATIVE_POSE ? TIKU_TRK_RECORD_POSE :
            family == TIKU_TRK_NATIVE_COLUMNS || family == TIKU_TRK_NATIVE_DESK_COLUMNS || family == TIKU_TRK_NATIVE_DISKS_COLUMNS ? TIKU_TRK_RECORD_COLUMNS : TIKU_TRK_RECORD_VIEW;
        if (!tiku_trk_native_record_encode(kind,value,bytes,sizeof bytes,&length)) {
            tiku_trk_folder_window_t *desktop = tiku_trk_desktop_view_folder(&app->desktop_view);
            if (desktop && tiku_trk_poseview_apply_record(desktop->view,attributes[family],node,bytes,length)) tiku_trk_folder_window_forget(desktop);
            for (i = 0; i < app->folder_count; i++) if (app->folders[i].win && app->folders[i].win->open &&
                tiku_trk_poseview_apply_record(app->folders[i].view,attributes[family],node,bytes,length)) tiku_trk_folder_window_forget(&app->folders[i]);
        }
    }
    desktop_damage_all(app);
}

/** @brief Copy the existing session owner's complete current folder list. */
static tiku_msg_t *native_session_capture(void *context)
{
    desktop_t *app = context; tiku_trk_session_entry_t entries[TIKU_TRK_SESSION_MAX];
    int i, count = tiku_trk_session_controller_entries(&app->session,entries,TIKU_TRK_SESSION_MAX);
    tiku_msg_t *out = trk_value_container("array");
    if (count < 0) { tiku_msg_free(out); return NULL; }
    for (i = 0; i < count; i++) {
        tiku_msg_t *item = trk_value_container("object");
        if (TEXT(item,"path",entries[i].path) || BOOLEAN(item,"minimized",entries[i].minimized)) { tiku_msg_free(item); tiku_msg_free(out); return NULL; }
        if (trk_value_item(out,item)) { tiku_msg_free(out); return NULL; }
    }
    return out;
}

/** @brief Discover owned native contexts and let their copied record controller advance. */
static void native_resources_poll(desktop_t *app, uint64_t now)
{
    int i;
    if (!app->native_settings) return;
    if (now < app->resource_next_native_scan) { tiku_trk_native_settings_poll(app->native_settings,now); return; }
    app->resource_next_native_scan = now + 500;
    native_view_records(app,tiku_trk_desktop_view_folder(&app->desktop_view),1);
    for (i = 0; i < app->folder_count; i++) if (app->folders[i].win && app->folders[i].win->open) native_view_records(app,&app->folders[i],0);
    tiku_state_cached_each(app->store,cached_record,app);
    tiku_trk_native_settings_poll(app->native_settings,now);
}

/** @brief Copy the current owner incarnation and native operation availability. */
static int window_source(void *opaque,tiku_window_t *window,tiku_trk_window_source_t *source)
{
    desktop_t *app=opaque; tiku_remote_window_t *remote=tiku_remote_window_of(&app->remote,window);
    struct embed_win *embedded=embed_win_of_window(app,window); int i,j;
    memset(source,0,sizeof *source);
    if (remote) {
        if (!remote->session || !remote->session->used || remote->session->fd<0 || !remote->opened || remote->reap==window) return -1;
        strcpy(source->kind,"application");snprintf(source->name,sizeof source->name,"%s",remote->session->name);
        source->connection=remote->session->generation;source->generation=remote->generation;source->local_id=remote->win_id;
    } else if (embedded) {
        strcpy(source->kind,"embedded");snprintf(source->name,sizeof source->name,"%s",embedded->slot->descriptor->name);
        source->generation=window->generation;source->local_id=embedded->id;
    } else {
        for(i=0;i<TIKU_REMOTE_SESSIONS;i++)for(j=0;j<TIKU_REMOTE_WINDOWS;j++)
            if(app->remote.session[i].win[j].reap==window)return -1;
        strcpy(source->kind,"desktop");snprintf(source->name,sizeof source->name,"%s",TIKU_SHELL_DESKTOP_NAME);
        source->generation=window->generation;
    }
    if (!window->hidden) source->actions=1u<<TIKU_TRK_WINDOW_FOCUS;
    if (!window->plain) {
        source->actions|=(1u<<TIKU_TRK_WINDOW_MOVE)|(1u<<TIKU_TRK_WINDOW_CLOSE)|(1u<<TIKU_TRK_WINDOW_MINIMIZE)|(1u<<TIKU_TRK_WINDOW_RESTORE);
        if(window->resizable)source->actions|=1u<<TIKU_TRK_WINDOW_RESIZE;
        if(window->zoomable && (remote || embedded || folder_of(app,window)))source->actions|=1u<<TIKU_TRK_WINDOW_MAXIMIZE;
    }
    return 0;
}

int desktop_resources_open(desktop_t *app)
{
    tiku_resource_registry_t *registry = tiku_trk_resource_session_registry(app->resource_session); int i;
    static const char *const keys[] = {"session-identity","session-restore","session-startup","session-state","session-windows"};
    if (!registry) return 0;
    app->native_settings = tiku_trk_native_settings_open(registry,app->record_settings);
    if (!app->native_settings) return -1;
    tiku_trk_native_settings_bind_saved(app->native_settings,native_record_saved,app);
    tiku_trk_native_settings_bind_capture(app->native_settings,native_session_capture,app);
    tiku_trk_native_settings_add(app->native_settings,TIKU_TRK_NATIVE_DESKBAR_FRAME,app->store,TIKU_KEY_SESSION);
    tiku_trk_native_settings_add(app->native_settings,TIKU_TRK_NATIVE_DOCK,app->store,TIKU_KEY_SESSION);
    tiku_trk_native_settings_add(app->native_settings,TIKU_TRK_NATIVE_REPLICANTS,app->store,TIKU_KEY_SESSION);
    tiku_trk_native_settings_add(app->native_settings,TIKU_TRK_NATIVE_SESSION,app->store,TIKU_KEY_SESSION);
    native_resources_poll(app,1);
    {
        static const char *const desktop_keys[]={"desktop","desktop-layout","desktop-selection","desktop-menus"};
        for(i=0;i<4;i++)app->shell_observers[i]=tiku_trk_resource_observer_open(registry,desktop_keys[i],desktop_observations[i].path,
            &desktop_observations[i],1,observed_count,observed_value,app);
    }
    app->shell_observers[4] = tiku_trk_resource_observer_open(registry,"deskbar","/deskbar",deskbar_observations,
        sizeof deskbar_observations / sizeof deskbar_observations[0],observed_count,observed_value,app);
    for (i = 0; i < 5; i++) app->shell_observers[i+5] = tiku_trk_resource_observer_open(registry,keys[i],session_observations[i].path,
        &session_observations[i],1,observed_count,observed_value,app);
    for (i = 0; i < 10; i++) if (!app->shell_observers[i]) { desktop_resources_close(app); return -1; }
    app->window_resources=tiku_trk_window_resources_open(registry,app->desk,app->surf->w,app->surf->h,window_source,desktop_window_resource_perform,app);
    if (!app->window_resources || desktop_lifecycle_vfs_open(app)) { desktop_resources_close(app); return -1; }
    return 0;
}

void desktop_resources_poll(desktop_t *app, uint64_t now)
{
    int i; tiku_trk_folder_window_t *folder = tiku_trk_desktop_view_folder(&app->desktop_view);
    if (folder && folder->view) tiku_trk_desktop_settings_feedback(app->desktop_settings,folder->bg_drawn ? &folder->bg_applied : NULL,tiku_trk_poseview_icon_size(folder->view));
    native_resources_poll(app,now);
    for (i = 0; i < 10; i++) tiku_trk_resource_observer_poll(app->shell_observers[i],now);
}

void desktop_resources_close(desktop_t *app)
{
    int i;
    desktop_lifecycle_vfs_close(app);
    tiku_trk_window_resources_close(app->window_resources); app->window_resources=NULL;
    tiku_trk_native_settings_close(app->native_settings); app->native_settings = NULL;
    for (i = 0; i < 10; i++) { tiku_trk_resource_observer_close(app->shell_observers[i]); app->shell_observers[i] = NULL; }
}
