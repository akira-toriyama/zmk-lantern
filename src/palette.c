/*
 * Copyright (c) 2026 akira-toriyama
 *
 * SPDX-License-Identifier: MIT
 *
 * The palette: four colours (0xRRGGBB, slots 0..3) that the host hands the
 * keyboard's split central (src/palette_port.c), which every half and the
 * central keep in their settings across boots, and whose slot 0 is the
 * colour ZMK's underglow paints the key LEDs with on a half. The other slots
 * are kept for effects to come (canon t-6ttk) and change nothing yet.
 *
 * - One path for every change, the behavior `lantern,behavior-palette`: the
 *   central's code invokes it with zmk_behavior_invoke_binding(), whose
 *   BEHAVIOR_LOCALITY_GLOBAL sends the same invocation to every half over
 *   ZMK's split transport (app/src/behavior.c) and then runs it here, so the
 *   central stores its own copy by the same handler the halves run. ZMK's
 *   transport carries a behavior by its devicetree node name and two 32-bit
 *   parameters (app/src/split/central.c): the node is named `palette` on
 *   every device (the consumer's keymap or overlay), and an invocation
 *   carries one pair of slots, param1 = pair << 24 | the first slot's colour,
 *   param2 = the second slot's, so a palette is two invocations. Press only;
 *   no release is sent or needed. Two per half fits the central's run queue
 *   (CONFIG_ZMK_SPLIT_BLE_CENTRAL_SPLIT_RUN_QUEUE_SIZE, 5 by default) with
 *   both halves resent at once; one invocation per slot would not.
 * - Settings: "lantern/palette/colours", the four colours as one value, saved
 *   when a colour changed or the first palette arrived and not otherwise:
 *   the central resends the palette at every battery report of a half
 *   (below), and a half must not rewrite its flash for a palette it already
 *   holds. Slot 0 reaches the underglow from the handler and, at boot, from
 *   this subtree's commit handler at commit priority 1: settings_load() (ZMK
 *   app/src/main.c) restores ZMK's own "rgb/underglow/state", colour
 *   included, in its set phase, and the commit phase runs after it, priority
 *   0 first, so the palette has the last word (zephyr
 *   subsys/settings/src/settings.c). The underglow saves its state a minute
 *   after each on/off (src/on_usb_power.c switches it), which stores slot 0's
 *   hue and saturation a second time; harmless, and overridden at every boot.
 *   Zephyr hands a key to the handler whose name matches most of it, so this
 *   subtree and on_usb_power.c's "lantern" (which refuses every key) coexist.
 * - What slot 0 changes: the underglow's hue and saturation. Its brightness
 *   stays what the device has (the consumer's CONFIG_ZMK_RGB_UNDERGLOW_BRT_START,
 *   or ZMK's saved state), read back through zmk_rgb_underglow_calc_brt(0),
 *   which returns the current colour unchanged: ZMK's public API has no other
 *   getter. A grey or black colour therefore paints white (saturation 0) at
 *   that brightness. zmk_rgb_underglow_set_hsb() only sets the colour; a lit
 *   half repaints at its next 50 ms tick, a dark one at its next on.
 * - Resend (a split central, CONFIG_ZMK_SPLIT_ROLE_CENTRAL): a half that was
 *   away when the host set the palette, or that lost its settings, gets it at
 *   its next battery report. ZMK's split central raises
 *   zmk_peripheral_battery_state_changed once after every (re)connection, from
 *   the one read it makes during service discovery, then at every change of
 *   the level, and with 0 on disconnection (app/src/split/bluetooth/central.c;
 *   hence CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING, asserted);
 *   there is no event on the central for a half's connection itself (ZMK
 *   4dd95d78 raises zmk_split_peripheral_status_changed on the half only).
 *   The send waits RESEND_DELAY_MS: the read's answer may arrive before the
 *   discovery has found the half's run-behavior characteristic, and a command
 *   sent until then is dropped with "Run behavior handle not found". A level
 *   report is a few an hour at most, and the half compares before it writes.
 * - Threads: the behavior handler runs where ZMK runs behaviors, on a half the
 *   Bluetooth host's thread that delivers the central's write (ZMK
 *   app/src/split/bluetooth/service.c), on the central the system work queue
 *   (the port's line, the resend); the settings handlers run on the main
 *   thread inside settings_load(); the port's reply and lantern_palette_get()
 *   read on the system work queue. The colours and the "have a palette" flag
 *   are atomics, read and written one at a time, so no lock; a reader between
 *   the two slots of a pair sees one old slot, which the next invocation or
 *   boot settles. The save is a work item on the system work queue: a settings
 *   write is a flash write and stays off the Bluetooth thread.
 */

#define DT_DRV_COMPAT lantern_behavior_palette

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

#include <drivers/behavior.h>
#include <zmk/behavior.h>

#if IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW)
#include <zmk/rgb_underglow.h>
#endif

