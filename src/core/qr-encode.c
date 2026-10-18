/* SeedSigner's QR encoders. See qr-encode.h. */

#include "qr-encode.h"

#include <stdio.h>
#include <string.h>

#include "cbor.h"
#include "wally_core.h"

/* SeedSigner's density settings. */
static size_t ur_fragment(qr_density_t d)
{
    return d == QR_DENSITY_LOW ? 10 : d == QR_DENSITY_HIGH ? 120 : 30;
}

static size_t specter_fragment(qr_density_t d)
{
    return d == QR_DENSITY_LOW ? 40 : d == QR_DENSITY_HIGH ? 90 : 65;
}

static void start(qr_encoder_t *e, qr_out_kind_t kind, uint8_t *buf, size_t cap)
{
    memset(e, 0, sizeof *e);
    e->kind = kind;
    e->buf = buf;
    e->cap = cap;
}

bool qr_encode_seedqr(qr_encoder_t *e, const seed_t *s, uint8_t *buf, size_t cap)
{
    start(e, QR_OUT_STATIC, buf, cap);
    if (!seedqr_encode_standard(s, (char *)buf, cap)) {
        return false;
    }
    e->len = strlen((const char *)buf);
    return true;
}

bool qr_encode_compact_seedqr(qr_encoder_t *e, const seed_t *s, uint8_t *buf, size_t cap)
{
    start(e, QR_OUT_STATIC, buf, cap);
    uint8_t ent[32];
    const size_t n = seedqr_encode_compact(s, ent);
    const bool ok = n > 0 && n <= cap;
    if (ok) {
        memcpy(buf, ent, n);
        e->len = n;
    }
    wally_bzero(ent, sizeof ent);
    return ok;
}

bool qr_encode_text(qr_encoder_t *e, const char *text, uint8_t *buf, size_t cap)
{
    start(e, QR_OUT_STATIC, buf, cap);
    const size_t n = strlen(text);
    if (n >= cap) {
        return false;
    }
    memcpy(buf, text, n + 1);
    e->len = n;
    return true;
}

bool qr_encode_xpub(qr_encoder_t *e, const seed_t *s, const char *path, btc_net_t net, btc_sig_t sig,
                    uint8_t *buf, size_t cap)
{
    start(e, QR_OUT_STATIC, buf, cap);
    if (!btc_xpub_export(s, path, net, sig, (char *)buf, cap)) {
        return false;
    }
    e->len = strlen((const char *)buf);
    return true;
}

bool qr_encode_xpub_specter(qr_encoder_t *e, const seed_t *s, const char *path, btc_net_t net, btc_sig_t sig,
                            qr_density_t density, uint8_t *buf, size_t cap)
{
    if (!qr_encode_xpub(e, s, path, net, sig, buf, cap)) {
        return false;
    }
    e->kind = QR_OUT_SPECTER;
    e->fragment = specter_fragment(density);
    e->parts = (uint32_t)((e->len - 1) / e->fragment + 1);
    return true;
}

/* ---------------------------------------------------------------- crypto-account */

/* The script expressions SeedSigner wraps the key in, outermost first. */
typedef struct { uint16_t tags[2]; uint8_t n; } script_t;

static size_t scripts_for(const uint32_t *p, int depth, script_t out[6])
{
    const uint32_t H = 0x80000000u;
    const script_t WPKH = { { 404 }, 1 }, SH_WPKH = { { 400, 404 }, 2 },
                   WSH = { { 401 }, 1 }, SH_WSH = { { 400, 401 }, 2 },
                   PKH = { { 403 }, 1 }, SH = { { 400 }, 1 }, TR = { { 409 }, 1 };
    if (depth > 0) {
        /* By index, hardened or not, as SeedSigner compares them. */
        const uint32_t purpose = p[0] & ~H;
        if (purpose == 84) { out[0] = WPKH; return 1; }
        if (purpose == 49) { out[0] = SH_WPKH; return 1; }
        if (purpose == 48 && depth >= 4) {
            const uint32_t script = p[3] & ~H;
            if (script == 2) { out[0] = WSH; return 1; }
            if (script == 1) { out[0] = SH_WSH; return 1; }
        }
        if (purpose == 86) { out[0] = TR; return 1; }
        if (purpose == 44) { out[0] = PKH; return 1; }
        if (purpose == 45) { out[0] = SH; return 1; }
    }
    out[0] = WPKH; out[1] = SH_WPKH; out[2] = WSH;
    out[3] = SH_WSH; out[4] = PKH; out[5] = SH;
    return 6;
}

static uint32_t be32(const uint8_t b[4])
{
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | b[3];
}

