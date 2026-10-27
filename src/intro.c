/*
 * Copyright (c) 2026 akira-toriyama
 *
 * SPDX-License-Identifier: MIT
 *
 * The intro (CONFIG_LANTERN_INTRO, canon t-5v1f): a show in place of the boot
 * light's picture, LANTERN_INTRO_MS long (intro.h), which the rail rule plays
 * at boot and whenever USB power arrives (src/on_usb_power.c) and the frame
 * engine draws in the boot light's frames (src/keys.c), in red, blue and
 * green up to full scale, not the palette's colours (the user's pick and
 * order, after Pokemon's first games), and in no other colour, white
 * included: on hardware the user found the first script's end, a white burst
 * leaving a rainbow, unrelated to the three and asked for other colours a
 * little at most (2026-10-22), then asked the star's white head, the last of
 * them, away too (canon t-ky8x, 2026-10-27). Each half plays it alone,
 * mirrored: an LED's depth runs from 0 at the half's outermost key to
 * DEPTH_ONE at its innermost, by its key's distance (the coords, src/frame.h)
 * from the bottom of the board's middle, the point between the two thumb
 * clusters, so the show moves from the outer columns' top to the inner thumb
 * and, from the burst, back. The script, in order: the star, a head crossing
 * the half in STAR_MS, each key in the colour of its third of the depths (the
 * thirds the burst ends in, below), each key glittering for GLITTER_MS from
 * the head's passing, in sight once the head's light has left it, every beat
 * a primary or none at random (the dice: an integer hash of the LED, the beat
 * and a seed taken at the start, so the frame keeps no state for it); the
 * waves, red, blue and green WAVE_GAP_MS apart, each front crossing in
 * WAVE_MS with a fading tail; the core, the deepest keys, each in the colour
 * of the newest wave to reach it, so that a wave coming into the core never
 * meets another colour there, with a flash at each wave's arrival, then
 * pulsing for CHARGE_MS a colour a beat, the last wave's green, red, blue,
 * green, and swelling outwards; and the burst, a red, a blue and a green ring
 * from the core out BURST_GAP_MS apart, each crossing in BURST_MS, the last
 * leaving each key it passes in its third of the depths, the outer red, the
 * middle blue and the inner green (the waves' order from the outside), held
 * THIRDS_HOLD_MS; the flow (canon t-x46t, 2026-10-27: the user asked for a
 * longer show, more scenes at the same tempo), the thirds as three crests, a
 * key dark at a band's edges and at the thirds' level at its middle,
 * deepening in place from the hold's flat light over FLOW_RAMP_MS, then
 * sliding outward at the waves' pace, so that a key changes colour while dark
 * and in the user's order, for three depths, and standing again where they
 * began; and the star out, a head running back from the innermost key to the
 * outermost in STAR_MS, brightening each key in its own colour, the flow
 * ending at each key as it passes and the key glittering after it as at the
 * start, the last glitter out GLITTER_MS after the head passes the outermost
 * key, at the end. The hold is the script's one stretch with every key lit,
 * the flow all but the keys at a band's edge. A key shows one primary, the
 * strongest of the light on it (the beat's alone on the charging core's
 * keys), light of one colour adding up to full scale, so that no retiming of
 * the acts can mix two, the star's head included. The frame's cap over a
 * black picture holds the whole (the engine applies it). Each frame is
 * composed from the time since the start, so a late frame loses nothing; the
 * depths are measured once, at the first frame. No effect is drawn and no
 * press noted while it plays (src/keys.c): a press noted then would show at
 * its end as a disc already in flight.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

#include "frame.h"
#include "intro.h"

/* An LED's depth: 0 at this half's outermost key, DEPTH_ONE at its innermost
 * (the header). */
#define DEPTH_ONE 1024

/* The intro's script (the header tells it): times in ms from its start,
 * reaches in depth, levels of 255. */
