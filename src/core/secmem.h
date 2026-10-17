/*
 * Where secrets may live (T33).
 *
 * The ESP32-S3's PSRAM is a separate chip on an external bus, so anything
 * that holds seed, passphrase or key material - and the framebuffer, which
 * draws the words - stays in internal RAM: static, on a task stack, or from
 * sec_alloc() here. sec_alloc() asks the platform for internal RAM only and
 * returns NULL rather than fall back to PSRAM; sec_free() wipes first.
 *
 * Large buffers that hold nothing secret (a PSBT being assembled, an animated
 * code being cut into parts) use plain malloc(), which ESP-IDF places in
 * PSRAM above 16 KB.
 *
 * The platform sets the allocator at boot; host tests install a recording
 * one to prove which memory was asked for.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    /* Zeroed memory; `internal_only` is always true from sec_alloc(). */
    void *(*alloc)(size_t n, bool internal_only);
    void (*free)(void *p);
} sec_backend_t;

void sec_set_backend(const sec_backend_t *b);

void *sec_alloc(size_t n);
void sec_free(void *p, size_t n);
