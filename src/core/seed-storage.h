/*
 * The seeds in memory: SeedSigner's SeedStorage (models/seed_storage.py).
 *
 * Loaded seeds, the one pending (scanned or typed, not yet finalised), and
 * the mnemonic being typed word by word. All of it is static, in internal
 * RAM (secmem.h), and nothing here survives a power cycle: the device is
 * stateless unless the user opts into the vault (Phase 5).
 *
 * SeedSigner keeps any number of seeds; this keeps SEED_STORAGE_MAX and
 * refuses one more rather than allocate.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "btc.h"

#define SEED_STORAGE_MAX 8

/* The pending seed. */
void storage_set_pending(const seed_t *s);
seed_t *storage_pending(void);              /* NULL if none */
void storage_clear_pending(void);
/* finalize_pending_seed(): store it (once: a seed already held, by
 * Seed.__eq__, the same seed bytes, is not added twice) and return its
 * index, or -1 if storage is full. */
int storage_finalize(void);

int storage_count(void);
seed_t *storage_seed(int i);
/* SeedSigner's discard_seed(): wipe it and close the gap. */
void storage_discard(int i);

/* The mnemonic being entered. init_pending_mnemonic(): `electrum` makes it
 * an ElectrumSeed when converted. */
void storage_init_mnemonic(size_t n_words);
void storage_init_electrum_mnemonic(void);
bool storage_mnemonic_is_electrum(void);
size_t storage_mnemonic_len(void);
void storage_set_word(size_t i, uint16_t word);
int storage_word(size_t i);                 /* -1 if not entered yet */
/* convert_pending_mnemonic_to_pending_seed(): false on a bad checksum, or
 * for an Electrum mnemonic, a seed that is not Electrum's segwit kind
 * (SeedSigner's InvalidSeedException), the mnemonic kept for editing. */
bool storage_mnemonic_to_pending(void);
void storage_discard_mnemonic(void);

/* Everything, wiped: for power off, restart and the tests. */
void storage_wipe_all(void);
