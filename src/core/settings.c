/* SeedSigner's settings in RAM. See settings.h. */
#include "settings.h"

#include <stdio.h>
#include <stdlib.h>
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
    SETTINGS.camera_rotation = 2;       /* 180 */
    SETTINGS.microsd_toast_timer = 1;   /* E: 5 seconds */
    SETTINGS.partner_logos = true;
    SETTINGS.display_config = 0;        /* st7789_240x240 */
    SETTINGS.qr_background_color = 62;
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

/* ---- SeedSigner's SettingsDefinition ---------------------------------- */

static const setting_option_t OPT_EN[] = { { "en", "English" } };
static const setting_option_t OPT_ONLY_DISABLED[] = { { "D", "Disabled" } };
static const setting_option_t OPT_ED[] = { { "E", "Enabled" }, { "D", "Disabled" } };
static const setting_option_t OPT_DENOM[] = {
    { "btc", "BTC" }, { "sats", "sats" }, { "thr", "Threshold at 0.01" }, { "hyb", "BTC | sats hybrid" },
};
static const setting_option_t OPT_NETWORK[] = { { "M", "Mainnet" }, { "T", "Testnet" }, { "R", "Regtest" } };
static const setting_option_t OPT_DENSITY[] = { { "L", "Low" }, { "M", "Medium" }, { "H", "High" } };
static const setting_option_t OPT_SIGS[] = { { "ss", "Single Sig" }, { "ms", "Multisig" } };
static const setting_option_t OPT_SCRIPTS[] = {
    { "nat", "Native Segwit" }, { "nes", "Nested Segwit" }, { "leg", "Legacy" }, { "tr", "Taproot" },
    { "cus", "Custom Derivation" },
};
static const setting_option_t OPT_XPUB_QR[] = {
    { "urca", "Animated (default)" }, { "sta", "Static" }, { "spl", "Specter legacy" },
};
static const setting_option_t OPT_EDR[] = { { "E", "Enabled" }, { "D", "Disabled" }, { "R", "Required" } };
/* Integer options in SeedSigner: a SettingsQR value must be digits. */
static const setting_option_t OPT_CAMERA[] = {
    { "0", "0\302\260" }, { "90", "90\302\260" }, { "180", "180\302\260" }, { "270", "270\302\260" },
};
static const setting_option_t OPT_TOAST[] = { { "D", "Disabled" }, { "E", "5 seconds" }, { "inf", "Until SD removed" } };
static const setting_option_t OPT_DISPLAY[] = {
    { "st7789_240x240", "st7789 240x240" }, { "st7789_320x240", "st7789 320x240" },
    { "ili9341_320x240", "ili9341 320x240 (beta)" },
};

#define N_(a) (int)(sizeof(a) / sizeof((a)[0]))
#define ED(cat, attr, abbr, name, vis, help) \
    { cat, attr, abbr, name, help, vis, STYPE_ENABLED_DISABLED, OPT_ED, 2 }

enum {
    S_LOCALE, S_WORDLIST, S_PERSISTENT, S_DENOM, S_NETWORK, S_DENSITY, S_SIGS, S_SCRIPTS, S_XPUB_QR,
    S_XPUB_DETAILS, S_PASSPHRASE, S_CAMERA, S_COMPACT, S_BIP85, S_ELECTRUM, S_TOAST, S_MESSAGE,
    S_PRIVACY, S_DIRE, S_QR_TIPS, S_PARTNERS, S_DISPLAY, S_INVERTED, S_QR_BACKGROUND,
};

