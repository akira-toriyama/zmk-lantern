/*
 * Copyright (c) 2026 akira-toriyama
 *
 * SPDX-License-Identifier: MIT
 *
 * The frame, for the effects (src/effects/) and the intros (src/intros/): what
 * the engine in src/keys.c, which composes and writes every frame, hands them
 * and takes from them. The lantern,keys node's tables, the levels and the
 * cap, the shades a frame is made of, a hit, the colour helpers, the board's
 * geometry, and the table entries an effect and an intro fill in. Layer
 * contract: an effect or an intro paints only the pixels of the frame it is
 * handed (px[]) and keeps only its own state; the strip, the hits' ring, the
 * stage, the picks, the boot light and the cap are the engine's, which calls
 * everything here on ZMK's low priority work queue, so nothing here locks.
 * Integers only: that queue's 768-byte stack is shared with ZMK's battery
 * sampling, and a thread that once uses the FPU keeps a larger context from
 * then on (src/keys.c).
 *
 * - The geometry (CONFIG_LANTERN_GEOMETRY, which each effect that measures
 *   the board selects, the star waves for the coords and the square root, and
 *   the gradient for the board's ends): distances come from the consumer's
 *   coords, each key position's x and y in hundredths of a key unit (canon:
 *   imprint.json, keymap-drawer's physical layout, each key's top left
 *   corner; every key is one unit, so the offset to the centre cancels), so a
 *   ring is round on the board, not in the matrix. An integer square root a
 *   hit an LED a frame, 8 x 29 of them every frame, costs a few percent of
 *   the CPU at most with eight live hits (about 0.7 ms a frame from the
 *   disassembly, Opus review 2026-10-21, when the ring held eight; it holds
 *   sixteen now; unmeasured) on the preemptible low priority thread, and
 *   keeps the FPU out of it.
 * - The effects' table: one entry a file under src/effects/, named by its
 *   line in LANTERN_EFFECT_LIST (src/effect.h), from which src/keys.c builds
 *   the table of the effects this image builds, in the list's order. Each
 *   frame the engine paints the picture, lets every picked entry paint its
 *   lasting state into it (picture: the stain's dye, which the cap then
 *   counts as picture), keeps that as the picture under the effects, lets
 *   every picked entry draw the live hits over it (draw, in the table's
 *   order, so a later entry draws over an earlier one), and holds the frame
 *   to the cap. A hit lives the picked entries' longest life, each the
 *   design's or the board's (lantern_life()); at a blank
 *   and when the stage or the pick moves, every entry's state goes (clear).
 *   An entry draws for its own line's bit, which the table gives it, and
 *   for any other line it names (serves: the thin ring's entry names the
 *   stain's bit, the stain being the ring that dyes, so that the ring is
 *   drawn once and first whichever of the two is picked; the stain's entry
 *   keeps the dye).
 * - The intros' table: one entry a file under src/intros/, named by its line
 *   in LANTERN_INTRO_LIST (src/intro.h), from which src/keys.c builds the
 *   table of the intros this image builds, in the list's order. While the
 *   boot light lasts the engine draws the picked entry's frame over a black
 *   picture (draw, with the time since the light opened), composes another
 *   while the entry says it moves, and holds each to the cap; the rail rule
 *   keeps the LEDs lit for the entry's length.
 * - The helpers below are inline in this header, a copy in every file that
 *   uses them (about a kilobyte of flash on canon's halves, measured at the
 *   split), so that the per-LED loops keep them inlined as they were in the
 *   one file.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/sys/util.h>

#include "palette.h"

/* The one lantern,keys node (src/keys.c asserts there is one), its LED count
 * (one positions entry an LED) and its key count (one slots entry a key
 * position of the keymap). */
#define LANTERN_KEYS_NODE DT_INST(0, lantern_keys)
#define LED_COUNT DT_PROP_LEN(LANTERN_KEYS_NODE, positions)
#define KEY_COUNT DT_PROP_LEN(LANTERN_KEYS_NODE, slots)

/* The device's brightness as a channel value, which the frame's cap counts
 * from, and the picture's, at most it (src/keys.c asserts). */
#define LEVEL ((255 * CONFIG_LANTERN_BRIGHTNESS + 50) / 100)
#define PICTURE_LEVEL ((255 * CONFIG_LANTERN_PICTURE_BRIGHTNESS + 50) / 100)

#if IS_ENABLED(CONFIG_LANTERN_FRAMES)
/* The brightest channel of an effect's colour: full scale (src/keys.c says
 * why not twice the picture's). */
#define HI_LEVEL 255
/* The channel sum no frame exceeds: every LED white at LEVEL, plus CAP_KEYS
 * LEDs white at HI_LEVEL instead (src/keys.c). */
#define CAP_KEYS 2
#define CAP (3 * (LED_COUNT * LEVEL + CAP_KEYS * (HI_LEVEL - LEVEL)))
#endif

