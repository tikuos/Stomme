/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richio.c - bounded native document loading and atomic replacement.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richio_private.h"
#include "tiku_atomic_file.h"
#include "tiku_zip_write.h"
#include <stdlib.h>
#include <string.h>

tiku_richio_status_t
tiku_rio_options(const tiku_richio_options_t *source,
    tiku_richio_options_t *out)
{
    memset(out, 0, sizeof *out);
    if (source) *out = *source;
    if (!out->xml_bytes) out->xml_bytes = (size_t)64 << 20;
    if (!out->memory_bytes) out->memory_bytes = (size_t)256 << 20;
    if (out->xml_bytes > (size_t)512 << 20) return TIKU_RICHIO_INVALID;
    if (out->memory_bytes < 2) return TIKU_RICHIO_LIMIT;
    if (!!out->document.allocator.allocate !=
        !!out->document.allocator.release) return TIKU_RICHIO_INVALID;
    return TIKU_RICHIO_OK;
}

tiku_richio_status_t
tiku_rio_model(tiku_rich_status_t status)
{
    if (status == TIKU_RICH_OK) return TIKU_RICHIO_OK;
    if (status == TIKU_RICH_NO_MEMORY) return TIKU_RICHIO_NO_MEMORY;
    if (status == TIKU_RICH_LIMIT) return TIKU_RICHIO_LIMIT;
    return TIKU_RICHIO_DAMAGED;
}

/** @brief Translate archive failure categories, retaining damage versus I/O. */
static tiku_richio_status_t
archive_status(tiku_archive_err_t status)
{
    switch (status) {
    case TIKU_ARCHIVE_OK: return TIKU_RICHIO_OK;
    case TIKU_ARCHIVE_MISSING:
    case TIKU_ARCHIVE_WRITE: return TIKU_RICHIO_IO;
    case TIKU_ARCHIVE_NOMEM: return TIKU_RICHIO_NO_MEMORY;
    case TIKU_ARCHIVE_TOO_LARGE: return TIKU_RICHIO_LIMIT;
    case TIKU_ARCHIVE_CANCELLED: return TIKU_RICHIO_CANCELLED;
    case TIKU_ARCHIVE_UNSUPPORTED:
    case TIKU_ARCHIVE_ENCRYPTED: return TIKU_RICHIO_UNSUPPORTED;
    default: return TIKU_RICHIO_DAMAGED;
    }
}

tiku_richio_status_t
tiku_richio_read_xml(const void *xml, size_t length,
    const tiku_richio_options_t *options, tiku_richdoc_t **out)
{
    tiku_rio_reader_t r = {0};
    tiku_xml_limits_t limits;
    tiku_xml_status_t status;
    tiku_xml_t *tree;

    if (!out) return TIKU_RICHIO_INVALID;
    *out = NULL;
    if (!xml || !length) return TIKU_RICHIO_INVALID;
    r.status = tiku_rio_options(options, &r.options);
    if (r.status != TIKU_RICHIO_OK) return r.status;
    if (length > r.options.xml_bytes) return TIKU_RICHIO_LIMIT;
    r.status = tiku_rio_preflight(xml, length, &r.options);
    if (r.status != TIKU_RICHIO_OK) return r.status;
    limits = (tiku_xml_limits_t){r.options.memory_bytes / 2, 2000000,
        r.options.cancel, r.options.ctx};
    tree = tiku_xml_parse_limited(xml, length, &limits, &status, NULL, 0);
    if (!tree) {
        switch (status) {
        case TIKU_XML_NOMEM: return TIKU_RICHIO_NO_MEMORY;
        case TIKU_XML_LIMIT: return TIKU_RICHIO_LIMIT;
        case TIKU_XML_CANCELLED: return TIKU_RICHIO_CANCELLED;
        default: return TIKU_RICHIO_DAMAGED;
        }
    }
    r.status = tiku_rio_decode(&r, tiku_xml_root(tree), out);
    tiku_xml_free(tree);
    return r.status;
}

