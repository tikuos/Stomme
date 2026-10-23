/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_msg.h - a message that describes itself.
 *
 * A message is a code saying what it is and any number of named fields,
 * each with a type and one or more values.  Fields are found by name, and
 * a field of a type this build does not know is read and kept whole.
 * kits/support/doc/message.md compares the format with Be's BMessage.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_MSG_H_
#define TIKU_MSG_H_

#include <stddef.h>
#include <stdint.h>

/** @brief What a value is.  The number is part of the wire format. */
typedef enum {
    TIKU_MSG_T_BOOL    = 1,
    TIKU_MSG_T_INT8    = 2,
    TIKU_MSG_T_INT16   = 3,
    TIKU_MSG_T_INT32   = 4,
    TIKU_MSG_T_INT64   = 5,
    TIKU_MSG_T_FLOAT   = 6,
    TIKU_MSG_T_DOUBLE  = 7,
    TIKU_MSG_T_STRING  = 8,     /* UTF-8, stored with its terminator     */
    TIKU_MSG_T_BLOB    = 9,     /* bytes, not interpreted here           */
    TIKU_MSG_T_POINT   = 10,    /* two floats                            */
    TIKU_MSG_T_RECT    = 11,    /* four floats: x, y, w, h               */
    TIKU_MSG_T_MESSAGE = 12,    /* another message, flattened            */
    TIKU_MSG_T_REF     = 13     /* a path, stored with its terminator    */
} tiku_msg_type_t;

/** @brief A message, opaque; each one returned is given back with
 *         tiku_msg_free(). */
typedef struct tiku_msg tiku_msg_t;

/** @brief A rectangle as a message carries one. */
typedef struct {
    float x, y, w, h;
} tiku_msg_rect_t;

/*---------------------------------------------------------------------------*/
/* Making and unmaking                                                       */
/*---------------------------------------------------------------------------*/

/** @brief An empty message whose code is @p what.  NULL if out of room. */
tiku_msg_t *tiku_msg_new(uint32_t what);

/** @brief Free @p m and everything it holds; NULL does nothing. */
void tiku_msg_free(tiku_msg_t *m);

/** @brief Empty @p m of fields, keeping its code and its room. */
void tiku_msg_clear(tiku_msg_t *m);

/**
 * @brief A new message with the same code and fields as @p m, made by
 *        flattening it and reading it back, so a field of a type this
 *        build does not know is copied whole.
 * @return the copy, or NULL for a NULL @p m or when out of room.
 */
tiku_msg_t *tiku_msg_copy(const tiku_msg_t *m);

/** @brief The code saying what @p m is; 0 for NULL. */
uint32_t tiku_msg_what(const tiku_msg_t *m);
/** @brief Set the code saying what @p m is. */
void tiku_msg_set_what(tiku_msg_t *m, uint32_t what);

/*---------------------------------------------------------------------------*/
/* Putting things in                                                         */
/*                                                                           */
/* Adding under a name that is already there appends a value to that         */
/* field.  Every one returns 1 when added, and 0 when out of room, for a     */
/* bad argument, or when the name is there already with another type.        */
/*---------------------------------------------------------------------------*/

/** @brief Add @p v under @p name as a bool, kept as 1 or 0. */
int tiku_msg_add_bool(tiku_msg_t *m, const char *name, int v);
/** @brief Add @p v under @p name as an int8. */
int tiku_msg_add_int8(tiku_msg_t *m, const char *name, int8_t v);
/** @brief Add @p v under @p name as an int16. */
int tiku_msg_add_int16(tiku_msg_t *m, const char *name, int16_t v);
/** @brief Add @p v under @p name as an int32. */
int tiku_msg_add_int32(tiku_msg_t *m, const char *name, int32_t v);
/** @brief Add @p v under @p name as an int64. */
int tiku_msg_add_int64(tiku_msg_t *m, const char *name, int64_t v);
/** @brief Add @p v under @p name as a float. */
int tiku_msg_add_float(tiku_msg_t *m, const char *name, float v);
/** @brief Add @p v under @p name as a double. */
int tiku_msg_add_double(tiku_msg_t *m, const char *name, double v);
/** @brief Add a copy of the string @p v, its terminator included, under
 *         @p name; a NULL @p v is refused. */
int tiku_msg_add_string(tiku_msg_t *m, const char *name,
                             const char *v);
/** @brief Add a copy of the @p len bytes at @p bytes under @p name;
 *         @p bytes may be NULL when @p len is 0. */
int tiku_msg_add_blob(tiku_msg_t *m, const char *name,
                           const void *bytes, size_t len);
/** @brief Add the point (@p x, @p y) under @p name. */
int tiku_msg_add_point(tiku_msg_t *m, const char *name,
                            float x, float y);
/** @brief Add the rectangle @p r under @p name. */
int tiku_msg_add_rect(tiku_msg_t *m, const char *name,
                           tiku_msg_rect_t r);
/** @brief Add a copy of @p path, its terminator included, under @p name
 *         as a ref; a NULL @p path is refused. */
int tiku_msg_add_ref(tiku_msg_t *m, const char *name,
                          const char *path);

/**
 * @brief Put a flattened copy of @p sub under @p name.
 *
 * The caller keeps @p sub, and may free it at once.
 */
int tiku_msg_add_message(tiku_msg_t *m, const char *name,
                              const tiku_msg_t *sub);

/*---------------------------------------------------------------------------*/
/* Taking things out                                                         */
/*                                                                           */
/* @p index picks among a field's values, 0 for the first.  The ones that    */
/* fill @p out return 1 when they found what was asked for, and 0, leaving   */
/* @p out alone, when the field is missing, of another type, or short of     */
/* that index.                                                               */
/*---------------------------------------------------------------------------*/