#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/split/central.h>
#endif

#include "palette.h"

LOG_MODULE_REGISTER(lantern_palette, LOG_LEVEL_INF);

BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) == 1,
             "CONFIG_LANTERN_PALETTE needs exactly one lantern,behavior-palette node");
BUILD_ASSERT(IS_ENABLED(CONFIG_SETTINGS),
             "CONFIG_LANTERN_PALETTE needs CONFIG_SETTINGS: the palette lives in the settings");
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
BUILD_ASSERT(IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING),
             "CONFIG_LANTERN_PALETTE on a split central needs "
             "CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING: a half's battery report is what "
             "tells the central the half is back, and the palette is resent to it then");
#endif

#define BEHAVIOR_NAME DEVICE_DT_NAME(DT_DRV_INST(0))
#define SUBTREE "lantern/palette"
#define KEY SUBTREE "/colours"
#define PAIRS (LANTERN_PALETTE_SLOTS / 2)
#define PAIR_SHIFT 24
#define RESEND_DELAY_MS 1000

static atomic_t colours[LANTERN_PALETTE_SLOTS];
static atomic_t have_palette;

bool lantern_palette_get(uint32_t out[LANTERN_PALETTE_SLOTS]) {
    if (!atomic_get(&have_palette)) {
        return false;
    }
    for (size_t i = 0; i < LANTERN_PALETTE_SLOTS; i++) {
        out[i] = (uint32_t)atomic_get(&colours[i]);
    }
    return true;
}

/* True when the slot's colour changed. */
static bool store(size_t slot, uint32_t rgb) {
    return (uint32_t)atomic_set(&colours[slot], (atomic_val_t)rgb) != rgb;
}

static void save(struct k_work *work) {
    ARG_UNUSED(work);
    uint32_t copy[LANTERN_PALETTE_SLOTS];
    for (size_t i = 0; i < LANTERN_PALETTE_SLOTS; i++) {
        copy[i] = (uint32_t)atomic_get(&colours[i]);
    }
    int rc = settings_save_one(KEY, copy, sizeof(copy));
    if (rc < 0) {
        LOG_ERR("save failed: %d", rc);
    }
}

static K_WORK_DEFINE(save_work, save);

#if IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW)

/* Hue 0..359 and saturation 0..100 of an 0xRRGGBB, with the brightness given;
 * a grey has saturation 0 and hue 0. */
static struct zmk_led_hsb hue_sat(uint32_t rgb, uint8_t brightness) {
    int r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
    int max = MAX(r, MAX(g, b)), min = MIN(r, MIN(g, b)), range = max - min;
    struct zmk_led_hsb hsb = {.h = 0, .s = 0, .b = brightness};

    if (max > 0) {
        hsb.s = (uint8_t)(range * 100 / max);
    }
    if (range > 0) {
        int h;
        if (max == r) {
            h = 60 * (g - b) / range;
        } else if (max == g) {
            h = 120 + 60 * (b - r) / range;
        } else {
            h = 240 + 60 * (r - g) / range;
        }
        hsb.h = (uint16_t)((h + 360) % 360);
    }
    return hsb;
}

static void apply(void) {
    uint32_t rgb = (uint32_t)atomic_get(&colours[0]);
    struct zmk_led_hsb hsb = hue_sat(rgb, zmk_rgb_underglow_calc_brt(0).b);
    int rc = zmk_rgb_underglow_set_hsb(hsb);
    if (rc < 0) {
        LOG_ERR("slot 0 %06x not applied: %d", rgb, rc);
        return;
    }
    LOG_INF("slot 0 %06x applied: hue %u sat %u brt %u", rgb, hsb.h, hsb.s, hsb.b);
}

#else

static void apply(void) {}

#endif /* IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW) */

