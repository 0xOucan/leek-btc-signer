/*
 * SeedSigner's PowerOptionsView, RestartView and PowerOffView (views/view.py),
 * ported. See views-power.c.
 */
#pragma once

/* MainMenuView's power button: here, BACK on the main menu. */
void PowerOptionsView(void);

/* The platform's restart (esp_restart on the board). Without one, the
 * restart screen stays up. */
void ui_set_restart_hook(void (*fn)(void));
