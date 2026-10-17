/*
 * SeedSigner's settings views (views/settings_views.py), ported. See
 * views-settings.c.
 */
#pragma once

#include <stddef.h>

#include "ui.h"

/* SettingsMenuView, at its general level. */
extern const screen_t SCREEN_SETTINGS;

/* A scan completed with a SettingsQR: SettingsIngestSettingsQRView, in the
 * scan screen's place. */
void settings_scanned(const char *text, size_t len);

/* SettingsEntryUpdateSelectionView for `attr_name` ("network"), opened
 * with clear_history: the settings menus that lead to it are under it. */
void settings_open_entry(const char *attr_name);

