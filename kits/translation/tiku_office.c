/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_office.c - an office document in, a PDF out.
 *
 * What a file is comes from its package, not its name: an OpenDocument
 * says so in its mimetype part, an Office file in its content types.  A
 * document is then read by its reader, laid out and drawn, or a deck
 * drawn a slide at a time, into one writer whose bytes are the answer.
 * Export writes them beside the file asked for and renames them over it,
 * so a failed export leaves nothing half written.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_office_private.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/** @brief The main part's content types, and what each is. */
static const struct { const char *type; tiku_office_kind_t kind; } mains[] = {
    {"wordprocessingml.document.main+xml", TIKU_OFFICE_DOCX},
    {"wordprocessingml.template.main+xml", TIKU_OFFICE_DOCX},
    {"ms-word.document.macroEnabled.main+xml", TIKU_OFFICE_DOCX},
    {"ms-word.template.macroEnabledTemplate.main+xml", TIKU_OFFICE_DOCX},
    {"presentationml.presentation.main+xml", TIKU_OFFICE_PPTX},
    {"presentationml.slideshow.main+xml", TIKU_OFFICE_PPTX},
    {"presentationml.template.main+xml", TIKU_OFFICE_PPTX},
    {"ms-powerpoint.presentation.macroEnabled.main+xml", TIKU_OFFICE_PPTX},
    {"ms-powerpoint.slideshow.macroEnabled.main+xml", TIKU_OFFICE_PPTX},
    {"ms-powerpoint.template.macroEnabled.main+xml", TIKU_OFFICE_PPTX},
};

/** @brief What the open package @p p holds. */
static tiku_office_kind_t
kind_of(of_pkg_t *p)
{
    size_t n = 0, i;
    unsigned char *mime = of_pkg_read(p, "mimetype", 256, &n);
    tiku_xml_t *ct;
    const tiku_xml_node_t *e;
    tiku_office_kind_t kind = TIKU_OFFICE_NONE;

    if (mime != NULL) {
        kind = strstr((char *)mime, "opendocument.text") != NULL
               ? TIKU_OFFICE_ODT : TIKU_OFFICE_NONE;
        free(mime);
        return kind;
    }
    ct = of_pkg_xml(p, "[Content_Types].xml");
    for (e = tiku_xml_child(tiku_xml_root(ct), "ct:Override");
         e != NULL && kind == TIKU_OFFICE_NONE;
         e = tiku_xml_next(e, "ct:Override")) {
        const char *type = tiku_xml_attr(e, "ContentType");

        for (i = 0; type != NULL && i < sizeof mains / sizeof mains[0]; i++) {
            if (strstr(type, mains[i].type) != NULL) {
                kind = mains[i].kind;
                break;
            }
        }
    }
    tiku_xml_free(ct);
    return kind;
}

tiku_office_kind_t
tiku_office_kind(const char *path)
{
    char why[128];
    of_pkg_t *p = path != NULL ? of_pkg_open(path, why, sizeof why) : NULL;
    tiku_office_kind_t kind = p != NULL ? kind_of(p) : TIKU_OFFICE_NONE;

    of_pkg_close(p);
    return kind;
}

/** @brief A word-processor document read, laid out and drawn into @p w.
 *  @return 0, or -1 with why said. */
static int
document(of_pkg_t *pkg, tiku_office_kind_t kind, of_faces_t *fs,
         tiku_pdf_writer_t *w, char *why, size_t max)
{
    of_doc_t *d = kind == TIKU_OFFICE_DOCX ? of_docx_read(pkg, why, max)
                                           : of_odt_read(pkg, why, max);
    of_layout_t lay;
    int err;

    if (d == NULL) {
        return -1;
    }
    memset(&lay, 0, sizeof lay);
    d->pkg = pkg;
    err = of_math_prepare(d, fs) != 0 ? -1 : of_layout(d, fs, &lay);
    if (err != 0) {
        snprintf(why, max, "%s", lay.npage >= OF_PAGES_MAX
                 ? "That document is longer than can be shown here."
                 : "There is not enough memory to lay out that document.");
    } else {
        tiku_pdf_writer_info(w, d->title, d->author);
        err = of_draw(d, fs, &lay, w);
        if (err != 0) {
            snprintf(why, max, "There is not enough memory to draw it.");
        }
    }
    of_layout_free(&lay);
    of_doc_free(d);
    return err;
}

unsigned char *
tiku_office_pdf(const char *path, size_t *len, char *why, size_t max)
{
    of_pkg_t *pkg;
    of_faces_t fs;
    tiku_pdf_writer_t *w;
    tiku_office_kind_t kind;
    unsigned char *bytes = NULL;
    int err;

    *len = 0;
    if ((pkg = of_pkg_open(path, why, max)) == NULL) {
        return NULL;
    }
    if ((kind = kind_of(pkg)) == TIKU_OFFICE_NONE) {
        snprintf(why, max, "That file is not a document read here.");
        of_pkg_close(pkg);
        return NULL;
    }
    if (tiku_fontset_count() == 0) {
        snprintf(why, max, "There are no fonts on this machine to set its "
                 "text in.");
        of_pkg_close(pkg);
        return NULL;
    }
    memset(&fs, 0, sizeof fs);
    if ((w = tiku_pdf_writer_new()) == NULL) {
        snprintf(why, max, "There is not enough memory to open it.");
        of_pkg_close(pkg);
        return NULL;
    }
    err = kind == TIKU_OFFICE_PPTX ? of_pptx_draw(pkg, &fs, w, why, max)
                                   : document(pkg, kind, &fs, w, why, max);
    if (err == 0 && (bytes = tiku_pdf_writer_finish(w, len)) == NULL) {
        snprintf(why, max, "There is not enough memory to write it.");
    } else if (err != 0) {
        tiku_pdf_writer_free(w);
    }
    of_faces_free(&fs);
    of_pkg_close(pkg);
    return bytes;
}

int
tiku_office_export(const char *path, const char *pdf, char *why, size_t max)
{
    size_t len = 0, done = 0;
    unsigned char *b = tiku_office_pdf(path, &len, why, max);
    char tmp[4096];
    int fd;

    if (b == NULL) {
        return -1;
    }
    snprintf(tmp, sizeof tmp, "%s.XXXXXX", pdf);
    if ((fd = mkstemp(tmp)) < 0) {
        snprintf(why, max, "The PDF could not be written there.");
        free(b);
        return -1;
    }
    (void)fchmod(fd, 0644);         /* mkstemp() made it 0600 */
    while (done < len) {
        ssize_t n = write(fd, b + done, len - done);

        if (n <= 0) {
            break;
        }
        done += (size_t)n;
    }
    free(b);
    if (close(fd) != 0 || done != len || rename(tmp, pdf) != 0) {
        unlink(tmp);
        snprintf(why, max, "The PDF could not be written there.");
        return -1;
    }
    return 0;
}
