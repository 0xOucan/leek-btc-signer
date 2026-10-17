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
