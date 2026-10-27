/*
 * Copyright (c) 2026 akira-toriyama
 *
 * SPDX-License-Identifier: MIT
 *
 * The key LEDs' picture (src/keys.c), for the rail rule (src/on_usb_power.c),
 * the palette (src/palette.c), the hit (src/hit.c) and the pick of the
 * effects (src/effect.c). Layer contract: this file is the strip's one
 * writer; callers say lit or dark, when the boot light lasts, which key was
 * pressed on the other half and that the pick changed, and never touch a
 * pixel. The frame is written on ZMK's low priority work queue; the
 * countdown's clock runs on the system work queue, so the light, the blank
 * and the boot light are called from there (the rail rule's thread), the
 * refresh, the hit and the pick's change from any thread.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/sys/util.h>

/* The intro's length (CONFIG_LANTERN_INTRO): the rail rule keeps the LEDs lit
 * this long from the light that starts it, the boot light's window. The
 * script that fills it is src/keys.c's, which asserts it fits. */
#define LANTERN_INTRO_MS 10000

/* Paint the palette's picture (white without a palette; the no picture's
 * black stays black) and start the countdown. Call with the rail on and
 * settled, on the system work queue. */
void lantern_keys_light(void);

/* Paint every LED black and stop the countdown. The frame is on its way when
 * this returns, not out: keep the rail on until it is (src/on_usb_power.c
 * waits BLANK_MS). System work queue. */
void lantern_keys_blank(void);

/* The palette changed: repaint while lit, nothing while dark. Any thread. */
void lantern_keys_refresh(void);

/* The boot light's state, before every light (on only for the boot light's)
 * and again at the boot light's end with the LEDs lit (off): the intro plays
 * from the light it is on for, and the no picture, which paints every key
 * black, shows the bands for it without the intro; either sends a frame when
 * it ends (src/keys.c). For every other build this compiles to nothing.
 * System work queue. */
#if IS_ENABLED(CONFIG_LANTERN_NO_PICTURE) || IS_ENABLED(CONFIG_LANTERN_INTRO)
void lantern_keys_boot_light(bool on);
#else
static inline void lantern_keys_boot_light(bool on) { ARG_UNUSED(on); }
#endif

/* A key press of another half, relayed by the split central (src/hit.c):
 * noted as a hit for the effects, as this half's own press, while the LEDs
 * are lit and the intro is not playing; the countdown's clock is not moved.
 * Any of ZMK's threads, which are cooperative (src/keys.c says what a
 * preemptible caller could leave behind). Without an effect
 * (CONFIG_LANTERN_EFFECTS) this compiles to nothing. */
#if IS_ENABLED(CONFIG_LANTERN_EFFECTS)
void lantern_keys_hit(uint8_t position);
#else
static inline void lantern_keys_hit(uint8_t position) { ARG_UNUSED(position); }
#endif

/* The pick of the effects changed (src/effect.c): the effects' state goes
 * (the hits, and the stain's dye at the next frame) and the LEDs are
 * repainted while lit, so the old effect's rings do not turn into the new
 * one's shape. Any thread. Without an effect this compiles to nothing. */
#if IS_ENABLED(CONFIG_LANTERN_EFFECTS)
void lantern_keys_effect_changed(void);
#else
static inline void lantern_keys_effect_changed(void) {}
#endif
