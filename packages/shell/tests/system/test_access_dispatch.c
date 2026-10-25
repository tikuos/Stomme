/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_access_dispatch.c - semantic owner routing and native gesture separation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_trk_desktop_private.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#define CHECK(t) do{if(!(t)){fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#t);exit(1);}}while(0)
static int messages;
/** @brief Capture the exact semantic message without an application transport. */
int fixture_session_tell(tiku_remote_session_t *session,const tiku_msg_t *message)
{
    int64_t document,generation;CHECK(session&&session->used&&tiku_msg_what(message)==TIKU_MSG_ACCESS_ACTION);
    CHECK(tiku_msg_find_int64(message,"document",0,&document)&&document==11&&tiku_msg_find_int64(message,"generation",0,&generation)&&generation==23);messages++;return 1;
}
/** @brief Record the native redraw boundary without opening a display host. */
void desktop_damage_all(desktop_t *app){app->dirty=1;}
int main(void)
{
    desktop_t *app=calloc(1,sizeof *app);CHECK(app);
#if TIKU_SHELL_REMOTE_SERVICES
    int pair[2];CHECK(!socketpair(AF_UNIX,SOCK_STREAM,0,pair));app->surf=tiku_surface_new(100,100,0);app->desk=tiku_workspace_new(app->surf);CHECK(app->desk);
    tiku_window_t *native=tiku_workspace_open(app->desk,"Access fixture",(tiku_rect_t){0,0,80,80},NULL,NULL,NULL);CHECK(native);
    tiku_remote_session_t *session=&app->remote.session[0];session->used=1;session->fd=pair[0];session->generation=5;
    tiku_remote_window_t *window=&session->win[0];window->session=session;window->generation=7;window->window=native;window->win_id=1;window->opened=1;window->focused=1;
    tiku_access_node_t node={.parent=TIKU_ACCESS_NO_PARENT,.role=TIKU_ACCESS_DOCUMENT,.state=TIKU_ACCESS_VISIBLE|TIKU_ACCESS_EDITABLE,.text="fixture",.selection_start=-1,.selection_end=-1,.caret=-1};
    CHECK(!tiku_access_create(11,23,&node,1,&window->access));
    CHECK(!desktop_access_perform(app,window,0,11,22,TIKU_ACCESS_SELECT,0,1,NULL,0)&&!messages&&!window->link_gesture);
    CHECK(desktop_access_perform(app,window,0,11,23,TIKU_ACCESS_SELECT,0,1,NULL,0)&&messages==1&&!window->link_gesture);
    CHECK(desktop_access_perform(app,window,0,11,23,TIKU_ACCESS_SELECT,0,1,NULL,1)&&messages==2&&window->link_gesture);
    session->used=0;CHECK(!desktop_access_perform(app,window,0,11,23,TIKU_ACCESS_SELECT,0,1,NULL,0)&&messages==2);
    tiku_access_release(window->access);tiku_workspace_free(app->desk);tiku_surface_free(app->surf);close(pair[0]);close(pair[1]);
#else
    CHECK(!desktop_access_perform(app,NULL,0,11,23,TIKU_ACCESS_SELECT,0,1,NULL,0)&&!messages);
#endif
    free(app);puts("PASS semantic dispatch: native owner generation, product support and explicit gesture source");return 0;
}

#define tiku_remote_session_tell fixture_session_tell

#include "tiku_trk_desktop_remote.c"