const setting_def_t SETTINGS_DEF[] = {
    [S_LOCALE] = { "system", "locale", "lang", "Language", NULL, SVIS_GENERAL, STYPE_SELECT_1, OPT_EN, 1 },
    [S_WORDLIST] = { "system", "wordlist_language", "wordlist_lang", "Mnemonic language", NULL, SVIS_HIDDEN,
                     STYPE_SELECT_1, OPT_EN, 1 },
    [S_PERSISTENT] = { "system", "persistent_settings", "persistent", "Persistent settings",
                       "Insert SD card to enable", SVIS_GENERAL, STYPE_ENABLED_DISABLED, OPT_ONLY_DISABLED, 1 },
    [S_DENOM] = { "system", "denomination", "denom", "Denomination display", NULL, SVIS_GENERAL, STYPE_SELECT_1,
                  OPT_DENOM, N_(OPT_DENOM) },
    [S_NETWORK] = { "features", "network", "network", "Bitcoin network", NULL, SVIS_ADVANCED, STYPE_SELECT_1,
                    OPT_NETWORK, N_(OPT_NETWORK) },
    [S_DENSITY] = { "features", "qr_density", "qr_density", "QR code density", NULL, SVIS_ADVANCED,
                    STYPE_SELECT_1, OPT_DENSITY, N_(OPT_DENSITY) },
    [S_SIGS] = { "features", "sig_types", "sigs", "Sig types", NULL, SVIS_ADVANCED, STYPE_MULTISELECT, OPT_SIGS,
                 N_(OPT_SIGS) },
    [S_SCRIPTS] = { "features", "script_types", "scripts", "Script types", NULL, SVIS_ADVANCED,
                    STYPE_MULTISELECT, OPT_SCRIPTS, N_(OPT_SCRIPTS) },
    [S_XPUB_QR] = { "features", "xpub_qr", "xpub_qr", "Xpub QR format", NULL, SVIS_ADVANCED, STYPE_MULTISELECT,
                    OPT_XPUB_QR, N_(OPT_XPUB_QR) },
    [S_XPUB_DETAILS] = ED("features", "xpub_details", "xpub_details", "Show xpub details", SVIS_ADVANCED, NULL),
    [S_PASSPHRASE] = { "features", "passphrase", "passphrase", "BIP-39 passphrase", NULL, SVIS_ADVANCED,
                       STYPE_SELECT_1, OPT_EDR, N_(OPT_EDR) },
    [S_CAMERA] = { "features", "camera_rotation", "camera", "Camera rotation", NULL, SVIS_ADVANCED,
                   STYPE_SELECT_1, OPT_CAMERA, N_(OPT_CAMERA) },
    [S_COMPACT] = ED("features", "compact_seedqr", "compact_seedqr", "Compact SeedQR", SVIS_ADVANCED, NULL),
    [S_BIP85] = ED("features", "bip85_child_seeds", "bip85", "BIP-85 child seeds", SVIS_ADVANCED, NULL),
    [S_ELECTRUM] = ED("features", "electrum_seeds", "electrum", "Electrum seeds", SVIS_ADVANCED,
                      "Native Segwit only"),
    [S_TOAST] = { "features", "microsd_toast_timer", "microsd_toast_timer", "MicroSD notification duration",
                  NULL, SVIS_ADVANCED, STYPE_SELECT_1, OPT_TOAST, N_(OPT_TOAST) },
    [S_MESSAGE] = ED("features", "message_signing", "message_signing", "Message signing", SVIS_ADVANCED, NULL),
    [S_PRIVACY] = ED("features", "privacy_warnings", "priv_warn", "Show privacy warnings", SVIS_ADVANCED, NULL),
    [S_DIRE] = ED("features", "dire_warnings", "dire_warn", "Show dire warnings", SVIS_ADVANCED, NULL),
    [S_QR_TIPS] = ED("features", "qr_brightness_tips", "qr_brightness_tips", "Show QR brightness tips",
                     SVIS_ADVANCED, NULL),
    [S_PARTNERS] = ED("features", "partner_logos", "partners", "Show partner logos", SVIS_ADVANCED, NULL),
    [S_DISPLAY] = { "system", "display_config", "disp_conf", "Display type", NULL, SVIS_HARDWARE, STYPE_SELECT_1,
                    OPT_DISPLAY, N_(OPT_DISPLAY) },
    [S_INVERTED] = ED("system", "color_inverted", "rgb_inv", "Invert colors", SVIS_HARDWARE, NULL),
    [S_QR_BACKGROUND] = { "system", "qr_background_color", "qr_brightness", "QR background color", NULL,
                          SVIS_HIDDEN, STYPE_FREE_ENTRY, NULL, 0 },
};
const int SETTINGS_DEF_COUNT = N_(SETTINGS_DEF);

int setting_by_attr(const char *attr_name)
{
    for (int i = 0; i < SETTINGS_DEF_COUNT; i++) {
        if (strcmp(SETTINGS_DEF[i].attr_name, attr_name) == 0) return i;
    }
    return -1;
}

