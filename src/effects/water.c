/*
 * Copyright (c) 2026 akira-toriyama
 *
 * SPDX-License-Identifier: MIT
 *
 * The water (CONFIG_LANTERN_WATER, canon t-6ttk; the pick's water), an entry
 * of the effects' table (src/frame.h): from a pressed key waves spread, a
 * front at WATER_SPEED key units a second and behind it the sine of the
 * front's distance past each key over WATER_WAVE, fading out over WATER_MS
 * and falling off with the distance from the key (by WATER_FALL), and the
 * waves of every live press add up at each LED, so that two crests meeting
 * rise higher and a crest meeting a trough stays still:
 * zmk-advanced-underglow-effects-vfx's water, from its idea (nothing copied).
 * The LED is pulled along span() (src/frame.h), the shades end to end, by the
 * surface's height, a full crest to shade 0, a full trough to the last shade
 * and a lower wave to the shades between, and as far as the height's size, so
 * the still surface shows the picture (the no picture's black), the pressed
 * key's first waves flicker between the first and last shades, once a
 * wavelength (four times a second at the speed and wavelength above), and
 * lower waves, later or farther out, between shades nearer the middle. Three
 * changes from the design (canon t-6ttk's research): its px = G(0.5 - 0.5 w)
 * over a lit picture gave the still surface the middle shade, and over the
 * no picture's black the height's size is the brightness, or a still surface
 * would glow; span() brings a blend of two shades back to full scale where G
 * is a plain mix; and the wavelength is three key units, not two, which on
 * canon's grid of keys a unit apart was two keys a wave, every key on a row
 * or column in step with the sign swapping key to key, a blinking
 * checkerboard that never showed the travel (Opus review, 2026-10-26). With
 * the hit the other half's presses start waves too, which come in across the
 * split low: from an inner column at about a fifth of a crest (6.5 units
 * off), from a thumb to the other half's thumb at three fifths (1.5 units).
 * The sine is a quarter-wave table of 65 entries folded (sine()); the
 * distance and its square root are the geometry's, as the thin ring's.
 */

#include <stddef.h>
#include <stdint.h>

#include <zephyr/sys/util.h>

#include "../effect.h"
#include "../frame.h"

/* Key units a second the water's front spreads, the thin ring's speed, and
 * the wavelength behind it in hundredths of a unit: three keys a wave on
 * canon's grid (the header says why not the design's 2.0). */
#define WATER_SPEED 12
#define WATER_WAVE 300
/* How long a hit draws the water, its waves fading out over it: the
 * design's 1.5 s. */
#define WATER_MS 1500
/* The waves' fall-off with the distance from the pressed key: a wave at d
 * hundredths of a unit is 1 / (1 + WATER_FALL x d / 1000) of one at the key,
 * the design's 1 / (1 + 0.3 d) in key units. */
#define WATER_FALL 3
BUILD_ASSERT(WATER_MS <= LANTERN_EFFECT_LIFE_MAX_MS, "the water outlives a hit");

/* 127 x sin(2 pi i / 256) for i 0..64, the first quarter of the turn;
 * sine() folds the other three onto it. */
static const int8_t sine_quarter[65] = {
    0,   3,   6,   9,   12,  16,  19,  22,  25,  28,  31,  34,  37,  40,  43,  46,  49,
    51,  54,  57,  60,  63,  65,  68,  71,  73,  76,  78,  81,  83,  85,  88,  90,  92,
    94,  96,  98,  100, 102, 104, 106, 107, 109, 111, 112, 113, 115, 116, 117, 118, 120,
    121, 122, 122, 123, 124, 125, 125, 126, 126, 126, 127, 127, 127, 127};

/* 127 x the sine of i / 256 of a turn, -127..127. */
static int32_t sine(uint32_t i) {
    i &= 0xFF;
    if (i < 128) {
        return sine_quarter[i < 64 ? i : 128 - i];
    }
    return -sine_quarter[i < 192 ? i - 128 : 256 - i];
}

/* Each hit's waves (the header): behind the front, WATER_SPEED x the hit's
 * age out, the surface at an LED d from the press is the sine of the front's
 * distance past it over WATER_WAVE, fading out over WATER_MS and falling off
 * with d, and the LED's height is the sum over the hits, a crest and a
 * trough cancelling. The LED is pulled toward span() of the height, a crest
 * to shade 0 and a trough to the last shade, as far as the height's size. */
static void water_draw(struct led_rgb px[], const struct shades *sh, const struct hit live[],
                       size_t n, uint32_t picked) {
    ARG_UNUSED(picked);
    for (size_t i = 0; i < LED_COUNT; i++) {
        int32_t height = 0; /* 1/256 of a full crest, up or down */

        for (size_t h = 0; h < n; h++) {
            if (live[h].age >= WATER_MS) {
                continue;
            }
            /* ms x units/s / 1000 x 100 hundredths */
            uint32_t r = (uint32_t)live[h].age * WATER_SPEED / 10;
            uint32_t d = distance(led_key(i), live[h].key);
            if (d > r) {
                continue;
            }
            /* the fade and the fall-off, each of 256; their product of 256
             * at most, so the term is within a crest, 256 */
            uint32_t fade = (WATER_MS - live[h].age) * 256 / WATER_MS;
            uint32_t fall = 256000 / (1000 + WATER_FALL * d);
            int32_t s = sine(((r - d) % WATER_WAVE) * 256 / WATER_WAVE);

            height += s * (int32_t)(fade * fall / 256) / 127;
        }
        if (height == 0) {
            continue;
        }
        height = CLAMP(height, -256, 256);
        unsigned int size = (unsigned int)(height < 0 ? -height : height);

        px[i] = mix(px[i], span(sh, (unsigned int)(256 - height) / 2), size);
    }
}

const struct lantern_effect_entry lantern_effect_water = {
    .life_ms = WATER_MS,
    .draw = water_draw,
};