size_t qr_crypto_account(const seed_t *s, const char *path, btc_net_t net,
                         uint8_t *out, size_t cap)
{
    uint32_t p[16];
    const int depth = btc_parse_path(path, p, 16);
    uint8_t pub[33], chain[32], parent[4];
    if (depth < 0 || !btc_pubkey_at(s, path, net, pub, chain, parent)) {
        return 0;
    }
    script_t scripts[6];
    const size_t n_scripts = scripts_for(p, depth, scripts);
    const bool test = net != BTC_NET_MAIN;

    CborWriter w;
    cbor_writer_init(&w, out, cap);
    cbor_write_map(&w, 2);
    cbor_write_uint(&w, 1);
    cbor_write_uint(&w, be32(s->fingerprint));
    cbor_write_uint(&w, 2);
    cbor_write_array(&w, n_scripts);
    for (size_t k = 0; k < n_scripts; k++) {
        for (uint8_t t = 0; t < scripts[k].n; t++) {
            cbor_write_tag(&w, scripts[k].tags[t]);
        }
        cbor_write_tag(&w, 303);                    /* crypto-hdkey */
        cbor_write_map(&w, test ? 5 : 4);
        cbor_write_uint(&w, 3);
        cbor_write_bytes(&w, pub, sizeof pub);
        cbor_write_uint(&w, 4);
        cbor_write_bytes(&w, chain, sizeof chain);
        if (test) {
            cbor_write_uint(&w, 5);
            cbor_write_tag(&w, 305);                /* crypto-coin-info */
            cbor_write_map(&w, 1);
            cbor_write_uint(&w, 2);                 /* network: testnet */
            cbor_write_uint(&w, 1);
        }
        cbor_write_uint(&w, 6);
        cbor_write_tag(&w, 304);                    /* crypto-keypath */
        cbor_write_map(&w, depth > 0 ? 3 : 2);
        cbor_write_uint(&w, 1);
        cbor_write_array(&w, (size_t)depth * 2);
        for (int i = 0; i < depth; i++) {
            cbor_write_uint(&w, p[i] & 0x7FFFFFFFu);
            cbor_write_bool(&w, (p[i] & 0x80000000u) != 0);
        }
        cbor_write_uint(&w, 2);
        cbor_write_uint(&w, be32(s->fingerprint));
        if (depth > 0) {
            cbor_write_uint(&w, 3);
            cbor_write_uint(&w, (uint32_t)depth);
        }
        cbor_write_uint(&w, 8);
        cbor_write_uint(&w, be32(parent));
    }
    return cbor_writer_ok(&w) ? w.length : 0;
}

bool qr_encode_xpub_ur(qr_encoder_t *e, const seed_t *s, const char *path, btc_net_t net,
                       qr_density_t density, uint8_t *buf, size_t cap)
{
    start(e, QR_OUT_UR, buf, cap);
    e->len = qr_crypto_account(s, path, net, buf, cap);
    return e->len > 0 &&
           ur_encoder_init(&e->ur, "crypto-account", buf, e->len, ur_fragment(density));
}

bool qr_encode_psbt_ur(qr_encoder_t *e, const uint8_t *psbt, size_t len,
                       qr_density_t density, uint8_t *buf, size_t cap)
{
    start(e, QR_OUT_UR, buf, cap);
    CborWriter w;
    cbor_writer_init(&w, buf, cap);
    cbor_write_bytes(&w, psbt, len);
    if (!cbor_writer_ok(&w)) {
        return false;
    }
    e->len = w.length;
    return ur_encoder_init(&e->ur, "crypto-psbt", buf, e->len, ur_fragment(density));
}

/* ---------------------------------------------------------------- parts */

uint32_t qr_encoder_seq_len(const qr_encoder_t *e)
{
    switch (e->kind) {
    case QR_OUT_SPECTER: return e->parts;
    case QR_OUT_UR:      return e->ur.seq_len;
    default:             return 1;
    }
}

void qr_encoder_restart(qr_encoder_t *e)
{
    e->sent = 0;
    e->ur.seq_num = 0;
}

size_t qr_encoder_next_part(qr_encoder_t *e, uint8_t *out, size_t cap)
{
    switch (e->kind) {
    case QR_OUT_STATIC:
        if (e->len > cap) {
            return 0;
        }
        memcpy(out, e->buf, e->len);
        return e->len;

    case QR_OUT_SPECTER: {
        const uint32_t i = e->sent % e->parts;
        const size_t from = (size_t)i * e->fragment;
        size_t n = e->len - from;
        if (n > e->fragment) {
            n = e->fragment;
        }
        int head = 0;
        if (e->parts > 1) {
            head = snprintf((char *)out, cap, "p%lluof%llu ", (unsigned long long)i + 1,
                            (unsigned long long)e->parts);
            if (head < 0 || (size_t)head >= cap) {
                return 0;
            }
        }
        if ((size_t)head + n > cap) {
            return 0;
        }
        memcpy(out + head, e->buf + from, n);
        e->sent = i + 1;
        return (size_t)head + n;
    }

    case QR_OUT_UR: {
        const size_t n = ur_encoder_next_part(&e->ur, (char *)out, cap);
        ur_to_upper((char *)out);
        return n;
    }
    }
    return 0;
}

void qr_encoder_wipe(qr_encoder_t *e)
{
    if (e->buf != NULL) {
        wally_bzero(e->buf, e->cap);
    }
    wally_bzero(e, sizeof *e);
}
