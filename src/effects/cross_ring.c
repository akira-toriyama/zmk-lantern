/*
 * Copyright (c) 2026 akira-toriyama
 *
 * SPDX-License-Identifier: MIT
 *
 * The crossing ring (CONFIG_LANTERN_CROSS_RING; the pick's cross-ring), an
 * entry of the effects' table (src/frame.h): the thin ring's shape, one key
 * wide at CROSS_SPEED key units a second, from a pressed key of either half
 * across the whole board: a press of this half spreads out through the other
 * half, and a press of the other half, relayed by the central (src/hit.c),
 * comes in at the thumbs (the two clusters sit 1.5 units apart) and then the
 * inner columns. It keeps its strength until it has left the board (CROSS_MS
 * covers canon's Imprint, whose farthest two keys are 16.8 units apart; a
 * wider board cuts it short), where the thin ring fades within a half, and
 * its colour is the pressed key's band, the node's slots, so that both halves
 * draw one press in one colour, where the thin ring's count of presses
 * differs between them. A relayed press reaches the other half a few
 * connection events late (7.5 ms each on canon's link, which a USB-powered
 * half attends every one of, src/latency.c), a tenth of a key unit each at
 * the ring's speed; nothing backdates it (canon t-6ttk: a toy, optimistic is
 * fine). The distances are the geometry's (src/frame.h).
 */

#include <stddef.h>
#include <stdint.h>

#include <zephyr/sys/util.h>

#include "../effect.h"
#include "../frame.h"

/* Key units a second the crossing ring's radius grows, the thin ring's
 * speed; full on the radius and gone CROSS_WIDTH hundredths of a unit from
 * it, one key wide. */
#define CROSS_SPEED 12
#define CROSS_WIDTH 100
/* How long a hit draws the crossing ring: until the ring has passed the
 * farthest key of canon's Imprint from any key (16.8 units) by its width,
 * 1.48 s at 12 (the header). */
#define CROSS_MS 1600
BUILD_ASSERT(CROSS_MS <= LANTERN_EFFECT_LIFE_MAX_MS, "the crossing ring outlives a hit");

/* Each hit's crossing ring (the header): its radius CROSS_SPEED x the hit's
 * age, full on the radius and gone CROSS_WIDTH from it, in the shade of the
 * pressed key's slot. An LED under several rings takes the strongest one's
 * colour at their summed strength. */
static void cross_ring_draw(struct led_rgb px[], const struct shades *sh, const struct hit live[],
                            size_t n, uint32_t picked) {
    ARG_UNUSED(picked);
    for (size_t i = 0; i < LED_COUNT; i++) {
        unsigned int w = 0, strongest = 0;
        size_t pick = 0;

        for (size_t h = 0; h < n; h++) {
            if (live[h].age >= CROSS_MS) {
                continue;
            }
            /* ms x units/s / 1000 x 100 hundredths */
            uint32_t r = (uint32_t)live[h].age * CROSS_SPEED / 10;
            uint32_t d = distance(led_key(i), live[h].key);
            uint32_t off = d > r ? d - r : r - d;
            if (off >= CROSS_WIDTH) {
                continue;
            }
            unsigned int s = (CROSS_WIDTH - off) * 256 / CROSS_WIDTH;
            if (s > strongest) {
                strongest = s;
                pick = h;
            }
            w += s;
        }
        if (w > 0) {
            px[i] = mix(px[i], shade_hi(sh, sh->rank[key_slot(live[pick].key)]), MIN(w, 256));
        }
    }
}

const struct lantern_effect_entry lantern_effect_cross_ring = {
    .life_ms = CROSS_MS,
    .draw = cross_ring_draw,
};
