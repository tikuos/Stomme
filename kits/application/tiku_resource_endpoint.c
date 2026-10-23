/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_resource_endpoint.c - private Unix listener and same-user connections.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
#include "tiku_resource_endpoint.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

struct tiku_resource_endpoint {
    int fd, directory_fd, created;
    pthread_t thread;
    dev_t device;ino_t inode;
    char directory[sizeof ((struct sockaddr_un *)0)->sun_path];
};

/** @brief Open and verify one private directory without following its final link. */
static int private_directory(const char *directory)
{
    int fd;struct stat state;
    if(!directory || directory[0]!='/' || strlen(directory)>=sizeof ((struct sockaddr_un *)0)->sun_path-14)return -1;
    fd=open(directory,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    if(fd<0)return -1;
    if(fstat(fd,&state) || state.st_uid!=geteuid() || (state.st_mode&0777)!=0700){close(fd);errno=EACCES;return -1;}
    return fd;
}

/** @brief Mark a local socket nonblocking and close-on-exec. */
static int socket_flags(int fd)
{
    int flags=fcntl(fd,F_GETFL);
    return flags<0 || fcntl(fd,F_SETFL,flags|O_NONBLOCK)<0 || fcntl(fd,F_SETFD,FD_CLOEXEC)<0 ? -1:0;
}

tiku_resource_endpoint_t *tiku_resource_endpoint_open(const char *directory)
{
    tiku_resource_endpoint_t *endpoint;struct sockaddr_un address={0};struct stat state;
    int created=0,dirfd,fd;
    if(!directory || directory[0]!='/' || strlen(directory)>=sizeof address.sun_path-14)return NULL;
    if(!mkdir(directory,0700))created=1;else if(errno!=EEXIST)return NULL;
    dirfd=private_directory(directory);if(dirfd<0)return NULL;
    if(!fstatat(dirfd,"control.sock",&state,AT_SYMLINK_NOFOLLOW) || errno!=ENOENT){close(dirfd);return NULL;}
    fd=socket(AF_UNIX,SOCK_STREAM,0);if(fd<0 || socket_flags(fd)){if(fd>=0)close(fd);close(dirfd);return NULL;}
    address.sun_family=AF_UNIX;snprintf(address.sun_path,sizeof address.sun_path,"%s/control.sock",directory);
    if(bind(fd,(struct sockaddr *)&address,sizeof address)){close(fd);close(dirfd);if(created)rmdir(directory);return NULL;}
    if(fchmodat(dirfd,"control.sock",0600,0) || listen(fd,8) || fstatat(dirfd,"control.sock",&state,AT_SYMLINK_NOFOLLOW)){
        close(fd);unlinkat(dirfd,"control.sock",0);close(dirfd);if(created)rmdir(directory);return NULL;
    }
    endpoint=calloc(1,sizeof *endpoint);
    if(!endpoint){close(fd);unlinkat(dirfd,"control.sock",0);close(dirfd);return NULL;}
    endpoint->fd=fd;endpoint->directory_fd=dirfd;endpoint->created=created;endpoint->thread=pthread_self();
    endpoint->device=state.st_dev;endpoint->inode=state.st_ino;strcpy(endpoint->directory,directory);return endpoint;
}

int tiku_resource_endpoint_accept(tiku_resource_endpoint_t *endpoint)
{
    int fd;if(!endpoint || !pthread_equal(endpoint->thread,pthread_self()))return -1;
    fd=accept(endpoint->fd,NULL,NULL);if(fd>=0 && socket_flags(fd)){close(fd);return -1;}return fd;
}

void tiku_resource_endpoint_close(tiku_resource_endpoint_t *endpoint)
{
    struct stat state;
    if(!endpoint || !pthread_equal(endpoint->thread,pthread_self()))return;
    close(endpoint->fd);
    if(!fstatat(endpoint->directory_fd,"control.sock",&state,AT_SYMLINK_NOFOLLOW) && state.st_dev==endpoint->device && state.st_ino==endpoint->inode)
        unlinkat(endpoint->directory_fd,"control.sock",0);
    close(endpoint->directory_fd);if(endpoint->created)rmdir(endpoint->directory);free(endpoint);
}

tiku_resource_channel_t *tiku_resource_endpoint_connect(const char *directory,int timeout)
{
    struct sockaddr_un address={0};struct stat state;int directory_fd,fd,error;socklen_t size=sizeof error;
    struct pollfd ready; tiku_resource_channel_t *channel=NULL;
    if(timeout<1 || timeout>30000)return NULL;
    directory_fd=private_directory(directory);if(directory_fd<0)return NULL;
    if(fstatat(directory_fd,"control.sock",&state,AT_SYMLINK_NOFOLLOW) || !S_ISSOCK(state.st_mode) ||
        state.st_uid!=geteuid() || (state.st_mode&0777)!=0600){close(directory_fd);return NULL;}
    fd=socket(AF_UNIX,SOCK_STREAM,0);if(fd<0 || socket_flags(fd)){if(fd>=0)close(fd);close(directory_fd);return NULL;}
    address.sun_family=AF_UNIX;snprintf(address.sun_path,sizeof address.sun_path,"%s/control.sock",directory);
    if(connect(fd,(struct sockaddr *)&address,sizeof address) && errno!=EINPROGRESS)goto done;
    ready=(struct pollfd){.fd=fd,.events=POLLOUT};
    if(poll(&ready,1,timeout)<=0 || getsockopt(fd,SOL_SOCKET,SO_ERROR,&error,&size) || error)goto done;
#ifdef __linux__
    {struct ucred peer;socklen_t length=sizeof peer;
     if(getsockopt(fd,SOL_SOCKET,SO_PEERCRED,&peer,&length) || length!=sizeof peer || peer.uid!=geteuid())goto done;}
#elif defined(__APPLE__)
    {uid_t user;gid_t group;if(getpeereid(fd,&user,&group) || user!=geteuid())goto done;}
#else
    goto done;
#endif
    channel=tiku_resource_channel_client(fd);
done:
    close(fd);close(directory_fd);return channel;
}

int tiku_resource_endpoint_ready(int fd,unsigned maximum)
{
    unsigned char header[4];int bytes;size_t size;struct pollfd ready={.fd=fd,.events=POLLIN};
    if(fd<0 || poll(&ready,1,0)<0 || (ready.revents&(POLLERR|POLLNVAL)))return -1;
    if(ioctl(fd,FIONREAD,&bytes))return -1;
    if(bytes<4)return (ready.revents&POLLHUP)?-1:0;
    if(recv(fd,header,4,MSG_PEEK|MSG_DONTWAIT)!=4)return -1;
    size=(size_t)header[0]<<24|(size_t)header[1]<<16|(size_t)header[2]<<8|header[3];
    if(size<12 || size>maximum)return -1;
    return (size_t)bytes>=size+4?1:(ready.revents&POLLHUP)?-1:0;
}
