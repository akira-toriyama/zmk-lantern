/*
 * Copyright (c) 2026 akira-toriyama
 *
 * SPDX-License-Identifier: MIT
 *
 * The effects' table and the pick (src/effect.c): which of the effects built
 * into a half's image draw, and in which colour, chosen at run time by the
 * split central's host, or by a key of the central's keymap bound to the
 * behavior (the next effect in turn: src/effect_next.c), and kept by every
 * device. For the port (src/port.c), which names them, and the key LEDs
 * (src/keys.c), which draw them. Layer contract: the port parses and replies
 * and never stores a pick; every change goes through the effect behavior, on
 * the central and the halves alike; the key LEDs only read.
 *
 * A new effect is one line in LANTERN_EFFECT_LIST, its Kconfig symbol
 * LANTERN_<SYMBOL> (which selects LANTERN_EFFECTS, or an effect that does, as
 * the stain's the thin ring) and a default-choice member
 * LANTERN_EFFECT_DEFAULT_<SYMBOL>, its file under src/effects/ defining the
 * entry lantern_effect_<ops> (src/frame.h's struct lantern_effect_entry) and that
 * file's line in CMakeLists.txt; the bit, the name, the mask of what is built
 * and the engine's table (src/keys.c) follow from the line. The bit is a wire
 * format (the behavior's param1, the settings value, canon's dongle.py names
 * it): never renumber a line, append.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/sys/util.h>

/* X(SYMBOL, bit, "name", ops): the effects, in the order the host's replies
 * list them and the engine draws them. SYMBOL is the Kconfig suffix, the name
 * the host's word for it, ops the entry its file defines, lantern_effect_<ops>
 * (the stain's is in the thin ring's file: the stain is that ring, dyeing). */
#define LANTERN_EFFECT_LIST(X)                                                                     \
    X(THIN_RING, 0, "thin-ring", thin_ring)                                                        \
    X(DISC, 1, "disc", disc)                                                                       \
    X(STAIN, 2, "stain", stain)                                                                    \
    X(CROSS_RING, 3, "cross-ring", cross_ring)                                                     \
    X(BEAMS, 4, "beams", beams)                                                                    \
    X(WATER, 5, "water", water)                                                                    \
    X(DART, 6, "dart", dart)

/* Each effect's bit in a pick: LANTERN_EFFECT_THIN_RING and so on. */
enum lantern_effect {
#define LANTERN_EFFECT_BIT_(sym, bit, name, ops) LANTERN_EFFECT_##sym = BIT(bit),
    LANTERN_EFFECT_LIST(LANTERN_EFFECT_BIT_)
#undef LANTERN_EFFECT_BIT_
};

#define LANTERN_EFFECT_OR_(sym, bit, name, ops) | BIT(bit)
#define LANTERN_EFFECT_BUILT_(sym, bit, name, ops)                                                 \
    | (IS_ENABLED(CONFIG_LANTERN_##sym) ? BIT(bit) : 0)
#define LANTERN_EFFECT_DEFAULT_(sym, bit, name, ops)                                               \
    | (IS_ENABLED(CONFIG_LANTERN_EFFECT_DEFAULT_##sym) ? BIT(bit) : 0)
#define LANTERN_EFFECT_ONE_(sym, bit, name, ops) +1

/* Every effect the table knows, those built into this image, the one the
 * Kconfig choice LANTERN_EFFECT_DEFAULT picks, and how many the table has. */
#define LANTERN_EFFECT_ALL (0 LANTERN_EFFECT_LIST(LANTERN_EFFECT_OR_))
#define LANTERN_EFFECT_BUILT (0 LANTERN_EFFECT_LIST(LANTERN_EFFECT_BUILT_))
#define LANTERN_EFFECT_DEFAULT (0 LANTERN_EFFECT_LIST(LANTERN_EFFECT_DEFAULT_))
#define LANTERN_EFFECT_COUNT (0 LANTERN_EFFECT_LIST(LANTERN_EFFECT_ONE_))

BUILD_ASSERT(__builtin_popcount(LANTERN_EFFECT_ALL) == LANTERN_EFFECT_COUNT,
             "LANTERN_EFFECT_LIST: two effects share a bit");

/* The pick's colour (the behavior's param2, canon t-rgp5): 0xRRGGBB, the one
 * colour every picked effect draws in, or 0 for the effects' own colours, the
 * palette's (src/keys.c says how). */
#define LANTERN_EFFECT_COLOUR_MAX 0xFFFFFF

/* The pick after mask in a cycle over the bits of set (src/effect_next.c:
 * the effects this device builds, or the whole table where it builds none):
 * the lowest bit of set above mask's highest, else set's lowest; set's lowest
 * for no pick (mask 0). One bit, always of set (set is not 0). */
static inline uint32_t lantern_effect_next(uint32_t mask, uint32_t set) {
    uint32_t above = mask == 0 ? set : set & ~((2u << (31 - __builtin_clz(mask))) - 1);

    if (above == 0) {
        above = set;
    }
    return above & -above;
}

#if IS_ENABLED(CONFIG_LANTERN_EFFECT)

/* The pick stored on this device, the host's bits as sent (built here or
 * not) and its colour, at most LANTERN_EFFECT_COLOUR_MAX (colour may be
 * NULL). False while none has been stored, and both are then left alone. */
bool lantern_effect_get(uint32_t *mask, uint32_t *colour);

/* Split central only: store the pick here and send it to every half. mask is
 * one or more bits of LANTERN_EFFECT_ALL and colour at most
 * LANTERN_EFFECT_COLOUR_MAX, else -EINVAL; -EAGAIN until the settings have
 * loaded at boot. 0 means this device stored the pick and handed the halves'
 * invocations to ZMK's split transport, which confirms nothing: a half that is
 * away gets the pick when it is back (src/pick.c). */
int lantern_effect_send(uint32_t mask, uint32_t colour);

#else

static inline bool lantern_effect_get(uint32_t *mask, uint32_t *colour) {
    ARG_UNUSED(mask);
    ARG_UNUSED(colour);
    return false;
}

#endif /* IS_ENABLED(CONFIG_LANTERN_EFFECT) */
