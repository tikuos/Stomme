/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_strings.h - the words the interface is made of.
 *
 * A key such as "menu.about" names a line the interface shows.  The
 * English lines are compiled in from assets/strings/en.xml; a translation
 * is read at run time from <lang>.xml, and any line it lacks stays in
 * English.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_STRINGS_H_
#define TIKU_STRINGS_H_

/**
 * @brief The line named @p key, in the language in force.
 *
 * Never NULL: a key with no translation gives the baked English, a key
 * with no English either gives the key itself, and a NULL key gives "".
 */
const char *tiku_str(const char *key);

/**
 * @brief Forget any translation, then load <lang>.xml for @p lang from
 *        the first place that has it: @p dir, $TIKU_STRINGS,
 *        ~/.config/tracker/strings, /usr/share/tiku-tracker/strings.
 *
 * @param dir  Where the .xml files are; NULL or "" for the places after.
 * @param lang The language, e.g. "fr" or "fr_FR.UTF-8"; NULL or "" reads
 *             it from $TIKU_LANG, else $LANG.  "en", "C" and "POSIX"
 *             load nothing.
 * @return the language in force ("en" when nothing was loaded).
 */
const char *tiku_strings_init(const char *dir, const char *lang);

/**
 * @brief Read the .xml catalogue at @p path over the loaded lines; a line
 *        replaces the one with its key, unless its printf conversions
 *        differ from the English line's.
 * @return the lines the file held, the refused ones counted too; 0 when
 *         it could not be read.
 */
int tiku_strings_load_file(const char *path);

/** @brief Forget any loaded translation, back to the baked English. */
void tiku_strings_reset(void);

/** @brief How many lines the baked English catalogue holds. */
int tiku_strings_builtin_count(void);

#endif /* TIKU_STRINGS_H_ */
