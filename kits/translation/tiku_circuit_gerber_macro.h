/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_circuit_gerber_macro.h - what macro instantiation and the macro
 * primitives share: building one primitive into a macro's own scene.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_CIRCUIT_GERBER_MACRO_H_
#define TIKU_CIRCUIT_GERBER_MACRO_H_

#include "tiku_circuit_gerber_private.h"

/**
 * @brief Add primitive @p code, with its @p n evaluated parameters @p p in
 *        file units, to the macro scene @p doc.  One that covers nothing
 *        (a zero size, a zero-length line, an outline with no area) adds
 *        nothing.
 * @return 1, or 0 with the failure recorded in @p g.
 */
int gerber_macro_primitive(circuit_gerber_t *g, tiku_circuit_t *doc,
                           int code, const double *p, size_t n);

#endif /* TIKU_CIRCUIT_GERBER_MACRO_H_ */
