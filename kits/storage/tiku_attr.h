/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_attr.h - attributes, and the text a column shows for one.
 *
 * A TikuOS node's manifest meta column is a typed descriptor
 * (vtype,unit,freshness,cost[,lo..hi]).  The descriptor formats a reading
 * ("1.2 MiB", "1:02:03") and refuses a value before it is sent: "banana"
 * for a u32, or 5 for a node declared 0..1.  The fitting functions shorten
 * a cell's text to a column's width.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_ATTR_H_
#define TIKU_ATTR_H_

#include <stddef.h>
#include <stdint.h>

#include "tiku_model.h"

/** @brief What a node's value is. */
typedef enum {
    TIKU_VT_NONE = 0,        /* untyped: the text is the value        */
    TIKU_VT_BOOL,
    TIKU_VT_U32,
    TIKU_VT_I32,
    TIKU_VT_STR
} tiku_vtype_t;

/** @brief What the number means, which picks the formatter. */
typedef enum {
    TIKU_UNIT_NONE = 0,
    TIKU_UNIT_BOOL,
    TIKU_UNIT_BYTES,
    TIKU_UNIT_HZ,
    TIKU_UNIT_SECONDS,
    TIKU_UNIT_COUNT,
    TIKU_UNIT_OTHER          /* known name, no formatter of its own   */
} tiku_unit_t;

/** @brief How current a reading is, which says whether it needs re-reading. */
typedef enum {
    TIKU_FRESH_UNKNOWN = 0,
    TIKU_FRESH_STATIC,       /* fixed for the life of the boot        */
    TIKU_FRESH_CACHED,
    TIKU_FRESH_LIVE
} tiku_fresh_t;

#define TIKU_UNIT_NAME_MAX 12

/** @brief A parsed typed descriptor. */
typedef struct {
    tiku_vtype_t vtype;
    tiku_unit_t  unit;
    tiku_fresh_t fresh;
    char             unit_name[TIKU_UNIT_NAME_MAX];  /* verbatim      */
    char             cost[TIKU_UNIT_NAME_MAX];
    int              has_range;
    long             lo, hi;
} tiku_desc_t;

/**
 * @brief Parse a manifest meta field.
 *
 * "-" and anything unrecognised give an untyped descriptor, whose value is
 * shown as text.
 *
 * @return 1 when a type was recognised, 0 when the result is untyped.
 */
int tiku_desc_parse(const char *meta, tiku_desc_t *out);

/**
 * @brief The read policy a node's metadata states.
 *
 * Revision-4 metadata ends a typed descriptor with ";read=<policy>" and,
 * for a node whose value is private, ";secret".  Only "read=poll" says a
 * read leaves the board as it was.
 *
 * @return 1 for read=poll; 0 for any other policy, a secret node or an
 *         extension this build does not recognise; -1 for no policy.
 * @note Read permission alone does not make a read safe to make unasked.
 */
int tiku_attr_read_policy(const char *meta);

/**
 * @brief Whether a node may be read without the user asking for it.
 *
 * The metadata's read policy decides when there is one.  Without one, the
 * nodes known to change the board when read are refused by name.
 *
 * @param meta The node's metadata, or NULL
 * @param path Its path on the board: below the mount point, not the
 *             desktop's /devices/<name> path
 * @return 1 when a read may be made unasked, 0 when not
 */
int tiku_attr_autoread(const char *meta, const char *path);

/**
 * @brief Format @p raw for display, by what the descriptor says it means.
 *
 * The text is not fitted to a width; the caller fits it like any other
 * cell. NULL displays "-" for an unread value; an empty string stays empty.
 *
 * @return Length written.
 */
int tiku_attr_format(const tiku_desc_t *d, const char *raw,
                         char *out, size_t max);

/**
 * @brief Whether this node's value can be edited in place: it is writable
 *        and not a container.  @p d is not consulted.
 */
int tiku_attr_editable(const tiku_model_t *m,
                           const tiku_desc_t *d);

