/*
 * Screens every flow uses. See screens-common.c.
 */
#pragma once

#include "qr-encode.h"
#include "ui.h"

/* SeedSigner's LargeIconStatusScreen family. */
typedef enum {
    NOTICE_INFO,
    NOTICE_SUCCESS,
    NOTICE_WARNING,     /* WarningScreen: "Caution", "I understand" */
    NOTICE_DIRE,        /* DireWarningScreen */
    NOTICE_ERROR,       /* ErrorScreen */
} notice_kind_t;

typedef struct {
    notice_kind_t kind;
    const char *title;      /* NULL: the kind's own ("Caution", "Error"...) */
    const char *headline;   /* the line under the icon, in the status colour */
    const char *body;       /* pages with UP and DOWN when it is long */
    const char *button;     /* NULL: "I understand" for warnings, else "OK" */
    void (*on_ok)(void);    /* after the screen has closed; NULL: just close */
} notice_t;

/* Push a status screen. The strings are copied. */
void ui_notice(const notice_t *n);
/* Lines the current notice's body wrapped to (for tests). */
int ui_notice_lines(void);

/* Full-screen QR (SeedSigner's QRDisplayScreen): animated codes advance on
 * the screen's tick, UP and DOWN set the brightness, OK and BACK leave.
 * `on_leave` runs as it closes, for the caller to wipe the encoder. */
#define QR_BRIGHTNESS_MAX 4
void ui_show_qr(qr_encoder_t *enc, void (*on_leave)(void));

/* Draw a "Working..." frame and push it to the panel now, before a slow
 * step that blocks the UI loop. */
void ui_working(const char *text);

/* Platform hooks: put the framebuffer on the panel; set the panel's
 * brightness, 0 to QR_BRIGHTNESS_MAX. */
void ui_set_flush_hook(void (*fn)(void));
void ui_set_brightness_hook(void (*fn)(int level));