int setting_by_abbreviated(const char *abbreviated)
{
    for (int i = 0; i < SETTINGS_DEF_COUNT; i++) {
        if (strcmp(SETTINGS_DEF[i].abbreviated_name, abbreviated) == 0) return i;
    }
    return -1;
}

const char *setting_type_name(setting_type_t t)
{
    static const char *const N[] = { "enabled_disabled", "select_1", "multiselect", "free_entry" };
    return (unsigned)t < 4 ? N[t] : "?";
}

const char *setting_vis_name(setting_vis_t v)
{
    static const char *const N[] = { "general", "advanced", "hardware", "hidden", "developer" };
    return (unsigned)v < 5 ? N[v] : "?";
}

/* Enabled/Disabled: option 0 is Enabled. */
static uint32_t ed(bool on) { return on ? 1u : 2u; }
static int lowest(uint32_t mask) { int i = 0; while (mask && !(mask & 1u)) { mask >>= 1; i++; } return i; }

static bool *ed_field(int id)
{
    switch (id) {
    case S_XPUB_DETAILS: return &SETTINGS.xpub_details;
    case S_COMPACT:      return &SETTINGS.compact_seedqr;
    case S_BIP85:        return &SETTINGS.bip85_child_seeds;
    case S_ELECTRUM:     return &SETTINGS.electrum_seeds;
    case S_MESSAGE:      return &SETTINGS.message_signing;
    case S_PRIVACY:      return &SETTINGS.privacy_warnings;
    case S_DIRE:         return &SETTINGS.dire_warnings;
    case S_QR_TIPS:      return &SETTINGS.qr_brightness_tips;
    case S_PARTNERS:     return &SETTINGS.partner_logos;
    case S_INVERTED:     return &SETTINGS.color_inverted;
    default:             return NULL;
    }
}

uint32_t settings_get_mask(int id)
{
    bool *b = ed_field(id);
    if (b) return ed(*b);
    uint32_t m = 0;
    switch (id) {
    case S_LOCALE: case S_WORDLIST: case S_PERSISTENT: return 1;
    case S_DENOM:   return 1u << SETTINGS.btc_denomination;
    case S_NETWORK: return 1u << SETTINGS.network;
    case S_DENSITY: return 1u << SETTINGS.qr_density;
    case S_SIGS:    return (SETTINGS.sig_single ? 1u : 0) | (SETTINGS.sig_multi ? 2u : 0);
    case S_SCRIPTS:
        for (int t = 0; t < SCRIPT_COUNT; t++) m |= SETTINGS.script_types[t] ? 1u << t : 0;
        return m;
    case S_XPUB_QR:
        for (int f = 0; f < XPUB_QR_COUNT; f++) m |= SETTINGS.xpub_qr_format[f] ? 1u << f : 0;
        return m;
    case S_PASSPHRASE:
        return SETTINGS.passphrase == OPT_ENABLED ? 1u : SETTINGS.passphrase == OPT_DISABLED ? 2u : 4u;
    case S_CAMERA:  return 1u << SETTINGS.camera_rotation;
    case S_TOAST:   return 1u << SETTINGS.microsd_toast_timer;
    case S_DISPLAY: return 1u << SETTINGS.display_config;
    default:        return 0;
    }
}

