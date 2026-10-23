/* tiku_image_io.inl -- part of tiku_image_window.c: reading and decoding
 * an image on a worker, one at a time; only the done and free callbacks,
 * on the loop, touch the viewer.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */

/** @brief Whether two stats are of one unchanged file: device, inode, size
 *         and times equal, so a rewritten or a replaced file differs. */
static int image_same_file(const struct stat *a, const struct stat *b)
{
    return a->st_dev == b->st_dev && a->st_ino == b->st_ino &&
        a->st_size == b->st_size &&
        a->st_mtim.tv_sec == b->st_mtim.tv_sec &&
        a->st_mtim.tv_nsec == b->st_mtim.tv_nsec &&
        a->st_ctim.tv_sec == b->st_ctim.tv_sec &&
        a->st_ctim.tv_nsec == b->st_ctim.tv_nsec;
}

/** @brief Read j->path whole into j->data: a regular file of at most
 *         TIKU_IMAGE_BYTES_MAX bytes that does not change while it is read;
 *         a watch of an unchanged file reads nothing.  @return 1, or 0. */
static int image_read_file(image_job_t *j)
{
    int fd = open(j->path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    struct stat after;
    size_t used = 0;
    if (fd < 0) return 0;
    if (fstat(fd, &j->stamp) || !S_ISREG(j->stamp.st_mode) ||
        j->stamp.st_size <= 0 ||
        (uint64_t)j->stamp.st_size > TIKU_IMAGE_BYTES_MAX) goto fail;
    if (j->watch && image_same_file(&j->stamp, &j->before)) {
        j->unchanged = 1; close(fd); return 1;
    }
    j->len = (size_t)j->stamp.st_size;
    j->data = malloc(j->len);
    if (!j->data) goto fail;
    while (used < j->len) {
        ssize_t n = read(fd, j->data + used, j->len - used);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) goto fail;
        used += (size_t)n;
    }
    if (fstat(fd, &after) || !image_same_file(&j->stamp, &after)) goto fail;
    close(fd); return 1;
fail:
    close(fd); return 0;
}

/** @brief On a worker: read the file, unless the bytes were handed in,
 *         and decode them; j->error says why when it fails. */
static void image_job_run(void *ctx)
{
    image_job_t *j = ctx;
    if (!j->data && !image_read_file(j)) {
        snprintf(j->error, sizeof j->error,
            "Cannot read a stable regular file (limit 32 MiB). "
            "Previous image kept; refresh paused.");
        return;
    }
    if (j->unchanged) return;
    tiku_image_err_t rc = tiku_image_decode(j->data, j->len, &j->image);
    if (rc != TIKU_IMG_OK) {
        snprintf(j->error, sizeof j->error, "%s Previous image kept.",
            rc == TIKU_IMG_NOT_IMAGE ? "Not a PNG, JPEG or BMP image." :
            rc == TIKU_IMG_UNSUPPORTED ?
                "Unsupported image variant (use baseline JPEG/8-bit PNG)." :
                "Image is incomplete, invalid or exceeds 16 megapixels.");
    }
}

/** @brief On the loop: show the decoded image, keeping the view when the
 *         same image comes back at its size; on failure keep the image
 *         shown, stop watching and say why. */
static void image_job_done(void *ctx)
{
    image_job_t *j = ctx;
    tiku_image_window_t *c = j->owner;
    if (j->generation != c->generation || !c->workspace) return;
    if (j->error[0]) {
        c->watching = 0;
        tiku_image_window_note(c, j->error);
    } else if (!j->unchanged) {
        if (!j->same || c->image.w != j->image.w ||
            c->image.h != j->image.h) {
            c->fit = 1; c->rotation = 0; c->pan_x = c->pan_y = 0;
        }
        tiku_image_free(&c->image); c->image = j->image;
        memset(&j->image, 0, sizeof j->image);
        snprintf(c->path, sizeof c->path, "%s", j->path);
        snprintf(c->label, sizeof c->label, "%s", j->label);
        c->stamp = j->stamp; c->bytes = j->len;
        if (!j->same || !j->path[0]) c->watching = 0;
        const char *name = strrchr(c->label, '/');
        if (c->window) snprintf(c->window->title, sizeof c->window->title,
            TIKU_PRODUCT_IMAGE_TITLE_FORMAT, name ? name + 1 : c->label);
        tiku_image_window_note(c, c->watching ?
            "Snapshot updated. Watching every 2 seconds." :
            "Drag to pan. + / - zoom. 0 fits; 1 shows actual pixels.");
        image_changed(c);
    }
}

/** @brief After done, or instead of it: free the job and drop its hold on
 *         the viewer. */
static void image_job_free(void *ctx)
{
    image_job_t *j = ctx;
    tiku_image_window_t *c = j->owner;
    c->busy = 0;
    if (c->workspace) {
        image_menus(c);
        if (c->dirty) *c->dirty = 1;
    }
    tiku_image_free(&j->image); free(j->data); free(j);
    image_unref(c);
}

/** @brief Read and decode @p source on a worker: the file it names, or
 *         @p len bytes of @p data, copied now.  One at a time.
 *  @return 1 when queued. */
static int image_request(tiku_image_window_t *c, const char *source,
                          const void *data, size_t len, int watch)
{
    if (!c || !c->workspace || !source || !*source) return 0;
    if (c->busy) {
        tiku_image_window_note(c, "An image is still loading. Try again.");
        return 0;
    }
    if (strlen(source) >= TIKU_PATH_MAX ||
        (data && (!len || len > TIKU_IMAGE_BYTES_MAX))) {
        tiku_image_window_note(c, "Image source exceeds the viewer limit.");
        return 0;
    }
    image_job_t *j = calloc(1, sizeof *j);
    if (!j) return 0;
    j->owner = c; j->generation = c->generation;
    j->same = c->image.px && !strcmp(source, c->label);
    j->watch = watch; j->before = c->stamp;
    snprintf(j->label, sizeof j->label, "%s", source);
    if (data) {
        j->data = malloc(len);
        if (!j->data) { free(j); return 0; }
        memcpy(j->data, data, len); j->len = len;
    } else snprintf(j->path, sizeof j->path, "%s", source);
    tiku_work_job_t work = { .run = image_job_run, .done = image_job_done,
                            .free = image_job_free, .ctx = j };
    c->refs++; c->busy = 1;
    if (tiku_work_post(&work) != 0) {
        image_job_free(j);
        tiku_image_window_note(c, "Image worker is busy. Try again.");
        return 0;
    }
    if (!watch) tiku_image_window_note(c, "Loading image...");
    image_menus(c);
    return 1;
}

int tiku_image_window_file(tiku_image_window_t *c, const char *path)
{
    tiku_image_window_open(c);
    return image_request(c, path, NULL, 0, 0);
}

int tiku_image_window_bytes(tiku_image_window_t *c,
    const char *label, const void *bytes, size_t len)
{
    if (!bytes) return 0;
    tiku_image_window_open(c);
    return image_request(c, label, bytes, len, 0);
}
