/*
 * SeedSigner's settings, ported: the definition table entry by entry, the
 * defaults, and SettingsQR parsing, against what SeedSigner's own code
 * gives (sim/vectors/seedsigner_settings.h, by gen_settings_vectors.py):
 * every SettingsQR its tests parse and generated ones for every option,
 * each with SeedSigner's result or the exception it raises.
 */
#include <stdio.h>
#include <string.h>

#include "check.h"
#include "settings.h"
#include "vectors/seedsigner_settings.h"

#define N(a) (sizeof(a) / sizeof((a)[0]))

static bool same(const char *a, const char *b)
{
    return (a == NULL && b == NULL) || (a && b && strcmp(a, b) == 0);
}

static void test_definition(void)
{
    printf("== the settings definition, entry by entry\n");
    CHECK(SETTINGS_DEF_COUNT == (int)N(VEC_SETTINGS), "%d settings, SeedSigner has %zu", SETTINGS_DEF_COUNT,
          N(VEC_SETTINGS));
    settings_reset();
    for (size_t i = 0; i < N(VEC_SETTINGS) && (int)i < SETTINGS_DEF_COUNT; i++) {
        const vec_setting_t *v = &VEC_SETTINGS[i];
        const setting_def_t *d = &SETTINGS_DEF[i];
        CHECK(same(d->category, v->category) && same(d->attr_name, v->attr_name) &&
              same(d->abbreviated_name, v->abbreviated_name) && same(d->display_name, v->display_name) &&
              same(d->help_text, v->help_text) && same(setting_vis_name(d->visibility), v->visibility) &&
              same(setting_type_name(d->type), v->type), "%s differs", v->attr_name);
        CHECK(d->n_options == (int)v->n_options, "%s: %d options, want %u", v->attr_name, d->n_options,
              v->n_options);
        for (int k = 0; k < d->n_options && k < (int)v->n_options; k++) {
            CHECK(same(d->options[k].value, v->options[k].value) &&
                  same(d->options[k].display_name, v->options[k].display_name),
                  "%s option %d differs", v->attr_name, k);
        }
        char text[64];
        settings_value_text((int)i, text, sizeof text);
        CHECK(same(text, v->default_value), "%s defaults to %s, want %s", v->attr_name, text, v->default_value);
        CHECK(setting_by_attr(v->attr_name) == (int)i && setting_by_abbreviated(v->abbreviated_name) == (int)i,
              "%s lookups", v->attr_name);
    }
}

static void test_settingsqr(void)
{
    printf("== SettingsQR: every parse SeedSigner made\n");
    unsigned ok = 0, refused = 0;
    for (size_t i = 0; i < N(VEC_SETTINGSQR); i++) {
        const vec_settingsqr_t *v = &VEC_SETTINGSQR[i];
        settingsqr_t q;
        const char *err = settingsqr_parse(v->data, &q);
        CHECK(same(err, v->error), "case %zu: error %s, want %s\n    %.80s", i, err ? err : "none",
              v->error ? v->error : "none", v->data);
        if (err || v->error) {
            refused += err != NULL;
            continue;
        }
        CHECK(same(q.has_name ? q.name : NULL, v->name), "case %zu: name \"%s\", want \"%s\"", i, q.name,
              v->name ? v->name : "(none)");
        char updates[2048] = "";
        size_t k = 0;
        for (int u = 0; u < q.n && k < sizeof updates; u++) {
            k += (size_t)snprintf(updates + k, sizeof updates - k, "%s=%s\n",
                                  SETTINGS_DEF[q.u[u].id].attr_name, q.u[u].text);
        }
        CHECK(same(updates, v->updates), "case %zu: updates\n%s    want\n%s", i, updates, v->updates);

        /* Applied, each setting holds what was named (as a set: "nat,nat"
           is "nat"). */
        settings_reset();
        settingsqr_apply(&q);
        for (int u = 0; u < q.n; u++) {
            uint32_t want = 0;
            const setting_def_t *d = &SETTINGS_DEF[q.u[u].id];
            char text[64];
            snprintf(text, sizeof text, "%s", q.u[u].text);
            for (char *t = strtok(text, ","); t; t = strtok(NULL, ",")) {
                for (int o = 0; o < d->n_options; o++) {
                    if (strcmp(d->options[o].value, t) == 0) want |= 1u << o;
                }
            }
            if (d->type != STYPE_MULTISELECT && want) {
                want &= -want;      /* a single select holds one */
            }
            CHECK(settings_get_mask(q.u[u].id) == want, "case %zu: %s not applied", i, d->attr_name);
        }
        ok++;
    }
    printf("   %u parsed and applied as SeedSigner's, %u refused as SeedSigner refuses them\n", ok, refused);
}

int main(void)
{
    test_definition();
    test_settingsqr();
    settings_reset();
    return CHECK_DONE("test_settings");
}
