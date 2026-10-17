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
} settings_t;

extern settings_t SETTINGS;

/* SeedSigner's defaults. */
void settings_reset(void);

/* Display names, as SeedSigner's settings list them. */
const char *script_type_name(script_opt_t t);
const char *xpub_qr_format_name(xpub_qr_t f);
