/*
 * Copyright (c) 2026 akira-toriyama
 *
 * SPDX-License-Identifier: MIT
 *
 * The key LEDs' picture (src/keys.c), for the rail rule (src/on_usb_power.c),
 * the palette (src/palette.c), the hit (src/hit.c), the pick of the effects
 * (src/effect.c) and an effect's state set from outside the frame (the
 * layer keys', src/effects/layer_keys.c). Layer contract: this file is the
 * strip's one writer; callers say lit or dark, when the boot light lasts,
 * which key was pressed on the other half, that the pick changed and that
 * something a frame draws from changed, and never touch a pixel. The frame
 * is written on ZMK's low priority work queue; the
 * countdown's clock runs on the system work queue, so the light, the blank
 * and the boot light are called from there (the rail rule's thread), the
 * refresh, the hit and the pick's change from any thread.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/sys/util.h>

/* Paint the palette's picture (white without a palette; the no picture's
 * black stays black) and start the countdown. Call with the rail on and
 * settled, on the system work queue. */
void lantern_keys_light(void);

/* Paint every LED black and stop the countdown. The frame is on its way when
 * this returns, not out: keep the rail on until it is (src/on_usb_power.c
 * waits BLANK_MS). System work queue. */
void lantern_keys_blank(void);

/* Something a frame draws from changed (the palette, the layer keys' state):
 * repaint while lit, nothing while dark. Any thread. */
void lantern_keys_refresh(void);

/* The boot light's state, before every light (on only for the boot light's)
 * and again at the boot light's end with the LEDs lit (off). On: the picked
 * intro (src/intro.h) starts, and its length is returned, how long the rail
 * rule keeps the LEDs lit for it, whose frames are then the intro's. Off: the
 * intro ends, which sends a frame while lit (src/keys.c); 0. Without an intro
 * built (CONFIG_LANTERN_INTROS) this compiles to nothing and returns 0: no
 * boot light. System work queue. */
#if IS_ENABLED(CONFIG_LANTERN_INTROS)
uint32_t lantern_keys_boot_light(bool on);
#else
static inline uint32_t lantern_keys_boot_light(bool on) {
    ARG_UNUSED(on);
    return 0;
}
#endif

/* A key press of another half, relayed by the split central (src/hit.c):
 * noted as a hit for the effects, as this half's own press, while the LEDs
 * are lit and no intro is playing; the countdown's clock is not moved.
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
