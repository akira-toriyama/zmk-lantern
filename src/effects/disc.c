/*
 * Copyright (c) 2026 akira-toriyama
 *
 * SPDX-License-Identifier: MIT
 *
 * The disc (CONFIG_LANTERN_DISC; the pick's disc), an entry of the effects'
 * table (src/frame.h): from a pressed key a rim spreads at DISC_SPEED key
 * units a second, and each LED it has passed starts at HI_LEVEL and fades
 * back to the picture over DISC_FADE_MS, so the disc is brightest at its rim
 * and dark behind it; inside it the palette's shades pass in slot order, the
 * LED's colour running once around the cycle through the distinct shades
 * while it fades, shade 0 at the rim. QMK's SPLASH written from its idea
 * (QMK is GPL: nothing copied). An LED under two discs shows the stronger
 * one's colour at their summed strength, as under two rings. A hit lives
 * DISC_MS, long enough for the rim to cross a half of the Imprint and the
 * last LED to fade, or the whole board where the hit (src/hit.c) brings the
 * other half's presses, whose discs come in across the split; the cap
 * (src/keys.c) holds a disc, which covers many LEDs at once, harder than it
 * holds a ring. The distances are the geometry's (src/frame.h).
 */

#include <stddef.h>
#include <stdint.h>

#include <zephyr/sys/util.h>

#include "../effect.h"
#include "../frame.h"

/* Key units a second the disc's rim spreads, the ring's speed. */
#define DISC_SPEED 12
/* How long an LED the rim has passed takes to fade back to the picture. */
#define DISC_FADE_MS 500
/* A disc's life: the rim across a half of the Imprint (8.75 units, 729 ms at
 * 12) and the last LED's fade; with the hit (src/hit.c) the other half's
 * presses come in too, and the rim must cross the whole board (16.8 units,
 * 1397 ms) before the last LED fades, or the far keys went black mid-fade
 * (Opus review, 2026-10-26). */
#if IS_ENABLED(CONFIG_LANTERN_HIT)
#define DISC_MS 1900
#else
#define DISC_MS 1500
#endif
BUILD_ASSERT(DISC_MS <= LANTERN_EFFECT_LIFE_MAX_MS, "the disc outlives a hit");

/* The cycle through the shades at HI_LEVEL: p, 0..255, is how far around,
 * shade 0 at 0 and again past 255, each shade an equal arc, linear between
 * neighbours. */
static struct led_rgb cycle(const struct shades *sh, unsigned int p) {
    unsigned int q = p * sh->n; /* 1/256 shades around; q >> 8 < n since p < 256 */

    return mix(shade_hi(sh, q >> 8), shade_hi(sh, ((q >> 8) + 1) % sh->n), q & 0xFF);
}

/* Each hit's disc (the header): an LED the rim, DISC_SPEED x the hit's age
 * out, reached tau ms ago is pulled toward cycle(tau / DISC_FADE_MS) with
 * strength 1 - tau / DISC_FADE_MS. An LED under several discs takes the
 * strongest one's colour at their summed strength. */
static void disc_draw(struct led_rgb px[], const struct shades *sh, const struct hit live[],
                      size_t n, uint32_t picked) {
    ARG_UNUSED(picked);
    for (size_t i = 0; i < LED_COUNT; i++) {
        unsigned int w = 0, strongest = 0, p = 0;

        for (size_t h = 0; h < n; h++) {
            /* hundredths / (units/s x 100 / 1000) = ms the rim takes */
            uint32_t arrival = distance(led_key(i), live[h].key) * 10 / DISC_SPEED;
            if (live[h].age < arrival) {
                continue;
            }
            uint32_t tau = live[h].age - arrival;
            if (tau >= DISC_FADE_MS) {
                continue;
            }
            unsigned int s = (DISC_FADE_MS - tau) * 256 / DISC_FADE_MS;
            if (s > strongest) {
                strongest = s;
                p = tau * 256 / DISC_FADE_MS;
            }
            w += s;
        }
        if (w > 0) {
            px[i] = mix(px[i], cycle(sh, p), MIN(w, 256));
        }
    }
}

const struct lantern_effect_entry lantern_effect_disc = {
    .life_ms = DISC_MS,
    .draw = disc_draw,
};
