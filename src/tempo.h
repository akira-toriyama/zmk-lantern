/*
 * Copyright (c) 2026 akira-toriyama
 *
 * SPDX-License-Identifier: MIT
 *
 * The tempo's pick (src/tempo.c): whether the tempo runs (src/keys.c,
 * CONFIG_LANTERN_TEMPO: the effects faster the faster the user types),
 * chosen at run time by the host on the central (a split's, or the keyboard
 * itself when it is not a split) and kept by every device.
 * For the port (src/port.c), which names the two states, and the key LEDs
 * (src/keys.c), which read it. Layer contract: the port parses and replies
 * and never stores a pick; every change goes through the tempo behavior, on
 * the central and the halves alike; the key LEDs only read.
 *
 * Two states, a bit each, since the pick mechanism takes no empty mask (0 is
 * no pick): on and off. The bit is a wire format (the behavior's param1, the
 * settings value, canon's dongle.py names it): never renumber a line.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/sys/util.h>

/* X(SYMBOL, bit, "name", ops): the states, in the order the host's replies
 * list them; ops is unused (the effects' and intros' lists have an entry a
 * line, this one none) and kept for the port's one table macro. */
#define LANTERN_TEMPO_LIST(X)                                                                      \
    X(ON, 0, "on", on)                                                                             \
    X(OFF, 1, "off", off)

/* Each state's bit in a pick: LANTERN_TEMPO_ON, LANTERN_TEMPO_OFF. */
enum lantern_tempo {
#define LANTERN_TEMPO_BIT_(sym, bit, name, ops) LANTERN_TEMPO_##sym = BIT(bit),
    LANTERN_TEMPO_LIST(LANTERN_TEMPO_BIT_)
#undef LANTERN_TEMPO_BIT_
};

#define LANTERN_TEMPO_OR_(sym, bit, name, ops) | BIT(bit)
#define LANTERN_TEMPO_ONE_(sym, bit, name, ops) +1

/* Every state the pick knows, and how many. */
#define LANTERN_TEMPO_ALL (0 LANTERN_TEMPO_LIST(LANTERN_TEMPO_OR_))
#define LANTERN_TEMPO_COUNT (0 LANTERN_TEMPO_LIST(LANTERN_TEMPO_ONE_))

BUILD_ASSERT(__builtin_popcount(LANTERN_TEMPO_ALL) == LANTERN_TEMPO_COUNT,
             "LANTERN_TEMPO_LIST: two states share a bit");

#if IS_ENABLED(CONFIG_LANTERN_TEMPO_PICK)

/* The pick stored on this device, the host's bits as sent; arg is the pick
 * mechanism's argument, which the tempo has none of (0; NULL to skip it).
 * False while none has been stored, and *mask is then left alone. */
bool lantern_tempo_get(uint32_t *mask, uint32_t *arg);

/* Where the keymap runs (a split central, or a keyboard that is not a
 * split): store the pick here and send it to every half of a split. mask is
 * one bit of LANTERN_TEMPO_ALL, else -EINVAL (none, or a bit the list lacks)
 * or -E2BIG (both); arg is 0 (the tempo takes none), else -EINVAL; -EAGAIN
 * until the settings have loaded at boot. 0 means this device stored the pick
 * and, on a split central, handed the halves' invocations to ZMK's split
 * transport, which confirms nothing: a half that is away gets the pick when
 * it is back (src/pick.c). */
int lantern_tempo_send(uint32_t mask, uint32_t arg);

#else

static inline bool lantern_tempo_get(uint32_t *mask, uint32_t *arg) {
    ARG_UNUSED(mask);
    ARG_UNUSED(arg);
    return false;
}

#endif /* IS_ENABLED(CONFIG_LANTERN_TEMPO_PICK) */
