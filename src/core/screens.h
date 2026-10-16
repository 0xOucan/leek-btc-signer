/*
 * The screen tree. Labels and order follow SeedSigner's MainMenuView,
 * LoadSeedView, ToolsMenuView and SettingsMenuView.
 */
#pragma once

#include "ui.h"

extern const screen_t SCREEN_HOME;

/* Text for the Version screen; set once at boot. */
void screens_set_version(const char *version, const char *board,
                         const char *profile, const char *image);