#define STAR_MS 600         /* the star's head from the outermost key to the innermost */
#define STAR_REACH 128      /* how far on either side of the head it lights */
#define GLITTER_MS 1000     /* how long a key glitters once the head has passed it */
#define GLITTER_BEAT_MS 60  /* every beat a new primary, or none */
#define GLITTER_ON 160      /* of 256: the chance that a key shows in a beat */
/* The glitter's level as it begins, fading to 0 over GLITTER_MS: at 255 the
 * star out's head with every key glittering behind it could pass the frame's
 * cap by nearly a tenth (Opus review, 2026-10-27); at this level neither
 * star's worst roll does (the star out's 4818 of 4851 on canon's halves). */
#define GLITTER_PEAK 225
#define WAVE_FROM_MS 1300   /* the first wave's start; the others follow */
#define WAVE_GAP_MS 500
#define WAVE_MS 1000        /* a wave's front from the outermost key to the innermost */
#define WAVE_HEAD 64        /* how far ahead of its front a wave lights, rising */
#define WAVE_TAIL 320       /* and behind it, fading */
/* The core: the depth where it begins to glow and where it glows fully, of
 * DEPTH_ONE: the deepest quarter or so of the half, the innermost keys
 * brightest (on canon's halves the thumb cluster, and nothing above it). */
#define CORE_FROM 700
#define CORE_FULL 940
#define CORE_SWELL 150      /* how much further out it begins by the burst */
#define CORE_LEVEL 170      /* its glow once a flash has faded */
/* Its flash at a wave's arrival, fading to CORE_LEVEL; the last wave's fades
 * to CHARGE_LOW within the charge's first beat, so that every later beat
 * pulses from the low alike. */
#define POP_MS 300
#define CHARGE_MS 600       /* the core's pulses, a colour a beat, and its swell */
#define CHARGE_BEAT_MS 150
#define CHARGE_LOW 80       /* the pulses' low */
#define BURST_FROM_MS (WAVE_FROM_MS + 2 * WAVE_GAP_MS + WAVE_MS + CHARGE_MS)
#define BURST_GAP_MS 150    /* between the rings' starts */
#define BURST_MS 450        /* a ring's front from the innermost key to the outermost */
#define BURST_REACH 128     /* how far on either side of its front a ring lights */
#define THIRDS_HOLD_MS 350  /* every key in its third, once the last ring's front is out */
/* The thirds' level: the design's 160, or what the frame's cap allows with
 * every key at it in one channel when that is less, so that the hold and
 * the flow show at their level and not under the cap. */
#define THIRDS_LEVEL MIN(160, CAP / LED_COUNT)
/* The flow: the thirds as three crests, a key dark at a band's edges and at
 * THIRDS_LEVEL at its middle, deepening in place from the hold's flat light
 * over FLOW_RAMP_MS (the pattern stands meanwhile: moved, it flipped a key's
 * colour while lit; Opus review, 2026-10-27), then sliding outward at the
 * waves' pace (a depth, three bands, in WAVE_MS), so that a key changes
 * colour while dark (at 14 of 255 at most on a frame's step) and in the
 * user's order, red, blue, green, for three depths, and standing again where
 * they began, so that the star out's head, in the key's third's colour,
 * brightens each key in its own colour. */
#define FLOW_FROM_MS (BURST_FROM_MS + 2 * BURST_GAP_MS + BURST_MS + THIRDS_HOLD_MS)
#define FLOW_RAMP_MS 333
#define FLOW_MS (FLOW_RAMP_MS + 3 * WAVE_MS)
#define FLOW_BAND 256       /* a band's width in the flow's positions, three to a depth */
/* The star out: a head from the innermost key to the outermost in STAR_MS,
 * the flow ending at each key as the head passes it and the key glittering
 * after it, as at the start; the last glitter is out GLITTER_MS after the
 * head passes the outermost key. */
