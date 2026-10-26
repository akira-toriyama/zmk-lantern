/*
 * Copyright (c) 2026 akira-toriyama
 *
 * SPDX-License-Identifier: MIT
 *
 * The pick: which of the effects built into a half's image draw (canon
 * t-5hqx), a mask of the bits src/effect.h gives them, chosen by the host on
 * the split central's port (src/port.c), kept by the central and every half
 * in their settings across boots, and read by the key LEDs at every frame
 * (src/keys.c: the bits built there draw, and with none of them built, or no
 * pick stored, the one the Kconfig choice LANTERN_EFFECT_DEFAULT names). The
 * image carries every effect the consumer's conf turns on; the pick says which
 * show, so canon changes the effect of the day without flashing (its
 * dongle.py effect) and a consumer without a host has the default.
 *
 * - One path for every change, the behavior lantern,behavior-effect, named
 *   effect on every device, invoked by the central's code with
 *   zmk_behavior_invoke_binding() as the palette's is (src/palette.c says how
 *   ZMK's split transport carries it by the node's name and why the name is at
 *   most 8 characters): param1 = the mask, absolute, param2 = 0, press only.
 *   One invocation a pick, so the central's run queue, which src/palette.c
 *   sizes for a whole palette a half, must hold one more a half (asserted
 *   below): a resend after a half's battery report (src/palette.c: the one
 *   signal ZMK's central raises for a half that is back) queues the palette's
 *   three and this one to that half at once, and both halves may report
 *   together.
 * - A mask is the host's bits as sent, the ones this image does not build
 *   included, so that a device flashed with more effects shows the pick it
 *   already holds, and the port answers with what the host sent; src/keys.c
 *   masks with what is built when it draws. Several bits draw together (the
 *   grammar allows it; canon sends one at a time, t-5hqx). 0 is no pick: the
 *   port refuses it, the halves show the default.
 * - Settings: "lantern/effect/mask", the mask as one 32-bit value, saved when
 *   it changed or the first pick arrived, never for a pick a device already
 *   holds (a half is resent the pick at every battery report, as the
 *   palette); a stored value of another size is ignored at load. On a half the
 *   handler clears the effects' state and repaints (lantern_keys_effect_changed(),
 *   src/keys.c) when the mask changed: a ring half-way out would otherwise
 *   turn into the new effect's shape. At boot the first frame reads the mask
 *   itself after settings_load() restored it, so this subtree's commit handler
 *   only marks the load done. Until then a command is refused with -EAGAIN, as
 *   the palette's.
 * - Threads, as src/palette.c's: the handler runs where ZMK runs behaviors (a
 *   half: the Bluetooth receive work queue; the central: the system work
 *   queue, where the port's line and the resend run); the mask and the two
 *   flags are atomics; the save is a work item on the system work queue (a
 *   flash write stays off the Bluetooth queue).
 * - Delivery is not confirmed, as the palette's: the next battery report of a
 *   half resends the pick.
 */

#define DT_DRV_COMPAT lantern_behavior_effect

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

#include "effect.h"

#if IS_ENABLED(CONFIG_LANTERN_KEYS)
#include "keys.h"
#endif
#if IS_ENABLED(CONFIG_LANTERN_PALETTE)
#include "palette.h"
#endif

LOG_MODULE_REGISTER(lantern_effect, LOG_LEVEL_INF);

BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) == 1,
             "CONFIG_LANTERN_EFFECT needs exactly one lantern,behavior-effect node");
BUILD_ASSERT(IS_ENABLED(CONFIG_SETTINGS),
             "CONFIG_LANTERN_EFFECT needs CONFIG_SETTINGS: the pick lives in the settings");
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
BUILD_ASSERT(IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING),
             "CONFIG_LANTERN_EFFECT on a split central needs "
             "CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING: a half's battery report is what "
             "tells the central the half is back, and the pick is resent to it then");
/* A resend to a half is the palette's invocations and this one; both halves
 * may be resent at once (the header). */
#if IS_ENABLED(CONFIG_LANTERN_PALETTE)
#define RESEND_INVOCATIONS (LANTERN_PALETTE_PAIRS + 1)
#else
#define RESEND_INVOCATIONS 1
#endif
BUILD_ASSERT(RESEND_INVOCATIONS * ZMK_SPLIT_CENTRAL_PERIPHERAL_COUNT <=
                 CONFIG_ZMK_SPLIT_BLE_CENTRAL_SPLIT_RUN_QUEUE_SIZE,
             "CONFIG_ZMK_SPLIT_BLE_CENTRAL_SPLIT_RUN_QUEUE_SIZE must hold a whole palette and the "
             "pick for every half at once (the header says why; 8 for two halves)");
