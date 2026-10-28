/*
 * Copyright (c) 2026 akira-toriyama
 *
 * SPDX-License-Identifier: MIT
 *
 * The layer's state (src/layer.c), for the layer keys
 * (src/effects/layer_keys.c): which keys the keymap's layer of the moment
 * binds, and the colour it shows in, as the split central sends it to the
 * halves. Layer contract: the central computes the state, a half only
 * stores it and draws it; the invocation below is a wire format, the same
 * on every device of the split (a change is a change on every device at
 * once).
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/sys/util.h>

#include <zmk/matrix.h>

/* A state goes as one invocation a chunk of LANTERN_LAYER_CHUNK_KEYS key
 * positions, in order, chunk c holding the positions from
 * c x LANTERN_LAYER_CHUNK_KEYS: param1 the first 32 of them, a bit each,
 * param2's low LANTERN_LAYER_HIGH_KEYS bits the next, and param2 from
 * LANTERN_LAYER_SLOT_SHIFT up the palette slot of the layer's colour, the
 * same in every chunk. The event's position carries the chunk's number in
 * its low bits and, on the state's last chunk, LANTERN_LAYER_LAST (ZMK's
 * split transport carries the position as a byte), so a half takes the state
 * whole at the chunk so marked, whatever its own chunk count. No key in any
 * chunk is no layer shown, sent as zeros. Each device counts its chunks from
 * its own keymap (ZMK_KEYMAP_LEN): one up to 61 key positions (canon's 58),
 * two up to 122 (the module's check builds: the stock Imprint halves' 76,
 * the stock dongle's 82); a half keeps the chunks its keymap holds and passes
 * over the rest, which name no key under its LEDs. */
#define LANTERN_LAYER_HIGH_KEYS 29u
#define LANTERN_LAYER_CHUNK_KEYS (32u + LANTERN_LAYER_HIGH_KEYS)
#define LANTERN_LAYER_SLOT_SHIFT LANTERN_LAYER_HIGH_KEYS
#define LANTERN_LAYER_HIGH_MASK (BIT(LANTERN_LAYER_HIGH_KEYS) - 1u)
#define LANTERN_LAYER_CHUNKS DIV_ROUND_UP((uint32_t)ZMK_KEYMAP_LEN, LANTERN_LAYER_CHUNK_KEYS)
#define LANTERN_LAYER_LAST 0x80u
#define LANTERN_LAYER_CHUNK_MASK (LANTERN_LAYER_LAST - 1u)

/* A state as words: chunk c's param1 at 2c, its param2 at 2c + 1. */
#define LANTERN_LAYER_WORDS (2u * LANTERN_LAYER_CHUNKS)

/* Whether key position k is in the state w (LANTERN_LAYER_WORDS words). */
static inline bool lantern_layer_has(const uint32_t w[], uint32_t k) {
    uint32_t c = k / LANTERN_LAYER_CHUNK_KEYS, off = k % LANTERN_LAYER_CHUNK_KEYS;

    if (c >= LANTERN_LAYER_CHUNKS) {
        return false;
    }
    if (off < 32) {
        return (w[2 * c] >> off) & 1;
    }
    return ((w[2 * c + 1] & LANTERN_LAYER_HIGH_MASK) >> (off - 32)) & 1;
}

/* The palette slot of a chunk's param2. */
static inline uint32_t lantern_layer_slot(uint32_t param2) {
    return param2 >> LANTERN_LAYER_SLOT_SHIFT;
}

#if IS_ENABLED(CONFIG_LANTERN_LAYER_KEYS)
/* Chunk chunk of a state arrived, last when it completes the state
 * (src/layer.c: on a half from the Bluetooth receive work queue, on a split
 * central with key LEDs from the relay's queue; the slot a palette slot): a
 * chunk this device's keymap holds is kept, and at the last the state is the
 * layer keys', the LEDs repainted while lit when it changed. Callers on one
 * device never interleave (src/effects/layer_keys.c says why). */
void lantern_layer_keys_set(uint32_t chunk, bool last, uint32_t param1, uint32_t param2);

/* No layer shown (a half's link to the central went, or its USB power). */
void lantern_layer_keys_clear(void);
#endif
