/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_darwin.h - what macOS spells differently.
 *
 * Force-included on Darwin by mk/toolchain.mk and named by no source:
 * it gives Darwin's libc the glibc names the tree uses.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_DARWIN_H
#define TIKU_DARWIN_H

#ifdef __APPLE__

/*
 * Only <stddef.h>: this header comes first in every translation unit,
 * and with <sys/socket.h> here a source's own static send() or accept()
 * would be a redeclaration error.
 */
#include <stddef.h>

/* Darwin keeps the sub-second stamps under the 4.4BSD names; the members
 * are the same three timespecs. */
#ifndef st_atim
#define st_atim  st_atimespec
#define st_mtim  st_mtimespec
#define st_ctim  st_ctimespec
#endif

/* memrchr(), a GNU extension Darwin's libc does not have. */
#ifndef TIKU_HAVE_MEMRCHR
static inline void *
tiku_memrchr(const void *block, int c, size_t n)
{
    const unsigned char *p = (const unsigned char *)block;

    while (n-- > 0u) {
        if (p[n] == (unsigned char)c) {
            return (void *)(p + n);
        }
    }
    return NULL;
}
#define memrchr tiku_memrchr
#endif

/* Darwin's termios speeds are the integers themselves (B115200 is
 * 115200); the two rates the tree uses above 230400, the highest the
 * header names, are defined the same way.  A driver may refuse such a
 * rate, or take it and run at another. */
#ifndef B460800
#define B460800 460800
#endif
#ifndef B921600
#define B921600 921600
#endif

/* Darwin's socket calls take no close-on-exec flag.  With this 0, a
 * descriptor they make stays open across exec unless FD_CLOEXEC is set
 * on it with fcntl(). */
#ifndef SOCK_CLOEXEC
#define SOCK_CLOEXEC 0
#endif

/*
 * Darwin's send() takes no per-call SIGPIPE suppression.  With this 0, a
 * send to a peer that hung up raises SIGPIPE unless the program ignores
 * SIGPIPE or the socket has the SO_NOSIGPIPE option.
 */
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#endif /* __APPLE__ */
#endif /* TIKU_DARWIN_H */
