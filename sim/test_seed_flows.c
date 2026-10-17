/*
 * SeedSigner's seed flow tests (tests/test_flows_seed.py), ported: the same
 * sequence of Views, driven through the device's own screens with the keys a
 * user would press. Each step names the SeedSigner View the device must be on.
 *
 * Where SeedSigner's test injects a screen's return value (a typed word, a
 * passphrase), this presses the keys that produce it, so the entry screens
 * are tested too.
 */
#include "btc-init.h"
#include "flow.h"
#include "quirc.h"
#include "vectors/seedsigner_core.h"
#include "views-tools.h"
#include "wally_core.h"

/* abandon x11 about: CompactSeedQR all zero bytes, fingerprint 73c5da0a. */
static const uint8_t ZERO16[16];

static void test_scan_seedqr_flow(void)
{
    printf("== test_scan_seedqr_flow\n");
    fresh();
    STEP("MainMenuView");
    choose("Scan");
    STEP("ScanView");
    scan(ZERO16, sizeof ZERO16);
    STEP("SeedFinalizeView");
    CHECK(strcmp(last_note, "fingerprint 73c5da0a") == 0, "note %s", last_note);
    choose("Done");
    STEP("SeedOptionsView");
    CHECK(storage_count() == 1, "the seed was not stored");
    key(IN_BACK);
    STEP("MainMenuView");       /* clear_history: BACK goes home */
}

static void test_passphrase_entry_flow(void)
{
    printf("== test_passphrase_entry_flow\n");
    fresh();
    choose("Scan");
    STEP("ScanView");
    scan(ZERO16, sizeof ZERO16);
    STEP("SeedFinalizeView");
    choose("BIP-39 Passphrase");
    STEP("SeedAddPassphraseView");
    type_passphrase("muhpassphrase", false);
    key(IN_BACK);                                   /* is_back_button */
    STEP("SeedAddPassphraseExitDialogView");
    choose("Discard passphrase");
    STEP("SeedFinalizeView");
    choose("BIP-39 Passphrase");
    STEP("SeedAddPassphraseView");
    type_passphrase("muhpassphrase", false);
    key(IN_BACK);
    STEP("SeedAddPassphraseExitDialogView");
    choose("Edit passphrase");
    STEP("SeedAddPassphraseView");
    type_passphrase("", true);                      /* it kept the text: just OK */
    STEP("SeedReviewPassphraseView");
    choose("Edit passphrase");
    STEP("SeedAddPassphraseView");
    type_passphrase("", true);
    STEP("SeedReviewPassphraseView");
    CHECK(strncmp(last_note, "passphrase 73c5da0a ", 20) == 0, "note %s", last_note);
    choose("Done");
    STEP("SeedOptionsView");
    CHECK(storage_count() == 1 && strcmp(storage_seed(0)->passphrase, "muhpassphrase") == 0,
          "the passphrase was not kept");

    /* Skipping: leave with nothing entered. */
    fresh();
    choose("Scan");
    scan(ZERO16, sizeof ZERO16);
    choose("BIP-39 Passphrase");
    key(IN_BACK);
    STEP("SeedAddPassphraseExitDialogView");
    choose("Skip passphrase");
    STEP("SeedFinalizeView");
}

static void test_mnemonic_entry_flow(void)
{
    printf("== test_mnemonic_entry_flow\n");
    const char *m[2] = {
        "tone flat shed cool census soul paddle boy flight fantasy stem social",
        "cotton artefact spy mind wing there echo steak child oak awful host despair online "
        "bicycle divorce middle firm diamond rare execute chimney almost hollow",
    };
    for (int k = 0; k < 2; k++) {
        fresh();
        choose("Seeds");
        STEP("LoadSeedView");       /* SeedsMenuView redirects with no seeds */
        choose(k == 0 ? "Enter 12-word seed" : "Enter 24-word seed");
        STEP("SeedMnemonicEntryView");
        type_mnemonic(m[k]);
        STEP("SeedFinalizeView");
        choose("Done");
        STEP("SeedOptionsView");
        seed_t ref;
        CHECK(seed_from_mnemonic(&ref, m[k], "") == SEED_OK &&
              memcmp(ref.fingerprint, storage_seed(0)->fingerprint, 4) == 0, "wrong seed stored");
        seed_clear(&ref);
    }
}

