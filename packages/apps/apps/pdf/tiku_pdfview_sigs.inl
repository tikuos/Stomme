/* tiku_pdfview_sigs.inl -- part of tiku_pdfview.c: the Signatures column,
 * a card per signature: its verdict, who signed and when, why and where,
 * and what the verdict does and does not say; a click goes to its field.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */

#define PV_SIG_NOTE 4               /* lines a card's note may take      */

/** @brief Signature @p i's verdict as a pill's label, and its tone. */
static const char *pv_sig_pill(const tiku_pdfview_t *c, int i, int *tone)
{
    const tiku_pdf_signature_t *s = &c->sig[i];

    *tone = TIKU_TONE_FAIL;
    if (s->status == TIKU_PDF_SIG_GOOD) {
        /* Integrity checked, identity unverified: the warning tone. */
        *tone = TIKU_TONE_WARN;
        return s->whole || i + 1 < c->nsig ? "Integrity verified"
                                           : "Intact; added to";
    }
    if (s->status == TIKU_PDF_SIG_CHANGED) return "Document changed";
    if (s->status == TIKU_PDF_SIG_BROKEN) return "Not valid";
    *tone = TIKU_TONE_IDLE;
    return "Not checked";
}

/** @brief What signature @p i's verdict says, and what it does not: that
 *         its certificate's owner is who it names is not checked. */
static void pv_sig_note(const tiku_pdfview_t *c, int i, char *out, size_t cap)
{
    const tiku_pdf_signature_t *s = &c->sig[i];
    const char *what = s->why;
    char who[240];

    if (s->status == TIKU_PDF_SIG_GOOD)
        what = s->whole ? "Nothing has changed since it was signed." :
               i + 1 < c->nsig ? "What it signs is intact; more was signed "
                                 "after it." :
               "What it signs is intact; the document was added to after.";
    else if (s->status == TIKU_PDF_SIG_CHANGED)
        what = "What it signs has been changed since it was signed.";
    if (s->self_signed || !s->issuer)
        snprintf(who, sizeof who, "Signer identity unverified. This "
                 "certificate is self-signed or has no known issuer.");
    else
        snprintf(who, sizeof who, "Signer identity unverified. Certificate "
                 "trust is not checked (issuer: %.100s).", s->issuer);
    snprintf(out, cap, "%s%s %s", what, what == s->why ? "." : "", who);
    if (what == s->why && out[0] >= 'a' && out[0] <= 'z') out[0] -= 32;
}

/** @brief @p t as "2026-09-28 12:34 UTC", to the minute, onto @p out. */
static void pv_sig_time(int64_t t, char *out, size_t cap)
{
    char full[40];

    tiku_der_time_text(t, full, sizeof full);
    snprintf(out, cap, "%.16s UTC", full);
}

/** @brief Card @p i's head at @p y in column @p r: its verdict, and who
 *         signed; drawn on @p s, or measured when it is NULL.
 *         @return its height. */
static int pv_sig_head(tiku_pdfview_t *c, tiku_surface_t *s, tiku_rect_t r,
                       int i, int y)
{
    static const char *const kinds[] = { "Signed by", "Certified by",
                                         "Timestamped by" };
    const tiku_pdf_signature_t *g = &c->sig[i];
    int rh = tiku_measure_row_h(), tone;
    const char *pill = pv_sig_pill(c, i, &tone);

    if (s) {
        tiku_ui_pill(s, (tiku_rect_t){r.x + 8, y + 2, tiku_ui_pill_w(pill),
                                      rh}, pill, tone);
        tiku_text_line_draw(s, (tiku_rect_t){r.x + 8, y + rh + 6, r.w - 16,
                            rh}, TIKU_MEASURE_PLAIN, kinds[g->kind % 3],
                            tiku_ink(TIKU_INK_FAINT));
        tiku_text_line_draw(s, (tiku_rect_t){r.x + 8, y + 2 * rh + 6,
                            r.w - 16, rh}, TIKU_MEASURE_BOLD,
                            g->signer ? g->signer : "an unnamed party",
                            TIKU_C_TEXT);
    }
    return 3 * rh + 6;
}

/** @brief Card @p i's facts at @p y (organisation, when, why, where and
 *         whose timestamp), then its note, drawn or measured.
 *         @return their height. */
