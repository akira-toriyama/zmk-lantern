/*
 * Copyright (c) 2026 akira-toriyama
 *
 * SPDX-License-Identifier: MIT
 *
 * The heatmap (CONFIG_LANTERN_HEATMAP; the pick's heatmap), an entry of the
 * effects' table (src/frame.h): each key holds a heat, which a press of it
 * raises by HEAT_PRESS and a press within HEAT_REACH of it by up to HEAT_NEAR,
 * less the farther, and which cools at HEAT_COOL a second, so that as the
 * user types the keys they type most, and the keys around those, glow, the
 * hotter the brighter and the nearer the palette's first colour, and when the
 * typing stops the glow cools to the palette's last colour and goes out. A
 * key's brightness is full from HEAT_FULL on, a third of the way up (three
 * presses of a key within a second or so), and its colour runs the palette
 * from the last shade cold to the first shade at the top of the scale. QMK's
 * TYPING_HEATMAP written from its idea (QMK is GPL: nothing copied), the
 * brightness ramp included, so that one press shows and a few turn the
 * colour. Each half keeps its own heat and warms from its own presses only:
 * the other half's relayed presses (src/hit.c) warm nothing here, their keys
 * being on the other half, under its LEDs (canon t-6ttk, 2026-10-28: the
 * design's choice); their hits still live the picked effects' life in the
 * engine's ring and keep the frames moving that long, warming nothing.
 *
 * - Its own state (heat[]): each LED's heat, in HEAT_ONE-ths, raised by the
 *   presses the live hits bring and cooled by the time between two frames on
 *   the engine's clock (k_uptime_get_32(), read here after the engine read
 *   it for the ages, so the frame's time never runs ahead of a hit's). A
 *   press is counted once, by its number: the engine's ring (src/keys.c:
 *   HITS, the last sixteen presses of both halves) lists a hit at every frame
 *   while it lives, and the entry keeps the number of the newest press it
 *   has counted, so a hit whose number is at most LANTERN_HIT_SEQ_CYCLE / 2
 *   ahead of it is new and one at or behind it is counted already; a stall
 *   of the frames long enough for more than that many presses (a second and
 *   a half at HEATMAP_TOP_RATE a second) would miss the overrun. The frame
 *   replays the presses it finds new in the order of their times (now less
 *   the hit's age), cooling every key up to each press's time before it adds
 *   the press, then up to now, so that a late frame (a frame's period after
 *   the press at best, a stalled frame's delay at worst) warms and cools as
 *   the frames on time would have: the cooling is the key's, at one rate
 *   whatever its heat, so a press's heat is never cooled apart from the
 *   key's (a first version did, and two presses in one frame cooled twice).
 *   A press the ring drops before a frame runs, more than HITS of both
 *   halves' within one frame, is never seen: the frame counts the last HITS.
 *   The heat outlives the hit's place in the ring: a burst of typing drops
 *   the oldest hits, and their heat cools on as it should.
 * - The life is the cooling of the hottest key, HEAT_ONE at HEAT_COOL a
 *   second (HEATMAP_MS, which the assert below keeps at or past it): a hit
 *   keeps the frames moving until every key it could have warmed is cold,
 *   and the frame with no live hit draws nothing and clears the heat and the
 *   count, so that nothing of a spent run reaches the next (the footprints'
 *   rule; here the heat is cold by then and the next run would cool it
 *   across the gap anyway, so this is hygiene, not a fix: the host harness
 *   passes without it). The engine's clear (a blank, a change of the stage
 *   or the pick) goes the same way; after a stage's move the live hits are
 *   replayed from the oldest, so the keys warm again to what they were, less
 *   what a press the ring had dropped gave them.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include "../effect.h"
#include "../frame.h"

/* One of heat: the scale's top, where a key is as hot as it gets. */
#define HEAT_ONE 65536u
/* What a press adds to its own key, and the most it adds to a key within
 * HEAT_REACH (hundredths of a key unit) of it, falling to nothing at the
 * reach; what a key sheds a second (canon t-6ttk's starting values, QMK's
 * proportions, tuned on hardware). */
#define HEAT_PRESS (HEAT_ONE / 8)
#define HEAT_NEAR (HEAT_ONE * 6 / 100)
#define HEAT_REACH 290u
#define HEAT_COOL (HEAT_ONE * 15 / 100)
/* The heat at which a key's brightness is full: a third of the way up, as
 * QMK's; the colour runs the whole scale. */
#define HEAT_FULL (HEAT_ONE / 3)
/* How long a hit lives: the hottest key cooling to nothing, rounded up to
 * the second. */
#define HEATMAP_MS 7000
BUILD_ASSERT(HEATMAP_MS <= LANTERN_EFFECT_LIFE_MAX_MS, "the heatmap outlives a hit");
BUILD_ASSERT((uint64_t)HEATMAP_MS * HEAT_COOL >= (uint64_t)HEAT_ONE * 1000,
             "HEATMAP_MS: a key could still be warm when its last hit's life ends");
/* The most presses a second, both halves together, at which a stall of the
 * frames must last LANTERN_HIT_SEQ_CYCLE / 2 presses for one to be missed
 * (the header). */
