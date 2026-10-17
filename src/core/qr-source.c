/* The QR frame mailbox. See qr-source.h. */
#include "qr-source.h"

#include <stdatomic.h>
#include <string.h>

static struct {
    uint8_t data[QR_SOURCE_FRAME_MAX];
    size_t len;
} slots[QR_SOURCE_SLOTS];

/* head is written only by the consumer, tail only by the producer. A slot
   is the producer's until tail moves past it, then the consumer's until
   head does. */
static atomic_uint head, tail;
static atomic_uint dropped;

bool qr_source_push(const uint8_t *data, size_t len)
{
    const unsigned t = atomic_load_explicit(&tail, memory_order_relaxed);
    const unsigned h = atomic_load_explicit(&head, memory_order_acquire);
    if (len > QR_SOURCE_FRAME_MAX || t - h >= QR_SOURCE_SLOTS) {
        atomic_fetch_add_explicit(&dropped, 1, memory_order_relaxed);
        return false;
    }
    memcpy(slots[t % QR_SOURCE_SLOTS].data, data, len);
    slots[t % QR_SOURCE_SLOTS].len = len;
    atomic_store_explicit(&tail, t + 1, memory_order_release);
    return true;
}

bool qr_source_next(uint8_t *out, size_t cap, size_t *len)
{
    const unsigned h = atomic_load_explicit(&head, memory_order_relaxed);
    const unsigned t = atomic_load_explicit(&tail, memory_order_acquire);
    if (h == t) {
        return false;
    }
    const size_t n = slots[h % QR_SOURCE_SLOTS].len;
    const bool fits = n <= cap;
    if (fits) {
        memcpy(out, slots[h % QR_SOURCE_SLOTS].data, n);
        *len = n;
    }
    memset(slots[h % QR_SOURCE_SLOTS].data, 0, n);
    atomic_store_explicit(&head, h + 1, memory_order_release);
    return fits;
}

void qr_source_drain(void)
{
    static uint8_t sink[1];
    size_t n;
    while (atomic_load_explicit(&head, memory_order_relaxed) !=
           atomic_load_explicit(&tail, memory_order_acquire)) {
        qr_source_next(sink, 0, &n);
    }
}

unsigned qr_source_dropped(void)
{
    return atomic_load_explicit(&dropped, memory_order_relaxed);
}