static void test_invalid_mnemonic(void)
{
    printf("== test_invalid_mnemonic\n");
    fresh();
    choose("Seeds");
    choose("Enter 12-word seed");
    type_mnemonic("blush twice taste dawn feed second opinion lazy thumb play neglect zoo");
    STEP("SeedMnemonicInvalidView");
    choose("Review & edit");
    STEP("SeedMnemonicEntryView");
    type_mnemonic("blush twice taste dawn feed second opinion lazy thumb play neglect zebra");
    STEP("SeedMnemonicInvalidView");
    choose("Discard");
    STEP("MainMenuView");
    CHECK(storage_count() == 0 && storage_mnemonic_len() == 0, "the mnemonic was not discarded");
}

static void load_blush(void)
{
    seed_t s;
    seed_from_mnemonic(&s, "blush twice taste dawn feed second opinion lazy thumb play neglect impact", "");
    storage_set_pending(&s);
    seed_clear(&s);
    storage_finalize();
}

static void test_export_xpub_standard_flow(void)
{
    printf("== test_export_xpub_standard_flow\n");
    static const char *sigs[] = { "Single Sig", "Multisig" };
    static const char *scripts[] = { "Native Segwit", "Nested Segwit", "Legacy", "Taproot" };
    static const char *formats[] = { "Animated (default)", "Static", "Specter legacy" };
    int runs = 0;
    for (int a = 0; a < 2; a++)
        for (int b = 0; b < 4; b++)
            for (int c = 0; c < 3; c++) {
                if (a == 1 && b == 3) continue;     /* multisig taproot, as SeedSigner skips */
                fresh();
                for (int t = 0; t < SCRIPT_COUNT; t++) SETTINGS.script_types[t] = true;
                for (int f = 0; f < XPUB_QR_COUNT; f++) SETTINGS.xpub_qr_format[f] = true;
                load_blush();
                SeedOptionsView(0);
                STEP("SeedOptionsView");
                choose("Export xpub");
                STEP("SeedExportXpubSigTypeView");
                choose(sigs[a]);
                STEP("SeedExportXpubScriptTypeView");
                choose(scripts[b]);
                STEP("SeedExportXpubQRFormatView");
                choose(formats[c]);
                STEP("SeedExportXpubWarningView");
                choose("I understand");
                STEP("SeedExportXpubDetailsView");
                choose("Export Xpub");
                STEP("SeedExportXpubQRDisplayView");
                key(IN_OK);
                STEP("MainMenuView");
                runs++;
            }
    printf("   %d sig/script/format combinations\n", runs);
}

static void test_discard_seed_flow(void)
{
    printf("== test_discard_seed_flow\n");
    fresh();
    load_blush();
    SeedOptionsView(0);
    choose("Discard seed");
    STEP("SeedDiscardView");
    choose("Discard");
    STEP("MainMenuView");
    CHECK(storage_count() == 0, "the seed is still held");
    choose("Seeds");
    STEP("LoadSeedView");
}

static void test_seeds_menu_and_backup(void)
{
    printf("== seeds menu, backup words\n");
    fresh();
    load_blush();
    choose("Seeds");
    STEP("SeedsMenuView");
    choose("Load a seed");
    STEP("LoadSeedView");
    key(IN_BACK);
    STEP("SeedsMenuView");
    key(IN_UP);
    key(IN_OK);
    STEP("SeedOptionsView");
    choose("Backup seed");
    STEP("SeedBackupView");
    choose("View seed words");
    STEP("SeedWordsWarningView");
    choose("I understand");
    STEP("SeedWordsView");
    key(IN_OK);
    CHECK(strcmp(last_note, "words page 2") == 0, "note %s", last_note);
    key(IN_BACK);
    STEP("SeedWordsView");
    key(IN_BACK);
    STEP("SeedBackupView");     /* the warning was skipped on the way back */
    choose("View seed words");
    choose("I understand");
    for (int i = 0; i < 3; i++) key(IN_OK);
    STEP("SeedWordsBackupTestPromptView");
}

/* ---- The backup test ---------------------------------------------------- */

/* The word SeedWordsBackupTestView asks for, from its note. */
static int asked_word(void)
{
    int k = 0;
    return sscanf(last_note, "verify word %d", &k) == 1 ? k : 0;
}

