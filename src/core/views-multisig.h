/*
 * SeedSigner's multisig wallet descriptor views (views/seed_views.py) and
 * the Controller's multisig_wallet_descriptor. See views-multisig.c.
 */
#pragma once

#include <stdbool.h>

#include "descriptor.h"

/* controller.multisig_wallet_descriptor, or NULL. */
const ms_descriptor_t *multisig_descriptor(void);

/* What MainMenuView drops: the descriptor. */
void multisig_flow_clear(void);

/* ScanView's wallet descriptor branch, `text` as DecodeQR gave it (NULL
 * where its getter has none): the "/0/(star)" rewrite, the parse, then
 * MultisigWalletDescriptorView in the scan screen's place. The scan screen
 * is on top. */
void multisig_scanned(const char *text);

/* "Scan descriptor" or "Cancel". `replace`: skip_current_view. */
void LoadMultisigWalletDescriptorView(bool replace);
void MultisigWalletDescriptorView(bool replace);