tiku_richio_status_t
tiku_richio_load(const char *path, const tiku_richio_options_t *options,
    tiku_richdoc_t **out)
{
    tiku_richio_options_t opt;
    tiku_archive_t *archive = NULL;
    const tiku_archive_entry_t *entry;
    unsigned char *xml = NULL;
    size_t length = 0;
    tiku_richio_status_t status;

    if (!out) return TIKU_RICHIO_INVALID;
    *out = NULL;
    if (!path || !*path) return TIKU_RICHIO_INVALID;
    status = tiku_rio_options(options, &opt);
    if (status != TIKU_RICHIO_OK) return status;
    if (opt.cancel && opt.cancel(opt.ctx)) return TIKU_RICHIO_CANCELLED;
    status = archive_status(tiku_archive_open_package(path,
        opt.xml_bytes + 4096, 4096, 2, &archive));
    if (status != TIKU_RICHIO_OK) return status;
    entry = tiku_archive_entry(archive, 0);
    if (tiku_archive_count(archive) != 1)
        status = TIKU_RICHIO_UNSUPPORTED;
    else if (!entry || strcmp(entry->path, "document.xml") ||
             entry->type != TIKU_ARC_FILE || entry->flags)
        status = TIKU_RICHIO_DAMAGED;
    else if (strcmp(entry->method, "stored") &&
             strcmp(entry->method, "deflate")) status = TIKU_RICHIO_UNSUPPORTED;
    else status = archive_status(tiku_archive_read(archive, 0, opt.xml_bytes,
                                                   &xml, &length));
    if (status == TIKU_RICHIO_OK)
        status = tiku_richio_read_xml(xml, length, &opt, out);
    free(xml);
    tiku_archive_close(archive);
    return status;
}

/** @brief Save callback state, retaining borrowed encoded XML and options. */
typedef struct {
    tiku_zip_entry_t entry;
    tiku_richio_options_t options;
    tiku_richio_status_t status;
} save_t;

/** @brief Write a complete ZIP and poll cancellation before replacement. */
static int
produce(FILE *out, void *context)
{
    save_t *save = context;

    if (save->options.cancel && save->options.cancel(save->options.ctx))
        save->status = TIKU_RICHIO_CANCELLED;
    else save->status = archive_status(tiku_zip_write(out, &save->entry, 1));
    if (save->status == TIKU_RICHIO_OK && save->options.cancel &&
        save->options.cancel(save->options.ctx))
        save->status = TIKU_RICHIO_CANCELLED;
    return save->status == TIKU_RICHIO_OK ? 0 : -1;
}

tiku_richio_status_t
tiku_richio_save(const char *path, const tiku_richsnapshot_t *snapshot,
    const tiku_richio_options_t *options)
{
    save_t save = {0};
    unsigned char *xml = NULL;
    size_t length = 0;
    int failed;

    if (!path || !*path || !snapshot) return TIKU_RICHIO_INVALID;
    save.status = tiku_rio_options(options, &save.options);
    if (save.status != TIKU_RICHIO_OK) return save.status;
    save.status = tiku_richio_write_xml(snapshot, &save.options, &xml, &length);
    if (save.status != TIKU_RICHIO_OK) return save.status;
    save.entry = (tiku_zip_entry_t){"document.xml", xml, length};
    failed = tiku_atomic_file(path, produce, &save);
    free(xml);
    if (failed && save.status == TIKU_RICHIO_OK) return TIKU_RICHIO_IO;
    return save.status;
}

const char *
tiku_richio_strerror(tiku_richio_status_t status)
{
    static const char *const messages[] = {"Done", "Invalid arguments",
        "The document is damaged", "The document uses unsupported features",
        "The document exceeds a resource limit", "Not enough memory",
        "Cancelled", "The file could not be read or saved durably"};

    return (unsigned)status < sizeof messages / sizeof messages[0]
        ? messages[status] : "Unknown document error";
}