#define STAR_OUT_FROM_MS (FLOW_FROM_MS + FLOW_MS)
BUILD_ASSERT(STAR_OUT_FROM_MS + STAR_MS + GLITTER_MS <= LANTERN_INTRO_MS,
             "the intro's script runs past LANTERN_INTRO_MS, where the rail rule ends it");
BUILD_ASSERT(CORE_SWELL < CORE_FROM && CORE_FROM < CORE_FULL && CORE_FULL <= DEPTH_ONE,
             "the core's depths are out of order");

static atomic_t intro_t0;   /* k_uptime_get_32() at the intro's start */
static atomic_t intro_seed; /* the glitter's seed, k_cycle_get_32() then */

/* The frame's thread: each LED's depth, measured from the coords at the
 * intro's first frame. */
static bool depth_measured;
static uint16_t depth[LED_COUNT];

/* Hundredths of a key unit from LED i's key to (mid, bottom). */
static uint32_t from_middle(size_t i, int32_t mid, int32_t bottom) {
    int32_t dx = (int32_t)key_x(led_key(i)) - mid;
    int32_t dy = (int32_t)key_y(led_key(i)) - bottom;

    return isqrt((uint32_t)(dx * dx + dy * dy));
}

/* The bottom of the board's middle: halfway between its leftmost and
 * rightmost keys, level with its lowest (y grows downwards; lantern_board(),
 * src/frame.h); the depths run from this half's farthest LED from it to its
 * nearest. */
static void depth_measure(void) {
    const struct lantern_board *b = lantern_board();
    int32_t mid = ((int32_t)b->left + b->right) / 2;
    int32_t bottom = b->bottom;
    uint32_t far = 0, near = UINT32_MAX;

    for (size_t i = 0; i < LED_COUNT; i++) {
        uint32_t r = from_middle(i, mid, bottom);
        far = MAX(far, r);
        near = MIN(near, r);
    }
    for (size_t i = 0; i < LED_COUNT; i++) {
        depth[i] = far == near ? DEPTH_ONE
                               : (uint16_t)((far - from_middle(i, mid, bottom)) * DEPTH_ONE /
                                            (far - near));
    }
    depth_measured = true;
}

/* The glitter's dice: a 32-bit integer hash, the improved lowbias32 constants
 * [16 21f0aaad 15 d35a2d97 15] listed in the README of Chris Wellons'
 * hash-prospector (public domain). */
static uint32_t dice(uint32_t x) {
    x ^= x >> 16;
    x *= 0x21f0aaadu;
    x ^= x >> 15;
    x *= 0xd35a2d97u;
    x ^= x >> 15;
    return x;
}

/* The intro's three colours in their order, red, blue, green, the order the
 * user wrote them (canon t-5v1f), as indexes into intro_led()'s c[]: the
 * waves', the rings' and the thirds' from the outside, and the star's head's
 * by the key's third. */
static const uint8_t primary[3] = {0, 2, 1};

/* LED i's colour t ms into the intro, before the cap: the strongest primary
 * of the light on it, c[0] red, c[1] green, c[2] blue (the header). */
static struct led_rgb intro_led(size_t i, uint32_t t, uint32_t seed) {
    int32_t d = depth[i];
    uint32_t c[3] = {0, 0, 0};
    /* the key's third of the depths: the star's head's colour and the thirds' */
    size_t third = MIN((uint32_t)d * 3 / DEPTH_ONE, 2u);

    /* the star */
    int32_t off = (int32_t)(t * DEPTH_ONE / STAR_MS) - d;
    if (off < 0) {
        off = -off;
    }
    if (off < STAR_REACH) {
        c[primary[third]] += (uint32_t)(STAR_REACH - off) * 255 / STAR_REACH;
    }
    /* the glitter, once the head's light has left the key: begun under the
     * head it outshone its trailing side (Opus review, 2026-10-27) */
    uint32_t passed = (uint32_t)d * STAR_MS / DEPTH_ONE;
    if (t >= passed && t - passed < GLITTER_MS && off >= STAR_REACH) {
        uint32_t age = t - passed;
        uint32_t roll = dice(seed ^ ((uint32_t)i << 8) ^ (age / GLITTER_BEAT_MS));

        if ((roll & 0xFF) < GLITTER_ON) {
            c[(roll >> 8) % 3] += GLITTER_PEAK * (GLITTER_MS - age) / GLITTER_MS;
        }
    }

