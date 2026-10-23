/*
 * KintsugiDesktop v0.01 Support Kit.
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_uri.h"
#include "tiku_sys.h"
#include <errno.h>
#include <signal.h>
#include <spawn.h>
#include <string.h>
#include <strings.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
/** @brief Decode one ASCII hexadecimal digit. */
static int
hex(unsigned char c)
{
    return c >= '0' && c <= '9'   ? c - '0'
           : c >= 'a' && c <= 'f' ? c - 'a' + 10
           : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                  : -1;
}
/** @brief Check shortest-form UTF-8 excluding controls and surrogate scalars.
 */
static int
utf8(const unsigned char *s, size_t n)
{
    for (size_t i = 0; i < n;) {
        unsigned c = s[i++], extra = 0, min = 0;
        if (c < 128)
            continue;
        if (c >= 0xc2 && c <= 0xdf) {
            extra = 1;
            c &= 31;
            min = 128;
        } else if (c >= 0xe0 && c <= 0xef) {
            extra = 2;
            c &= 15;
            min = 2048;
        } else if (c >= 0xf0 && c <= 0xf4) {
            extra = 3;
            c &= 7;
            min = 65536;
        } else
            return 0;
        if (extra > n - i)
            return 0;
        while (extra--) {
            if ((s[i] & 0xc0) != 0x80)
                return 0;
            c = (c << 6) | (s[i++] & 63);
        }
        if (c < min || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff) ||
            (c >= 0x80 && c <= 0x9f) || c == 0x2028 || c == 0x2029)
            return 0;
    }
    return 1;
}
int
tiku_uri_external(const char *target)
{
    if (!target)
        return 0;
    size_t length = strnlen(target, 2049), offset;
    if (!length || length > 2048 ||
        !utf8((const unsigned char *)target, length))
        return 0;
    for (size_t i = 0; i < length; i++) {
        unsigned char c = target[i];
        if (c <= ' ' || c == 127 || c == '\\')
            return 0;
        if (c == '%') {
            if (length - i < 3 || hex(target[i + 1]) < 0 ||
                hex(target[i + 2]) < 0)
                return 0;
            c = hex(target[i + 1]) * 16 + hex(target[i + 2]);
            if (c < ' ' || c == 127 || c == '\\')
                return 0;
            i += 2;
        }
    }
    if (!strncasecmp(target, "mailto:", 7))
        return length > 7;
    if (!strncasecmp(target, "https://", 8))
        offset = 8;
    else if (!strncasecmp(target, "http://", 7))
        offset = 7;
    else
        return 0;
    return length > offset && target[offset] != '/' && target[offset] != '?' &&
           target[offset] != '#';
}
int
tiku_uri_open(const char *target)
{
    if (!tiku_uri_external(target))
        return -1;
    char *argv[] = {(char *)tiku_sys_uri_launcher(), (char *)target, NULL};
    pid_t child;
    if (posix_spawnp(&child, argv[0], NULL, NULL, argv, environ))
        return -1;
    int status = 0;
    for (unsigned i = 0; i < 500; i++) {
        pid_t done = waitpid(child, &status, WNOHANG);
        if (done == child)
            return WIFEXITED(status) && !WEXITSTATUS(status) ? 0 : -1;
        if (done < 0 && errno != EINTR)
            return -1;
        usleep(10000);
    }
    kill(child, SIGKILL);
    while (waitpid(child, &status, 0) < 0 && errno == EINTR) {
    }
    return -1;
}
