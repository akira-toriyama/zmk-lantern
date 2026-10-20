/*
 * Copyright (c) 2026 akira-toriyama
 *
 * SPDX-License-Identifier: MIT
 *
 * The key LEDs' picture: each LED shows one palette slot (src/palette.c), the
 * one the consumer's lantern,keys node assigns to the key under it, so the
 * palette's colours fall on the keyboard in bands (canon t-69g7: slots 0..4
 * left to right across both halves). This file is the strip's one writer. The
 * picture is static, so a frame goes out on demand, never on a tick: when the
 * rail rule lights or blanks the LEDs (src/on_usb_power.c) and when the
 * palette changes while they are lit; effects to come (canon t-6ttk) add
 * their tick here.
 *
 * - Why the module writes the strip itself and ZMK's underglow is asserted
 *   off: rgb_underglow.c repaints every LED with its one colour and effect at
 *   each 50 ms tick while on, so a per-LED picture cannot share the strip
 *   with it, and it saves its state (on, colour) and restores it at the next
 *   boot, which the rail rule had to override every time (zmk-lantern up to
 *   2406809 lit the LEDs through it). With it off nothing else touches the
 *   strip, and a frame is written only when something changed.
 * - The picture: LED i shows the colour of slot slots[positions[i]]. A slot
 *   that is 0x000000 shows slot 0 instead: the host clears the slots it does
 *   not send (lantern_palette_send()), so a palette of one colour is a solid
 *   one, and a black among the colours of a sprite (canon t-0967 keeps
 *   black) does not leave keys dark. Without a palette every LED is white.
 *   Each colour is scaled so that its brightest channel is
 *   CONFIG_LANTERN_BRIGHTNESS percent of full scale: hue and saturation are
 *   the colour's, the brightness is the device's (the LEDs sit under keycaps;
 *   16 is what ZMK's underglow drove at canon's earlier settings, brightness
 *   25 between 5 and 50). A grey has no hue and scales to white; so does
 *   black, which has no brightest channel, when it is slot 0.
 * - Threads: the frame is composed and written on ZMK's low priority work
 *   queue (zmk_workqueue_lowprio_work_q(), the one rgb_underglow.c used; the
 *   Kconfig selects it). led_strip_update_rgb() blocks for the SPI transfer
 *   (the WS2812 SPI driver sends the node's whole chain-length, 24 bytes an
 *   LED at 4 MHz: 1.4 ms for 29 LEDs, 7 ms for Cyboard's placeholder 150,
 *   hence the consumer sets the real count) and stays off the system work
 *   queue, where ZMK processes key events, and off the Bluetooth receive
 *   queue, where a half runs the palette behavior. A caller only sets the
 *   wanted state (lit or dark, one atomic) and submits the one work item; the
 *   handler reads the state and the palette's atomics when it runs, so two
 *   submissions coalesce, the last state wins, a submission during the
 *   handler runs it again, and nothing holds a lock.
 * - The rail rule's order holds: lantern_keys_blank() has the black frame on
 *   the queue when it returns, and the rail drops BLANK_MS later
 *   (src/on_usb_power.c); lantern_keys_light() is called with the rail up and
 *   settled, so the frame meets powered LEDs.
 */

#define DT_DRV_COMPAT lantern_keys

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

#include <zmk/matrix.h>
#include <zmk/workqueue.h>

#include "keys.h"
#include "palette.h"

LOG_MODULE_REGISTER(lantern_keys, LOG_LEVEL_INF);

#define NODE DT_DRV_INST(0)
#define STRIP DT_INST_PHANDLE(0, strip)
#define LED_COUNT DT_INST_PROP_LEN(0, positions)
#define KEY_COUNT DT_INST_PROP_LEN(0, slots)

BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) == 1,
             "CONFIG_LANTERN_KEYS needs exactly one lantern,keys node");
