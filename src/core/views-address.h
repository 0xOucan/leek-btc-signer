/*
 * SeedSigner's address verification (views/seed_views.py and
 * scan_views.py), ported. See views-address.c.
 */
#pragma once

#include <stdbool.h>

#include "btc.h"

/* A scan completed with an address: AddressVerificationStartView, in the
 * scan screen's place (skip_current_view). */
void address_scanned(const char *address, btc_script_t script, btc_net_t net);

/* SeedOptionsView's redirect when a seed was loaded to verify an address
 * (resume_main_flow == FLOW__VERIFY_SINGLESIG_ADDR): opens the
 * verification with it and returns true. */
bool address_verify_resume(int seed);

/* What MainMenuView drops: the unverified address and the flow. */
void address_flow_clear(void);
