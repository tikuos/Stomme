/*
 * KintsugiOS v0.01 kits -- the foundation KintsugiDesktop and KintsugiTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 *
 * tiku_sys_linux.c - the machine underneath, as Linux describes it.
 *
 * The Linux definitions of tiku_sys.h; mk/kits.mk compiles this file or
 * tiku_sys_darwin.c.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define _GNU_SOURCE 1           /* REG_RIP and the rest live behind it */

#include "tiku_sys.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <spawn.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <ucontext.h>
#include <unistd.h>

int
tiku_sys_stat_same(const struct stat *a, const struct stat *b)
{
    return a && b && a->st_dev == b->st_dev && a->st_ino == b->st_ino &&
        a->st_size == b->st_size && a->st_mode == b->st_mode &&
        a->st_mtim.tv_sec == b->st_mtim.tv_sec &&
        a->st_mtim.tv_nsec == b->st_mtim.tv_nsec &&
        a->st_ctim.tv_sec == b->st_ctim.tv_sec &&
        a->st_ctim.tv_nsec == b->st_ctim.tv_nsec;
}

/* The UTF-8 locales tried for reading text, in order: C.UTF-8 orders
 * characters by code point, and en_US.UTF-8 stands in where the host has
 * no C.UTF-8. */
static const char *const utf8_locales[] = { "C.UTF-8", "en_US.UTF-8" };

void *
tiku_sys_locale_utf8(void)
{
    locale_t locale = (locale_t)0;
    size_t i;

    for (i = 0; locale == (locale_t)0 &&
                i < sizeof utf8_locales / sizeof utf8_locales[0]; i++) {
        locale = newlocale(LC_CTYPE_MASK | LC_COLLATE_MASK, utf8_locales[i],
                           (locale_t)0);
    }
    return locale;
}

void *
tiku_sys_locale_use(void *locale)
{
    return uselocale((locale_t)locale);
}

void
tiku_sys_locale_free(void *locale)
{
    if (locale != NULL) {
        freelocale((locale_t)locale);
    }
}

int tiku_sys_sync_file(int fd)
{
    return fsync(fd);
}

void *
tiku_sys_fault_pc(const void *context)
{
    const ucontext_t *uc = context;

    if (uc == NULL) {
        return NULL;
    }
#if defined(__x86_64__)
    return (void *)(uintptr_t)uc->uc_mcontext.gregs[REG_RIP];
#elif defined(__i386__)
    return (void *)(uintptr_t)uc->uc_mcontext.gregs[REG_EIP];
#elif defined(__aarch64__)
    return (void *)(uintptr_t)uc->uc_mcontext.pc;
#elif defined(__arm__)
    return (void *)(uintptr_t)uc->uc_mcontext.arm_pc;
#else
    /* Another architecture: no program counter. */
    return NULL;
#endif
}

int
tiku_sys_processes(void (*fn)(const tiku_sys_process_t *process, void *ctx),
                   void *ctx)
{
    DIR *directory = opendir("/proc");
    struct dirent *entry;
    uid_t me = getuid();
    int offered = 0;

    if (directory == NULL || fn == NULL) {
        if (directory != NULL) {
            (void)closedir(directory);
        }
        return -1;
    }
    while ((entry = readdir(directory)) != NULL) {
        char path[280], name[64], command[160];
        tiku_sys_process_t process;
        struct stat owner;
        size_t length;
        FILE *file;

        if (entry->d_name[0] < '1' || entry->d_name[0] > '9') {
            continue;
        }
        if (snprintf(path, sizeof path, "/proc/%s", entry->d_name) < 0 ||
            stat(path, &owner) != 0 || owner.st_uid != me) {
            continue;
        }
        /* An empty command line is a kernel thread's. */
        if (snprintf(path, sizeof path, "/proc/%s/cmdline",
                     entry->d_name) < 0) {
            continue;
        }
        file = fopen(path, "r");
        if (file == NULL) {
            continue;
        }
        length = fread(command, 1u, sizeof command - 1u, file);
        (void)fclose(file);
        if (length == 0u) {
            continue;
        }
        /* The executable's path, from /proc/<pid>/exe; a process that
         * has exited since the directory was read is skipped. */
        if (snprintf(path, sizeof path, "/proc/%s/exe", entry->d_name) < 0) {
            continue;
        }
        {
            ssize_t got = readlink(path, command, sizeof command - 1u);

            if (got <= 0) {
                continue;
            }
            command[got] = '\0';
        }
        if (snprintf(path, sizeof path, "/proc/%s/comm",
                     entry->d_name) < 0) {
            continue;
        }
        file = fopen(path, "r");
        if (file == NULL) {
            continue;
        }
        if (fgets(name, sizeof name, file) == NULL) {
            (void)fclose(file);
            continue;
        }
        (void)fclose(file);
        length = strlen(name);
        while (length > 0u &&
               (name[length - 1u] == '\n' || name[length - 1u] == ' ')) {
            name[--length] = '\0';
        }
        if (name[0] == '\0') {
            continue;
        }
        process.pid = atol(entry->d_name);
        process.name = name;
        process.command = command;
        fn(&process, ctx);
        offered++;
    }
    (void)closedir(directory);
    return offered;
}