static int on_binding_pressed(struct zmk_behavior_binding *binding,
                              struct zmk_behavior_binding_event event) {
    ARG_UNUSED(event);
    uint32_t pair = binding->param1 >> PAIR_SHIFT;
    if (pair >= PAIRS) {
        LOG_WRN("no slot pair %u", pair);
        return -EINVAL;
    }
    size_t first = 2 * pair;
    uint32_t a = binding->param1 & LANTERN_PALETTE_RGB_MAX;
    uint32_t b = binding->param2 & LANTERN_PALETTE_RGB_MAX;
    bool changed = store(first, a);
    changed = store(first + 1, b) || changed;
    bool new = atomic_set(&have_palette, 1) == 0;
    if (changed || new) {
        k_work_submit(&save_work);
    }
    LOG_INF("slots %u-%u %06x %06x %s", first, first + 1, a, b, changed || new ? "stored" : "unchanged");
    if (pair == 0) {
        apply();
    }
    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_binding_released(struct zmk_behavior_binding *binding,
                               struct zmk_behavior_binding_event event) {
    ARG_UNUSED(binding);
    ARG_UNUSED(event);
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api api = {
    .locality = BEHAVIOR_LOCALITY_GLOBAL,
    .binding_pressed = on_binding_pressed,
    .binding_released = on_binding_released,
};

BEHAVIOR_DT_INST_DEFINE(0, NULL, NULL, NULL, NULL, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,
                        &api);

static int on_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg) {
    const char *next;
    if (!settings_name_steq(name, "colours", &next) || next) {
        return -ENOENT;
    }
    uint32_t copy[LANTERN_PALETTE_SLOTS];
    if (len != sizeof(copy)) {
        LOG_WRN("stored palette of %u bytes ignored", (unsigned int)len);
        return -EINVAL;
    }
    ssize_t rc = read_cb(cb_arg, copy, sizeof(copy));
    if (rc < 0) {
        return (int)rc;
    }
    for (size_t i = 0; i < LANTERN_PALETTE_SLOTS; i++) {
        atomic_set(&colours[i], (atomic_val_t)(copy[i] & LANTERN_PALETTE_RGB_MAX));
    }
    atomic_set(&have_palette, 1);
    return 0;
}

static int on_commit(void) {
    if (atomic_get(&have_palette)) {
        apply();
    }
    return 0;
}

/* Commit priority 1: after ZMK's priority 0 handlers, the underglow's set
 * phase among them (the header). */
SETTINGS_STATIC_HANDLER_DEFINE_WITH_CPRIO(lantern_palette, SUBTREE, NULL, on_set, on_commit, NULL,
                                          1);

#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)

static struct zmk_behavior_binding_event local_event(void) {
    return (struct zmk_behavior_binding_event){
        .layer = 0,
        .position = 0,
        .timestamp = k_uptime_get(),
        .source = ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL,
    };
}

static struct zmk_behavior_binding pair_binding(uint32_t pair, uint32_t a, uint32_t b) {
    return (struct zmk_behavior_binding){
        .behavior_dev = BEHAVIOR_NAME,
        .param1 = (pair << PAIR_SHIFT) | a,
        .param2 = b,
    };
}

int lantern_palette_send(const uint32_t *in, size_t count) {
    if (count < 1 || count > LANTERN_PALETTE_SLOTS) {
        return -EINVAL;
    }
    uint32_t next[LANTERN_PALETTE_SLOTS];
    for (size_t i = 0; i < LANTERN_PALETTE_SLOTS; i++) {
        if (i < count) {
            if (in[i] > LANTERN_PALETTE_RGB_MAX) {
                return -EINVAL;
            }
            next[i] = in[i];
        } else {
            next[i] = (uint32_t)atomic_get(&colours[i]);
        }
    }
    int rc = 0;
    for (uint32_t pair = 0; pair < PAIRS && 2 * pair < count; pair++) {
        struct zmk_behavior_binding binding = pair_binding(pair, next[2 * pair], next[2 * pair + 1]);
        int err = zmk_behavior_invoke_binding(&binding, local_event(), true);
        if (err < 0 && rc == 0) {
            rc = err;
        }
    }
    return rc;
}

struct resend {
    struct k_work_delayable work;
    uint8_t source;
};

static void resend(struct k_work *work) {
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    struct resend *r = CONTAINER_OF(dwork, struct resend, work);

    if (!atomic_get(&have_palette)) {
        return;
    }
    for (uint32_t pair = 0; pair < PAIRS; pair++) {
        struct zmk_behavior_binding binding =
            pair_binding(pair, (uint32_t)atomic_get(&colours[2 * pair]),
                         (uint32_t)atomic_get(&colours[2 * pair + 1]));
        int err = zmk_split_central_invoke_behavior(r->source, &binding, local_event(), true);
        if (err < 0) {
            LOG_WRN("resend to half %u failed: %d", r->source, err);
            return;
        }
    }
    LOG_INF("resent to half %u", r->source);
}

static struct resend resends[ZMK_SPLIT_CENTRAL_PERIPHERAL_COUNT];

static int battery_listener(const zmk_event_t *eh) {
    const struct zmk_peripheral_battery_state_changed *ev =
        as_zmk_peripheral_battery_state_changed(eh);
    if (ev == NULL) {
        return ZMK_EV_EVENT_BUBBLE;
    }
    if (ev->source < ARRAY_SIZE(resends) && ev->state_of_charge > 0 &&
        atomic_get(&have_palette)) {
        k_work_reschedule(&resends[ev->source].work, K_MSEC(RESEND_DELAY_MS));
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(lantern_palette, battery_listener);
ZMK_SUBSCRIPTION(lantern_palette, zmk_peripheral_battery_state_changed);

static int resend_init(void) {
    for (size_t i = 0; i < ARRAY_SIZE(resends); i++) {
        k_work_init_delayable(&resends[i].work, resend);
        resends[i].source = (uint8_t)i;
    }
    return 0;
}

SYS_INIT(resend_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

#endif /* IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL) */