/* The node's tables, which src/keys.c defines from the node and asserts: the
 * key position under each LED in the chain's order, each key position's
 * palette slot, and with the geometry each key position's x and y. */
extern const uint8_t lantern_positions[LED_COUNT];
extern const uint8_t lantern_slots[KEY_COUNT];
#if IS_ENABLED(CONFIG_LANTERN_GEOMETRY)
extern const uint16_t lantern_coords[2 * KEY_COUNT];
#endif

/* The key position under LED i, and a key position's slot and place. */
static inline uint8_t led_key(size_t i) { return lantern_positions[i]; }
static inline uint8_t key_slot(uint8_t k) { return lantern_slots[k]; }
#if IS_ENABLED(CONFIG_LANTERN_GEOMETRY)
static inline uint16_t key_x(uint8_t k) { return lantern_coords[2 * k]; }
static inline uint16_t key_y(uint8_t k) { return lantern_coords[2 * k + 1]; }
#endif

/* The shades a frame is made of: the palette's colours at PICTURE_LEVEL, the
 * distinct ones in slot order (a cleared slot is slot 0's; without a palette
 * one white), each with the 0xRRGGBB it was scaled from, for an effect that
 * paints it at another level and for the gradient's full-scale blend, and
 * each slot's rank among them. */
struct shades {
    size_t n; /* 1..LANTERN_PALETTE_SLOTS */
    struct led_rgb at_level[LANTERN_PALETTE_SLOTS];
    uint32_t rgb[LANTERN_PALETTE_SLOTS];
    uint8_t rank[LANTERN_PALETTE_SLOTS];
};

/* The 0xRRGGBB scaled so that its brightest channel is level; black to white. */
static inline struct led_rgb scale(uint32_t rgb, unsigned int level) {
    unsigned int r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
    unsigned int max = MAX(r, MAX(g, b));
    if (max == 0) {
        return (struct led_rgb){.r = level, .g = level, .b = level};
    }
    return (struct led_rgb){
        .r = (uint8_t)((r * level + max / 2) / max),
        .g = (uint8_t)((g * level + max / 2) / max),
        .b = (uint8_t)((b * level + max / 2) / max),
    };
}

/* a pulled toward b by w / 256. */
static inline struct led_rgb mix(struct led_rgb a, struct led_rgb b, unsigned int w) {
    return (struct led_rgb){
        .r = (uint8_t)(a.r + ((int)b.r - (int)a.r) * (int)w / 256),
        .g = (uint8_t)(a.g + ((int)b.g - (int)a.g) * (int)w / 256),
        .b = (uint8_t)(a.b + ((int)b.b - (int)a.b) * (int)w / 256),
    };
}

#if IS_ENABLED(CONFIG_LANTERN_FRAMES)

/* Shade r at HI_LEVEL. */
static inline struct led_rgb shade_hi(const struct shades *sh, size_t r) {
    return scale(sh->rgb[r], HI_LEVEL);
}

/* The shades at HI_LEVEL end to end (the water, the dart): p, 0..256, is
 * how far from shade 0 at 0 to the last shade at 256, linear between
 * neighbours, a blend brought back to full scale after the mix (mixed at
 * that scale two hues dim each other, as the gradient's ramp() says). */
static inline struct led_rgb span(const struct shades *sh, unsigned int p) {
    if (sh->n == 1) {
        return shade_hi(sh, 0);
    }
    /* 1/256 shades along: q >> 8 < n - 1 below p = 256, and at it the last
     * shade itself */
    unsigned int q = p * (sh->n - 1);
    size_t k = q >> 8;

    if ((q & 0xFF) == 0) {
        return shade_hi(sh, k);
    }
    struct led_rgb m = mix(shade_hi(sh, k), shade_hi(sh, k + 1), q & 0xFF);
    return scale(((uint32_t)m.r << 16) | ((uint32_t)m.g << 8) | m.b, HI_LEVEL);
}

#endif /* IS_ENABLED(CONFIG_LANTERN_FRAMES) */

#if IS_ENABLED(CONFIG_LANTERN_GEOMETRY)

/* The integer square root of v, rounded down, bit by bit. */
static inline uint32_t isqrt(uint32_t v) {
    uint32_t root = 0, bit = 1u << 30;

    while (bit > v) {
        bit >>= 2;
    }
    while (bit != 0) {
        if (v >= root + bit) {
            v -= root + bit;
            root = (root >> 1) + bit;
        } else {
            root >>= 1;
        }
        bit >>= 2;
    }
    return root;
}

/* The square of the distance between key positions a and b, in hundredths
 * of a key unit (inside 32 bits: src/keys.c asserts a coordinate at most
 * 30000), and the distance. */
static inline uint32_t distance2(uint8_t a, uint8_t b) {
    int32_t dx = (int32_t)key_x(a) - (int32_t)key_x(b);
    int32_t dy = (int32_t)key_y(a) - (int32_t)key_y(b);

    return (uint32_t)(dx * dx + dy * dy);
}

