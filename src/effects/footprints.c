/*
 * Copyright (c) 2026 akira-toriyama
 *
 * SPDX-License-Identifier: MIT
 *
 * The footprints (CONFIG_LANTERN_FOOTPRINTS; the pick's footprints), an entry
 * of the effects' table (src/frame.h): each pressed key lights at once, at
 * HI_LEVEL in the palette's next shade (the press's number mod the shade
 * count, the thin ring's rule), and fades out over FOOTPRINT_MS, so that as
 * the user types the last seconds' keys trail behind, the newest the
 * brightest; a key pressed again lights afresh. The user's own idea (canon
 * t-6ttk, 2026-10-28), after three effects of one lit key the user dropped
 * (the press glow, twice, and the held): what tells it apart is the trail,
 * each key staying FOOTPRINT_MS. Nothing spreads, so it measures nothing (no
 * CONFIG_LANTERN_GEOMETRY), and the other half's relayed presses (src/hit.c)
 * light nothing here, their keys having no LED on this half; their hits still
 * live FOOTPRINT_MS in the engine's ring and keep the frames moving that
 * long, drawing nothing.
 *
 * - Its own state (at[], seq1[]): each LED's last press, the time and the
 *   press's number, taken from the live hits at every frame, so that a
 *   footprint outlives its hit's place in the engine's ring (src/keys.c:
 *   HITS, the last sixteen presses of both halves): from the ring alone,
 *   eight presses a second, a burst of typing, would end the oldest
 *   footprint in one frame. A footprint is never newer than the newest live
 *   hit and lives as long, so the frames keep moving while one is lit (the
 *   engine's rule: while a hit lives), and the frame after the last hit's
 *   finds every footprint spent. The press's time is now less the hit's age,
 *   on the engine's clock (k_uptime_get_32()), read here after the engine
 *   read it for the ages, so the time is never put later than now and the
 *   age never wraps; the same press, seen again at the next frame, is known
 *   by its number and keeps the time first taken (a press's number comes
 *   round after sixty presses, more than FOOTPRINT_MS holds). The state goes
 *   with the engine's clear (a blank, a change of the stage or the pick):
 *   after a stage's move the live hits bring their footprints back as they
 *   were, and a press the ring had dropped is gone. A press the ring drops
 *   before a frame runs, more than HITS of both halves' within one frame,
 *   is never seen: the frame shows the last HITS.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include "../effect.h"
#include "../frame.h"

/* How long a footprint lasts, fading out over it: the design's two seconds
 * (canon t-6ttk's starting value, tuned on hardware). */
#define FOOTPRINT_MS 2000
BUILD_ASSERT(FOOTPRINT_MS <= LANTERN_EFFECT_LIFE_MAX_MS, "the footprints outlive a hit");

/* The frame's thread (the header): each LED's last press, its time on the
 * engine's clock and its number + 1, 0 for none. */
static uint32_t at[LED_COUNT];
static uint8_t seq1[LED_COUNT];

static uint32_t footprints_life(void) { return FOOTPRINT_MS; }

/* Each LED's footprint (the header): a live hit on its key goes into the
 * table unless the table holds that press or a newer one; then the table's
 * press, full at 0 and gone at FOOTPRINT_MS, in the shade its number picks. */
static void footprints_draw(struct led_rgb px[], const struct shades *sh, const struct hit live[],
                            size_t n, uint32_t picked) {
    ARG_UNUSED(picked);
    uint32_t now = k_uptime_get_32();

    for (size_t i = 0; i < LED_COUNT; i++) {
        uint8_t key = led_key(i);

        for (size_t h = 0; h < n; h++) {
            if (live[h].key != key || seq1[i] == (uint8_t)(live[h].seq + 1)) {
                continue;
            }
            if (seq1[i] == 0 || live[h].age < now - at[i]) {
                at[i] = now - live[h].age;
                seq1[i] = (uint8_t)(live[h].seq + 1);
            }
        }
        if (seq1[i] == 0) {
            continue;
        }
        uint32_t age = now - at[i];
        if (age >= FOOTPRINT_MS) {
            seq1[i] = 0;
            continue;
        }
        px[i] = mix(px[i], shade_hi(sh, (seq1[i] - 1u) % sh->n),
                    (FOOTPRINT_MS - age) * 256 / FOOTPRINT_MS);
    }
}

static void footprints_clear(void) { memset(seq1, 0, sizeof(seq1)); }

const struct lantern_effect_entry lantern_effect_footprints = {
    .life = footprints_life,
    .draw = footprints_draw,
    .clear = footprints_clear,
};
