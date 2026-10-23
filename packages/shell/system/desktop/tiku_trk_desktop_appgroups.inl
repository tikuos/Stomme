/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_desktop_appgroups.inl - the Deskbar group an installed
 * application's manifest category puts it in.  Included by
 * tiku_trk_desktop_apps.c and by tests/system/test_app_groups.c.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @brief The Deskbar group for manifest category @p category: device tools,
 *        programming (or development), documents, graphics, demos, else
 *        applications.  The category places the row only; it does not
 *        change how the application runs.
 */
static int installed_group(const char *category)
{
    if (!strcmp(category, "device-tools"))
        return TIKU_TRK_DB_GROUP_DEVICE_TOOLS;
    if (!strcmp(category, "programming") || !strcmp(category, "development"))
        return TIKU_TRK_DB_GROUP_PROGRAMMING;
    if (!strcmp(category, "documents")) return TIKU_TRK_DB_GROUP_DOCUMENTS;
    if (!strcmp(category, "graphics")) return TIKU_TRK_DB_GROUP_GRAPHICS;
    if (!strcmp(category, "demos")) return TIKU_TRK_DB_GROUP_DEMOS;
    return TIKU_TRK_DB_GROUP_APPS;
}