void settings_set_mask(int id, uint32_t mask)
{
    if (id < 0 || id >= SETTINGS_DEF_COUNT || SETTINGS_DEF[id].type == STYPE_FREE_ENTRY) {
        return;
    }
    mask &= (1u << SETTINGS_DEF[id].n_options) - 1;
    bool *b = ed_field(id);
    if (b) {
        if (mask) *b = (mask & 1u) != 0;
        return;
    }
    switch (id) {
    case S_DENOM:   if (mask) SETTINGS.btc_denomination = (btc_denom_t)lowest(mask); break;
    case S_NETWORK: if (mask) SETTINGS.network = (btc_net_t)lowest(mask); break;
    case S_DENSITY: if (mask) SETTINGS.qr_density = (qr_density_t)lowest(mask); break;
    case S_SIGS:
        SETTINGS.sig_single = mask & 1u;
        SETTINGS.sig_multi = (mask & 2u) != 0;
        break;
    case S_SCRIPTS:
        for (int t = 0; t < SCRIPT_COUNT; t++) SETTINGS.script_types[t] = (mask >> t) & 1u;
        break;
    case S_XPUB_QR:
        for (int f = 0; f < XPUB_QR_COUNT; f++) SETTINGS.xpub_qr_format[f] = (mask >> f) & 1u;
        break;
    case S_PASSPHRASE:
        if (mask) {
            static const setting_opt_t P[] = { OPT_ENABLED, OPT_DISABLED, OPT_REQUIRED };
            SETTINGS.passphrase = P[lowest(mask)];
        }
        break;
    case S_CAMERA:  if (mask) SETTINGS.camera_rotation = (uint8_t)lowest(mask); break;
    case S_TOAST:   if (mask) SETTINGS.microsd_toast_timer = (uint8_t)lowest(mask); break;
    case S_DISPLAY: if (mask) SETTINGS.display_config = (uint8_t)lowest(mask); break;
    default:        break;      /* locale, wordlist, persistent: one option each */
    }
}

int settings_get_int(int id)
{
    return id == S_QR_BACKGROUND ? SETTINGS.qr_background_color : 0;
}

void settings_set_int(int id, int value)
{
    if (id == S_QR_BACKGROUND) {
        SETTINGS.qr_background_color = value;
    }
}

void settings_value_text(int id, char *out, size_t cap)
{
    if (cap == 0) return;
    out[0] = '\0';
    if (id < 0 || id >= SETTINGS_DEF_COUNT) return;
    const setting_def_t *d = &SETTINGS_DEF[id];
    if (d->type == STYPE_FREE_ENTRY) {
        snprintf(out, cap, "%d", settings_get_int(id));
        return;
    }
    const uint32_t m = settings_get_mask(id);
    size_t k = 0;
    for (int i = 0; i < d->n_options && k < cap; i++) {
        if (m & (1u << i)) {
            k += (size_t)snprintf(out + k, cap - k, "%s%s", k ? "," : "", d->options[i].value);
        }
    }
}

/* ---- SettingsQR ---------------------------------------------------------- */

#define TOKENS_MAX 64

static bool is_space(char c)
{
    return c == ' ' || c == '\n' || c == '\t' || c == '\r' || c == '\f' || c == '\v';
}

/* str.isdigit() for ASCII. */
static bool all_digits(const char *s, size_t n)
{
    if (n == 0) return false;
    for (size_t i = 0; i < n; i++) {
        if (s[i] < '0' || s[i] > '9') return false;
    }
    return true;
}

/* An option whose value is an integer (camera rotation) matches only a value
   that was all digits, compared as numbers; a string option only a value
   that was not. As `v in [opt[0] ...]` with int() applied first. */
static int find_option(const setting_def_t *d, const char *v, size_t n, long *as_int)
{
    const bool num = all_digits(v, n);
    long iv = 0;
    if (num) {
        for (size_t i = 0; i < n && iv < 1000000; i++) iv = iv * 10 + (v[i] - '0');
        *as_int = iv;
    }
    for (int i = 0; i < d->n_options; i++) {
        const char *o = d->options[i].value;
        const bool onum = all_digits(o, strlen(o));
        if (num && onum && strtol(o, NULL, 10) == iv) return i;
        if (!num && !onum && strlen(o) == n && memcmp(o, v, n) == 0) return i;
    }
    return -1;
}

