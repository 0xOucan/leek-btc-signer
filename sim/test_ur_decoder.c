/**
 * Assembling a message from animated-QR parts.
 *
 * Carried over from LeekWallet. The part strings below were emitted by
 * Blockchain Commons' own UREncoder, so this suite answers the question that
 * matters: can we decode what other wallets actually send? The decoder has
 * since been rewritten to follow SeedSigner's algorithm step for step;
 * sim/test_qr_decode.c replays SeedSigner's own decodes frame by frame.
 */

#include <stdio.h>
#include <string.h>

#include <stdlib.h>

#include "ur-decoder.h"

#include "check.h"
#include "ur-encoder.h"

/* message: 250 bytes, msg[i] = (i*7+3) & 0xFF */
/* seq_len 5, max fragment 60 */
static const char *PARTS[] = {
    "ur:bytes/1-5/lpadahcszscyvadpiyhnhdeyaxbkbycsctdsdpeefrfwgagdhghyihjzjkknlylomymtntoxpyprrhrtsttotluovlwdwnyazmambtbbcwcpdtdyemfmfegsguhtpdeoplsf",
    "ur:bytes/2-5/lpaoahcszscyvadpiyhnhdeyhsisjlkokilrlumonlnbosplrerfsrsgtttpurvawewkzoaoasbechckdadweoftfpfdgwhfhliejejpkklaltmnmdnsotpkparofniemhkp",
    "ur:bytes/3-5/lpaxahcszscyvadpiyhnhdeyrsswsntyuyvowlwtylzeahbnbwcycldedlenfsfygrgmhkhniojtkpkelslememkneolpmqzrksasotitsuevwwpwfzsadaybscmfdrhsrzt",
    "ur:bytes/4-5/lpaaahcszscyvadpiyhnhdeycadkdneyesfzflglgohhiaimjskslblnlgmwndoeptpfrlrnsksftetnvyvswsynzcaabdbgcfcxdidmecfnfxgegyhdheiyjnjydiinmwjt",
    "ur:bytes/5-5/lpahahcszscyvadpiyhnhdeykglfldmhmsnnonpsqdrdsesptktbutvewmwzytaeatbabzcecndrehetfhfggtghhpidinjoktkblplkmunyoypdperprysssbtdoyeshyns",
    "ur:bytes/6-5/lpamahcszscyvadpiyhnhdeystglgohhguhthsislbamlgmwlumonlnbrlrnfesfsrsgtttpwsynzclrkgaoasbedidmecfneordfpfdheiyjnjyjejpytlamsnnfnkbkbmu",
    "ur:bytes/7-5/lpatahcszscyvadpiyhnhdeyiyoloeoepluevovovavaoeoernploeidiyiyidoeplrnoeoevavavovoueploeoeoliyididjthyoeoeololvovozewyvooeololsajnesmu",
    "ur:bytes/8-5/lpayahcszscyvadpiyhnhdeyondwdneyehyasttoutveiaiminjofhambzcecwoeoypdrlkbgtghguhtnlvtwsynlplklumometpdidmfssssrsgsotictiykpkeoydehttd",
    "ur:bytes/9-5/lpasahcszscyvadpiyhnhdeycfvtylzeykfnaxbkbyhddlendpeekgfwgagdosjtihjzjkrdlylonevautoxpyprytrttsuetlcevlwdwnetbscmbtbbhpcpdtdystykvtot",
    "ur:bytes/10-5/lpbkahcszscyvadpiyhnhdeyckdmftdrdsiyimknjtckdrftdsdscywdzewycydrdsdsdrcyjtkbimcydsdsftdrckwyzswdvadsdrftdmckimkniyiycydrfmdmwycpvyln",
    "ur:bytes/11-5/lpbdahcszscyvadpiyhnhdeyckdmftdrdsiyimknjtckdrftdsdscywdzewycydrdsdsdrcyjtkbimcydsdsftdrckwyzswdvadsdrftdmckimkniyiycydrfmdmmuwspsrh",
    "ur:bytes/12-5/lpbnahcszscyvadpiyhnhdeyrsswsntyuyvowlwtylzeahbnbwcycldedlenfsfygrgmhkhniojtkpkelslememkneolpmqzrksasotitsuevwwpwfzsadaybscmrdcpfxkn",
    "ur:bytes/13-5/lpbtahcszscyvadpiyhnhdeyrfsfuosfsssssssssfrfgshhfyfyfyfyhhgsrfsfsssssssssfuosfrffyfyfyfykegshhgsfysssssssfztsfuossssfyfyhhgslywzrpbb",
    "ur:bytes/14-5/lpbaahcszscyvadpiyhnhdeytnimkbjtimcydsdsdrhtwyzezswdvadsftdruejtimkniyiydrftdmhynywdvavatndrfmdmdrnyiyiyimhtdmfmftdrolvazswdztetvamk",
    "ur:bytes/15-5/lpbsahcszscyvadpiyhnhdeykegsfyfyfysssfuosfztssssssssrfgshhgsfyfyfyfygsrfsfuossssssssuosfrfgsfyfyfyfygshhgsrfssssssssztsfuosfpmpyktzt",
};
/* single-part, payload deadbeef as cbor bytes */
static const char *SINGLE = "ur:bytes/uepmrnwskensotht";