static void choose_wrong(const char *right)
{
    for (int i = 0; i < 8; i++) key(IN_UP);
    for (int i = 0; i < 4; i++) {
        const char *sel = ui_selected_label();
        if (sel && strcmp(sel, right) != 0) {
            key(IN_OK);
            return;
        }
        key(IN_DOWN);
    }
    CHECK(0, "no wrong word offered");
}

static void test_backup_test_flow(void)
{
    printf("== backup test: wrong word, try again, review, verify every word\n");
    fresh();
    notes_reset();
    load_blush();
    SeedOptionsView(0);
    choose("Backup seed");
    choose("View seed words");
    choose("I understand");
    for (int i = 0; i < 3; i++) key(IN_OK);
    STEP("SeedWordsBackupTestPromptView");
    choose("Verify");
    STEP("SeedWordsBackupTestView");
    const seed_t *s = storage_seed(0);
    int k = asked_word();
    CHECK(k >= 1 && k <= 12, "note %s", last_note);
    choose_wrong(BIP39_WORDLIST[s->words[k - 1]]);
    STEP("SeedWordsBackupTestMistakeView");
    choose("Try again");
    STEP("SeedWordsBackupTestView");
    CHECK(asked_word() == k, "Try again asked another word");
    choose_wrong(BIP39_WORDLIST[s->words[k - 1]]);
    choose("Review seed words");
    STEP("SeedWordsView");
    for (int i = 0; i < 3; i++) key(IN_OK);
    STEP("SeedWordsBackupTestPromptView");
    choose("Verify");
    bool seen[12] = { false };
    for (int i = 0; i < 12; i++) {
        STEP("SeedWordsBackupTestView");
        k = asked_word();
        CHECK(k >= 1 && k <= 12 && !seen[k - 1], "word %d asked twice", k);
        if (k >= 1 && k <= 12) {
            seen[k - 1] = true;
            choose(BIP39_WORDLIST[s->words[k - 1]]);
        }
    }
    STEP("SeedWordsBackupTestSuccessView");
    choose("OK");
    STEP("SeedOptionsView");
    key(IN_BACK);
    STEP("MainMenuView");

    /* A seed not loaded yet (from dice): Skip goes on to finalise it. */
    fresh();
    choose("Tools");
    choose("New seed (dice)");
    choose("12 words (50 rolls)");
    for (int i = 0; i < 50; i++) key(IN_OK);
    STEP("SeedWordsWarningView");
    choose("I understand");
    for (int i = 0; i < 3; i++) key(IN_OK);
    STEP("SeedWordsBackupTestPromptView");
    choose("Skip");
    STEP("SeedFinalizeView");
}

/* ---- Export as SeedQR ------------------------------------------------------ */

/* Read the panel back with quirc, as a camera would. */
static size_t read_screen(uint8_t *out, size_t cap)
{
    struct quirc *q = quirc_new();
    struct quirc_data *d = malloc(sizeof *d);
    size_t n = 0;
    const int up = 3;
    if (q && d && quirc_resize(q, GFX_W * up, GFX_H * up) == 0) {
        int w, h;
        uint8_t *img = quirc_begin(q, &w, &h);
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                const gfx_color_t c = gfx_get(x / up, y / up);
                const uint32_t rgb = gfx_role_rgb(c);
                const bool light = GFX_MONO ? gfx_role_lit(c)
                                            : ((rgb >> 16) & 255) + ((rgb >> 8) & 255) + (rgb & 255) > 384;
                img[(size_t)y * (size_t)w + (size_t)x] = light ? 255 : 0;
            }
        quirc_end(q);
        for (int i = 0; n == 0 && i < quirc_count(q); i++) {
            struct quirc_code c;
            quirc_extract(q, i, &c);
            if (quirc_decode(&c, d) == QUIRC_SUCCESS && (size_t)d->payload_len <= cap) {
                memcpy(out, d->payload, (size_t)d->payload_len);
                n = (size_t)d->payload_len;
            }
        }
    }
    free(d);
    if (q) quirc_destroy(q);
    return n;
}