    /* the waves; reached counts those whose light has come to this LED */
    size_t reached = 0;
    for (size_t w = 0; w < 3; w++) {
        uint32_t from = WAVE_FROM_MS + w * WAVE_GAP_MS;
        if (t < from) {
            break;
        }
        int32_t behind = (int32_t)((t - from) * DEPTH_ONE / WAVE_MS) - d;
        if (behind <= -WAVE_HEAD) {
            break;
        }
        reached = w + 1;
        if (behind < 0) {
            c[primary[w]] += (uint32_t)(WAVE_HEAD + behind) * 255 / WAVE_HEAD;
        } else if (behind < WAVE_TAIL) {
            c[primary[w]] += (uint32_t)(WAVE_TAIL - behind) * 255 / WAVE_TAIL;
        }
    }

    /* the core, until the burst */
    if (t < BURST_FROM_MS) {
        size_t arrived = 0;
        uint32_t last = 0;

        for (size_t w = 0; w < 3; w++) {
            uint32_t at = WAVE_FROM_MS + w * WAVE_GAP_MS + WAVE_MS;
            if (t >= at) {
                arrived = w + 1;
                last = at;
            }
        }
        if (arrived > 0) {
            uint32_t since = t - last;
            uint32_t low = arrived == 3 ? CHARGE_LOW : CORE_LEVEL;
            uint32_t pop = arrived == 3 ? CHARGE_BEAT_MS : POP_MS;
            uint32_t level = low;
            int32_t from = CORE_FROM;
            /* the newest wave to reach this key; an arrived wave has reached
             * every key, so reached >= arrived >= 1 */
            size_t colour = reached - 1;

            if (since < pop) {
                level += (255 - low) * (pop - since) / pop;
            }
            if (arrived == 3) {
                /* the last arrival is CHARGE_MS before the burst, so since < CHARGE_MS */
                uint32_t beat = since % CHARGE_BEAT_MS;
                uint32_t tri = (beat < CHARGE_BEAT_MS / 2 ? beat : CHARGE_BEAT_MS - beat) * 512 /
                               CHARGE_BEAT_MS;

                level = MAX(level, CHARGE_LOW + (255 - CHARGE_LOW) * MIN(tri, 256) / 256);
                from -= (int32_t)(CORE_SWELL * since / CHARGE_MS);
                /* a colour a beat: the last wave's, then the waves' order round again */
                colour = (2 + since / CHARGE_BEAT_MS) % 3;
            }
            if (d > from) {
                uint32_t k = d >= CORE_FULL ? 256 : (uint32_t)(d - from) * 256 / (CORE_FULL - from);

                if (arrived == 3) {
                    /* the last wave's fading tail would outshine a beat at its
                     * low on the deepest keys */
                    uint32_t own = c[primary[colour]];

                    c[0] = c[1] = c[2] = 0;
                    c[primary[colour]] = own;
                }
                c[primary[colour]] += level * k / 256;
            }
        }
    }

    /* the burst: the rings, and behind the last the thirds */
    if (t >= BURST_FROM_MS) {
        uint32_t since = t - BURST_FROM_MS;

        for (size_t j = 0; j < 3 && since >= j * BURST_GAP_MS; j++) {
            int32_t front =
                DEPTH_ONE - (int32_t)((since - j * BURST_GAP_MS) * DEPTH_ONE / BURST_MS);
            off = front > d ? front - d : d - front;
            if (off < BURST_REACH) {
                c[primary[j]] += (uint32_t)(BURST_REACH - off) * 255 / BURST_REACH;
            }
        }
        passed = 2 * BURST_GAP_MS + (uint32_t)(DEPTH_ONE - d) * BURST_MS / DEPTH_ONE;
        if (since >= passed && t < FLOW_FROM_MS) {
            c[primary[third]] += THIRDS_LEVEL;
        }
    }

