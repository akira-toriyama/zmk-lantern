/*
 * Copyright (c) 2026 akira-toriyama
 *
 * SPDX-License-Identifier: MIT
 *
 * The layer keys (CONFIG_LANTERN_LAYER_KEYS; the pick's layer-keys), an entry
 * of the effects' table (src/frame.h): while a layer the split central shows
 * is held (src/layer.c: its node's layers property names those layers and
 * their colours; canon t-6ttk's research, 2.4), the keys it binds to
 * something of its own light in its colour, the palette slot the property
 * gives it, at full brightness within the frame's cap, and every other key
 * shows what the picture and the other effects leave there; with no shown
 * layer held, nothing. The keys are what the layer can type: canon's Number
 * layer lights the right half's ten digits and its Function layer the twelve
 * F keys, its Vkey layers fifteen keys on each half, so a layer of one
 * half's keys leaves the other half as it was.
 *
 * - Drawn as an effect (draw), over the picture and the live presses, never
 *   painted into the picture: the cap then holds the lit keys (sixteen keys
 *   white at full scale are two and a half times CAP on canon's halves;
 *   src/keys.c scales them all alike), where the picture's share is never
 *   scaled. So a layer of many keys, or of a whitish colour, shows dimmer
 *   than one of few, and with a press effect picked too the layer's keys dim
 *   while the press's effect lives, the two sharing the cap. The last line of
 *   the table, so with other effects picked it draws over them. A pick's
 *   colour paints every layer in it, as every effect (the frame's shades):
 *   the layers told apart by colour alone then look alike.
 * - The state is the layer's, not a press's: life() is 0, so with the layer
 *   keys picked alone a press costs one frame and moves nothing, and a
 *   change of the state repaints through lantern_keys_refresh(), a still
 *   frame with no tick.
 * - The state arrives in chunks (src/layer.h): each is kept aside (staged)
 *   and the one marked last copies them all into the atomics the frame reads
 *   (live), so a frame never draws half a state's chunks from the wire. The
 *   copy is word by word, so a frame composed during it may draw a mix of
 *   the old and the new state once, which the repaint after it mends, as
 *   with the picks' two atomics (src/pick.c). The writers never interleave:
 *   on a half they are the Bluetooth receive work queue (the behavior) and
 *   the system work queue (the link's end, which Zephyr's conn.c reports
 *   from its deferred work there, and USB power's end), both cooperative
 *   threads that run the store to its end without yielding; on a central
 *   with key LEDs, the relay's queue alone. Only the frame's thread, ZMK's
 *   preemptible low priority queue, can be preempted mid-read.
 * - The engine's clear (a blank, a change of the stage or the pick) leaves
 *   the state alone: it is the central's, and src/layer.c keeps it current on
 *   a lit half and drops it when the half's link or USB power goes.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

#include "../effect.h"
#include "../frame.h"
#include "../keys.h"
#include "../layer.h"
#include "../palette.h"

/* The state (the header): the chunks as they arrive, the writer's alone, and
 * the last whole state, which the frame reads. */
static uint32_t staged[LANTERN_LAYER_WORDS];
static atomic_t live[LANTERN_LAYER_WORDS];

/* The writer's thread: staged into live; repaints when it changed. */
static void commit(void) {
    bool changed = false;

    for (size_t w = 0; w < LANTERN_LAYER_WORDS; w++) {
        changed |= (uint32_t)atomic_set(&live[w], (atomic_val_t)staged[w]) != staged[w];
    }
    if (changed) {
        lantern_keys_refresh();
    }
}

void lantern_layer_keys_set(uint32_t chunk, bool last, uint32_t param1, uint32_t param2) {
    if (lantern_layer_slot(param2) >= LANTERN_PALETTE_SLOTS) {
        return; /* src/layer.c refuses it before */
    }
    if (chunk < LANTERN_LAYER_CHUNKS) {
        staged[2 * chunk] = param1;
        staged[2 * chunk + 1] = param2;
    }
    if (last) {
        commit();
    }
}

void lantern_layer_keys_clear(void) {
    for (size_t w = 0; w < LANTERN_LAYER_WORDS; w++) {
        staged[w] = 0;
    }
    commit();
}

/* The keys of the shown layer, each its LED pulled all the way to the
 * layer's shade at full scale (the header). */
static void layer_keys_draw(struct led_rgb px[], const struct shades *sh, const struct hit live_hits[],
                            size_t n, uint32_t picked) {
    ARG_UNUSED(live_hits);
    ARG_UNUSED(n);
    ARG_UNUSED(picked);
    uint32_t w[LANTERN_LAYER_WORDS];
    bool any = false;

    for (size_t i = 0; i < LANTERN_LAYER_WORDS; i++) {
        w[i] = (uint32_t)atomic_get(&live[i]);
        any |= (i % 2 == 0 ? w[i] : w[i] & LANTERN_LAYER_HIGH_MASK) != 0;
    }
    if (!any) {
        return;
    }
    /* every chunk carries the slot, a palette slot (lantern_layer_keys_set());
     * chunk 0's is read */
    struct led_rgb c = shade_hi(sh, sh->rank[lantern_layer_slot(w[1])]);

    for (size_t i = 0; i < LED_COUNT; i++) {
        if (lantern_layer_has(w, led_key(i))) {
            px[i] = c;
        }
    }
}

/* A press draws nothing of the layer keys (the header). */
static uint32_t layer_keys_life(void) { return 0; }

const struct lantern_effect_entry lantern_effect_layer_keys = {
    .life = layer_keys_life,
    .draw = layer_keys_draw,
};
