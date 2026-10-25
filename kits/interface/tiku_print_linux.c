/*
 * KintsugiDesktop v0.01 Interface Kit.
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 * tiku_print_linux.c - native portal dialogs with owned PDF and cancellation.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_access.h"
#include "tiku_print_native.h"
#include <gio/gio.h>
#include <gio/gunixfdlist.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define PRINT_JOBS 4
/** @brief One isolated portal worker; owner reads results after finished. */
typedef struct {
    pthread_t thread;
    atomic_int cancel, finished;
    GCancellable *stop;
    uint64_t request;
    void *pdf;
    size_t bytes;
    char title[256], parent[128], path[256], reason[256];
    unsigned response;
    int responded, status;
    GVariant *results;
    tiku_print_done_fn done;
    void *context;
} print_job_t;
struct tiku_print_native {
    unsigned refs, closing; /**< owner and active poll lifetime holds */
    char parent[128];       /**< copied native portal parent identifier */
    print_job_t *jobs[PRINT_JOBS]; /**< owner-thread job slots */
    size_t bytes;                  /**< aggregate owned PDF storage */
};
/** @brief Receive the response for the worker's current portal request. */
static void
response(GDBusConnection *bus, const char *sender, const char *path,
         const char *interface, const char *member, GVariant *parameters,
         void *context)
{
    (void)bus;
    (void)sender;
    (void)interface;
    (void)member;
    print_job_t *job = context;
    if (strcmp(path, job->path) || job->responded)
        return;
    g_variant_get(parameters, "(u@a{sv})", &job->response, &job->results);
    job->responded = 1;
}
/** @brief Dispatch a native request and wait on its private context with
 * cancellation. */
static int
request(print_job_t *job, GDBusConnection *bus, GMainContext *context,
        const char *method, GVariant *parameters, GUnixFDList *fds)
{
    GError *error = NULL;
    unsigned subscription = g_dbus_connection_signal_subscribe(
        bus, "org.freedesktop.portal.Desktop", "org.freedesktop.portal.Request",
        "Response", NULL, NULL, G_DBUS_SIGNAL_FLAGS_NONE, response, job, NULL);
    job->responded = 0;
    job->response = 2;
    job->path[0] = 0;
    g_clear_pointer(&job->results, g_variant_unref);
    GVariant *reply = g_dbus_connection_call_with_unix_fd_list_sync(
        bus, "org.freedesktop.portal.Desktop",
        "/org/freedesktop/portal/desktop", "org.freedesktop.portal.Print",
        method, parameters, G_VARIANT_TYPE("(o)"), G_DBUS_CALL_FLAGS_NONE, 5000,
        fds, NULL, job->stop, &error);
    int status = -1;
    if (reply) {
        const char *path;
        g_variant_get(reply, "(&o)", &path);
        if (strlen(path) < sizeof job->path) {
            strcpy(job->path, path);
            gint64 deadline = g_get_monotonic_time() + INT64_C(900000000);
            while (!job->responded && !atomic_load(&job->cancel) &&
                   g_get_monotonic_time() < deadline) {
                for (unsigned i = 0; i < 64 && g_main_context_pending(context);
                     i++)
                    g_main_context_iteration(context, FALSE);
                if (!job->responded)
                    g_usleep(20000);
            }
            if (job->responded)
                status = job->response == 0 ? 1 : job->response == 1 ? 0 : -1;
            else if (atomic_load(&job->cancel))
                status = 0;
            else
                snprintf(job->reason, sizeof job->reason,
                         "Native print dialog timed out.");
        }
        g_variant_unref(reply);
    }
    if (atomic_load(&job->cancel))
        status = 0;
    if (status != 1 && job->path[0]) {
        GVariant *closed = g_dbus_connection_call_sync(
            bus, "org.freedesktop.portal.Desktop", job->path,
            "org.freedesktop.portal.Request", "Close", NULL, NULL,
            G_DBUS_CALL_FLAGS_NONE, 1000, NULL, NULL);
        if (closed)
            g_variant_unref(closed);
    }
    if (error) {
        snprintf(job->reason, sizeof job->reason, "%s", error->message);
        g_error_free(error);
    }
    g_dbus_connection_signal_unsubscribe(bus, subscription);
    job->path[0] = 0;
    return status;
}
/** @brief Create a copied options dictionary for one distinct request phase. */
static GVariant *
options(print_job_t *job, const char *phase, unsigned token)
{
    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE("a{sv}"));
    char handle[64];
    snprintf(handle, sizeof handle, "tiku_%llu_%s",
             (unsigned long long)job->request, phase);
    g_variant_builder_add(&builder, "{sv}", "handle_token",
                          g_variant_new_string(handle));
    g_variant_builder_add(&builder, "{sv}", "modal",
                          g_variant_new_boolean(TRUE));
    if (!strcmp(phase, "prepare")) {
        const char *formats[] = {"pdf", NULL};
        g_variant_builder_add(&builder, "{sv}", "supported_output_file_formats",
                              g_variant_new_strv(formats, -1));
    } else
        g_variant_builder_add(&builder, "{sv}", "token",
                              g_variant_new_uint32(token));
    return g_variant_builder_end(&builder);
}
/** @brief Run the native dialog and submit exactly the copied final PDF via an
 * FD. */
