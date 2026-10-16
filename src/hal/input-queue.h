/*
 * The one queue every input source posts to and the UI task reads.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "input.h"

void input_queue_init(void);
/* From any task. When full, the OLDEST event is dropped: it is the one most
 * likely aimed at a screen that has since changed (LeekWallet button.c). */
void input_post(input_ev_t ev);
/* Wait up to `timeout_ms` (negative = forever). */
bool input_wait(input_ev_t *ev, int timeout_ms);
/* Discard queued events, after a long blocking operation. */
void input_drain(void);

/* The hardware poll task: buttons and encoder (not in QEMU). */
void input_hw_start(void);