/* On the heap, not static: the suite also runs in the QEMU self-test
   image, whose internal RAM cannot hold every suite's buffers at once. */
#define ARENA_SIZE (16 * 1024)
static uint8_t *arena;
static UrDecoder *dec_p;
#define dec (*dec_p)

#define N_PARTS (sizeof PARTS / sizeof PARTS[0])

/* The message the parts carry: 250 bytes, msg[i] = (i * 7 + 3) & 0xFF. */
static bool message_is_right(void)
{
    size_t n = 0;
    const uint8_t *m = ur_decoder_message(&dec, &n);
    if (m == NULL) {
        return false;
    }
    /* UR carries the CBOR the caller handed it; it does not add a wrapper of
       its own. So what comes out is byte for byte what went in, and the 4527
       types layered on top are responsible for their own CBOR. */
    if (n != 250) {
        return false;
    }
    for (size_t i = 0; i < 250; i++) {
        if (m[i] != (uint8_t)((i * 7 + 3) & 0xFF)) {
            return false;
        }
    }
    return true;
}

static void feed(const char *ur, UrPartResult *out)
{
    *out = ur_decoder_receive(&dec, ur, strlen(ur));
}

static void test_in_order(void)
{
    printf("== parts in order\n");
    ur_decoder_reset(&dec);

    UrPartResult r = UR_PART_REJECTED;
    size_t used = 0;
    for (size_t i = 0; i < N_PARTS && r != UR_PART_COMPLETE; i++) {
        feed(PARTS[i], &r);
        used++;
        CHECK(r != UR_PART_REJECTED, "part %zu rejected", i + 1);
    }
    CHECK(r == UR_PART_COMPLETE, "never completed after %zu parts", used);
    CHECK(message_is_right(), "assembled the wrong bytes");
}

/* The real case: a camera misses frames and the user starts watching part way
   through the animation. Starting at the fourth part means the first three
   plain fragments are never seen as themselves - they have to come out of the
   mixed parts. */
static void test_starting_late(void)
{
    printf("== joining the animation late, and looping\n");
    ur_decoder_reset(&dec);

    UrPartResult r = UR_PART_REJECTED;
    for (int pass = 0; pass < 3 && r != UR_PART_COMPLETE; pass++) {
        for (size_t i = 3; i < N_PARTS && r != UR_PART_COMPLETE; i++) {
            feed(PARTS[i], &r);
            CHECK(r != UR_PART_REJECTED, "part %zu rejected", i + 1);
        }
    }
    CHECK(r == UR_PART_COMPLETE, "never completed");
    CHECK(message_is_right(), "assembled the wrong bytes");
}

static void test_dropped_frames(void)
{
    printf("== every third frame dropped\n");
    ur_decoder_reset(&dec);

    UrPartResult r = UR_PART_REJECTED;
    for (int pass = 0; pass < 3 && r != UR_PART_COMPLETE; pass++) {
        for (size_t i = 0; i < N_PARTS && r != UR_PART_COMPLETE; i++) {
            if (i % 3 == 2) {
                continue;
            }
            feed(PARTS[i], &r);
            CHECK(r != UR_PART_REJECTED, "part %zu rejected", i + 1);
        }
    }
    CHECK(r == UR_PART_COMPLETE, "never completed");
    CHECK(message_is_right(), "assembled the wrong bytes");
}

static void test_single_part(void)
{
    printf("== a single-part UR needs no assembly\n");
    ur_decoder_reset(&dec);

    UrPartResult r;
    feed(SINGLE, &r);
    CHECK(r == UR_PART_COMPLETE, "single-part UR did not complete");

    size_t n = 0;
    const uint8_t *m = ur_decoder_message(&dec, &n);
    CHECK(m != NULL && n == 4 && m[0] == 0xDE && m[1] == 0xAD &&
          m[2] == 0xBE && m[3] == 0xEF, "single-part payload is wrong");
}

/* Two senders, one decoder. A part from another message must not be able
   to steer an assembly that is under way. */