static int pv_sig_body(tiku_pdfview_t *c, tiku_surface_t *s, tiku_rect_t r,
                       int i, int y)
{
    const tiku_pdf_signature_t *g = &c->sig[i];
    const char *line[5];
    char when[64] = "", by[200] = "", note[400];
    int rh = tiku_measure_row_h(), top = y;

    if (g->stamp == TIKU_PDF_SIG_GOOD && g->stamped_at)
        pv_sig_time(g->stamped_at, when, sizeof when);
    else if (g->signed_at)
        pv_sig_time(g->signed_at, when, sizeof when);
    if (g->stamp == TIKU_PDF_SIG_GOOD && g->stamp_by &&
        g->kind != TIKU_PDF_SIG_TIMESTAMP)
        snprintf(by, sizeof by, "Timestamp by %s", g->stamp_by);
    line[0] = g->org;
    line[1] = when;
    line[2] = g->reason;
    line[3] = g->location;
    line[4] = by;
    for (int k = 0; k < 5; k++) {
        if (!line[k] || !line[k][0]) continue;
        if (s) tiku_text_line_draw(s, (tiku_rect_t){r.x + 8, y, r.w - 16, rh},
                                   TIKU_MEASURE_PLAIN, line[k], TIKU_C_TEXT);
        y += rh;
    }
    pv_sig_note(c, i, note, sizeof note);
    if (s) tiku_text_block_draw(s, (tiku_rect_t){r.x + 8, y + 4, r.w - 16,
                                PV_SIG_NOTE * (rh + 2)}, TIKU_MEASURE_PLAIN,
                                note, tiku_ink(TIKU_INK_FAINT), 2);
    return y - top + 4 + PV_SIG_NOTE * (rh + 2);
}

/** @brief Signature @p i's card at @p y in column @p r, a rule under it:
 *         drawn on @p s, or measured when it is NULL.  @return its
 *         height. */
static int pv_sig_card(tiku_pdfview_t *c, tiku_surface_t *s, tiku_rect_t r,
                       int i, int y)
{
    int h = pv_sig_head(c, s, r, i, y);

    h += pv_sig_body(c, s, r, i, y + h) + 10;
    if (s) tiku_fill(s, (tiku_rect_t){r.x + 6, y + h - 5, r.w - 12, 1},
                     tiku_tint(TIKU_C_PANEL, TIKU_DARKEN_4));
    return h;
}

/** @brief The column: every card, scrolled by sig_top. */
static void pv_sigs_draw(tiku_pdfview_t *c, tiku_surface_t *s, tiku_rect_t r)
{
    tiku_rect_t clip = s->clip;
    int y = r.y + 8 - c->sig_top, total = 0;

    for (int i = 0; i < c->nsig; i++) total += pv_sig_card(c, NULL, r, i, 0);
    if (c->sig_top > total - r.h + 16) c->sig_top = total - r.h + 16;
    if (c->sig_top < 0) c->sig_top = 0;
    y = r.y + 8 - c->sig_top;
    tiku_ui_sunken(s, r, TIKU_C_DOC);
    tiku_clip_set(s, r);
    for (int i = 0; i < c->nsig && y < r.y + r.h; i++)
        y += pv_sig_card(c, s, r, i, y);
    s->clip = clip;
}

/** @brief The wheel scrolls the column; a click on a card goes to its
 *         field, when it shows on a page. */
static int pv_sigs_pointer(tiku_pdfview_t *c, const tiku_event_t *ev,
                           const pv_layout_t *l)
{
    int y = l->side.y + 8 - c->sig_top, h;

    if (ev->type == TIKU_EVENT_WHEEL) {
        c->sig_top += ev->dy * PV_LINE;
        pv_changed(c);
        return 1;
    }
    if (ev->type != TIKU_EVENT_POINTER_DOWN || ev->button != 1) return 1;
    for (int i = 0; i < c->nsig; i++, y += h) {
        const tiku_pdf_signature_t *g = &c->sig[i];
        float box[4] = { g->x0, g->y0, g->x1, g->y1 };
        double at[4];

        h = pv_sig_card(c, NULL, l->side, i, 0);
        if (ev->y < y || ev->y >= y + h || g->page < 0 ||
            g->page >= c->npage) continue;
        pv_turn_box(c, g->page, box, at);
        pv_go(c, g->page, g->x1 > g->x0 ? fmax(at[1] - 24, 0) : 0, 1);
        break;
    }
    pv_changed(c);
    return 1;
}

/** @brief Keys for the column: up and down scroll it, Home to its top. */
static int pv_sigs_key(tiku_pdfview_t *c, const tiku_event_t *ev)
{
    if (ev->key == TIKU_KEY_UP) c->sig_top -= PV_LINE;
    else if (ev->key == TIKU_KEY_DOWN) c->sig_top += PV_LINE;
    else if (ev->key == TIKU_KEY_HOME) c->sig_top = 0;
    else return 0;
    pv_changed(c);
    return 1;
}

/** @brief The signature fields on page @p p, while the column is open,
 *         tinted by their verdicts where the page is at @p x, @p y. */
static void pv_sig_marks(tiku_pdfview_t *c, int p, int x, int y)
{
    for (int i = 0; c->side && c->sigs && i < c->nsig; i++) {
        const tiku_pdf_signature_t *g = &c->sig[i];
        float box[4] = { g->x0, g->y0, g->x1, g->y1 };
        int tone;

        if (g->page != p || g->x1 <= g->x0) continue;
        (void)pv_sig_pill(c, i, &tone);
        pv_tint_box(c, p, x, y, box, tone == TIKU_TONE_PASS ? 0x2830a050u :
                    tone == TIKU_TONE_WARN ? 0x30e0a020u : 0x30e03030u);
    }
}