const char *settingsqr_parse(const char *data, settingsqr_t *out)
{
    memset(out, 0, sizeof *out);
    if (strncmp(data, "settings::", 10) != 0) {
        return "InvalidSettingsQRData";
    }
    /* data.split(): whitespace-separated tokens. */
    const char *tok[TOKENS_MAX];
    size_t len[TOKENS_MAX];
    int n = 0;
    for (const char *p = data; *p;) {
        while (*p && is_space(*p)) p++;
        if (!*p) break;
        const char *start = p;
        while (*p && !is_space(*p)) p++;
        if (n == TOKENS_MAX) return "InvalidSettingsQRData";
        tok[n] = start;
        len[n++] = (size_t)(p - start);
    }
    /* version = data.split()[0].split("::")[1] */
    const char *v = tok[0] + 10;
    const char *v_end = v;
    while (v_end < tok[0] + len[0] && !(v_end[0] == ':' && v_end + 1 < tok[0] + len[0] && v_end[1] == ':')) {
        v_end++;
    }
    if (v_end - v != 2 || memcmp(v, "v1", 2) != 0) {
        return "InvalidSettingsQRData";     /* Unsupported SettingsQR version */
    }
    if (n < 2) {
        return "IndexError";                /* data.split()[1] */
    }
    int first = 1;
    /* `"name=" in data.split()[1]`; the name is what follows the first
       "name=" anywhere, to the next whitespace, with "_" read as " ". */
    bool has = false;
    for (size_t i = 0; i + 5 <= len[1]; i++) {
        if (memcmp(tok[1] + i, "name=", 5) == 0) { has = true; break; }
    }
    if (has) {
        const char *p = strstr(data, "name=") + 5;
        while (*p && is_space(*p)) p++;
        if (!*p) return "IndexError";
        size_t k = 0;
        for (; *p && !is_space(*p) && k + 1 < sizeof out->name; p++) {
            out->name[k++] = *p == '_' ? ' ' : *p;
        }
        out->name[k] = '\0';
        out->has_name = true;
        first = 2;
    }
    for (int t = first; t < n; t++) {
        /* abbreviated_name, value = entry.split("=") */
        const char *eq = memchr(tok[t], '=', len[t]);
        if (eq == NULL || memchr(eq + 1, '=', len[t] - (size_t)(eq + 1 - tok[t]))) {
            return "ValueError";
        }
        char abbr[48];
        const size_t alen = (size_t)(eq - tok[t]);
        if (alen >= sizeof abbr) continue;      /* no setting has so long a name */
        memcpy(abbr, tok[t], alen);
        abbr[alen] = '\0';
        const char *val = eq + 1;
        const size_t vlen = len[t] - alen - 1;
        if (vlen == 0) {
            return "InvalidSettingsQRData";     /* cannot be empty */
        }
        const int id = setting_by_abbreviated(abbr);
        if (id < 0) {
            continue;                           /* Ignoring unrecognized attribute */
        }
        const setting_def_t *d = &SETTINGS_DEF[id];
        if (d->options == NULL) {
            /* A free-entry setting has no options; SeedSigner's check
               iterates over None and raises. */
            return "TypeError";
        }
        uint32_t mask = 0;
        char text[64];
        size_t tk = 0;
        text[0] = '\0';
        bool persistent_off = false;
        for (const char *p = val; p <= val + vlen;) {
            const char *e = memchr(p, ',', (size_t)(val + vlen - p));
            if (e == NULL) e = val + vlen;
            long iv = 0;
            const int o = find_option(d, p, (size_t)(e - p), &iv);
            if (o < 0) {
                if (id == S_PERSISTENT && e - p == 1 && *p == 'E') {
                    /* Enabling it with no card: explicitly Disabled. */
                    persistent_off = true;
                    break;
                }
                return "InvalidSettingsQRData";  /* not valid */
            }
            mask |= 1u << o;
            if (all_digits(p, (size_t)(e - p))) {
                tk += (size_t)snprintf(text + tk, sizeof text - tk, "%s%ld", tk ? "," : "", iv);
            } else {
                tk += (size_t)snprintf(text + tk, sizeof text - tk, "%s%.*s", tk ? "," : "", (int)(e - p), p);
            }
            if (tk >= sizeof text) tk = sizeof text - 1;
            p = e + 1;
        }
        if (persistent_off) {
            mask = 1u;
            snprintf(text, sizeof text, "D");
        }
        /* updated_settings[attr] = value: a repeat keeps its place. */
        int slot = -1;
        for (int i = 0; i < out->n; i++) {
            if (out->u[i].id == id) slot = i;
        }
        if (slot < 0) {
            if (out->n == SETTINGSQR_MAX) return "InvalidSettingsQRData";
            slot = out->n++;
        }
        out->u[slot].id = id;
        out->u[slot].mask = mask;
        snprintf(out->u[slot].text, sizeof out->u[slot].text, "%s", text);
    }
    return NULL;
}

void settingsqr_apply(const settingsqr_t *q)
{
    for (int i = 0; i < q->n; i++) {
        settings_set_mask(q->u[i].id, q->u[i].mask);
    }
}
