/*
 * Copyright (c) 2026 akira-toriyama
 *
 * SPDX-License-Identifier: MIT
 *
 * The bands (CONFIG_LANTERN_INTRO_BANDS; the pick's bands), an entry of the
 * intros' table (src/frame.h): the palette's bands for BANDS_MS at boot and
 * whenever USB power arrives, each key its slot's shade (the lantern,keys
 * node's slots) at the picture's level, so that a half shows it has power
 * whatever its picture (the no picture paints every key black; canon t-6ttk,
 * the user's choice of 2026-10-19), and the intro a consumer has without a
 * line. What the boot light of before the table showed for the no picture and
 * the bands picture; for the solid, the gradient and the scatter it showed
 * their own picture, which this does not. One still frame: the engine ticks
 * nothing for it, and the rail rule ends the light.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/sys/util.h>

#include "../frame.h"
#include "../intro.h"

/* How long the rail rule keeps the LEDs lit for it, the boot light's second
 * (the rail rule ends a light of 0 at once). */
#define BANDS_MS 1000
BUILD_ASSERT(BANDS_MS > 0, "the bands need a length");

static bool bands_draw(struct led_rgb px[], const struct shades *sh, uint32_t t) {
    ARG_UNUSED(t);
    for (size_t i = 0; i < LED_COUNT; i++) {
        px[i] = sh->at_level[sh->rank[key_slot(led_key(i))]];
    }
    return false;
}

const struct lantern_intro_entry lantern_intro_bands = {
    .length_ms = BANDS_MS,
    .draw = bands_draw,
};
