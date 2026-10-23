/* tiku_calc_model.inl -- part of tiku_calc.c: the arithmetic of a plain
 * calculator, an entry, a pending operation and a result, driven one key
 * at a time.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */

/** @brief Write @p v with twelve significant digits and an unsigned zero;
 *         "Error" past 1e300 or for no number. */
static void calc_show(double v, char *out, size_t max)
{
    if (v != v || v > 1e300 || v < -1e300) { snprintf(out, max, "Error"); return; }
    if (v == 0) v = 0;                      /* no minus on a zero */
    snprintf(out, max, "%.12g", v);
}

/** @brief Fold the pending operation into the accumulator with the
 *         entry; a division by zero or an overflow sets the error. */
static void calc_apply(calc_model_t *m)
{
    double rhs = strtod(m->entry, NULL);

    switch (m->op) {
    case '+': m->acc += rhs; break;
    case '-': m->acc -= rhs; break;
    case '*': m->acc *= rhs; break;
    case '/':
        if (rhs == 0) { m->error = 1; return; }
        m->acc /= rhs;
        break;
    default:  m->acc = rhs; break;
    }
    if (m->acc != m->acc || m->acc > 1e300 || m->acc < -1e300) m->error = 1;
}

/** @brief Start over, as the C key does. */
static void calc_clear(calc_model_t *m)
{
    memset(m, 0, sizeof *m);
    snprintf(m->entry, sizeof m->entry, "0");
    m->fresh = 1;
}

/**
 * @brief One key: a digit, a point, an operator, equals, C, backspace (b),
 *        negate (n) or percent (%).  Operators apply left to right,
 *        without precedence.  After an error only C answers.
 */
static void calc_key(calc_model_t *m, char key)
{
    size_t n = strlen(m->entry);

    if (key == 'c') { calc_clear(m); return; }
    if (m->error) return;
    if ((key >= '0' && key <= '9') || key == '.') {
        if (m->fresh) { snprintf(m->entry, sizeof m->entry, "0"); n = 1; m->fresh = 0; }
        if (key == '.' && strchr(m->entry, '.')) return;
        if (n >= 15) return;            /* what the display holds */
        if (key != '.' && n == 1 && m->entry[0] == '0') n = 0;
        m->entry[n] = key; m->entry[n + 1] = 0;
        return;
    }
    if (key == 'b') {
        if (m->fresh) return;
        if (n > 1 && !(n == 2 && m->entry[0] == '-')) m->entry[n - 1] = 0;
        else snprintf(m->entry, sizeof m->entry, "0");
        return;
    }
    if (key == 'n') {
        double v = -strtod(m->entry, NULL);
        calc_show(v, m->entry, sizeof m->entry); m->fresh = 0;
        return;
    }
    if (key == '%') {
        double v = strtod(m->entry, NULL) / 100.0;
        calc_show(v, m->entry, sizeof m->entry); m->fresh = 0;
        return;
    }
    if (key == '+' || key == '-' || key == '*' || key == '/' || key == '=') {
        /* An operator pressed straight after another replaces it. */
        if (!(m->fresh && m->op && key != '=')) calc_apply(m);
        calc_show(m->acc, m->entry, sizeof m->entry);
        m->op = (key == '=') ? 0 : key;
        m->fresh = 1;
        if (m->error) snprintf(m->entry, sizeof m->entry, "Error");
    }
}
