/*
 * SeedSigner's message signing (views/seed_views.py, and OptionDisabledView
 * and NetworkMismatchErrorView from views/view.py), ported. See
 * views-message.c.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

/* A scan completed with a signmessage request: SeedSignMessageStartView,
 * in the scan screen's place. */
void message_scanned(const char *path, size_t path_len, const char *msg, size_t msg_len);

/* SeedOptionsView's "Sign message": the seed is chosen, then the request
 * is scanned. */
void message_from_seed(int seed);

/* SeedOptionsView's redirect when a seed was loaded to sign a message
 * (resume_main_flow == FLOW__SIGN_MESSAGE). */
bool message_resume(int seed);

/* What MainMenuView drops: sign_message_data and the flow. */
void message_flow_clear(void);
