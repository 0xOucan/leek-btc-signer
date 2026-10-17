/* SeedSigner's settings in RAM. See settings.h. */
#include "settings.h"

#include <string.h>

settings_t SETTINGS;

void settings_reset(void)
{
    memset(&SETTINGS, 0, sizeof SETTINGS);
    SETTINGS.btc_denomination = BTC_DENOM_THRESHOLD;
    SETTINGS.network = BTC_NET_MAIN;
    SETTINGS.qr_density = QR_DENSITY_MEDIUM;
    SETTINGS.sig_single = SETTINGS.sig_multi = true;
    SETTINGS.script_types[SCRIPT_NAT] = true;
    SETTINGS.script_types[SCRIPT_NES] = true;
    SETTINGS.script_types[SCRIPT_TR] = true;
    SETTINGS.xpub_qr_format[XPUB_QR_UR] = true;
    SETTINGS.xpub_qr_format[XPUB_QR_STATIC] = true;
    SETTINGS.xpub_details = true;
    SETTINGS.passphrase = OPT_ENABLED;
    SETTINGS.compact_seedqr = true;
    SETTINGS.privacy_warnings = true;
    SETTINGS.dire_warnings = true;
    SETTINGS.qr_brightness_tips = true;
}

const char *script_type_name(script_opt_t t)
{
    static const char *const N[SCRIPT_COUNT] = {
        "Native Segwit", "Nested Segwit", "Legacy", "Taproot", "Custom Derivation",
    };
    return t < SCRIPT_COUNT ? N[t] : "?";
}

const char *xpub_qr_format_name(xpub_qr_t f)
{
    static const char *const N[XPUB_QR_COUNT] = { "Animated (default)", "Static", "Specter legacy" };
    return f < XPUB_QR_COUNT ? N[f] : "?";
}
