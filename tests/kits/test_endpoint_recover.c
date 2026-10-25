/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_endpoint_recover.c - stale listener recovery and path preservation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_resource_endpoint.h"
#include <signal.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#define CHECK(t) do{if(!(t)){fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#t);exit(1);}}while(0)
int main(void)
{
    char directory[]="/tmp/stomme-endpoint-XXXXXX",path[128],link[128],ready;int pair[2],status;
    CHECK(mkdtemp(directory)&&!pipe(pair));snprintf(path,sizeof path,"%s/control.sock",directory);snprintf(link,sizeof link,"%s/alias",directory);
    pid_t child=fork();CHECK(child>=0);
    if(!child){close(pair[0]);tiku_resource_endpoint_t *owner=tiku_resource_endpoint_open(directory);CHECK(owner);CHECK(write(pair[1],"1",1)==1);close(pair[1]);pause();_exit(0);}
    close(pair[1]);CHECK(read(pair[0],&ready,1)==1);close(pair[0]);
    CHECK(tiku_resource_endpoint_recover(directory)<0);CHECK(!tiku_resource_endpoint_open(directory));
    CHECK(!kill(child,SIGKILL)&&waitpid(child,&status,0)==child&&WIFSIGNALED(status));
    CHECK(!tiku_resource_endpoint_open(directory));CHECK(!tiku_resource_endpoint_recover(directory));
    tiku_resource_endpoint_t *replacement=tiku_resource_endpoint_open(directory);CHECK(replacement);CHECK(tiku_resource_endpoint_recover(directory)<0);tiku_resource_endpoint_close(replacement);
    int legacy=socket(AF_UNIX,SOCK_STREAM,0);struct sockaddr_un address={.sun_family=AF_UNIX};CHECK(legacy>=0);
    CHECK(strlen(path)<sizeof address.sun_path);memcpy(address.sun_path,path,strlen(path)+1);
    CHECK(!bind(legacy,(struct sockaddr *)&address,sizeof address)&&!chmod(path,0600)&&!listen(legacy,8));
    CHECK(tiku_resource_endpoint_recover(directory)<0);close(legacy);
    CHECK(!chmod(path,0400)&&tiku_resource_endpoint_recover(directory)<0);
    CHECK(!chmod(path,0600)&&!tiku_resource_endpoint_recover(directory));
    FILE *file=fopen(path,"w");CHECK(file&&fputs("keep",file)>=0&&!fclose(file));
    CHECK(tiku_resource_endpoint_recover(directory)<0&&!access(path,F_OK));CHECK(!unlink(path));
    CHECK(!symlink("missing",path));CHECK(tiku_resource_endpoint_recover(directory)<0);struct stat state;CHECK(!lstat(path,&state)&&S_ISLNK(state.st_mode));CHECK(!unlink(path));
    CHECK(!symlink(directory,link));CHECK(tiku_resource_endpoint_recover(link)<0);CHECK(!unlink(link));
    CHECK(!tiku_resource_endpoint_recover(directory)&&!rmdir(directory));CHECK(!tiku_resource_endpoint_recover(directory));
    puts("PASS endpoint recovery: dead listener replaced, live listener and unrelated paths retained");return 0;
}
