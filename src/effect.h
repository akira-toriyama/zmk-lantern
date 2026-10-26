/*
 * Copyright (c) 2026 akira-toriyama
 *
 * SPDX-License-Identifier: MIT
 *
 * The effects' table and the pick (src/effect.c): which of the effects built
 * into a half's image draw, chosen at run time by the split central's host and
 * kept by every device. For the port (src/port.c), which names them, and the
 * key LEDs (src/keys.c), which draw them. Layer contract: the port parses and
 * replies and never stores a pick; every change goes through the effect
 * behavior, on the central and the halves alike; the key LEDs only read.
 *
 * A new effect is one line in LANTERN_EFFECT_LIST, its Kconfig symbol
 * LANTERN_<SYMBOL> (which selects LANTERN_EFFECTS) and a default-choice member
 * LANTERN_EFFECT_DEFAULT_<SYMBOL>, and its draw in src/keys.c; the bit, the
 * name and the mask of what is built follow from the line. The bit is a wire
 * format (the behavior's param1, the settings value, canon's dongle.py names
 * it): never renumber a line, append.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/sys/util.h>

/* X(SYMBOL, bit, "name"): the effects, in the order the host's replies list
 * them. SYMBOL is the Kconfig suffix, the name the host's word for it. */
#define LANTERN_EFFECT_LIST(X)                                                                     \
    X(THIN_RING, 0, "thin-ring")                                                                   \
    X(DISC, 1, "disc")                                                                             \
    X(STAIN, 2, "stain")                                                                           \
    X(CROSS_RING, 3, "cross-ring")

/* Each effect's bit in a pick: LANTERN_EFFECT_THIN_RING and so on. */
enum lantern_effect {
#define LANTERN_EFFECT_BIT_(sym, bit, name) LANTERN_EFFECT_##sym = BIT(bit),
    LANTERN_EFFECT_LIST(LANTERN_EFFECT_BIT_)
#undef LANTERN_EFFECT_BIT_
};

#define LANTERN_EFFECT_OR_(sym, bit, name) | BIT(bit)
#define LANTERN_EFFECT_BUILT_(sym, bit, name) | (IS_ENABLED(CONFIG_LANTERN_##sym) ? BIT(bit) : 0)
#define LANTERN_EFFECT_DEFAULT_(sym, bit, name)                                                    \
    | (IS_ENABLED(CONFIG_LANTERN_EFFECT_DEFAULT_##sym) ? BIT(bit) : 0)
#define LANTERN_EFFECT_ONE_(sym, bit, name) +1

/* Every effect the table knows, those built into this image, the one the
 * Kconfig choice LANTERN_EFFECT_DEFAULT picks, and how many the table has. */
#define LANTERN_EFFECT_ALL (0 LANTERN_EFFECT_LIST(LANTERN_EFFECT_OR_))
#define LANTERN_EFFECT_BUILT (0 LANTERN_EFFECT_LIST(LANTERN_EFFECT_BUILT_))
#define LANTERN_EFFECT_DEFAULT (0 LANTERN_EFFECT_LIST(LANTERN_EFFECT_DEFAULT_))
#define LANTERN_EFFECT_COUNT (0 LANTERN_EFFECT_LIST(LANTERN_EFFECT_ONE_))
/* The longest name a line of the table may carry (src/port.c sizes its line
 * and its reply from it; asserted there). */
#define LANTERN_EFFECT_NAME_MAX 15

BUILD_ASSERT(__builtin_popcount(LANTERN_EFFECT_ALL) == LANTERN_EFFECT_COUNT,
             "LANTERN_EFFECT_LIST: two effects share a bit");

#if IS_ENABLED(CONFIG_LANTERN_EFFECT)

/* The pick stored on this device, the host's bits as sent (built here or
 * not). False while none has been stored, and *mask is then left alone. */
bool lantern_effect_get(uint32_t *mask);

/* Split central only: store the pick here and send it to every half. mask is
 * one or more bits of LANTERN_EFFECT_ALL, else -EINVAL; -EAGAIN until the
 * settings have loaded at boot. 0 means this device stored the pick and
 * handed the halves' invocations to ZMK's split transport, which confirms
 * nothing: a half that is away gets the pick when it is back (src/effect.c). */
int lantern_effect_send(uint32_t mask);

#else

static inline bool lantern_effect_get(uint32_t *mask) {
    ARG_UNUSED(mask);
    return false;
}

#endif /* IS_ENABLED(CONFIG_LANTERN_EFFECT) */
