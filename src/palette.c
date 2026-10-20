/*
 * Copyright (c) 2026 akira-toriyama
 *
 * SPDX-License-Identifier: MIT
 *
 * The palette: six colours (0xRRGGBB, slots 0..5: five from the day's sprite
 * and a spare, canon t-0967) that the host hands the keyboard's split central
 * (src/palette_port.c), which every half and the central keep in their
 * settings across boots, and which a half's key LEDs show in the bands its
 * lantern,keys node draws (src/keys.c: slots 0..4 left to right across
 * canon's halves; its effects, canon t-6ttk, paint from the same slots).
 *
 * - One path for every change, the behavior `lantern,behavior-palette`: the
 *   central's code invokes it with zmk_behavior_invoke_binding(), whose
 *   BEHAVIOR_LOCALITY_GLOBAL sends the same invocation to every half over
 *   ZMK's split transport (app/src/behavior.c) and then runs it here, so the
 *   central stores its own copy by the same handler the halves run. ZMK's
 *   transport carries a behavior by its devicetree node name (at most 8
 *   characters: ZMK_SPLIT_RUN_BEHAVIOR_DEV_LEN 9 in the Bluetooth payload,
 *   app/include/zmk/split/bluetooth/service.h) and two 32-bit parameters
 *   (app/src/split/central.c): the node is named `palette` on every device
 *   (the consumer's keymap or overlay), and an invocation
 *   carries one pair of slots, param1 = pair << 24 | the first slot's colour,
 *   param2 = the second slot's, so a palette is three invocations. Press
 *   only; no release is sent or needed. ZMK's central puts each command into
 *   its run queue (CONFIG_ZMK_SPLIT_BLE_CENTRAL_SPLIT_RUN_QUEUE_SIZE, 5 by
 *   default) from the caller's thread, here the system work queue, and
 *   drains it on its own preemptible thread, one GATT write without response
 *   per command, which blocks only while the ATT TX pool
 *   (CONFIG_BT_ATT_TX_COUNT, shared by both halves) is empty; a put into a
 *   full queue waits up to 100 ms for the drain and then drops the oldest
 *   queued command (app/src/split/bluetooth/central.c,
 *   split_bt_invoke_behavior_payload). A set from the port queues every
 *   half's three in one go, 6 for two halves. The queue is asserted to hold
 *   a whole palette for every half, PAIRS x the peripheral count (a consumer
 *   with two halves sets 6, canon's config/imprint_dongle.conf): a set that
 *   starts on an empty queue then goes in without waiting, and a drain
 *   stalled for 100 ms (a half that stopped acknowledging) drops commands
 *   queued before the set, never the set's own, where ZMK's 5 would drop
 *   the first half's slots 0-1, the ones seen (read 2026-10-19, ZMK
 *   4dd95d78; on an idle link 5 drops nothing either, the drain takes the
 *   first command while the sixth put waits). A resend is one half's three.
 * - Settings: "lantern/palette/colours", the six colours as one value, saved
 *   when a colour changed or the first palette arrived and not otherwise; a
 *   value of another size (stored by a build with another slot count) is
 *   ignored at load, and the device has no palette until the host sends one:
 *   the central resends the palette at every battery report of a half
 *   (below), and a half must not rewrite its flash for a palette it already
 *   holds. The key LEDs are repainted from the handler when a pair changed
 *   (src/keys.c, only while they are lit); at boot the first frame reads the
 *   palette directly after settings_load() restored it (src/on_usb_power.c
 *   starts from a commit handler), so this subtree's commit handler only
 *   marks the load done, and the order of the two commit handlers does not
 *   matter. Zephyr hands a key to the handler whose name matches most of
 *   it, so this subtree and on_usb_power.c's "lantern" (which refuses every
 *   key) coexist.
 * - What the slots change: the picture src/keys.c paints, each key its
 *   slot's colour, at the device's brightness (CONFIG_LANTERN_BRIGHTNESS); a
 *   grey or black paints white, and a black slot other than 0 paints slot 0.
 *   A device without the keys node (canon's Imprint Dongle) keeps and
 *   forwards the palette and paints nothing.
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
 *   Bluetooth receive work queue that delivers the central's write (ZMK
 *   app/src/split/bluetooth/service.c, CONFIG_BT_RECV_WORKQ_BT), on the
 *   central the system work queue (the port's line, src/palette_port.c, and
 *   the resend); the settings handlers run on the main thread inside
 *   settings_load(); the port's reply and lantern_palette_get() read on the
 *   system work queue, and src/keys.c reads on ZMK's low priority work
 *   queue. The colours and the two flags are atomics, read and
 *   written one at a time, so no lock; a reader between the two slots of a
 *   pair sees one old slot, which the next invocation or boot settles. The
 *   save is a work item on the system work queue: a settings write is a flash
 *   write and stays off the Bluetooth queue. Until the settings have loaded
 *   (this subtree's commit) a command is refused with -EAGAIN: a line taken
 *   earlier would store colours that the load then overwrites, and save those.
 * - Delivery is not confirmed: zmk_behavior_invoke_binding() returns the
 *   local handler's result and ignores the transport's, and the transport
 *   itself drops a command for a half that is not connected or still in
 *   discovery, or when its queue overflows (above). The next battery report
 *   of the half resends the whole palette; on a half whose level does not
 *   move (full, on a charger) that can be hours. canon's call (t-0967): the
 *   palette is a toy, optimistic is fine.
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

#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/split/central.h>
#endif

#include "palette.h"

#if IS_ENABLED(CONFIG_LANTERN_KEYS)
#include "keys.h"
#endif

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

BUILD_ASSERT(LANTERN_PALETTE_SLOTS % 2 == 0, "an invocation carries a pair of slots");
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
BUILD_ASSERT(PAIRS * ZMK_SPLIT_CENTRAL_PERIPHERAL_COUNT <=
                 CONFIG_ZMK_SPLIT_BLE_CENTRAL_SPLIT_RUN_QUEUE_SIZE,
             "CONFIG_ZMK_SPLIT_BLE_CENTRAL_SPLIT_RUN_QUEUE_SIZE must hold a whole palette for "
             "every half at once, one invocation per pair of slots (the header says why)");
#endif

static atomic_t colours[LANTERN_PALETTE_SLOTS];
static atomic_t have_palette;
static atomic_t settings_loaded;

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

static void repaint(void) {
#if IS_ENABLED(CONFIG_LANTERN_KEYS)
    lantern_keys_refresh();
#endif
}

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
    if (changed || new) {
        repaint();
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
    atomic_set(&settings_loaded, 1);
    return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(lantern_palette, SUBTREE, NULL, on_set, on_commit, NULL);

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
    if (!atomic_get(&settings_loaded)) {
        return -EAGAIN;
    }
    uint32_t next[LANTERN_PALETTE_SLOTS] = {0};
    for (size_t i = 0; i < count; i++) {
        if (in[i] > LANTERN_PALETTE_RGB_MAX) {
            return -EINVAL;
        }
        next[i] = in[i];
    }
    int rc = 0;
    for (uint32_t pair = 0; pair < PAIRS; pair++) {
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
