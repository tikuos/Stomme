/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_resource_literal.h - typed resource literals for clients and inspectors.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_RESOURCE_LITERAL_H_
#define TIKU_RESOURCE_LITERAL_H_
#include "tiku_resource.h"
/** @brief Parse an explicit scalar type or JSON into an owned typed value.
 * @note JSON preserves counter/enum/color with exact $type/$value objects.
 * Duplicate members, trailing data and protocol bounds are rejected. */
tiku_resource_error_t tiku_resource_literal_parse(const char *type,
                                                  const char *text,
                                                  tiku_msg_t **out);
/** @brief Render a lossless JSON literal, including a newline, without truncation. */
int tiku_resource_literal_format(const tiku_msg_t *, void *, size_t);
/** @brief Render ordinary JSON, quoting root strings, without truncation. */
int tiku_resource_value_json(const tiku_msg_t *, void *, size_t);
#endif