static void test_mixing_two_messages_is_refused(void)
{
    printf("== a part from a different message is refused\n");
    ur_decoder_reset(&dec);

    UrPartResult r;
    feed(PARTS[0], &r);
    CHECK(r == UR_PART_ACCEPTED, "first part not accepted");

    /* Part 2 of another 250-byte message, five fragments too. */
    static uint8_t other[250];
    UrEncoder *enc_p = malloc(sizeof *enc_p);
#define enc (*enc_p)
    static char part[1024];
    memset(other, 0x55, sizeof other);
    CHECK(ur_encoder_init(&enc, "bytes", other, sizeof other, 60), "encoder init");
    CHECK(ur_encoder_part(&enc, 2, part, sizeof part) > 0, "encoder part");
    feed(part, &r);
    CHECK(r == UR_PART_REJECTED,
          "a part from another message was accepted mid-assembly");

    /* and the assembly still finishes correctly afterwards */
    for (size_t i = 1; i < N_PARTS && r != UR_PART_COMPLETE; i++) {
        feed(PARTS[i], &r);
    }
    CHECK(r == UR_PART_COMPLETE, "did not recover after the bad part");
    CHECK(message_is_right(), "assembled the wrong bytes");
    free(enc_p);
#undef enc
}

/* SeedSigner's decoder takes a single-part UR of the same type as the whole
   message even part-way through a multi-part one, so this does too. What
   completes is then reviewed like anything else. */
static void test_single_part_mid_assembly(void)
{
    printf("== a single-part UR mid-assembly ends it, as in SeedSigner\n");
    ur_decoder_reset(&dec);

    UrPartResult r;
    feed(PARTS[0], &r);
    feed(SINGLE, &r);
    CHECK(r == UR_PART_COMPLETE, "the single part did not complete the decode");
    size_t n = 0;
    const uint8_t *m = ur_decoder_message(&dec, &n);
    CHECK(m != NULL && n == 4 && m[0] == 0xDE && m[3] == 0xEF, "wrong message");
    feed(PARTS[1], &r);
    CHECK(r == UR_PART_COMPLETE, "a part after the end was not reported complete");
}

/* The arena decides what fits, at the first part. */
static void test_arena_is_respected(void)
{
    printf("== a first part that cannot fit the arena is refused\n");
    static uint8_t tiny[300];
    UrDecoder small;
    ur_decoder_init(&small, tiny, sizeof tiny);
    CHECK(ur_decoder_receive(&small, PARTS[0], strlen(PARTS[0])) == UR_PART_REJECTED,
          "a 250-byte message was started in a 300-byte arena");
    ur_decoder_init(&small, arena, ARENA_SIZE);
}

static void test_garbage_is_refused(void)
{
    printf("== malformed parts are refused\n");
    ur_decoder_reset(&dec);

    UrPartResult r;
    feed("ur:bytes/0-5/lpadahcszscyvadpiyhnhdey", &r);
    CHECK(r == UR_PART_REJECTED, "part zero was accepted");

    ur_decoder_reset(&dec);
    feed("ur:bytes/1-0/lpadahcszscyvadpiyhnhdey", &r);
    CHECK(r == UR_PART_REJECTED, "a zero fragment count was accepted");

    ur_decoder_reset(&dec);
    feed("ur:bytes/1-5/", &r);
    CHECK(r == UR_PART_REJECTED, "an empty body was accepted");

    ur_decoder_reset(&dec);
    feed("ur:bytes/a-5/lpadahcszscyvadpiyhnhdey", &r);
    CHECK(r == UR_PART_REJECTED, "a non-numeric sequence was accepted");

    /* A single flipped character anywhere in a real part must fail its CRC. */
    ur_decoder_reset(&dec);
    char broken[512];
    strcpy(broken, PARTS[0]);
    const size_t last = strlen(broken) - 1;
    broken[last] = (broken[last] == 'a') ? 'z' : 'a';
    feed(broken, &r);
    CHECK(r == UR_PART_REJECTED, "a corrupted part was accepted");
}



int main(void)
{
    arena = test_alloc(ARENA_SIZE);
    dec_p = test_alloc(sizeof *dec_p);
    if (!arena || !dec_p) {
        CHECK(0, "out of memory");
        test_free_all();
        return CHECK_DONE("test_ur_decoder");
    }
    ur_decoder_init(&dec, arena, ARENA_SIZE);

    test_in_order();
    test_starting_late();
    test_dropped_frames();
    test_single_part();
    test_mixing_two_messages_is_refused();
    test_single_part_mid_assembly();
    test_arena_is_respected();
    test_garbage_is_refused();

    test_free_all();
    return CHECK_DONE("test_ur_decoder");
}
