/*
 * Copyright (c) 2026 akira-toriyama
 *
 * SPDX-License-Identifier: MIT
 *
 * The held keys (CONFIG_LANTERN_HELD, canon t-6ttk; the pick's held), an
 * entry of the effects' table (src/frame.h): a key of this half held down
 * HELD_AFTER_MS or longer lights in its band's shade (the key's slot, the
 * crossing ring's colour source) at HI_LEVEL until it is let go, and then
 * fades back to the picture over HELD_FADE_MS; a key let go sooner, a tap,
 * shows nothing, so what shows is what is being held, a layer key, a
 * modifier, an arrow key held down, while typing shows nothing (the press
 * glow, every press lighting its key, was dropped on canon's scores). The
 * research's key held down (canon t-6ttk), the module's own idea. The holds
 * are the engine's (src/keys.c, CONFIG_LANTERN_HOLDS, which this selects):
 * this half's own keys only, since a relayed press (src/hit.c) carries no
 * release, so the other half's held keys show on that half. No distance,
 * so no geometry. The life is the fade: how long the engine keeps a key let
 * go for this entry (and a hit, which this entry never draws). The engine settles a key down past LANTERN_HOLD_SETTLE_MS and moves
 * no frame for it, so the moment must come before that (asserted).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/sys/util.h>

#include "../effect.h"
#include "../frame.h"

/* A key down this long lights, the design's 150 ms; let go, it fades over
 * this long, the design's 0.2 s. */
#define HELD_AFTER_MS 150
#define HELD_FADE_MS 200
BUILD_ASSERT(HELD_AFTER_MS < LANTERN_HOLD_SETTLE_MS,
             "the held keys light after the engine settles a key: no frame would show it");
BUILD_ASSERT(HELD_FADE_MS <= LANTERN_EFFECT_LIFE_MAX_MS, "the held keys outlive a hold");

static uint32_t held_life(void) { return HELD_FADE_MS; }

/* Each hold (the header): a key down HELD_AFTER_MS or longer at full strength
 * while down, fading out over HELD_FADE_MS once let go; a key let go sooner
 * is nothing. One hold a key, so nothing sums. */
static void held_hold(struct led_rgb px[], const struct shades *sh, const struct hold held[],
                      size_t m) {
    for (size_t h = 0; h < m; h++) {
        if (held[h].held < HELD_AFTER_MS) {
            continue;
        }
        unsigned int w = 256;

        if (held[h].released) {
            if (held[h].age >= HELD_FADE_MS) {
                continue;
            }
            w = (HELD_FADE_MS - held[h].age) * 256 / HELD_FADE_MS;
        }
        struct led_rgb c = shade_hi(sh, sh->rank[key_slot(held[h].key)]);

        for (size_t i = 0; i < LED_COUNT; i++) {
            if (led_key(i) == held[h].key) {
                px[i] = mix(px[i], c, w);
            }
        }
    }
}

const struct lantern_effect_entry lantern_effect_held = {
    .life = held_life,
    .hold = held_hold,
};
