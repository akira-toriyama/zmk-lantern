/*
 * Copyright (c) 2026 akira-toriyama
 *
 * SPDX-License-Identifier: MIT
 *
 * The rail rule (src/on_usb_power.c), for the hit (src/hit.c). Layer
 * contract: a caller hands the rule an input and never its state, which
 * lives on the system work queue; the rule decides lit or dark and tells
 * src/keys.c.
 */

#pragma once

#include <zephyr/sys/util.h>

/* A key press of another half, relayed by the split central: the half is in
 * use for CONFIG_ZMK_IDLE_TIMEOUT ms from now, as from its own input, for
 * the LEDs only, ZMK's activity untouched (the
 * wake, src/on_usb_power.c). Any thread; stores the time and submits work.
 * Without the wake (CONFIG_LANTERN_WAKE_ON_HIT) this compiles to nothing. */
#if IS_ENABLED(CONFIG_LANTERN_WAKE_ON_HIT)
void lantern_rule_hit(void);
#else
static inline void lantern_rule_hit(void) {}
#endif
