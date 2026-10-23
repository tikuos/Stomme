/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_layout.h - a rectangle divided as it was described.
 *
 * A window says what its parts are: this one as tall as a button, that
 * one taking what is left, this one never narrower than its word.  Then
 * it asks where part three went, and the draw and the click get the
 * same answer from the same description.  Nothing is cached: the
 * arithmetic is a few adds, so a face that changed size needs nothing
 * invalidated.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_LAYOUT_H_
#define TIKU_LAYOUT_H_

#include "tiku_geom.h"

/**
 * @brief How many parts one description holds.
 *
 * More parts than this are rows of rows: a part's slot is laid out with
 * a description of its own.
 */
#define TIKU_LAYOUT_MAX 16

/** @brief One part: fixed, or a share of what is left. */
typedef struct {
    int size;       /* its extent, or 0 to take a share            */
    int weight;     /* how many shares, when size is 0             */
    int least;      /* never smaller than this, share or not       */
} tiku_layout_item_t;

/** @brief A description: its direction, gap, padding and parts. */
typedef struct {
    int horiz;      /* nonzero lays a row across; zero lays a stack */
    int gap;        /* between parts                               */
    int pad;        /* around the whole                            */
    int n;
    tiku_layout_item_t item[TIKU_LAYOUT_MAX];
} tiku_layout_t;

/** @brief An empty description. */
void tiku_layout_init(tiku_layout_t *l, int horiz, int gap, int pad);

/**
 * @brief Add a part.
 *
 * @param size   its extent, or 0 to take a share of what is left.
 * @param weight how many shares, when @p size is 0.  A part with
 *               neither size nor weight is a spring of one share,
 *               which is how a row is pushed to the right.
 * @param least  the smallest it may be.
 * @return its index, or -1 when the description is full.
 */
int tiku_layout_add(tiku_layout_t *l, int size, int weight, int least);

/**
 * @brief Where part @p i falls when the description is laid in @p r.
 *
 * A part the description does not have comes back with zero extent, so a
 * caller looping past the count draws and hits nothing.
 */
tiku_rect_t tiku_layout_slot(const tiku_layout_t *l, tiku_rect_t r, int i);

/** @brief Which part @p x, @p y lands in, or -1 for none of them. */
int tiku_layout_at(const tiku_layout_t *l, tiku_rect_t r, int x, int y);

/**
 * @brief The smallest @p r may be before parts start losing their
 *        least: every fixed size, every least, the gaps and the pad.
 *
 * What a window asks when it is deciding how big to open, so that it
 * grows with the face.
 */
int tiku_layout_least(const tiku_layout_t *l);

#endif /* TIKU_LAYOUT_H_ */