static void *
worker(void *context)
{
    print_job_t *job = context;
    GMainContext *main = g_main_context_new();
    g_main_context_push_thread_default(main);
    char *address =
        g_dbus_address_get_for_bus_sync(G_BUS_TYPE_SESSION, job->stop, NULL);
    GDBusConnection *bus =
        address ? g_dbus_connection_new_for_address_sync(
                      address,
                      G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
                          G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION,
                      NULL, job->stop, NULL)
                : NULL;
    g_free(address);
    job->status = -1;
    if (!bus) {
        snprintf(job->reason, sizeof job->reason,
                 "Native print service unavailable.");
        goto finish;
    }
    GVariantBuilder settings, page;
    g_variant_builder_init(&settings, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_init(&page, G_VARIANT_TYPE("a{sv}"));
    job->status = request(
        job, bus, main, "PreparePrint",
        g_variant_new("(ss@a{sv}@a{sv}@a{sv})", job->parent, job->title,
                      g_variant_builder_end(&settings),
                      g_variant_builder_end(&page), options(job, "prepare", 0)),
        NULL);
    if (job->status == 1) {
        unsigned token = 0;
        if (!job->results ||
            !g_variant_lookup(job->results, "token", "u", &token)) {
            job->status = -1;
            snprintf(job->reason, sizeof job->reason,
                     "Native print service returned no settings token.");
        } else {
            FILE *file = tmpfile();
            if (!file || fwrite(job->pdf, 1, job->bytes, file) != job->bytes ||
                fflush(file) || fseek(file, 0, SEEK_SET)) {
                job->status = -1;
                snprintf(job->reason, sizeof job->reason,
                         "Cannot stage the final PDF for printing.");
            } else {
                GUnixFDList *fds = g_unix_fd_list_new();
                int fd = g_unix_fd_list_append(fds, fileno(file), NULL);
                if (fd < 0) {
                    job->status = -1;
                    snprintf(job->reason, sizeof job->reason,
                             "Native PDF descriptor refused.");
                } else
                    job->status = request(
                        job, bus, main, "Print",
                        g_variant_new("(ssh@a{sv})", job->parent, job->title,
                                      fd, options(job, "print", token)),
                        fds);
                g_object_unref(fds);
            }
            if (file)
                fclose(file);
        }
    }
    g_dbus_connection_close_sync(bus, NULL, NULL);
    g_object_unref(bus);
finish:
    g_clear_pointer(&job->results, g_variant_unref);
    if (atomic_load(&job->cancel))
        job->status = 0;
    if (job->status == 0)
        snprintf(job->reason, sizeof job->reason, "Printing cancelled.");
    else if (job->status == 1)
        snprintf(job->reason, sizeof job->reason,
                 "Native print submission accepted.");
    else if (!job->reason[0])
        snprintf(job->reason, sizeof job->reason,
                 "Native print submission failed.");
    g_main_context_pop_thread_default(main);
    g_main_context_unref(main);
    atomic_store(&job->finished, 1);
    return NULL;
}
tiku_print_native_t *
tiku_print_native_open(const char *parent, void *native_window)
{
    (void)native_window;
    if (parent && strlen(parent) >= 128)
        return NULL;
    tiku_print_native_t *native = calloc(1, sizeof *native);
    if (native) {
        native->refs = 1;
        snprintf(native->parent, sizeof native->parent, "%s",
                 parent ? parent : "");
    }
    return native;
}
/** @brief Join a completed/cancelled worker before releasing its captured
 * storage. */
static void
release(print_job_t *job)
{
    pthread_join(job->thread, NULL);
    g_object_unref(job->stop);
    free(job->pdf);
    free(job);
}
int
tiku_print_native_begin(tiku_print_native_t *native, uint64_t request_id,
                        const void *pdf, size_t bytes, const char *title,
                        tiku_print_done_fn done, void *context)
{
    if (!native || native->closing || !request_id || !pdf || bytes < 5 ||
        bytes > TIKU_PRINT_PDF_MAX || memcmp(pdf, "%PDF-", 5) || !title ||
        strlen(title) > 255 ||
        tiku_access_characters(title, strlen(title)) < 0 || !done ||
        bytes > 256u * 1024u * 1024u - native->bytes)
        return 0;
    unsigned slot = PRINT_JOBS;
    for (unsigned i = 0; i < PRINT_JOBS; i++) {
        if (native->jobs[i] && native->jobs[i]->request == request_id)
            return 0;
        if (!native->jobs[i])
            slot = i;
    }
    if (slot == PRINT_JOBS)
        return 0;
    print_job_t *job = calloc(1, sizeof *job);
    if (!job)
        return 0;
    job->pdf = malloc(bytes);
    job->stop = g_cancellable_new();
    if (!job->pdf) {
        g_object_unref(job->stop);
        free(job);
        return 0;
    }
    memcpy(job->pdf, pdf, bytes);
    job->bytes = bytes;
    job->request = request_id;
    snprintf(job->title, sizeof job->title, "%s", title);
    strcpy(job->parent, native->parent);
    job->done = done;
    job->context = context;
    atomic_init(&job->cancel, 0);
    atomic_init(&job->finished, 0);
    if (pthread_create(&job->thread, NULL, worker, job)) {
        g_object_unref(job->stop);
        free(job->pdf);
        free(job);
        return 0;
    }
    native->jobs[slot] = job;
    native->bytes += bytes;
    return 1;
}
void
tiku_print_native_cancel(tiku_print_native_t *native, uint64_t request_id)
{
    if (native)
        for (unsigned i = 0; i < PRINT_JOBS; i++)
            if (native->jobs[i] && native->jobs[i]->request == request_id) {
                atomic_store(&native->jobs[i]->cancel, 1);
                g_cancellable_cancel(native->jobs[i]->stop);
            }
}
void
tiku_print_native_poll(tiku_print_native_t *native)
{
    if (!native || native->closing)
        return;
    native->refs++;
    for (unsigned i = 0; i < PRINT_JOBS && !native->closing; i++) {
        print_job_t *job = native->jobs[i];
        if (!job || !atomic_load(&job->finished))
            continue;
        native->jobs[i] = NULL;
        native->bytes -= job->bytes;
        job->done(job->context, job->request, job->status, job->reason);
        release(job);
    }
    if (!--native->refs)
        free(native);
}
void
tiku_print_native_close(tiku_print_native_t *native)
{
    if (!native || native->closing)
        return;
    native->closing = 1;
    for (unsigned i = 0; i < PRINT_JOBS; i++)
        if (native->jobs[i])
            tiku_print_native_cancel(native, native->jobs[i]->request);
    for (unsigned i = 0; i < PRINT_JOBS; i++)
        if (native->jobs[i])
            release(native->jobs[i]);
    if (!--native->refs)
        free(native);
}

size_t tiku_print_native_observe(tiku_print_native_t *native,tiku_print_job_observation_t *out,size_t capacity)
{
    size_t count=0;if(!native)return 0;
    for(unsigned i=0;i<PRINT_JOBS;i++)if(native->jobs[i]) {
        print_job_t *job=native->jobs[i];
        if(out&&count<capacity) {
            tiku_print_job_observation_t *row=&out[count];memset(row,0,sizeof *row);
            row->request=job->request;row->bytes=job->bytes;row->started=1;row->cancellation_requested=atomic_load(&job->cancel);
            row->finished=atomic_load(&job->finished);snprintf(row->title,sizeof row->title,"%s",job->title);
            if(row->finished){row->status=job->status;snprintf(row->reason,sizeof row->reason,"%s",job->reason);}
        }
        count++;
    }
    return count;
}
