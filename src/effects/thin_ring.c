/*
 * Copyright (c) 2026 akira-toriyama
 *
 * SPDX-License-Identifier: MIT
 *
 * The thin ring (CONFIG_LANTERN_THIN_RING; the pick's thin-ring) and the
 * stain (CONFIG_LANTERN_STAIN; the pick's stain), the thin ring that dyes:
 * two entries of the effects' table (src/frame.h). The ring's entry draws for
 * both bits, with the dye when the stain is picked, so that the ring is drawn
 * once whichever is picked; the stain's entry keeps the dye, paints it into
 * the picture and clears it.
 *
 * - The thin ring: from a pressed key a ring one key wide spreads at
 *   RING_SPEED key units a second and fades out over RING_MS, so it has
 *   crossed the half when it is gone; each press takes the palette's next
 *   shade (the press's number mod the shade count), at HI_LEVEL, and an LED
 *   under two rings shows the stronger one's colour at their summed strength.
 *   The idea is zmk-rgb-matrix's ripple, written from its description (its
 *   code is no clean room, its author says). The distances are the
 *   geometry's (src/frame.h: the coords, an integer square root), so the ring
 *   is round on the board, not in the matrix.
 * - The stain: the thin ring dyes the keys it passes. An LED within
 *   STAIN_REACH of the press keeps the ring's shade as its picture colour, at
 *   the picture's level like a band, from the frame in which the ring's crest
 *   reaches it, so the half is painted over in the day's colours as the user
 *   types; zmk-pk-underglow's rainbow ripple, from its idea. The reach is
 *   where the crest still has STAIN_LEAST of full strength (the ring fades as
 *   it spreads), the design's 0.6; it is judged by the crest's position, not
 *   by the strength a frame happens to sample, because the crest is narrow
 *   and frames 30 ms apart (the engine's FRAME_MS) miss it beyond about
 *   three units (Opus review,
 *   2026-10-21). The newest ring to have passed an LED dyes it. The dye is
 *   the frame thread's alone (stains[], written where the ring is drawn),
 *   goes into the picture before any effect draws (the engine counts it as
 *   picture for the cap) and goes when the countdown moves the picture (the
 *   next key brings it back, and its ring dyes it afresh), when the pick
 *   changes and when the LEDs blank (the entry's clear). In the pick the
 *   stain is the thin ring that dyes: its bit draws the ring, the thin ring's
 *   bit the ring alone, and both together the dyeing ring. It indexes the
 *   shades, so a new palette recolours the dye with it, and a dye past the
 *   new shade count shows the picture.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/sys/util.h>

#include "../effect.h"
#include "../frame.h"

/* The ring's life, how long after the press it still draws. */
#define RING_MS 1000
/* Key units a second the ring's radius grows: at 12 it has crossed a half of
 * the Imprint (8.75 units at most) in under a second. */
#define RING_SPEED 12
/* Hundredths of a key unit from the radius at which the ring is gone: full
 * on the radius, one key wide at half strength. */
#define RING_WIDTH 100
BUILD_ASSERT(RING_MS <= LANTERN_EFFECT_LIFE_MAX_MS, "the thin ring outlives a hit");

#if IS_ENABLED(CONFIG_LANTERN_STAIN)
/* The least strength, of 256, a ring's crest must still have to dye: the
 * design's 0.6. */
#define STAIN_LEAST 154
/* How far the crest keeps that strength, in hundredths of a key unit: it
 * reaches distance d at age d x 10 / RING_SPEED, with 256 x (RING_MS - age)
 * / RING_MS of its strength (the header). */
#define STAIN_REACH ((RING_MS - (RING_MS * STAIN_LEAST + 255) / 256) * RING_SPEED / 10)
BUILD_ASSERT(STAIN_REACH > 0, "STAIN_LEAST leaves the ring no reach");

/* The frame's thread: each LED's dye, a shade's rank + 1, 0 for none (the
 * header). */
static uint8_t stains[LED_COUNT];
#endif

/* Each hit's ring (the header): its radius RING_SPEED x the hit's age, full
 * on the radius and gone RING_WIDTH from it, fading out over RING_MS, in the
 * shade the hit's number picks. An LED under several rings takes the
 * strongest one's colour at their summed strength. With stain, the ring dyes
 * the LEDs its crest reaches (the stain's bit in the pick). */
static void thin_ring(struct led_rgb px[], const struct shades *sh, const struct hit live[],
                      size_t n, bool stain) {
#if !IS_ENABLED(CONFIG_LANTERN_STAIN)
    ARG_UNUSED(stain);
#endif
    for (size_t i = 0; i < LED_COUNT; i++) {
        unsigned int w = 0, strongest = 0;
        size_t pick = 0;
#if IS_ENABLED(CONFIG_LANTERN_STAIN)
        size_t dye = 0; /* the newest hit whose crest has reached this LED within STAIN_REACH, + 1 */
#endif

        for (size_t h = 0; h < n; h++) {
            if (live[h].age >= RING_MS) {
                continue;
            }
            /* ms x units/s / 1000 x 100 hundredths */
            uint32_t r = (uint32_t)live[h].age * RING_SPEED / 10;
            uint32_t d = distance(led_key(i), live[h].key);
#if IS_ENABLED(CONFIG_LANTERN_STAIN)
            if (stain && r >= d && d <= STAIN_REACH &&
                (dye == 0 || live[h].age < live[dye - 1].age)) {
                dye = h + 1;
            }
#endif
            uint32_t off = d > r ? d - r : r - d;
            if (off >= RING_WIDTH) {
                continue;
            }
            unsigned int s = (RING_WIDTH - off) * 256 / RING_WIDTH;
            s = s * (RING_MS - live[h].age) / RING_MS;
            if (s > strongest) {
                strongest = s;
                pick = h;
            }
            w += s;
        }
        if (w > 0) {
            px[i] = mix(px[i], shade_hi(sh, live[pick].seq % sh->n), MIN(w, 256));
        }
#if IS_ENABLED(CONFIG_LANTERN_STAIN)
        if (dye != 0) {
            stains[i] = (uint8_t)(live[dye - 1].seq % sh->n + 1);
        }
#endif
    }
}

static void thin_ring_draw(struct led_rgb px[], const struct shades *sh, const struct hit live[],
                           size_t n, uint32_t picked) {
    thin_ring(px, sh, live, n, (picked & LANTERN_EFFECT_STAIN) != 0);
}

const struct lantern_effect_entry lantern_effect_thin_ring = {
    .serves = LANTERN_EFFECT_STAIN,
    .life_ms = RING_MS,
    .draw = thin_ring_draw,
};

#if IS_ENABLED(CONFIG_LANTERN_STAIN)

/* The dye into the picture: a dyed LED shows its shade at the picture's
 * level; a dye past the shade count shows the picture. */
static void stain_picture(struct led_rgb px[], const struct shades *sh) {
    for (size_t i = 0; i < LED_COUNT; i++) {
        if (stains[i] != 0 && stains[i] <= sh->n) {
            px[i] = sh->at_level[stains[i] - 1];
        }
    }
}

static void stain_clear(void) { memset(stains, 0, sizeof(stains)); }

const struct lantern_effect_entry lantern_effect_stain = {
    .life_ms = RING_MS,
    .picture = stain_picture,
    .clear = stain_clear,
};

#endif /* IS_ENABLED(CONFIG_LANTERN_STAIN) */
