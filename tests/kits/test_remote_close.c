/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_remote_close.c - application close negotiation without input authority.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_remote.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#define CHECK(t) do {if(!(t)){fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#t);exit(1);}}while(0)
int main(void)
{
    int pair[2];uint32_t header[2],id;tiku_event_t event;
    tiku_window_t window={.open=1};tiku_remote_listener_t *listener=calloc(1,sizeof *listener);
    unsigned char payload[4+sizeof event];CHECK(listener&&!socketpair(AF_UNIX,SOCK_STREAM,0,pair));
    listener->fd=-1;listener->session[0].used=1;listener->session[0].fd=pair[0];
    listener->session[0].win[0].session=&listener->session[0];
    listener->session[0].win[0].window=&window;listener->session[0].win[0].win_id=7;
    CHECK(!tiku_remote_request_close(listener,&window));
    listener->session[0].win[0].opened=1;
    CHECK(tiku_remote_request_close(listener,&window));
    CHECK(read(pair[1],header,sizeof header)==sizeof header&&header[0]==TIKU_RMSG_EVENT&&header[1]==sizeof payload);
    CHECK(read(pair[1],payload,sizeof payload)==sizeof payload);
    memcpy(&id,payload,4);memcpy(&event,payload+4,sizeof event);
    CHECK(id==7&&event.type==TIKU_EVENT_KEY_DOWN&&event.key=='w'&&(event.modifiers&TIKU_MOD_CMD));
    CHECK(window.open&&listener->session[0].win[0].window==&window);
    CHECK(listener->session[0].win[0].link_gesture==0);
    CHECK(tiku_remote_request_quit(listener,&window));
    CHECK(read(pair[1],header,sizeof header)==sizeof header&&header[0]==TIKU_RMSG_EVENT);
    CHECK(read(pair[1],payload,sizeof payload)==sizeof payload);memcpy(&event,payload+4,sizeof event);
    CHECK(event.key=='q'&&(event.modifiers&TIKU_MOD_CMD)&&window.open);
    CHECK(listener->session[0].win[0].link_gesture==0);
    listener->session[0].used=0;CHECK(!tiku_remote_request_close(listener,&window));
    free(listener->session[0].out);close(pair[0]);close(pair[1]);free(listener);
    puts("PASS remote close: queued owner request, retained window and no gesture grant");return 0;
}
