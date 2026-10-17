/* Minimal test harness: CHECK records a failure and carries on. */
#pragma once
#include <stdio.h>

static int check_failures;
#define CHECK(c, ...) do { if (!(c)) { printf("  FAIL %s:%d: ", __FILE__, __LINE__); \
    printf(__VA_ARGS__); printf("\n"); check_failures++; } } while (0)
#define CHECK_DONE(name) (printf("%s %s (%d failure%s)\n", check_failures ? "FAILED" : "PASSED", \
    (name), check_failures, check_failures == 1 ? "" : "s"), check_failures ? 1 : 0)

/* Test buffers on the heap rather than static: the suites also run in the
 * QEMU self-test image, which has no PSRAM and cannot hold every suite's
 * buffers at once. test_free_all() releases them on every way out. */
#include <stdlib.h>
static void *check_allocs[32];
static int check_nallocs;
__attribute__((unused)) static void *test_alloc(size_t n)
{
    void *p = check_nallocs < 32 ? calloc(1, n) : NULL;
    if (p) {
        check_allocs[check_nallocs++] = p;
    }
    return p;
}
__attribute__((unused)) static void test_free_all(void)
{
    while (check_nallocs > 0) {
        free(check_allocs[--check_nallocs]);
    }
}
