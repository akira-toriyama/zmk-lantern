/*
 * Copyright (c) 2026 akira-toriyama
 *
 * SPDX-License-Identifier: MIT
 *
 * The dart (CONFIG_LANTERN_DART, canon t-6ttk; the pick's dart), an entry of
 * the effects' table (src/frame.h): from a pressed key a bar runs across the
 * rows toward the far side of the board at DART_SPEED key units a second (a
 * key left of the board's middle sends it right, one right of it left), full
 * at its head and gone DART_WIDTH from it, full on the pressed key's row and
 * DART_ROWS of 256 on the others (a key is on the row within DART_LINE of
 * its y, the beams' rule), fading out over DART_MS, and in the shade of how
 * far it has run along span() (src/frame.h), shade 0 at the key and the last
 * shade a board's width away, so the bar's colour slides along the palette
 * as it runs: zmk-advanced-underglow-effects-vfx's dart, from its idea
 * (nothing copied), the research's light that runs at a press. The board's
 * ends are measured once, at the first frame that draws it, as the
 * gradient's. With the hit the bar runs on through the other half: a press
 * of this half sends it out through the inner column and the thumbs, and a
 * press of the other half, relayed, brings it in across the split (from an
 * inner column, 6.5 units off on canon's Imprint, 406 ms at 16 and two
 * thirds of its strength at the head, less by the frame's phase; from a
 * thumb, 1.5 units, nearly full) and on to the outer column. Along the rows
 * the distance is the difference of x, so the dart takes no square root. An
 * LED under two darts takes the strongest one's colour at their summed
 * strength.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/sys/util.h>

#include "../effect.h"
#include "../frame.h"

/* Key units a second the dart's head runs, the design's 16: across canon's
 * board (16.5 units) from any key within its life; full at the head and
 * gone DART_WIDTH hundredths of a unit from it, the design's 0.8. */
#define DART_SPEED 16
#define DART_WIDTH 80
/* A key is on the pressed key's row within this many hundredths of a unit
 * of its y (the beams' rule); off the row the bar is DART_ROWS of 256 as
 * strong, the design's 0.4. */
#define DART_LINE 50
#define DART_ROWS 102
/* How long a hit draws the dart, fading out over it: the design's 1.2 s. */
#define DART_MS 1200
BUILD_ASSERT(DART_MS <= LANTERN_EFFECT_LIFE_MAX_MS, "the dart outlives a hit");

/* The frame's thread: the leftmost and the rightmost key's x, measured from
 * the coords at the first frame that draws the dart (the header). */
static bool dart_measured;
static uint16_t dart_left, dart_right;

/* Each hit's dart (the header): its head DART_SPEED x the hit's age from
 * the pressed key toward the far side of the board, full at the head and
 * gone DART_WIDTH from it, DART_ROWS of 256 as strong off the pressed
 * key's row, fading out over DART_MS, in span() of how far it has run over
 * the board's width. An LED under several darts takes the strongest one's
 * colour at their summed strength. */
static void dart_draw(struct led_rgb px[], const struct shades *sh, const struct hit live[],
                      size_t n, uint32_t picked) {
    ARG_UNUSED(picked);
    if (!dart_measured) {
        uint16_t lo = UINT16_MAX, hi = 0;

        for (size_t k = 0; k < KEY_COUNT; k++) {
            lo = MIN(lo, key_x(k));
            hi = MAX(hi, key_x(k));
        }
        dart_left = lo;
        dart_right = hi;
        dart_measured = true;
    }
    uint32_t width = MAX((uint32_t)(dart_right - dart_left), 1u);

    for (size_t i = 0; i < LED_COUNT; i++) {
        unsigned int w = 0, strongest = 0;
        uint32_t run = 0;
        int32_t x = key_x(led_key(i));

        for (size_t h = 0; h < n; h++) {
            if (live[h].age >= DART_MS) {
                continue;
            }
            /* ms x units/s / 1000 x 100 hundredths */
            uint32_t r = (uint32_t)live[h].age * DART_SPEED / 10;
            int32_t from = key_x(live[h].key);
            /* right from a key left of the middle (the key centres sit half a
             * unit right of the corners coords give, on both sides) */
            int32_t head = 2 * from < dart_left + dart_right ? from + (int32_t)r : from - (int32_t)r;
            uint32_t off = (uint32_t)(x > head ? x - head : head - x);
            if (off >= DART_WIDTH) {
                continue;
            }
            unsigned int s = (DART_WIDTH - off) * 256 / DART_WIDTH;
            if (apart(key_y(led_key(i)), key_y(live[h].key)) >= DART_LINE) {
                s = s * DART_ROWS / 256;
            }
            s = s * (DART_MS - live[h].age) / DART_MS;
            if (s > strongest) {
                strongest = s;
                run = r;
            }
            w += s;
        }
        if (w > 0) {
            px[i] = mix(px[i], span(sh, MIN(run * 256 / width, 256)), MIN(w, 256));
        }
    }
}

const struct lantern_effect_entry lantern_effect_dart = {
    .life_ms = DART_MS,
    .draw = dart_draw,
};