static void test_transcribe_every_format(void)
{
    printf("== SeedQR transcription: standard and compact, 12 and 24 words, read back\n");
    static const char *m24 = "cotton artefact spy mind wing there echo steak child oak awful host despair "
                             "online bicycle divorce middle firm diamond rare execute chimney almost hollow";
    for (int len = 0; len < 2; len++) {
        for (int compact = 0; compact < 2; compact++) {
            fresh();
            notes_reset();
            seed_t ref;
            if (len == 0) {
                load_blush();
                seed_from_mnemonic(&ref, "blush twice taste dawn feed second opinion lazy thumb play neglect impact", "");
            } else {
                seed_from_mnemonic(&ref, m24, "");
                storage_set_pending(&ref);
                storage_finalize();
            }
            const int size = len == 0 ? (compact ? 21 : 25) : (compact ? 25 : 29);
            SeedOptionsView(0);
            choose("Backup seed");
            choose("Export as SeedQR");
            STEP("SeedTranscribeSeedQRFormatView");
            char label[24];
            snprintf(label, sizeof label, "%s: %dx%d", compact ? "Compact" : "Standard", size, size);
            choose(label);
            STEP("SeedTranscribeSeedQRWarningView");
            choose("I understand");
            STEP("SeedTranscribeSeedQRWholeQRView");
            char want[32];
            snprintf(want, sizeof want, "seedqr %dx%d", size, size);
            CHECK(noted(want), "no \"%s\"", want);
            /* The code on the panel is the seed's own SeedQR. */
            uint8_t got[100], exp[100];
            size_t elen = compact ? seedqr_encode_compact(&ref, exp) : 0;
            if (!compact) {
                seedqr_encode_standard(&ref, (char *)exp, sizeof exp);
                elen = strlen((char *)exp);
            }
            const size_t glen = read_screen(got, sizeof got);
            CHECK(glen == elen && memcmp(got, exp, elen) == 0, "%s: the screen does not read back as the SeedQR", label);
            key(IN_BACK);
            STEP("SeedTranscribeSeedQRFormatView");     /* the warning was skipped on the way back */
            choose(label);
            choose("I understand");
            key(IN_OK);
            STEP("SeedTranscribeSeedQRZoomedInView");
            /* Every zone, in reading order, back round to A-1. */
            const int per = size == 21 ? 7 : 5, zones = (size + per - 1) / per;
            for (int z = 1; z <= zones * zones; z++) key(IN_DOWN);
            CHECK(strcmp(last_note, "zone A-1") == 0, "%d zones did not wrap: %s", zones * zones, last_note);
            key(IN_UP);
            snprintf(want, sizeof want, "zone %c-%d", 'A' + zones - 1, zones);
            CHECK(strcmp(last_note, want) == 0, "last zone: %s, want %s", last_note, want);
            key(IN_OK);
            STEP("SeedTranscribeSeedQRConfirmQRPromptView");
            choose("Done");
            STEP("SeedOptionsView");
            seed_clear(&ref);
            wally_bzero(got, sizeof got);
            wally_bzero(exp, sizeof exp);
        }
    }
}