/**
 * @brief Check @p text before it is sent to the device.
 *
 * @param err Receives the reason, worded for the user.
 * @return 1 when acceptable.
 */
int tiku_attr_validate(const tiku_desc_t *d, const char *text,
                           char *err, size_t errmax);

/**
 * @brief Canonical text to send for @p text.
 *
 * "on", "true" and "1" all mean the same thing to a boolean node, but the
 * device is sent one spelling.
 */
int tiku_attr_canonical(const tiku_desc_t *d, const char *text,
                            char *out, size_t max);

/**
 * @brief Order two values of the same descriptor.
 *
 * Numbers compare as numbers, not as their formatted text, so 9 bytes sort
 * before 10 KiB.  An empty value sorts after every value, and other text
 * compares case-insensitively.
 *
 * @return <0, 0 or >0.
 */
int tiku_attr_compare(const tiku_desc_t *d, const char *a,
                          const char *b);

/** @brief Where an over-long string loses its middle, or its end. */
typedef enum {
    TIKU_TRUNC_MIDDLE = 0,   /* names and generic values              */
    TIKU_TRUNC_END,          /* sizes, and the open-with relation     */
    TIKU_TRUNC_BEGIN         /* type-ahead: the newest characters win */
} tiku_trunc_t;

/**
 * @brief Fit @p in to @p width pixels, eliding with a single ellipsis.
 *
 * TIKU_TRUNC_MIDDLE keeps both ends of a name such as
 * "gpio_bank_1_direction" readable.  The mark is one U+2026, and a text that
 * already fits is copied whole.
 *
 * @param measure Returns the pixel width of a string.
 * @return Length written.
 */
int tiku_text_fit(const char *in, int width, tiku_trunc_t how,
                      int (*measure)(const char *, void *), void *ctx,
                      char *out, size_t max);

/**
 * @brief The longest date+time form that fits @p width (MA-025).
 *
 * Tried longest first -- long date with seconds, long date, medium date,
 * short date, each with its time -- and the first that fits wins.  If none
 * fits, the short date alone, elided in the middle when it is still too
 * wide.
 *
 * @param when Seconds since the epoch; zero or less reads "-", for a store
 *             with no wall clock.
 * @return Length written.
 */
int tiku_attr_time_fit(int64_t when, int width,
                           int (*measure)(const char *, void *), void *ctx,
                           char *out, size_t max);

/**
 * @brief A size that fits @p width (MA-023).
 *
 * A negative @p bytes is unknown and reads "-".  Under a kibibyte it is "N
 * B"; above, two decimals and a binary prefix, a trailing zero dropped
 * ("1.0 KiB").  When that is too wide, the bare byte count, and failing that
 * the formatted form with an end ellipsis.
 */
int tiku_attr_size_fit(int64_t bytes, int width,
                           int (*measure)(const char *, void *), void *ctx,
                           char *out, size_t max);

/**
 * @brief A rating from 0 to 10 as five cells: '*' full, '+' half and '.'
 *        empty (MA-037).
 *
 * A cell is full above its midpoint and half above its start.  The marks are
 * ASCII: the drawing floor's font is single-byte.
 */
int tiku_attr_rating(long value, char *out, size_t max);

/**
 * @brief A value over its descriptor's declared range, drawn in cells as
 *        tiku_attr_rating() draws its five.
 *
 * @param cells How many cells to draw.
 * @return Length written, or 0 when the descriptor declares no range or
 *         @p raw is not a number.
 */
int tiku_attr_gauge(const tiku_desc_t *d, const char *raw, int cells,
                        char *out, size_t max);

/**
 * @brief A display name that tells same-named neighbours apart: the leaf
 *        alone when it is unique among @p others, otherwise "leaf --
 *        .../suffix" with the shortest trailing path that distinguishes it
 *        from @p others, or "leaf -- parent" when only the whole parent
 *        does.
 *
 * @return the length written.
 */
int tiku_attr_disambiguate(const char *path,
                               const char *const *others, int n,
                               char *out, size_t max);

#endif /* TIKU_ATTR_H_ */
