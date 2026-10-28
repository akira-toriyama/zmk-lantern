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

/* A state goes as LANTERN_LAYER_CHUNKS invocations, chunk c carried in the
 * event's position (0 first; the last one completes the state, so the
 * chunks of a state go in order, as ZMK's run queue keeps them), each
 * holding the key positions from c x LANTERN_LAYER_CHUNK_KEYS: param1 the
 * first 32 of them, a bit each, param2's low LANTERN_LAYER_HIGH_KEYS bits
 * the next, and param2 from LANTERN_LAYER_SLOT_SHIFT up the palette slot of
 * the layer's colour, the same in every chunk. No key in any chunk is no
 * layer shown, sent as zeros. One chunk up to 61 key positions (canon's 58),
 * two up to 122 (Cyboard's stock Imprint keymap, 76). */
#define LANTERN_LAYER_HIGH_KEYS 29u
#define LANTERN_LAYER_CHUNK_KEYS (32u + LANTERN_LAYER_HIGH_KEYS)
#define LANTERN_LAYER_SLOT_SHIFT LANTERN_LAYER_HIGH_KEYS
#define LANTERN_LAYER_HIGH_MASK (BIT(LANTERN_LAYER_HIGH_KEYS) - 1u)
#define LANTERN_LAYER_CHUNKS DIV_ROUND_UP((uint32_t)ZMK_KEYMAP_LEN, LANTERN_LAYER_CHUNK_KEYS)

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
/* Chunk c of a state arrived (src/layer.c: on a half from the Bluetooth
 * receive work queue, on a split central with key LEDs from the relay's
 * queue; c below LANTERN_LAYER_CHUNKS, the slot a palette slot): kept, and
 * at the last chunk the state is the layer keys', the LEDs repainted while
 * lit when it changed. One thread at a time per device. */
void lantern_layer_keys_set(uint32_t chunk, uint32_t param1, uint32_t param2);

/* No layer shown, as from the central (a half's link to it went). */
void lantern_layer_keys_clear(void);
#endif