BUILD_ASSERT(!IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW),
             "CONFIG_ZMK_RGB_UNDERGLOW must be n with CONFIG_LANTERN_KEYS: its tick would repaint "
             "the strip this module paints (the header says why)");
BUILD_ASSERT(LED_COUNT == DT_PROP(STRIP, chain_length),
             "lantern,keys: positions needs one entry per LED, the strip's chain-length");
BUILD_ASSERT(KEY_COUNT == ZMK_KEYMAP_LEN,
             "lantern,keys: slots needs one entry per key position of the keymap");

#define POSITION_IN_RANGE(node_id, prop, idx)                                                     \
    BUILD_ASSERT(DT_PROP_BY_IDX(node_id, prop, idx) < KEY_COUNT,                                  \
                 "lantern,keys: a positions entry is no key position of slots");
DT_FOREACH_PROP_ELEM(NODE, positions, POSITION_IN_RANGE)

#define SLOT_IN_RANGE(node_id, prop, idx)                                                         \
    BUILD_ASSERT(DT_PROP_BY_IDX(node_id, prop, idx) < LANTERN_PALETTE_SLOTS,                      \
                 "lantern,keys: a slots entry is no palette slot (0 to 5)");
DT_FOREACH_PROP_ELEM(NODE, slots, SLOT_IN_RANGE)

/* The brightest channel of every colour painted. */
#define LEVEL ((255 * CONFIG_LANTERN_BRIGHTNESS + 50) / 100)

static const struct device *const strip = DEVICE_DT_GET(STRIP);
static const uint8_t positions[LED_COUNT] = DT_INST_PROP(0, positions);
static const uint8_t slots[KEY_COUNT] = DT_INST_PROP(0, slots);
static struct led_rgb pixels[LED_COUNT];
static atomic_t lit;

/* The 0xRRGGBB scaled so that its brightest channel is LEVEL; black to white. */
static struct led_rgb scaled(uint32_t rgb) {
    unsigned int r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
    unsigned int max = MAX(r, MAX(g, b));
    if (max == 0) {
        return (struct led_rgb){.r = LEVEL, .g = LEVEL, .b = LEVEL};
    }
    return (struct led_rgb){
        .r = (uint8_t)((r * LEVEL + max / 2) / max),
        .g = (uint8_t)((g * LEVEL + max / 2) / max),
        .b = (uint8_t)((b * LEVEL + max / 2) / max),
    };
}

static void compose(void) {
    uint32_t palette[LANTERN_PALETTE_SLOTS];
    bool have = IS_ENABLED(CONFIG_LANTERN_PALETTE) && lantern_palette_get(palette);
    struct led_rgb colour[LANTERN_PALETTE_SLOTS];

    for (size_t s = 0; s < LANTERN_PALETTE_SLOTS; s++) {
        uint32_t rgb = 0xFFFFFF;
        if (have) {
            rgb = palette[s] != 0 ? palette[s] : palette[0];
        }
        colour[s] = scaled(rgb);
    }
    for (size_t i = 0; i < LED_COUNT; i++) {
        pixels[i] = colour[slots[positions[i]]];
    }
}

static void paint(struct k_work *work) {
    ARG_UNUSED(work);
    if (atomic_get(&lit)) {
        compose();
    } else {
        memset(pixels, 0, sizeof(pixels));
    }
    int rc = led_strip_update_rgb(strip, pixels, LED_COUNT);
    if (rc < 0) {
        LOG_ERR("frame not written: %d", rc);
    }
}

static K_WORK_DEFINE(paint_work, paint);

static void submit(void) { k_work_submit_to_queue(zmk_workqueue_lowprio_work_q(), &paint_work); }

void lantern_keys_light(void) {
    atomic_set(&lit, 1);
    submit();
}

void lantern_keys_blank(void) {
    atomic_set(&lit, 0);
    submit();
}

void lantern_keys_refresh(void) {
    if (atomic_get(&lit)) {
        submit();
    }
}
