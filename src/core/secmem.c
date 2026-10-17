/* Allocation for secrets. See secmem.h. */
#include "secmem.h"

#include <stdlib.h>

#include "wally_core.h"

static void *host_alloc(size_t n, bool internal_only)
{
    (void)internal_only;    /* the host has one kind of memory */
    return calloc(1, n);
}

static const sec_backend_t HOST = { host_alloc, free };
static const sec_backend_t *backend = &HOST;

void sec_set_backend(const sec_backend_t *b)
{
    backend = b ? b : &HOST;
}

void *sec_alloc(size_t n)
{
    return backend->alloc(n, true);
}

void sec_free(void *p, size_t n)
{
    if (p != NULL) {
        wally_bzero(p, n);
        backend->free(p);
    }
}
