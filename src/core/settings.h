/*
 * SeedSigner's settings (models/settings_definition.py), with its defaults,
 * held in RAM. Persistence is off by default there and absent here until the
 * vault (Phase 5); the Settings screens come with T41.
 *
 * Only the settings the ported flows read are here; each is named after
 * SeedSigner's SETTING__ constant.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "btc.h"
#include "qr-encode.h"
#include "widgets.h"

typedef enum { OPT_DISABLED, OPT_ENABLED, OPT_REQUIRED } setting_opt_t;

/* SeedSigner's ALL_SCRIPT_TYPES, in its order. */
typedef enum { SCRIPT_NAT, SCRIPT_NES, SCRIPT_LEG, SCRIPT_TR, SCRIPT_CUSTOM, SCRIPT_COUNT } script_opt_t;
/* ALL_XPUB_QR_FORMATS, in its order. */
typedef enum { XPUB_QR_UR, XPUB_QR_STATIC, XPUB_QR_SPECTER, XPUB_QR_COUNT } xpub_qr_t;

typedef struct {
    btc_denom_t btc_denomination;       /* THRESHOLD */
    btc_net_t network;                  /* MAINNET */
    qr_density_t qr_density;            /* MEDIUM */
    bool sig_single, sig_multi;         /* both */
    bool script_types[SCRIPT_COUNT];    /* native, nested, taproot */
    bool xpub_qr_format[XPUB_QR_COUNT]; /* UR crypto-account, static */
    bool xpub_details;                  /* enabled */
    setting_opt_t passphrase;           /* enabled */
    bool compact_seedqr;                /* enabled */
    bool bip85_child_seeds;             /* disabled */
    bool electrum_seeds;                /* disabled */
    bool message_signing;               /* disabled */
    bool privacy_warnings;              /* enabled */
    bool dire_warnings;                 /* enabled */
    bool qr_brightness_tips;            /* enabled */
    /* Kept so the table and SettingsQR codes made for SeedSigner work in
       full. Some describe SeedSigner's hardware and change nothing here
       yet: camera rotation (T9), the microSD notification (no card),
       display type and colour inversion (this board's panel is chosen at
       build time). */
    uint8_t camera_rotation;            /* index into 0, 90, 180, 270: 180 */
    uint8_t microsd_toast_timer;        /* index into D, E, inf: E */
    bool partner_logos;                 /* enabled */
    uint8_t display_config;             /* index: st7789_240x240 */
    bool color_inverted;                /* disabled */
    int qr_background_color;            /* 62 (hidden, free entry) */
} settings_t;

extern settings_t SETTINGS;

/* SeedSigner's defaults. */
void settings_reset(void);

/* ---- SeedSigner's SettingsDefinition ---------------------------------- */

typedef enum {
    STYPE_ENABLED_DISABLED, STYPE_SELECT_1, STYPE_MULTISELECT, STYPE_FREE_ENTRY,
} setting_type_t;

typedef enum {
    SVIS_GENERAL, SVIS_ADVANCED, SVIS_HARDWARE, SVIS_HIDDEN, SVIS_DEVELOPER,
} setting_vis_t;

typedef struct {
    const char *value;          /* as SettingsQR writes it: "M", "nat", "180" */
    const char *display_name;
} setting_option_t;

typedef struct {
    const char *category, *attr_name, *abbreviated_name, *display_name, *help_text;
    setting_vis_t visibility;
    setting_type_t type;
    const setting_option_t *options;
    int n_options;
} setting_def_t;

/* Every SettingsEntry, in SeedSigner's order, as SeedSigner OS has them
 * with no microSD card (Persistent settings offers only Disabled: this
 * device has no card, and the vault that will persist settings is
 * Phase 5). */
extern const setting_def_t SETTINGS_DEF[];
extern const int SETTINGS_DEF_COUNT;

int setting_by_attr(const char *attr_name);         /* -1 if none */
int setting_by_abbreviated(const char *abbreviated); /* -1 if none */
const char *setting_type_name(setting_type_t t);     /* "select_1"... */
const char *setting_vis_name(setting_vis_t v);       /* "general"... */

/* A selection setting's value as a mask of its options (one bit for the
 * single selects), read from and written to SETTINGS. */
uint32_t settings_get_mask(int id);
void settings_set_mask(int id, uint32_t mask);
/* A free-entry setting's value. */
int settings_get_int(int id);
void settings_set_int(int id, int value);
/* The value as SeedSigner prints it: options in order, comma-joined. */
void settings_value_text(int id, char *out, size_t cap);

/* ---- SettingsQR: Settings.parse_settingsqr, then update() ------------- */

#define SETTINGSQR_MAX 32

typedef struct {
    char name[64];              /* the config name, "" if none */
    bool has_name;
    int n;
    struct {
        int id;
        uint32_t mask;          /* the options named */
        int value;              /* a free entry's */
        char text[64];          /* the value as SeedSigner's parse returns it */
    } u[SETTINGSQR_MAX];
} settingsqr_t;

/* Parse as SeedSigner does, quirks included. NULL on success, else the
 * Python exception class SeedSigner raises ("InvalidSettingsQRData",
 * "IndexError", "ValueError"); nothing is applied either way. */
const char *settingsqr_parse(const char *data, settingsqr_t *out);
void settingsqr_apply(const settingsqr_t *q);

/* Display names, as SeedSigner's settings list them. */
const char *script_type_name(script_opt_t t);
const char *xpub_qr_format_name(xpub_qr_t f);
