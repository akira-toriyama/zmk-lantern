/*
 * Copyright (c) 2026 akira-toriyama
 *
 * SPDX-License-Identifier: MIT
 *
 * The intros' table and the pick (src/intro.c): the shows the boot light can
 * play, those built into a half's image, of which the host's pick says which
 * (canon t-qvgv), chosen at run time by the split central's host and kept by
 * every device. For the port (src/port.c), which names them, the key LEDs
 * (src/keys.c), which play them and hand the rail rule (src/on_usb_power.c)
 * the picked one's length. Layer contract: the port parses and replies and
 * never stores a pick; every change goes through the intro behavior, on the
 * central and the halves alike; the key LEDs only read.
 *
 * A new intro is one line in LANTERN_INTRO_LIST, its Kconfig symbol
 * LANTERN_INTRO_<SYMBOL> (which selects LANTERN_INTROS, and LANTERN_FRAMES
 * when it moves) and a default-choice member LANTERN_INTRO_DEFAULT_<SYMBOL>,
 * its file under src/intros/ defining the entry lantern_intro_<ops>
 * (src/frame.h's struct lantern_intro_entry) and that file's line in
 * CMakeLists.txt; the bit, the name, the mask of what is built and the
 * engine's table (src/keys.c) follow from the line. The bit is a wire format
 * (the behavior's param1, the settings value, canon's dongle.py names it):
 * never renumber a line, append.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/sys/util.h>

/* X(SYMBOL, bit, "name", ops): the intros, in the order the host's replies
 * list them and a half prefers them when a stored pick names several. SYMBOL
 * is the Kconfig suffix, the name the host's word for it, ops the entry its
 * file defines, lantern_intro_<ops>. The bands are a second of the palette's
 * bands whatever the picture (what the boot light of before the table showed
 * for the no picture); the star waves the ten-second show in red, blue and
 * green (canon t-5v1f). */
#define LANTERN_INTRO_LIST(X)                                                                      \
    X(BANDS, 0, "bands", bands)                                                                    \
    X(STAR_WAVES, 1, "star-waves", star_waves)

/* Each intro's bit in a pick: LANTERN_INTRO_BANDS and so on. */
enum lantern_intro {
#define LANTERN_INTRO_BIT_(sym, bit, name, ops) LANTERN_INTRO_##sym = BIT(bit),
    LANTERN_INTRO_LIST(LANTERN_INTRO_BIT_)
#undef LANTERN_INTRO_BIT_
};

#define LANTERN_INTRO_OR_(sym, bit, name, ops) | BIT(bit)
#define LANTERN_INTRO_BUILT_(sym, bit, name, ops)                                                  \
    | (IS_ENABLED(CONFIG_LANTERN_INTRO_##sym) ? BIT(bit) : 0)
#define LANTERN_INTRO_DEFAULT_(sym, bit, name, ops)                                                \
    | (IS_ENABLED(CONFIG_LANTERN_INTRO_DEFAULT_##sym) ? BIT(bit) : 0)
#define LANTERN_INTRO_ONE_(sym, bit, name, ops) +1

/* Every intro the table knows, those built into this image, the one the
 * Kconfig choice LANTERN_INTRO_DEFAULT picks, and how many the table has. */
#define LANTERN_INTRO_ALL (0 LANTERN_INTRO_LIST(LANTERN_INTRO_OR_))
#define LANTERN_INTRO_BUILT (0 LANTERN_INTRO_LIST(LANTERN_INTRO_BUILT_))
#define LANTERN_INTRO_DEFAULT (0 LANTERN_INTRO_LIST(LANTERN_INTRO_DEFAULT_))
#define LANTERN_INTRO_COUNT (0 LANTERN_INTRO_LIST(LANTERN_INTRO_ONE_))

BUILD_ASSERT(__builtin_popcount(LANTERN_INTRO_ALL) == LANTERN_INTRO_COUNT,
             "LANTERN_INTRO_LIST: two intros share a bit");

#if IS_ENABLED(CONFIG_LANTERN_INTRO)

/* The pick stored on this device, the host's bits as sent (built here or
 * not). False while none has been stored, and *mask is then left alone. */
bool lantern_intro_get(uint32_t *mask);

/* Split central only: store the pick here and send it to every half. mask is
 * one bit of LANTERN_INTRO_ALL, else -EINVAL (none, or a bit the table lacks)
 * or -E2BIG (several); -EAGAIN until the settings have loaded at boot. 0
 * means this device stored the pick and handed the halves' invocations to
 * ZMK's split transport, which confirms nothing: a half that is away gets the
 * pick when it is back (src/pick.c). */
int lantern_intro_send(uint32_t mask);

#else

static inline bool lantern_intro_get(uint32_t *mask) {
    ARG_UNUSED(mask);
    return false;
}

#endif /* IS_ENABLED(CONFIG_LANTERN_INTRO) */
