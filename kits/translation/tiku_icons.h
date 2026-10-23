/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_icons.h - the HVIF icon set and its render cache.
 *
 * The standard Tracker artwork is compiled in as HVIF blobs, which the
 * renderer rasterises at the size asked for; TikuOS's own icons and an
 * icon directory add to it.  Icons are named in the Tracker's vocabulary:
 * "folder", "trash_full", "folder_queries".
 *
 * Artwork from Haiku (haiku-os.org), MIT licensed; the TrackerIcons.rdef
 * blobs additionally carry the Open Tracker License (Be Incorporated,
 * 1991-2000).
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef TIKU_ICONS_H_
#define TIKU_ICONS_H_

#include <stddef.h>
#include <stdint.h>

/** @brief How many icons are baked in. */
#define TIKU_ICON_COUNT 34

/** @brief How many icons the set takes past the baked ones: TikuOS's own
 *         and an icon directory's. */
#define TIKU_ICON_EXTRA 128

/** @brief One named HVIF blob. */
typedef struct {
    const char    *name;
    const uint8_t *data;
    size_t         len;
} tiku_icon_t;

/** @brief Number of icons in the set: the baked ones and those added. */
int tiku_icons_count(void);

/** @brief The @p i th icon in set order, or NULL if @p i is out of range. */
const tiku_icon_t *tiku_icons_at(int i);

/**
 * @brief Look an icon up by short name.
 *
 * @param name One of the set's names, e.g. "folder" or "trash_full".
 * @return The icon, or NULL if nothing carries that name.
 */
const tiku_icon_t *tiku_icons_find(const char *name);

/**
 * @brief Take every icon in @p dir into the set, by its file name.
 *
 * A file called `folder.hvif` becomes the icon named `folder`, replacing
 * the icon of that name; a new name is added while the set has room.
 * Only files named *.hvif, of at most 256 KB, are read.
 *
 * @return how many files were taken.
 */
int tiku_icons_load_dir(const char *dir);

/**
 * @brief Take one icon from memory into the set, under @p name.
 *
 * The blob is copied, so static bytes may be passed.  A name already in
 * the set is replaced and keeps its place.
 *
 * @return 1 when it was taken; 0 for no name or data, a blob empty or
 *         over 256 KB, a full set or a failed allocation.
 */
int tiku_icons_add(const char *name, const void *data, size_t len);

/**
 * @brief Render @p name at @p size x @p size, once per size.
 *
 * The result is premultiplied 0xAARRGGBB owned by the cache.
 *
 * @param name Icon name.
 * @param size Edge length in pixels, 1..1024.
 * @return The bitmap, or NULL for an unknown name or a failed render.
 * @note The bitmap stays valid until the next render, change to the set
 *       or flush, any of which may evict it: callers blit it at once and
 *       never free it.
 */
const uint32_t *tiku_icons_bitmap(const char *name, int size);

/**
 * @brief @p name at @p size with its rgb blended toward @p wash by
 *        @p mix / 255, alpha kept: the selected look (IV-019).
 *
 * Built from the plain render and cached beside it until @p wash or
 * @p mix changes or the render is evicted.
 *
 * @return The washed bitmap, under the same lifetime as
 *         tiku_icons_bitmap()'s, or NULL when that render or an
 *         allocation fails.
 */
const uint32_t *tiku_icons_bitmap_washed(const char *name, int size,
                                             uint32_t wash, unsigned mix);

/** @brief How many washed bitmaps have been built since the program
 *         started. */
unsigned tiku_icons_washed_builds(void);

/**
 * @brief How many bytes of rendered icon the cache may hold (IV-012).
 *
 * A new plain render evicts the oldest renders until the total fits; a
 * washed copy counts toward the total but evicts nothing.
 */
#define TIKU_ICONS_BUDGET (512u * 1024u)

/** @brief How many bytes of rendered icon are held right now. */
size_t tiku_icons_cached_bytes(void);

/** @brief Whether the render of @p name at @p size is in the cache. */
int tiku_icons_cached(const char *name, int size);

/** @brief Drop every cached bitmap. */
void tiku_icons_flush(void);

#endif /* TIKU_ICONS_H_ */