/* tests/test_flows_seed.py: test_transcribe_seedqr_and_verify */
static void test_transcribe_seedqr_and_verify(void)
{
    printf("== test_transcribe_seedqr_and_verify\n");
    fresh();
    seed_t s;
    seed_from_mnemonic(&s, "abandon abandon abandon abandon abandon abandon abandon abandon abandon "
                           "abandon abandon about", "");
    storage_set_pending(&s);
    seed_clear(&s);
    storage_finalize();
    static const char *const scans[] = {
        "013801380138013801380138013801380138013801380138013801380138013801380138013801380138013801380138",
        "I like cheese",
        "UR:CRYPTO-PSBT/HKADHEJOJKIDJYZMADAEGMAOAEAEAEADHHGLEYKGBDSBKEMTSARLVYSGIABEDELFSKWLLUDKLYESJOFDLPJPFXSOHHWZTYWEAEAEAEAEAEZCZMZMZMADBKBZJEDEHEAEAEAECMAEBBVEPFDWDMBBGSPFZTKIMYCLKPSOBDDTRDWTWPYKGAQDKNAEAEGWADAAECLTTKAXWSWTUTSNLAAEAEAEPKCTBSHGISSSRETIVEWSGYNEPTHTESNSWMLBTARYEMHTBTBTLNWSBKJLMKYNFGHLAXJYBBTTIDHKDICHAMLEHHDSRKATGDLYSBIYHNHDNEWPJKZSZMDKVETYNSGOCXKNDNBEOLFSRSNSGHAEAELAADAEAELAAEAEAELAAEADADCTKSBZJEDEHEAEAEAECMAEBBVEPFDWDMBBGSPFZTKIMYCLKPSOBDDTRDWTWPYKGAADAXAAADAEAEAECPAMAODSFTLDTYFRECSKVWYLKNBANNKKZCRTHYJTPSHLHNHKNBPDCLCFDMSOLPDKFXLFQZCSOLFSRSNSGHAEAELAADAEAELAAEAEAELAAEAEAEAEAEAEAEAEAECPAOAODSFTLDTYFRECSKVWYLKNBANNKKZCRTHYJTPSHLHNHKNBPDCLCFDMSOLPDKFXLFQZCSOLFSRSNSGHAEAELAADAEAELAAEAEAELAAEAEAEAEAEAEAEAEAELGMKFZCW",
        "000000000000000000000000000000000000000000000003",
    };
    static const char *const outcome[] = {
        "SeedTranscribeSeedQRConfirmWrongSeedView", "SeedTranscribeSeedQRConfirmInvalidQRView",
        "SeedTranscribeSeedQRConfirmInvalidQRView", "SeedTranscribeSeedQRConfirmSuccessView",
    };
    choose("Seeds");
    STEP("SeedsMenuView");
    for (int i = 0; i < 40; i++) key(IN_UP);
    key(IN_OK);
    STEP("SeedOptionsView");
    choose("Backup seed");
    STEP("SeedBackupView");
    choose("Export as SeedQR");
    STEP("SeedTranscribeSeedQRFormatView");
    choose("Standard: 25x25");
    STEP("SeedTranscribeSeedQRWarningView");
    choose("I understand");
    STEP("SeedTranscribeSeedQRWholeQRView");
    key(IN_OK);
    STEP("SeedTranscribeSeedQRZoomedInView");
    for (int i = 0; i < 4; i++) {
        key(IN_OK);
        STEP("SeedTranscribeSeedQRConfirmQRPromptView");
        choose("Confirm SeedQR");
        STEP("SeedTranscribeSeedQRConfirmScanView");
        scan((const uint8_t *)scans[i], strlen(scans[i]));
        STEP(outcome[i]);
        if (i < 3) {
            choose("Review SeedQR");
            STEP("SeedTranscribeSeedQRZoomedInView");
        }
    }
    choose("OK");
    STEP("SeedOptionsView");
}

/* ---- BIP-85 child seeds ---------------------------------------------------- */

static void test_bip85_flow(void)
{
    printf("== BIP-85 child seeds: every case SeedSigner's code derived, on screen\n");
    for (size_t i = 0; i < sizeof VEC_BIP85 / sizeof VEC_BIP85[0]; i++) {
        const vec_bip85_t *v = &VEC_BIP85[i];
        fresh();
        SETTINGS.bip85_child_seeds = true;
        seed_t s;
        seed_from_mnemonic(&s, v->mnemonic, "");
        storage_set_pending(&s);
        seed_clear(&s);
        storage_finalize();
        SeedOptionsView(0);
        choose("BIP-85 child seed");
        STEP("SeedBIP85SelectNumWordsView");
        choose(v->words == 12 ? "12 Words" : "24 Words");
        STEP("SeedBIP85SelectChildIndexView");
        char index[16];
        snprintf(index, sizeof index, "%u", v->index);
        keypad("0123456789", index);
        keypad_to(11);      /* OK */
        key(IN_OK);
        STEP("SeedWordsWarningView");
        choose("I understand");
        STEP("SeedWordsView");
        /* The child's words, as SeedSigner derives them. */
        char want[300];
        snprintf(want, sizeof want, "%s", v->child);
        size_t k = 0;
        for (char *w = strtok(want, " "); w; w = strtok(NULL, " "), k++) {
            const char *got = views_shown_word(k);
            CHECK(got && strcmp(got, w) == 0, "bip85 %zu: word %zu is %s, want %s", i, k + 1,
                  got ? got : "-", w);
        }
        CHECK(k == v->words && views_shown_word(k) == NULL, "bip85 %zu: %zu words", i, k);
        for (unsigned p = 0; p < v->words / 4; p++) key(IN_OK);
        STEP("SeedWordsBackupTestPromptView");
        choose("Skip");
        STEP("SeedOptionsView");
        /* The child is wiped: the words are the seed's own again. */
        char first[16];
        sscanf(v->mnemonic, "%15s", first);
        CHECK(views_shown_word(0) && strcmp(views_shown_word(0), first) == 0,
              "bip85 %zu: the child's words were not wiped", i);
    }

    /* Out of range, and empty: SeedBIP85InvalidChildIndexView, then again. */
    fresh();
    SETTINGS.bip85_child_seeds = true;
    load_blush();
    SeedOptionsView(0);
    choose("BIP-85 child seed");
    choose("12 Words");
    keypad("0123456789", "2147483648");
    keypad_to(11);
    key(IN_OK);
    STEP("SeedBIP85InvalidChildIndexView");
    key(IN_BACK);
    STEP("SeedBIP85InvalidChildIndexView");     /* no way back */
    choose("Try again");
    STEP("SeedBIP85SelectChildIndexView");
    keypad_to(11);
    key(IN_OK);
    STEP("SeedBIP85InvalidChildIndexView");
    choose("Try again");
    keypad("0123456789", "2147483647");
    keypad_to(11);
    key(IN_OK);
    STEP("SeedWordsWarningView");

    /* Off (SeedSigner's default): not offered. */
    fresh();
    load_blush();
    SeedOptionsView(0);
    for (int i = 0; i < 12; i++) {
        const char *sel = ui_selected_label();
        CHECK(!sel || strcmp(sel, "BIP-85 child seed") != 0, "BIP-85 offered while disabled");
        key(IN_DOWN);
    }
}

