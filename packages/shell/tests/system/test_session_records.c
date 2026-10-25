/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_session_records.c - explicit next-start records survive shutdown capture.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_trk_session_controller.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(test) do { if (!(test)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#test); exit(1); } } while (0)

/** @brief The fixture contains real folder views rather than query results. */
int tiku_trk_poseview_is_query(const tiku_trk_poseview_t *view) { (void)view; return 0; }
/** @brief View persistence is exercised independently of the session-list boundary. */
int tiku_trk_poseview_save_state(tiku_trk_poseview_t *view) { (void)view; return 0; }
/** @brief No column changes exist in this session fixture. */
int tiku_trk_poseview_save_columns(tiku_trk_poseview_t *view) { (void)view; return 0; }
/** @brief The fixture does not change folder geometry. */
void tiku_trk_folder_lifecycle_save_frame(tiku_trk_folder_lifecycle_t *owner,tiku_trk_folder_window_t *folder) { (void)owner; (void)folder; }
/** @brief Dock observations remain constant while testing the session-list record. */
tiku_trk_db_dock_t tiku_trk_deskbar_dock(const tiku_trk_deskbar_t *bar) { (void)bar; return TIKU_TRK_DB_RIGHT_EXPANDED; }
/** @brief Return the fixture's unchanged Deskbar width. */
int tiku_trk_deskbar_width(const tiku_trk_deskbar_t *bar) { (void)bar; return 180; }
/** @brief Restoration setters are unused by the shutdown fixture. */
void tiku_trk_deskbar_set_dock(tiku_trk_deskbar_t *bar,tiku_trk_db_dock_t dock) { (void)bar; (void)dock; }
/** @brief Restoration width changes are unused by the shutdown fixture. */
int tiku_trk_deskbar_set_width(tiku_trk_deskbar_t *bar,int width) { (void)bar; return width; }
/** @brief No new folder is opened by the shutdown fixture. */
tiku_trk_folder_window_t *tiku_trk_folder_lifecycle_open(tiku_trk_folder_lifecycle_t *owner,const char *path) { (void)owner; (void)path; return NULL; }
/** @brief The fixture's root folder is available to session restoration. */
int tiku_trk_desktop_view_shows_disks(const tiku_trk_desktop_view_t *view) { (void)view; return 1; }

int main(void)
{
    tiku_store_t *store=tiku_store_memory_open(); tiku_window_t window={.open=1,.minimized=1};
    tiku_trk_folder_window_t folder={.win=&window}; int count=1;
    tiku_trk_folder_lifecycle_t folders={.folders=&folder,.count=&count}; tiku_trk_session_controller_t session;
    tiku_trk_session_entry_t entries[2], requested={.path="/next-start",.minimized=0};
    CHECK(store); strcpy(folder.path,"/current");
    tiku_trk_session_controller_init(&session,store,NULL,NULL,&folders);
    CHECK(tiku_trk_session_controller_entries(&session,entries,0)==-1);
    CHECK(tiku_trk_session_controller_entries(&session,entries,2)==1 && !strcmp(entries[0].path,"/current") && entries[0].minimized);
    CHECK(tiku_trk_session_save_entries(store,&requested,1)==1); session.preserve_restore=1;
    tiku_trk_session_controller_save(&session);
    CHECK(tiku_trk_session_load_entries(store,entries,2)==1 && !strcmp(entries[0].path,"/next-start"));
    session.preserve_restore=0; tiku_trk_session_controller_save(&session);
    CHECK(tiku_trk_session_load_entries(store,entries,2)==1 && !strcmp(entries[0].path,"/current") && entries[0].minimized);
    tiku_store_free(store); puts("PASS session records: complete capture, explicit next-start policy and normal shutdown capture"); return 0;
}
