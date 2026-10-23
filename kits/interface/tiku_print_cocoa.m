/*
 * KintsugiDesktop v0.01 Interface Kit.
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 * tiku_print_cocoa.m - native print sheets retaining the captured final PDF.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_access.h"
#include "tiku_print_native.h"
#import <Cocoa/Cocoa.h>
#import <PDFKit/PDFKit.h>
#include <stdlib.h>
#include <string.h>

@class TikuPrintJob;
struct tiku_print_native {
    NSWindow *window; /**< retained native parent through outstanding sheets */
    NSMutableDictionary *jobs; /**< owned immutable PDF jobs */
    TikuPrintJob *active; /**< borrowed currently displayed native operation */
    size_t bytes;         /**< aggregate copied PDF size */
    unsigned refs, closing; /**< owner, jobs and current poll lifetime holds */
};
/** @brief A print operation owns its PDF until native completion arrives. */
@interface TikuPrintJob : NSObject {
  @public
    tiku_print_native_t *native;
    uint64_t request;
    NSData *data;
    NSString *title;
    PDFDocument *document;
    NSPrintOperation *operation;
    NSWindow *sheet;
    int started, finished, cancelled, status;
    tiku_print_done_fn done;
    void *context;
}
- (void)completed:(NSNumber *)success;
- (void)printOperationDidRun:(NSPrintOperation *)operation
                     success:(BOOL)success
                 contextInfo:(void *)context;
@end
/** @brief Release endpoint storage after its final native job or poll. */
static void
release(tiku_print_native_t *native)
{
    if (!--native->refs) {
        [native->jobs release];
        [native->window release];
        free(native);
    }
}
/** @brief Remove a completed job before its user callback can close the host.
 */
static void
retire(TikuPrintJob *job, int notify)
{
    tiku_print_native_t *native = job->native;
    [job retain];
    if (native->active == job)
        native->active = nil;
    native->bytes -= [job->data length];
    [native->jobs removeObjectForKey:@(job->request)];
    if (notify && !native->closing)
        job->done(job->context, job->request, job->status,
                  job->status == 1   ? "Native print operation completed."
                  : job->status == 0 ? "Printing cancelled."
                                     : "Native print operation failed.");
    release(native);
    job->native = NULL;
    [job release];
}
@implementation TikuPrintJob
- (void)dealloc
{
    [sheet release];
    [operation release];
    [document release];
    [data release];
    [title release];
    [super dealloc];
}
- (void)completed:(NSNumber *)success
{
    finished = 1;
    status = [success boolValue] ? 1
             : cancelled || [[operation.printInfo jobDisposition]
                                isEqualToString:NSPrintCancelJob]
                 ? 0
                 : -1;
    if (native->closing)
        retire(self, 0);
    [self release];
}
- (void)printOperationDidRun:(NSPrintOperation *)op
                     success:(BOOL)success
                 contextInfo:(void *)info
{
    (void)op;
    (void)info;
    [self retain];
    if ([NSThread isMainThread])
        [self completed:@(success)];
    else
        [self performSelectorOnMainThread:@selector(completed:)
                               withObject:@(success)
                            waitUntilDone:NO];
}
@end

tiku_print_native_t *
tiku_print_native_open(const char *parent, void *native_window)
{
    (void)parent;
    tiku_print_native_t *native = calloc(1, sizeof *native);
    if (native) {
        native->window = [(NSWindow *)native_window retain];
        native->jobs = [[NSMutableDictionary alloc] init];
        native->refs = 1;
    }
    return native;
}
int
tiku_print_native_begin(tiku_print_native_t *native, uint64_t request,
                        const void *pdf, size_t bytes, const char *title,
                        tiku_print_done_fn done, void *context)
{
    if (!native || native->closing || !native->window || !request || !pdf ||
        bytes < 5 || bytes > TIKU_PRINT_PDF_MAX || memcmp(pdf, "%PDF-", 5) ||
        !title || strlen(title) > 255 ||
        tiku_access_characters(title, strlen(title)) < 0 || !done ||
        [native->jobs count] >= 4 || [native->jobs objectForKey:@(request)] ||
        bytes > 256u * 1024u * 1024u - native->bytes)
        return 0;
    TikuPrintJob *job = [[[TikuPrintJob alloc] init] autorelease];
    job->data = [[NSData alloc] initWithBytes:pdf length:bytes];
    job->title = [[NSString alloc] initWithUTF8String:title];
    if (!job->data || !job->title)
        return 0;
    native->refs++;
    native->bytes += bytes;
    job->native = native;
    job->request = request;
    job->done = done;
    job->context = context;
    [native->jobs setObject:job forKey:@(request)];
    return 1;
}
void
tiku_print_native_cancel(tiku_print_native_t *native, uint64_t request)
{
    if (!native)
        return;
    TikuPrintJob *job = [native->jobs objectForKey:@(request)];
    if (!job || job->finished)
        return;
    job->cancelled = 1;
    if (job->operation)
        [job->operation.printInfo setJobDisposition:NSPrintCancelJob];
    if (!job->started) {
        job->finished = 1;
        job->status = 0;
        return;
    }
    if (job->sheet && [native->window attachedSheet] == job->sheet) {
        [NSApp endSheet:job->sheet returnCode:NSModalResponseCancel];
        [job->sheet orderOut:nil];
    }
}
/** @brief Start one native sheet only when its parent has no other sheet. */
static void
start(TikuPrintJob *job)
{
    tiku_print_native_t *native = job->native;
    job->document = [[PDFDocument alloc] initWithData:job->data];
    NSPrintInfo *info = [[NSPrintInfo sharedPrintInfo] copy];
    job->operation =
        [[job->document printOperationForPrintInfo:info
                                       scalingMode:kPDFPrintPageScaleToFit
                                        autoRotate:YES] retain];
    [info release];
    if (!job->document || ![job->document pageCount] || !job->operation) {
        job->finished = 1;
        job->status = -1;
        return;
    }
    job->started = 1;
    native->active = job;
    [job->operation setJobTitle:job->title];
    [job->operation setShowsPrintPanel:YES];
    [job->operation setShowsProgressPanel:YES];
    [job->operation setCanSpawnSeparateThread:YES];
    [job->operation runOperationModalForWindow:native->window
                                      delegate:job
                                didRunSelector:@selector
                                (printOperationDidRun:success:contextInfo:)
                                   contextInfo:NULL];
    job->sheet = [[native->window attachedSheet] retain];
}
void
tiku_print_native_poll(tiku_print_native_t *native)
{
    if (!native || native->closing)
        return;
    native->refs++;
    NSArray *jobs = [[native->jobs allValues] copy];
    for (TikuPrintJob *job in jobs) {
        if (native->closing)
            break;
        if (job->finished)
            retire(job, 1);
        else if (!native->active && ![native->window attachedSheet])
            start(job);
    }
    [jobs release];
    release(native);
}
void
tiku_print_native_close(tiku_print_native_t *native)
{
    if (!native || native->closing)
        return;
    native->closing = 1;
    NSArray *jobs = [[native->jobs allValues] copy];
    for (TikuPrintJob *job in jobs) {
        tiku_print_native_cancel(native, job->request);
        if (job->native && job->finished)
            retire(job, 0);
    }
    [jobs release];
    release(native);
}
