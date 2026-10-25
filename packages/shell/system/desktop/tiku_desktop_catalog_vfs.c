/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_desktop_catalog_vfs.c - loaded catalog observations and native launches.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_desktop_lifecycle_vfs_private.h"
#include <unistd.h>

int desktop_lifecycle_catalog_count(void *opaque,const char *table)
{
    struct desktop_lifecycle_vfs *owner=opaque;
    return !strcmp(table,"manifests")||!strcmp(table,"capabilities")?owner->app->apps.count:owner->app->types.n;
}

tiku_msg_t *desktop_lifecycle_catalog_capture(void *opaque,const char *table,int index)
{
    struct desktop_lifecycle_vfs *owner=opaque;desktop_t *app=owner->app;tiku_msg_t *out=trk_value_container("object");int bad=0;
    if(!strcmp(table,"manifests")||!strcmp(table,"capabilities")) {
        const tiku_trk_app_t *item=&app->apps.app[index];
        bad|=LIFE_TEXT(out,"id",item->id);bad|=LIFE_TEXT(out,"name",item->name);
        if(!strcmp(table,"manifests")) {
            tiku_msg_t *types=trk_value_container("array");
            bad|=LIFE_TEXT(out,"module",item->module);bad|=LIFE_TEXT(out,"icon",item->icon);bad|=LIFE_TEXT(out,"category",item->category);
            bad|=LIFE_TEXT(out,"startup",item->startup);bad|=LIFE_TEXT(out,"help",item->help);bad|=LIFE_BOOL(out,"native_desktop",item->is_tracker);
            for(int i=0;i<item->ntype;i++)bad|=trk_value_item(types,tiku_resource_value_string(TIKU_RESOURCE_TEXT,item->type[i]));
            bad|=trk_value_member(out,"types",types);bad|=LIFE_BOOL(out,"takes_anything",item->takes_anything);
        } else {
            bad|=LIFE_BOOL(out,"launch",item->id[0]&&!item->is_tracker);bad|=LIFE_BOOL(out,"open",!item->is_tracker&&(item->ntype||item->takes_anything));
            bad|=LIFE_BOOL(out,"force_quit",0);bad|=LIFE_TEXT(out,"launch_completion","child_exit_report");
        }
    } else {
        const tiku_trk_typerec_t *type=&app->types.rec[index];
        bad|=LIFE_TEXT(out,"mime",type->type);
        if(!strcmp(table,"types")){bad|=LIFE_TEXT(out,"description",type->desc);bad|=LIFE_TEXT(out,"icon",type->icon);}
        else {
            char path[TIKU_RESOURCE_PATH_MAX];tiku_resource_caller_t caller={.authenticated=1,.scopes=63};tiku_resource_snapshot_t value={0};
            strcpy(caller.principal,"00000000000000000000000000000001");
            if(tiku_trk_catalog_settings_path("/settings/associations",type->type,"preferred",path) || tiku_resource_read(owner->registry,&caller,path,&value)) {
                bad|=LIFE_BOOL(out,"available",0);
            } else {
                bad|=LIFE_BOOL(out,"available",1);bad|=trk_value_member(out,"desired",tiku_msg_copy(value.desired));
                bad|=trk_value_member(out,"persisted",tiku_msg_copy(value.persisted));bad|=trk_value_member(out,"applied",tiku_msg_copy(value.applied));
                bad|=LIFE_TEXT(out,"apply_state",value.apply_state);
            }
            tiku_resource_snapshot_clear(&value);
        }
    }
    if(bad){tiku_msg_free(out);return NULL;}return out;
}

tiku_resource_error_t desktop_lifecycle_catalog_run(void *opaque,const tiku_trk_app_t *item,const char *path,tiku_msg_t **out)
{
    struct desktop_lifecycle_vfs *owner=opaque;desktop_t *app=owner->app;tiku_msg_t *result=trk_value_container("object");int accepted;
    if(LIFE_TEXT(result,"app_id",item->id)||LIFE_TEXT(result,"stage","launch_requested")){tiku_msg_free(result);return TIKU_RESOURCE_NO_MEMORY;}
    if(path){const char *paths[]={path};accepted=tiku_trk_launch_controller_run(&app->launcher,item,paths,1);}
    else accepted=tiku_trk_launch_run(item,NULL,0)==TIKU_TRK_LAUNCH_OK;
    if(!accepted){tiku_msg_free(result);return TIKU_RESOURCE_APPLY_FAILED;}
    *out=result;desktop_damage_all(app);return 0;
}