void
tiku_sys_memory(long *total_kb, long *avail_kb)
{
    FILE *f = fopen("/proc/meminfo", "r");
    char line[128];

    *total_kb = 0;
    *avail_kb = 0;
    while (f != NULL && fgets(line, sizeof line, f) != NULL) {
        if (strncmp(line, "MemTotal:", 9) == 0) {
            (void)sscanf(line + 9, "%ld", total_kb);
        } else if (strncmp(line, "MemAvailable:", 13) == 0) {
            (void)sscanf(line + 13, "%ld", avail_kb);
        }
    }
    if (f != NULL) {
        (void)fclose(f);
    }
}

long
tiku_sys_uptime(void)
{
    FILE *f = fopen("/proc/uptime", "r");
    double up = 0.0;

    if (f != NULL) {
        if (fscanf(f, "%lf", &up) != 1) {
            up = 0.0;
        }
        (void)fclose(f);
    }
    return (long)up;
}

int
tiku_sys_self_path(char *out, size_t max)
{
    ssize_t n = readlink("/proc/self/exe", out, max - 1u);

    if (n <= 0) {
        out[0] = '\0';
        return -1;
    }
    out[n] = '\0';
    return 0;
}

/**
 * @brief Copy a frame offset, "+" and an optional "0x" then hex digits up to
 *        ")", into @p out.
 * @return 1, or 0 for any other text or one that does not fit.
 */
static int
frame_offset(const char *text, char *out, size_t max)
{
    size_t length = strcspn(text, ")"), start = 1;

    if (text[0] != '+' || text[length] != ')' || length >= max) {
        return 0;
    }
    if (text[1] == '0' && (text[2] == 'x' || text[2] == 'X')) {
        start = 3;
    }
    if (length <= start || length - start > sizeof(uintptr_t) * 2) {
        return 0;
    }
    for (size_t i = start; i < length; i++) {
        if (strchr("0123456789abcdefABCDEF", text[i]) == NULL) {
            return 0;
        }
    }
    memcpy(out, text, length);
    out[length] = '\0';
    return 1;
}

/** @brief Read addr2line output with the image and offset as arguments. */
static int
frame_symbol(char *image, char *offset, char *line, size_t max)
{
    int fd[2], status, result = 0;
    pid_t child, waited;
    FILE *stream;
    posix_spawn_file_actions_t actions;
    char *argv[] = {"addr2line", "-e", image, "-f", "-C", "-p",
                    offset, NULL};

    if (pipe2(fd, O_CLOEXEC) != 0) {
        return 0;
    }
    /* Keep pipe descriptors separate from the child's standard streams. */
    for (int i = 0; i < 2; i++) {
        if (fd[i] <= STDERR_FILENO) {
            int duplicate = fcntl(fd[i], F_DUPFD_CLOEXEC, 3);

            if (duplicate < 0) {
                goto close_pipe;
            }
            close(fd[i]);
            fd[i] = duplicate;
        }
    }
    if (posix_spawn_file_actions_init(&actions) != 0) {
        goto close_pipe;
    }
    status = posix_spawn_file_actions_addopen(&actions, STDIN_FILENO,
                                              "/dev/null", O_RDONLY, 0);
    if (!status) {
        status = posix_spawn_file_actions_addopen(&actions, STDERR_FILENO,
                                                  "/dev/null", O_WRONLY, 0);
    }
    if (!status) {
        status = posix_spawn_file_actions_adddup2(&actions, fd[1],
                                                  STDOUT_FILENO);
    }
    if (!status) {
        status = posix_spawn_file_actions_addclose(&actions, fd[0]);
    }
    if (!status) {
        status = posix_spawn_file_actions_addclose(&actions, fd[1]);
    }
    if (!status) {
        status = posix_spawnp(&child, argv[0], &actions, NULL, argv, environ);
    }
    posix_spawn_file_actions_destroy(&actions);
    if (status != 0) {
        goto close_pipe;
    }
    close(fd[1]);
    stream = fdopen(fd[0], "r");
    if (stream != NULL) {
        result = fgets(line, (int)max, stream) != NULL;
        fclose(stream);
    } else {
        close(fd[0]);
    }
    do {
        waited = waitpid(child, &status, 0);
    } while (waited < 0 && errno == EINTR);
    return result && waited == child && WIFEXITED(status) &&
           WEXITSTATUS(status) == 0;

close_pipe:
    close(fd[0]);
    close(fd[1]);
    return 0;
}

/** @brief Match an offset frame to the current executable's native basename. */
static int frame_is_self(const char *leaf, const char *paren, char *image,
                         size_t capacity)
{
    if (paren <= leaf || tiku_sys_self_path(image, capacity))
        return 0;
    const char *name = strrchr(image, '/');
    name = name ? name + 1 : image;
    size_t length = (size_t)(paren - leaf);
    return length == strlen(name) && !memcmp(leaf, name, length);
}

