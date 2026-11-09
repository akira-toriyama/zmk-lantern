/*
 * Copyright (c) 2026 akira-toriyama
 *
 * SPDX-License-Identifier: MIT
 *
 * The pick of the tempo (canon t-6ttk): whether the effects run faster the
 * faster the user types (the bits of src/tempo.h, on or off, one a pick),
 * chosen by the host on the central's port (src/port.c), kept by the
 * central and every half in their settings across boots and read by the key
 * LEDs at every frame that reads the hits (src/keys.c: on for the on bit,
 * off for the off bit, and with no pick stored, or neither bit in it, the
 * Kconfig default LANTERN_TEMPO_DEFAULT). An instance of the pick (src/pick.h,
 * which says how it is stored, sent and resent): the node
 * lantern,behavior-tempo, named tempo on every device, the key
 * "lantern/tempo/mask", one bit a pick and no argument, and nothing to do on
 * a half when it changes, since the next frame reads it.
 */

#define DT_DRV_COMPAT lantern_behavior_tempo

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/util.h>

#include <drivers/behavior.h>
#include <zmk/behavior.h>

#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>
#endif

#include "pick.h"
#include "tempo.h"

BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) == 1,
             "CONFIG_LANTERN_TEMPO_PICK needs exactly one lantern,behavior-tempo node");

static struct lantern_pick pick = {
    .name = "tempo",
    .key = "lantern/tempo/mask",
    .behavior = DEVICE_DT_NAME(DT_DRV_INST(0)),
    .all = LANTERN_TEMPO_ALL,
    .arg_max = 0,
    .one = true,
    .changed = NULL,
};

bool lantern_tempo_get(uint32_t *mask, uint32_t *arg) { return lantern_pick_get(&pick, mask, arg); }

static int on_binding_pressed(struct zmk_behavior_binding *binding,
                              struct zmk_behavior_binding_event event) {
    ARG_UNUSED(event);
    int rc = lantern_pick_pressed(&pick, binding->param1, binding->param2);
    return rc < 0 ? rc : ZMK_BEHAVIOR_OPAQUE;
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
    return lantern_pick_set(&pick, name, len, read_cb, cb_arg);
}

static int on_commit(void) {
    lantern_pick_commit(&pick);
    return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(lantern_tempo, "lantern/tempo", NULL, on_set, on_commit, NULL);

#if !IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)

int lantern_tempo_send(uint32_t mask, uint32_t arg) { return lantern_pick_send(&pick, mask, arg); }

#endif /* where the keymap runs */

#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)

static int battery_listener(const zmk_event_t *eh) {
    lantern_pick_resend_after(&pick, eh);
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(lantern_tempo, battery_listener);
ZMK_SUBSCRIPTION(lantern_tempo, zmk_peripheral_battery_state_changed);

#endif /* IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL) */

static int tempo_init(void) {
    lantern_pick_init(&pick);
    return 0;
}

SYS_INIT(tempo_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