/* tests/test_flows_seed.py: test_export_xpub_custom_derivation_flow */
static void test_export_xpub_custom_derivation_flow(void)
{
    printf("== test_export_xpub_custom_derivation_flow\n");
    fresh();
    notes_reset();
    load_blush();
    for (int t = 0; t < SCRIPT_COUNT; t++) {
        SETTINGS.script_types[t] = t == SCRIPT_NAT || t == SCRIPT_NES || t == SCRIPT_CUSTOM;
    }
    for (int f = 0; f < XPUB_QR_COUNT; f++) SETTINGS.xpub_qr_format[f] = true;
    SeedOptionsView(0);
    choose("Export xpub");
    STEP("SeedExportXpubSigTypeView");
    choose("Single Sig");
    STEP("SeedExportXpubScriptTypeView");
    choose("Custom Derivation");
    STEP("SeedExportXpubCustomDerivationView");
    CHECK(strcmp(views_keypad_text(), "m/") == 0, "starts from %s", views_keypad_text());
    keypad("/'0123456789", "0'/0'");
    keypad_to(13);      /* OK */
    key(IN_OK);
    STEP("SeedExportXpubQRFormatView");
    choose("Specter legacy");
    STEP("SeedExportXpubWarningView");
    choose("I understand");
    STEP("SeedExportXpubDetailsView");
    CHECK(noted("custom m/0'/0'"), "the path typed");
    seed_t s;
    seed_from_mnemonic(&s, "blush twice taste dawn feed second opinion lazy thumb play neglect impact", "");
    char xpub[120], want[160];
    btc_xpub(s.bytes, sizeof s.bytes, "m/0'/0'", BTC_NET_MAIN, btc_slip132_version("m/0'/0'", BTC_NET_MAIN), xpub,
             sizeof xpub);
    seed_clear(&s);
    snprintf(want, sizeof want, "xpub m/0'/0' %s", xpub);
    CHECK(noted(want), "no \"%s\"", want);
    choose("Export Xpub");
    STEP("SeedExportXpubQRDisplayView");
    key(IN_OK);
    STEP("MainMenuView");

    /* A path that does not derive: SeedSigner's get_xpub raises. */
    SeedOptionsView(0);
    choose("Export xpub");
    choose("Single Sig");
    choose("Custom Derivation");
    keypad("/'0123456789", "''");
    keypad_to(13);
    key(IN_OK);
    choose("Static");
    choose("I understand");
    STEP("UnhandledExceptionView");
}

int main(void)
{
    btc_init(rand_fill);
    ui_set_clock(clock_fn);
    ui_set_note_hook(note);
    test_scan_seedqr_flow();
    test_passphrase_entry_flow();
    test_mnemonic_entry_flow();
    test_invalid_mnemonic();
    test_export_xpub_standard_flow();
    test_discard_seed_flow();
    test_seeds_menu_and_backup();
    test_backup_test_flow();
    test_transcribe_every_format();
    test_transcribe_seedqr_and_verify();
    test_bip85_flow();
    test_export_xpub_custom_derivation_flow();
    fresh();
    storage_wipe_all();
    return CHECK_DONE("test_seed_flows");
}