/** @brief The bool at @p index under @p name, as 1 or 0, into @p out. */
int tiku_msg_find_bool(const tiku_msg_t *m, const char *name,
                            int index, int *out);
/** @brief The int8 at @p index under @p name, into @p out. */
int tiku_msg_find_int8(const tiku_msg_t *m, const char *name,
                            int index, int8_t *out);
/** @brief The int16 at @p index under @p name, into @p out. */
int tiku_msg_find_int16(const tiku_msg_t *m, const char *name,
                             int index, int16_t *out);
/** @brief The int32 at @p index under @p name, into @p out. */
int tiku_msg_find_int32(const tiku_msg_t *m, const char *name,
                             int index, int32_t *out);
/** @brief The int64 at @p index under @p name, into @p out. */
int tiku_msg_find_int64(const tiku_msg_t *m, const char *name,
                             int index, int64_t *out);
/** @brief The float at @p index under @p name, into @p out. */
int tiku_msg_find_float(const tiku_msg_t *m, const char *name,
                             int index, float *out);
/** @brief The double at @p index under @p name, into @p out. */
int tiku_msg_find_double(const tiku_msg_t *m, const char *name,
                              int index, double *out);
/** @brief The point at @p index under @p name, into @p x and @p y. */
int tiku_msg_find_point(const tiku_msg_t *m, const char *name,
                             int index, float *x, float *y);
/** @brief The rectangle at @p index under @p name, into @p out. */
int tiku_msg_find_rect(const tiku_msg_t *m, const char *name,
                            int index, tiku_msg_rect_t *out);

/**
 * @brief The string at @p index under @p name.
 *
 * @return a pointer into the message, good until the message changes or
 *         is freed, or NULL.  Nothing is copied.
 */
const char *tiku_msg_find_string(const tiku_msg_t *m,
                                      const char *name, int index);

/** @brief The path at @p index under @p name, or NULL: a pointer into the
 *         message, good until the message changes or is freed. */
const char *tiku_msg_find_ref(const tiku_msg_t *m,
                                   const char *name, int index);

/**
 * @brief The bytes at @p index under @p name, and how many into @p len
 *        when it is not NULL.
 *
 * @return a pointer into the message, or NULL.  Nothing is copied.
 */
const void *tiku_msg_find_blob(const tiku_msg_t *m,
                                    const char *name, int index,
                                    size_t *len);

/**
 * @brief A new message unflattened from the value at @p index under
 *        @p name.
 *
 * @return a message the caller frees, or NULL.
 */
tiku_msg_t *tiku_msg_find_message(const tiku_msg_t *m,
                                            const char *name, int index);

/*---------------------------------------------------------------------------*/
/* Looking at what is there                                                  */
/*                                                                           */
/* These list a message's fields, with their types and value counts.         */
/*---------------------------------------------------------------------------*/

/** @brief How many fields @p m has; 0 for NULL. */
int tiku_msg_field_count(const tiku_msg_t *m);

/**
 * @brief The @p i th field's name, and what is under it.
 *
 * @param type Filled with the field's type; may be NULL.
 * @param count Filled with how many values it has; may be NULL.
 * @return the name, or NULL past the end.
 */
const char *tiku_msg_field_at(const tiku_msg_t *m, int i,
                                   tiku_msg_type_t *type, int *count);

/** @brief How many values @p name has (0 when it is not there at all). */
int tiku_msg_count(const tiku_msg_t *m, const char *name);

/** @brief Whether @p name is there with @p type and at least one value. */
int tiku_msg_has(const tiku_msg_t *m, const char *name,
                      tiku_msg_type_t type);

/** @brief Take the field @p name out with all its values.
 *  @return 1 when there was one. */
int tiku_msg_remove(tiku_msg_t *m, const char *name);

/** @brief The name of @p type, such as "int32"; "unknown" for a type
 *         not listed. */
const char *tiku_msg_type_name(tiku_msg_type_t type);

/*---------------------------------------------------------------------------*/
/* The flat form                                                             */
/*                                                                           */
/*   u32 magic 'TKMS' | u32 what | u32 field count                           */
/*   per field:  u16 name length | name | u32 type | u32 value count         */
/*               per value:  u32 length | bytes                              */
/*                                                                           */
/* Little-endian throughout, written a byte at a time, so the layout does    */
/* not depend on the host's byte order.  Every value carries its length, so  */
/* tiku_msg_unflatten() reads a field of a type it does not know.            */
/*---------------------------------------------------------------------------*/

/** @brief The magic four bytes a flattened message opens with. */
#define TIKU_MSG_MAGIC 0x534D4B54u      /* 'TKMS', little-endian */

/** @brief How many bytes @p m would flatten to; 0 for NULL, or for a
 *         size that does not fit in a size_t. */
size_t tiku_msg_flat_size(const tiku_msg_t *m);

/**
 * @brief Write @p m into @p buf.
 *
 * @param max How much room @p buf has.
 * @param wrote Filled with how much was used; may be NULL.
 * @return 1 when it fitted and was written, 0 otherwise.
 */
int tiku_msg_flatten(const tiku_msg_t *m, void *buf, size_t max,
                          size_t *wrote);

/**
 * @brief Read a message out of the @p len bytes at @p buf.
 *
 * Every length is checked against the bytes left before it is used, and
 * no allocation is sized from a count in the bytes.  A name may not be
 * empty or hold a NUL, text must end in its NUL, and a fixed-size value
 * must be its type's size.
 *
 * @param read Filled with how many bytes the message took; may be NULL.
 * @return a message the caller frees, or NULL if the bytes are not one.
 */
tiku_msg_t *tiku_msg_unflatten(const void *buf, size_t len,
                                         size_t *read);

#endif /* TIKU_MSG_H_ */