#endif

#define BEHAVIOR_NAME DEVICE_DT_NAME(DT_DRV_INST(0))
#define SUBTREE "lantern/effect"
#define KEY SUBTREE "/mask"
#define RESEND_DELAY_MS 1000

static atomic_t mask;
static atomic_t have_pick;
static atomic_t settings_loaded;

bool lantern_effect_get(uint32_t *out) {
    if (!atomic_get(&have_pick)) {
        return false;
    }
    *out = (uint32_t)atomic_get(&mask);
    return true;
}

static void save(struct k_work *work) {
    ARG_UNUSED(work);
    uint32_t copy = (uint32_t)atomic_get(&mask);
    int rc = settings_save_one(KEY, &copy, sizeof(copy));
    if (rc < 0) {
        LOG_ERR("save failed: %d", rc);
    }
}

static K_WORK_DEFINE(save_work, save);

static int on_binding_pressed(struct zmk_behavior_binding *binding,
                              struct zmk_behavior_binding_event event) {
    ARG_UNUSED(event);
    uint32_t m = binding->param1;
    if (m == 0 || (m & ~(uint32_t)LANTERN_EFFECT_ALL) != 0) {
        LOG_WRN("no such pick %08x", m);
        return -EINVAL;
    }
    bool changed = (uint32_t)atomic_set(&mask, (atomic_val_t)m) != m;
    bool new = atomic_set(&have_pick, 1) == 0;
    if (changed || new) {
        k_work_submit(&save_work);
    }
    LOG_INF("effect %08x %s", m, changed || new ? "stored" : "unchanged");
    if (changed || new) {
#if IS_ENABLED(CONFIG_LANTERN_KEYS)
        lantern_keys_effect_changed();
#endif
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
    if (!settings_name_steq(name, "mask", &next) || next) {
        return -ENOENT;
    }
    uint32_t copy;
    if (len != sizeof(copy)) {
        LOG_WRN("stored pick of %u bytes ignored", (unsigned int)len);
        return -EINVAL;
    }
    ssize_t rc = read_cb(cb_arg, &copy, sizeof(copy));
    if (rc < 0) {
        return (int)rc;
    }
    atomic_set(&mask, (atomic_val_t)copy);
    atomic_set(&have_pick, 1);
    return 0;
}

static int on_commit(void) {
    atomic_set(&settings_loaded, 1);
    return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(lantern_effect, SUBTREE, NULL, on_set, on_commit, NULL);

#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)

static struct zmk_behavior_binding_event local_event(void) {
    return (struct zmk_behavior_binding_event){
        .layer = 0,
        .position = 0,
        .timestamp = k_uptime_get(),
        .source = ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL,
    };
}

static struct zmk_behavior_binding pick_binding(uint32_t m) {
    return (struct zmk_behavior_binding){
        .behavior_dev = BEHAVIOR_NAME,
        .param1 = m,
        .param2 = 0,
    };
}

int lantern_effect_send(uint32_t m) {
    if (m == 0 || (m & ~(uint32_t)LANTERN_EFFECT_ALL) != 0) {
        return -EINVAL;
    }
    if (!atomic_get(&settings_loaded)) {
        return -EAGAIN;
    }
    struct zmk_behavior_binding binding = pick_binding(m);
    return zmk_behavior_invoke_binding(&binding, local_event(), true);
}

struct resend {
    struct k_work_delayable work;
    uint8_t source;
};

static void resend(struct k_work *work) {
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    struct resend *r = CONTAINER_OF(dwork, struct resend, work);

    if (!atomic_get(&have_pick)) {
        return;
    }
    struct zmk_behavior_binding binding = pick_binding((uint32_t)atomic_get(&mask));
    int err = zmk_split_central_invoke_behavior(r->source, &binding, local_event(), true);
    if (err < 0) {
        LOG_WRN("resend to half %u failed: %d", r->source, err);
        return;
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
    if (ev->source < ARRAY_SIZE(resends) && ev->state_of_charge > 0 && atomic_get(&have_pick)) {
        k_work_reschedule(&resends[ev->source].work, K_MSEC(RESEND_DELAY_MS));
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(lantern_effect, battery_listener);
ZMK_SUBSCRIPTION(lantern_effect, zmk_peripheral_battery_state_changed);

static int resend_init(void) {
    for (size_t i = 0; i < ARRAY_SIZE(resends); i++) {
        k_work_init_delayable(&resends[i].work, resend);
        resends[i].source = (uint8_t)i;
    }
    return 0;
}

SYS_INIT(resend_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

#endif /* IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL) */