void
tiku_sys_frame_name(const char *raw, char *out, size_t max)
{
    const char *paren = strchr(raw, '(');
    const char *self = strrchr(raw, '/');
    char leaf[64] = "", image[512];

    self = (self != NULL) ? self + 1 : raw;
    (void)sscanf(self, "%63[^(]", leaf);
    /* Offset lookup uses the running executable's image. */
    if (paren != NULL && paren[1] == '+' &&
        frame_is_self(self, paren, image, sizeof image)) {
        char offset[2 * sizeof(uintptr_t) + 4], line[160];

        if (frame_offset(paren + 1, offset, sizeof offset) &&
            frame_symbol(image, offset, line, sizeof line)) {
            char *at = strstr(line, " at ");

            if (at != NULL) {
                *at = '\0';
            }
            line[strcspn(line, "\n")] = '\0';
            if (strncmp(line, "??", 2) == 0) {
                /* addr2line does not know the offset: the report is an
                 * earlier build's. */
                snprintf(out, max, "an earlier build's %.*s",
                         (int)strcspn(paren + 1, ")"), paren + 1);
            } else {
                snprintf(out, max, "%.119s", line);
            }
            return;
        }
    }
    if (paren != NULL && paren[1] != '+' && paren[1] != ')') {
        snprintf(out, max, "%s: %.*s", leaf, (int)strcspn(paren + 1, "+)"),
                 paren + 1);
    } else if (paren != NULL) {
        snprintf(out, max, "%s %.*s", leaf, (int)strcspn(paren + 1, ")"),
                 paren + 1);
    } else {
        snprintf(out, max, "%.100s", raw);
    }
}

int
tiku_sys_process_pin(int fd, long *pid)
{
#if defined(SO_PEERPIDFD)
    struct ucred cred;
    socklen_t size = sizeof cred;
    int handle = -1;

    if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cred, &size) != 0) {
        return -1;
    }
    /* Refuse init, this process, and another user's process. */
    if (size != sizeof cred || cred.uid != geteuid() ||
        cred.pid <= 1 || cred.pid == getpid()) {
        errno = EPERM;
        return -1;
    }
    size = sizeof handle;
    if (getsockopt(fd, SOL_SOCKET, SO_PEERPIDFD, &handle, &size) != 0) {
        return -1;
    }
    if (fcntl(handle, F_SETFD, FD_CLOEXEC) < 0) {
        int keep = errno;

        (void)close(handle);
        errno = keep;
        return -1;
    }
    if (pid != NULL) {
        *pid = (long)cred.pid;
    }
    return handle;
#else
    /* Built without SO_PEERPIDFD: nothing is pinned. */
    (void)fd;
    (void)pid;
    errno = ENOTSUP;
    return -1;
#endif
}

int
tiku_sys_process_kill(int handle)
{
#if defined(SYS_pidfd_send_signal)
    return (int)syscall(SYS_pidfd_send_signal, handle, SIGKILL, NULL, 0);
#else
    (void)handle;
    errno = ENOTSUP;
    return -1;
#endif
}

int
tiku_sys_reserve(int fd, long long bytes)
{
    /* A mapping over a sparse tmpfs file can be made and then raise
     * SIGBUS on the first write; posix_fallocate() refuses up front. */
    return posix_fallocate(fd, 0, (off_t)bytes);
}

int
tiku_sys_serial_speed(int fd, int baud)
{
    /* Every rate the tree uses has a termios constant here: there is
     * nothing more to try. */
    (void)fd;
    (void)baud;
    return -1;
}

int
tiku_sys_serial_ports(char out[][256], int max)
{
    /* udev's stable names carry the probe or board: a J-Link, a CDC
     * device, an FT232 bridge, a Pico, or a board whose own USB port
     * carries its console under the KintsugiOS name. */
    static const char *dir = "/dev/serial/by-id";
    DIR *d = opendir(dir);
    struct dirent *e;
    int n = 0;

    if (d == NULL) {
        return -1;
    }
    while (n < max && (e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') {
            continue;
        }
        if (strstr(e->d_name, "J-Link") == NULL &&
            strstr(e->d_name, "CDC")    == NULL &&
            strstr(e->d_name, "FT232")  == NULL &&
            strstr(e->d_name, "TikuOS") == NULL &&
            strstr(e->d_name, "Pico")   == NULL) {
            continue;
        }
        /* A path too long for out[] is skipped, not cut. */
        if (strlen(dir) + strlen(e->d_name) + 2u > 256u) {
            continue;
        }
        (void)snprintf(out[n], 256, "%s/%s", dir, e->d_name);
        n++;
    }
    (void)closedir(d);
    return n;
}

const char *
tiku_sys_uri_launcher(void)
{
    return "xdg-open";
}