static inline uint32_t distance(uint8_t a, uint8_t b) { return isqrt(distance2(a, b)); }

/* The board, measured from the coords once, at the first frame that asks
 * (src/keys.c), in hundredths of a key unit: its leftmost and rightmost
 * keys' x and its lowest key's y (y grows downwards), its width from the one
 * x to the other, the farthest two of its keys apart (span) and the farthest
 * two of this half's keys, those under its LEDs (half_span; a key of this
 * half without an LED counts as the other half's). What an effect's life
 * follows from, under the design's own value (lantern_life()): no life
 * carries a board's size but the beams' fade, which answers to the gap
 * between the halves, not the board (src/effects/beams.c says). Measured
 * at boot on the main thread (src/keys.c), read on the frame's. */
struct lantern_board {
    uint16_t left, right, bottom;
    uint32_t width;
    uint32_t span;
    uint32_t half_span;
};

const struct lantern_board *lantern_board(void);

/* How far apart two coordinates are: along a row or a column the distance
 * (the beams, the dart). */
static inline uint32_t apart(uint16_t a, uint16_t b) { return a > b ? a - b : b - a; }

#endif /* IS_ENABLED(CONFIG_LANTERN_GEOMETRY) */

#if IS_ENABLED(CONFIG_LANTERN_EFFECTS)

/* A press the effects draw, read out of the engine's ring (src/keys.c). */
struct hit {
    uint8_t key;  /* a key position */
    uint8_t seq;  /* the press's number, mod 60, a multiple of every shade count */
    uint16_t age; /* ms since the press, below the picked effects' life */
};

/* The most an effect lives after a press: the engine keeps a hit's age in
 * 16 bits and its time in 18 (src/keys.c asserts both hold this), and ten
 * seconds leaves either far; each effect asserts its design's life against
 * it, and lantern_life() holds the board's under it. */
#define LANTERN_EFFECT_LIFE_MAX_MS 10000

/* An effect's life: the design's, or the board's when that is longer (a ring
 * until it has left the board, a bar until it has run the board's width),
 * and never past LANTERN_EFFECT_LIFE_MAX_MS. */
static inline uint32_t lantern_life(uint32_t design_ms, uint32_t board_ms) {
    return MIN(MAX(design_ms, board_ms), LANTERN_EFFECT_LIFE_MAX_MS);
}

/* An effect's entry in the table (the header): lantern_effect_<ops> for the
 * line X(SYMBOL, bit, "name", ops) of LANTERN_EFFECT_LIST, defined by its
 * file under src/effects/ and built when CONFIG_LANTERN_<SYMBOL> is. */
struct lantern_effect_entry {
    /* The pick's bits of other lines this entry draws for as well as its own
     * line's, which the table gives it (src/effect.h): the thin ring's entry
     * names the stain's; 0 for none. */
    uint32_t serves;
    /* How long after a press it still draws (lantern_life()): read every
     * frame, so an entry keeps what the board gave it. */
    uint32_t (*life)(void);
    /* Its lasting state into the picture px, before any effect draws, which
     * the cap then counts as picture (the stain's dye); NULL for none. */
    void (*picture)(struct led_rgb px[], const struct shades *sh);
    /* The n live hits over the picture in px; picked is what this frame
     * draws, the pick's bits built here (src/effect.h), for an entry that
     * draws for more than one bit; NULL for none. */
    void (*draw)(struct led_rgb px[], const struct shades *sh, const struct hit live[], size_t n,
                 uint32_t picked);
    /* Its state goes: at a blank, and when the stage or the pick moves; NULL
     * for none. */
    void (*clear)(void);
};

#endif /* IS_ENABLED(CONFIG_LANTERN_EFFECTS) */

#if IS_ENABLED(CONFIG_LANTERN_INTROS)

/* An intro's entry in the table (the header): lantern_intro_<ops> for the
 * line X(SYMBOL, bit, "name", ops) of LANTERN_INTRO_LIST, defined by its file
 * under src/intros/ and built when CONFIG_LANTERN_INTRO_<SYMBOL> is. */
struct lantern_intro_entry {
    /* How long the rail rule keeps the LEDs lit for it, from the light that
     * opens the boot light; above 0 (the file asserts), or the rail rule ends
     * the light at once. */
    uint32_t length_ms;
    /* The boot light opens (the system work queue): a seed, a clock of its
     * own; NULL for none. */
    void (*start)(void);
    /* The frame's thread: the frame t ms into the intro, into px (every LED),
     * over a black picture for the cap, from the frame's shades (the
     * palette's colours at the picture's level); true while the frame moves,
     * so that the engine composes another FRAME_MS on (a moving intro selects
     * CONFIG_LANTERN_FRAMES), false for a still frame and from a moving one's
     * end, where px must be black: the rail rule ends the light at length_ms,
     * and the engine may compose a frame past it. */
    bool (*draw)(struct led_rgb px[], const struct shades *sh, uint32_t t);
};

#endif /* IS_ENABLED(CONFIG_LANTERN_INTROS) */
