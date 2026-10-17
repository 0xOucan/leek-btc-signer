/* Seeds in memory. See seed-storage.h. */
#include "seed-storage.h"

#include <string.h>

#include "wally_core.h"

static seed_t seeds[SEED_STORAGE_MAX];
static int n_seeds;
static seed_t pending;
static bool has_pending;
static uint16_t mnemonic[SEED_MAX_WORDS];
static bool entered[SEED_MAX_WORDS];
static size_t mnemonic_len;

void storage_set_pending(const seed_t *s)
{
    pending = *s;
    has_pending = true;
}

seed_t *storage_pending(void)
{
    return has_pending ? &pending : NULL;
}

void storage_clear_pending(void)
{
    seed_clear(&pending);
    has_pending = false;
}

static bool same(const seed_t *a, const seed_t *b)
{
    return a->n_words == b->n_words &&
           memcmp(a->words, b->words, a->n_words * sizeof a->words[0]) == 0 &&
           strcmp(a->passphrase, b->passphrase) == 0;
}

int storage_finalize(void)
{
    if (!has_pending) {
        return -1;
    }
    for (int i = 0; i < n_seeds; i++) {
        if (same(&seeds[i], &pending)) {
            storage_clear_pending();
            return i;
        }
    }
    if (n_seeds >= SEED_STORAGE_MAX) {
        return -1;
    }
    seeds[n_seeds] = pending;
    storage_clear_pending();
    return n_seeds++;
}

int storage_count(void)
{
    return n_seeds;
}

seed_t *storage_seed(int i)
{
    return i >= 0 && i < n_seeds ? &seeds[i] : NULL;
}

void storage_discard(int i)
{
    if (i < 0 || i >= n_seeds) {
        return;
    }
    for (int k = i; k + 1 < n_seeds; k++) {
        seeds[k] = seeds[k + 1];
    }
    n_seeds--;
    seed_clear(&seeds[n_seeds]);
}

void storage_init_mnemonic(size_t n_words)
{
    storage_discard_mnemonic();
    mnemonic_len = n_words <= SEED_MAX_WORDS ? n_words : SEED_MAX_WORDS;
}

size_t storage_mnemonic_len(void)
{
    return mnemonic_len;
}

void storage_set_word(size_t i, uint16_t word)
{
    if (i < mnemonic_len) {
        mnemonic[i] = word;
        entered[i] = true;
    }
}

int storage_word(size_t i)
{
    return i < mnemonic_len && entered[i] ? mnemonic[i] : -1;
}

bool storage_mnemonic_to_pending(void)
{
    for (size_t i = 0; i < mnemonic_len; i++) {
        if (!entered[i]) {
            return false;
        }
    }
    seed_t s;
    const bool ok = seed_from_indices(&s, mnemonic, mnemonic_len, "") == SEED_OK;
    if (ok) {
        storage_set_pending(&s);
    }
    seed_clear(&s);
    return ok;
}

void storage_discard_mnemonic(void)
{
    wally_bzero(mnemonic, sizeof mnemonic);
    wally_bzero(entered, sizeof entered);
    mnemonic_len = 0;
}

void storage_wipe_all(void)
{
    for (int i = 0; i < SEED_STORAGE_MAX; i++) {
        seed_clear(&seeds[i]);
    }
    n_seeds = 0;
    storage_clear_pending();
    storage_discard_mnemonic();
}
