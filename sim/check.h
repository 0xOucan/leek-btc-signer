/* Minimal test harness: CHECK records a failure and carries on. */
#pragma once
#include <stdio.h>

static int check_failures;
#define CHECK(c, ...) do { if (!(c)) { printf("  FAIL %s:%d: ", __FILE__, __LINE__); \
    printf(__VA_ARGS__); printf("\n"); check_failures++; } } while (0)
#define CHECK_DONE(name) (printf("%s %s (%d failure%s)\n", check_failures ? "FAILED" : "PASSED", \
    (name), check_failures, check_failures == 1 ? "" : "s"), check_failures ? 1 : 0)