    /* the flow, until the star out's head passes this key */
    uint32_t out = STAR_OUT_FROM_MS + (uint32_t)(DEPTH_ONE - d) * STAR_MS / DEPTH_ONE;
    if (t >= FLOW_FROM_MS && t < out) {
        uint32_t since = t - FLOW_FROM_MS;
        uint32_t pos = MIN((uint32_t)d * 3 * FLOW_BAND / DEPTH_ONE, 3u * FLOW_BAND - 1);
        /* the pattern's run: none during the ramp, three depths, then it stands */
        uint32_t run = CLAMP(since, FLOW_RAMP_MS, FLOW_MS) - FLOW_RAMP_MS;
        uint32_t at = (pos + run * 3 * FLOW_BAND / WAVE_MS) % (3 * FLOW_BAND);
        uint32_t frac = at % FLOW_BAND;
        uint32_t crest = frac < FLOW_BAND / 2 ? frac * 2 : (FLOW_BAND - 1 - frac) * 2;
        uint32_t ramp = MIN(since * 256 / FLOW_RAMP_MS, 256u);

        c[primary[at / FLOW_BAND]] +=
            THIRDS_LEVEL - THIRDS_LEVEL * ramp * (255 - crest) / (255 * 256);
    }

    /* the star out, and the glitter once its head's light has left the key */
    if (t >= STAR_OUT_FROM_MS) {
        off = DEPTH_ONE - (int32_t)((t - STAR_OUT_FROM_MS) * DEPTH_ONE / STAR_MS) - d;
        if (off < 0) {
            off = -off;
        }
        if (off < STAR_REACH) {
            c[primary[third]] += (uint32_t)(STAR_REACH - off) * 255 / STAR_REACH;
        } else if (t >= out && t - out < GLITTER_MS) {
            uint32_t age = t - out;
            uint32_t roll = dice(seed ^ ((uint32_t)i << 8) ^ (age / GLITTER_BEAT_MS) ^ 0x80000000u);

            if ((roll & 0xFF) < GLITTER_ON) {
                c[(roll >> 8) % 3] += GLITTER_PEAK * (GLITTER_MS - age) / GLITTER_MS;
            }
        }
    }

    size_t top = c[1] > c[0] ? 1 : 0;
    if (c[2] > c[top]) {
        top = 2;
    }
    uint8_t p = (uint8_t)MIN(c[top], 255u);

    return (struct led_rgb){
        .r = top == 0 ? p : 0,
        .g = top == 1 ? p : 0,
        .b = top == 2 ? p : 0,
    };
}

void lantern_intro_start(void) {
    atomic_set(&intro_t0, (atomic_val_t)k_uptime_get_32());
    atomic_set(&intro_seed, (atomic_val_t)k_cycle_get_32());
}

/* Not inlined into the engine's frame (a call from paint() either way, in a
 * file of its own; the attribute holds it under link-time optimisation): its
 * locals would sit in paint()'s frame under the strip driver's calls too,
 * nearer the end of the 768-byte stack: 24 bytes for the first script, 40
 * for the second on a model build (Opus reviews, 2026-10-21 and -22), the
 * flow and the star out a few words more. */
__noinline bool lantern_intro_draw(struct led_rgb *px) {
    uint32_t t = k_uptime_get_32() - (uint32_t)atomic_get(&intro_t0);

    if (!depth_measured) {
        depth_measure();
    }
    if (t >= LANTERN_INTRO_MS) {
        memset(px, 0, LED_COUNT * sizeof(*px));
        return false;
    }
    uint32_t seed = (uint32_t)atomic_get(&intro_seed);

    for (size_t i = 0; i < LED_COUNT; i++) {
        px[i] = intro_led(i, t, seed);
    }
    return true;
}