#define HEATMAP_TOP_RATE 20
BUILD_ASSERT(1000 * (LANTERN_HIT_SEQ_CYCLE / 2) / HEATMAP_TOP_RATE >= 1500,
             "LANTERN_HIT_SEQ_CYCLE: a stall of the frames under 1.5 s could miss a press");

/* The frame's thread (the header): each LED's heat, the number + 1 of the
 * newest press counted (0 for none yet), the engine's clock at the last
 * frame (the time the heat stands at) and whether that reading is valid. */
static uint32_t heat[LED_COUNT];
static uint8_t last_seq1;
static uint32_t last_now;
static bool running;

static uint32_t heatmap_life(void) { return HEATMAP_MS; }

static void heatmap_clear(void) {
    memset(heat, 0, sizeof(heat));
    last_seq1 = 0;
    running = false;
}

/* Every key cools by ms. */
static void cool(uint32_t ms) {
    uint32_t lost = (uint32_t)(((uint64_t)ms * HEAT_COOL + 500) / 1000);

    for (size_t i = 0; i < LED_COUNT; i++) {
        heat[i] = heat[i] > lost ? heat[i] - lost : 0;
    }
}

/* Whether the press numbered seq comes after the one numbered seq1 - 1 (the
 * header): by one to LANTERN_HIT_SEQ_CYCLE / 2, the count cycling; every
 * press does for seq1 0, none counted yet. */
static bool after(uint8_t seq, uint8_t seq1) {
    if (seq1 == 0) {
        return true;
    }
    uint32_t ahead = (seq + LANTERN_HIT_SEQ_CYCLE - (seq1 - 1u)) % LANTERN_HIT_SEQ_CYCLE;

    return ahead >= 1 && ahead <= LANTERN_HIT_SEQ_CYCLE / 2;
}

/* Whether a key is under an LED of this half: a relayed press's key is not. */
static bool here(uint8_t key) {
    for (size_t i = 0; i < LED_COUNT; i++) {
        if (led_key(i) == key) {
            return true;
        }
    }
    return false;
}

/* A press at key into the heat: its own key and those within reach. */
static void warm(uint8_t key) {
    for (size_t i = 0; i < LED_COUNT; i++) {
        uint8_t k = led_key(i);
        uint32_t add;

        if (k == key) {
            add = HEAT_PRESS;
        } else {
            uint32_t d = distance(k, key);
            if (d >= HEAT_REACH) {
                continue;
            }
            add = HEAT_NEAR * (HEAT_REACH - d) / HEAT_REACH;
        }
        heat[i] = MIN(heat[i] + add, HEAT_ONE);
    }
}

/* Each LED's heat (the header): with no live hit the heat goes; else the
 * frame replays the live hits not yet counted from the oldest, cooling every
 * key up to each press's time (from the last frame's, or from the oldest
 * press where this is the first frame) and warming the press's keys, then
 * cools every key up to now; and each LED shows its heat, full from
 * HEAT_FULL, in the shade its heat picks along the palette, the last shade
 * cold and the first at the top. */
static void heatmap_draw(struct led_rgb px[], const struct shades *sh, const struct hit live[],
                         size_t n, uint32_t picked) {
    ARG_UNUSED(picked);
    if (n == 0) {
        heatmap_clear();
        return;
    }
    uint32_t now = k_uptime_get_32();

    /* the presses not yet counted, oldest first (n is at most the ring's
     * length: an insertion sort of their indices), and the newest of them */
    uint8_t fresh[32];
    size_t m = 0;
    uint8_t newest1 = last_seq1;

    for (size_t h = 0; h < n && m < ARRAY_SIZE(fresh); h++) {
        if (!after(live[h].seq, last_seq1)) {
            continue;
        }
        size_t j = m;
        while (j > 0 && live[fresh[j - 1]].age < live[h].age) {
            fresh[j] = fresh[j - 1];
            j--;
        }
        fresh[j] = (uint8_t)h;
        m++;
        if (after(live[h].seq, newest1)) {
            newest1 = (uint8_t)(live[h].seq + 1);
        }
    }
    last_seq1 = newest1;

    uint32_t cursor = running ? last_now : m > 0 ? now - live[fresh[0]].age : now;

    for (size_t f = 0; f < m; f++) {
        uint32_t at = now - live[fresh[f]].age;

        if ((int32_t)(at - cursor) > 0) {
            cool(at - cursor);
            cursor = at;
        }
        if (here(live[fresh[f]].key)) {
            warm(live[fresh[f]].key);
        }
    }
    if ((int32_t)(now - cursor) > 0) {
        cool(now - cursor);
    }
    last_now = now;
    running = true;

    for (size_t i = 0; i < LED_COUNT; i++) {
        if (heat[i] == 0) {
            continue;
        }
        unsigned int bright = MIN(heat[i] * 256 / HEAT_FULL, 256);
        unsigned int cold = 256 - heat[i] * 256 / HEAT_ONE;

        px[i] = mix(px[i], span(sh, cold), bright);
    }
}

const struct lantern_effect_entry lantern_effect_heatmap = {
    .life = heatmap_life,
    .draw = heatmap_draw,
    .clear = heatmap_clear,
};
