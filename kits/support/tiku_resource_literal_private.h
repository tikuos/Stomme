/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_resource_literal_private.h - locale-independent numeric conversion.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_RESOURCE_LITERAL_PRIVATE_H_
#define TIKU_RESOURCE_LITERAL_PRIVATE_H_
/** @brief Decode one complete finite number under the thread's C numeric locale. */
int tiku_resource_number_decode(const char *, double *);
#endif
