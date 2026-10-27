/*
 * Copyright (c) 2026 akira-toriyama
 *
 * SPDX-License-Identifier: MIT
 *
 * The beams (CONFIG_LANTERN_BEAMS, canon t-6ttk; the pick's beams), an entry
 * of the effects' table (src/frame.h): from a pressed key four beams run
 * outward at BEAMS_SPEED key units a second, two along its row and two along
 * its column, each full at its head and gone BEAMS_WIDTH from it, fading out
 * over BEAMS_MS as the thin ring does. A key is on the row or the column
 * when its y or x is within BEAMS_LINE of the pressed key's: canon's rows and
 * columns sit a unit apart, and its thumbs half a unit off the bottom row and
 * the inner column, so the thumb clusters are two rows of three and three
 * columns of two, and the two clusters share their rows, a unit and a half
 * apart. QMK's cross and nexus from their idea (nothing copied). The colour
 * is the pressed key's band, as the crossing ring's, so a beam that crosses
 * the split keeps its colour: with the hit a press of the other half sends
 * its row's beam in here, from an inner column at about half strength (the
 * inner columns of canon's Imprint are 6.5 units apart, 542 ms at 12; 117 of
 * 256 at the head, less by the frame's phase), from an outer one all but
 * spent (the outermost's 10 of 255), and from a thumb nearly at full
 * strength, the strongest crossing; the column's beams never leave their
 * half. The design's fade of 0.6 s had the row's beam die in the gap (the
 * inner column's reaching the other half at a tenth of its strength); a
 * second, the thin ring's, lets it arrive. An LED under two beams takes the
 * strongest one's colour at their summed strength. Along a line the distance
 * is the difference of one coordinate, so the beams take no square root.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/sys/util.h>

#include "../effect.h"
#include "../frame.h"

/* Key units a second a beam's head runs, the thin ring's speed; full at the
 * head and gone BEAMS_WIDTH hundredths of a unit from it, the design's 0.8. */
#define BEAMS_SPEED 12
#define BEAMS_WIDTH 80
/* A key is on the pressed key's row or column within this many hundredths of
 * a unit of its y or x (the header). */
#define BEAMS_LINE 50
/* How long a hit draws the beams, fading out over it: the design's 600 had
 * the row's beam die between canon's halves (the header). No board term: the
 * row's beam's arrival on the other half depends on the gap between the
 * halves, not on the board's size, and a wider gap wants a longer fade. */
#define BEAMS_MS 1000
BUILD_ASSERT(BEAMS_MS <= LANTERN_EFFECT_LIFE_MAX_MS, "the beams outlive a hit");

static uint32_t beams_life(void) { return BEAMS_MS; }

/* Whether key position a lies on b's row or column (the header), and then
 * how far along it, in hundredths of a key unit; b itself lies on both, at 0. */
static bool along_line(uint8_t a, uint8_t b, uint32_t *along) {
    uint32_t dx = apart(key_x(a), key_x(b));
    uint32_t dy = apart(key_y(a), key_y(b));

    if (dy < BEAMS_LINE) {
        *along = dx;
        return true;
    }
    if (dx < BEAMS_LINE) {
        *along = dy;
        return true;
    }
    return false;
}

/* Each hit's beams (the header): their heads BEAMS_SPEED x the hit's age out
 * along the pressed key's row and column, full at the head and gone
 * BEAMS_WIDTH from it, fading out over BEAMS_MS, in the shade of the pressed
 * key's slot. An LED under several beams takes the strongest one's colour at
 * their summed strength. */
static void beams_draw(struct led_rgb px[], const struct shades *sh, const struct hit live[],
                       size_t n, uint32_t picked) {
    ARG_UNUSED(picked);
    for (size_t i = 0; i < LED_COUNT; i++) {
        unsigned int w = 0, strongest = 0;
        size_t pick = 0;

        for (size_t h = 0; h < n; h++) {
            uint32_t along;

            if (live[h].age >= BEAMS_MS || !along_line(led_key(i), live[h].key, &along)) {
                continue;
            }
            /* ms x units/s / 1000 x 100 hundredths */
            uint32_t r = (uint32_t)live[h].age * BEAMS_SPEED / 10;
            uint32_t off = along > r ? along - r : r - along;
            if (off >= BEAMS_WIDTH) {
                continue;
            }
            unsigned int s = (BEAMS_WIDTH - off) * 256 / BEAMS_WIDTH;
            s = s * (BEAMS_MS - live[h].age) / BEAMS_MS;
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

const struct lantern_effect_entry lantern_effect_beams = {
    .life = beams_life,
    .draw = beams_draw,
};
