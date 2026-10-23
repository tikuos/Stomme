/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richcmd_catalog.c - discoverable document command metadata.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richcmd_private.h"

/** @brief Define a catalog entry with a typed parameter and selection scope. */
#define ENTRY(id, key, label, parameter, scope) \
    {TIKU_RICHCMD_##id, key, label, TIKU_RICHCMD_##parameter##_PARAMETER, \
     TIKU_RICHCMD_##scope##_SCOPE}

static const tiku_richcmd_info_t catalog[] = {
    ENTRY(REPLACE_TEXT, "text.replace", "Replace Text", TEXT, RANGE),
    ENTRY(DELETE, "text.delete", "Delete Selection", NO, RANGE),
    ENTRY(PARAGRAPH_BREAK, "paragraph.break", "Paragraph Break", NO, RANGE),
    ENTRY(PASTE_FRAGMENT, "fragment.paste", "Paste Styled Content",
          FRAGMENT, RANGE),
    ENTRY(UNDO, "history.undo", "Undo", NO, DOCUMENT),
    ENTRY(REDO, "history.redo", "Redo", NO, DOCUMENT),
    ENTRY(BOLD, "character.bold", "Bold", NO, CHARACTER),
    ENTRY(ITALIC, "character.italic", "Italic", NO, CHARACTER),
    ENTRY(UNDERLINE, "character.underline", "Underline", NO, CHARACTER),
    ENTRY(STRIKE, "character.strike", "Strikethrough", NO, CHARACTER),
    ENTRY(CHARACTER_FORMAT, "character.format", "Character Formatting",
          CHARACTER, CHARACTER),
    ENTRY(CLEAR_CHARACTER_OVERRIDES, "character.clear",
          "Clear Character Overrides", NO, CHARACTER),
    ENTRY(ALIGN_LEFT, "paragraph.left", "Align Left", NO, PARAGRAPH),
    ENTRY(ALIGN_CENTER, "paragraph.center", "Align Center", NO, PARAGRAPH),
    ENTRY(ALIGN_RIGHT, "paragraph.right", "Align Right", NO, PARAGRAPH),
    ENTRY(ALIGN_JUSTIFY, "paragraph.justify", "Justify", NO, PARAGRAPH),
    ENTRY(PARAGRAPH_FORMAT, "paragraph.format", "Paragraph Formatting",
          PARAGRAPH, PARAGRAPH),
    ENTRY(PARAGRAPH_STYLE, "paragraph.style", "Paragraph Style",
          STYLE, PARAGRAPH),
    ENTRY(CHARACTER_STYLE, "character.style", "Character Style",
          STYLE, CHARACTER),
    ENTRY(LIST_SET, "paragraph.list", "List Settings", LIST, PARAGRAPH),
    ENTRY(LINK_SET, "link.set", "Set Link", LINK, CHARACTER),
    ENTRY(LINK_REMOVE, "link.remove", "Remove Link", NO, CHARACTER),
    ENTRY(STYLE_DEFINE, "style.define", "Define Style", DEFINITION, DOCUMENT),
    ENTRY(STYLE_REMOVE, "style.remove", "Remove Style", STYLE, DOCUMENT),
    ENTRY(IMAGE_INSERT, "image.insert", "Insert Picture", IMAGE, RANGE),
    ENTRY(IMAGE_SET, "image.set", "Picture Properties", IMAGE, CHARACTER),
    ENTRY(TABLE_INSERT, "table.insert", "Insert Table", TABLE, PARAGRAPH),
    ENTRY(TABLE_REMOVE, "table.remove", "Delete Table", NO, PARAGRAPH),
    ENTRY(ROW_BEFORE, "table.row.before", "Insert Row Above", NO, PARAGRAPH),
    ENTRY(ROW_AFTER, "table.row.after", "Insert Row Below", NO, PARAGRAPH),
    ENTRY(ROW_REMOVE, "table.row.remove", "Delete Row", NO, PARAGRAPH),
    ENTRY(COLUMN_BEFORE, "table.column.before", "Insert Column Left",
          NO, PARAGRAPH),
    ENTRY(COLUMN_AFTER, "table.column.after", "Insert Column Right",
          NO, PARAGRAPH),
    ENTRY(COLUMN_REMOVE, "table.column.remove", "Delete Column", NO, PARAGRAPH),
    ENTRY(TABLE_SET, "table.set", "Table Properties", TABLE, PARAGRAPH),
    ENTRY(CELL_SET, "cell.set", "Cell Shading", COLOR, PARAGRAPH)
};

#undef ENTRY

size_t
tiku_richcmd_count(void)
{
    return sizeof catalog / sizeof catalog[0];
}

const tiku_richcmd_info_t *
tiku_richcmd_at(size_t index)
{
    return index < tiku_richcmd_count() ? &catalog[index] : NULL;
}

const tiku_richcmd_info_t *
tiku_richcmd_info(tiku_richcmd_kind_t kind)
{
    size_t i;

    for (i = 0; i < tiku_richcmd_count(); i++)
        if (catalog[i].kind == kind)
            return &catalog[i];
    return NULL;
}

unsigned
tiku_rc_toggle_bit(tiku_richcmd_kind_t kind)
{
    switch (kind) {
    case TIKU_RICHCMD_BOLD: return TIKU_RICH_BOLD;
    case TIKU_RICHCMD_ITALIC: return TIKU_RICH_ITALIC;
    case TIKU_RICHCMD_UNDERLINE: return TIKU_RICH_UNDERLINE;
    case TIKU_RICHCMD_STRIKE: return TIKU_RICH_STRIKE;
    default: return 0;
    }
}

tiku_rich_align_t
tiku_rc_alignment(tiku_richcmd_kind_t kind)
{
    switch (kind) {
    case TIKU_RICHCMD_ALIGN_CENTER: return TIKU_RICH_CENTER;
    case TIKU_RICHCMD_ALIGN_RIGHT: return TIKU_RICH_RIGHT;
    case TIKU_RICHCMD_ALIGN_JUSTIFY: return TIKU_RICH_JUSTIFY;
    default: return TIKU_RICH_LEFT;
    }
}
