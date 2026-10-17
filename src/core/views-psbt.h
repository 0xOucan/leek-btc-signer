/*
 * SeedSigner's PSBT views (views/psbt_views.py), ported. See views-psbt.c.
 *
 * The flow's state is SeedSigner's Controller fields: the PSBT in memory
 * (controller.psbt), the seed chosen to sign it (psbt_seed, an index into
 * seed-storage), the parse (psbt_parser) and resume_main_flow, which sends a
 * seed loaded mid-flow back into the review. Going home clears it, as
 * MainMenuView does.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* A scan completed with a PSBT: keep a copy and open PSBTSelectSeedView in
 * place of the scan screen (skip_current_view). False if out of memory. */
bool psbt_flow_scanned(const uint8_t *psbt, size_t len);

/* SeedOptionsView's "Scan transaction": the seed that will sign. */
void psbt_flow_set_seed(int seed);

/* SeedOptionsView's redirect: if a PSBT is waiting for a seed loaded with
 * resume_main_flow set, and `seed` looks like it can sign it, make it the
 * signing seed, open PSBTOverviewView and return true. */
bool psbt_flow_resume(int seed);

/* What MainMenuView drops: the PSBT, the parse, the signing seed and the
 * flow to resume. */
void psbt_flow_clear(void);

bool psbt_flow_loaded(void);

/* For the flow tests: the PSBT in memory (signed, once signing has run),
 * the signing seed (-1 for none) and whether a parse is held. */
const uint8_t *psbt_flow_bytes(size_t *len);
int psbt_flow_seed(void);
bool psbt_flow_parsed(void);

void PSBTSelectSeedView(void);
void PSBTOverviewView(void);
