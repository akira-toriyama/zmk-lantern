/*
 * Copyright (c) 2026 akira-toriyama
 *
 * SPDX-License-Identifier: MIT
 *
 * The pick of the effects (canon t-5hqx): which of the effects built into a
 * half's image draw, a mask of the bits src/effect.h gives them, and in which
 * colour (canon t-rgp5: the behavior's param2, 0xRRGGBB, or 0 for the effects'
 * own colours, the palette's), chosen by the host on the split central's port
 * (src/port.c) or by a key of the central's keymap bound to this behavior
 * (`&effect <mask> <colour>`; src/effect_next.c cycles the mask from a key),
 * kept by the central and every half in their settings across boots, and read
 * by the key LEDs at every frame (src/keys.c: the bits built there draw, and
 * with none of them built, or no pick stored, the one the Kconfig choice
 * LANTERN_EFFECT_DEFAULT names, in the colour LANTERN_EFFECT_COLOUR names). An
 * instance of the pick (src/pick.h, which says how it is stored, sent and
 * resent): the node lantern,behavior-effect, named effect on every device, the
 * key "lantern/effect/mask" (the mask, then the colour), one or more bits a
 * pick (the grammar allows several, which draw together; canon sends one at a
 * time), and on a half a changed pick clears the effects' state and repaints
 * (lantern_keys_effect_changed(), src/keys.c): a ring half-way out would
 * otherwise turn into the new effect's shape, or keep the old colour.
 */

#define DT_DRV_COMPAT lantern_behavior_effect

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

#include "effect.h"
#include "pick.h"

#if IS_ENABLED(CONFIG_LANTERN_KEYS)
#include "keys.h"
#endif

BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) == 1,
             "CONFIG_LANTERN_EFFECT needs exactly one lantern,behavior-effect node");

static void changed(void) {
#if IS_ENABLED(CONFIG_LANTERN_KEYS)
    lantern_keys_effect_changed();
#endif
}

static struct lantern_pick pick = {
    .name = "effect",
    .key = "lantern/effect/mask",
    .behavior = DEVICE_DT_NAME(DT_DRV_INST(0)),
    .all = LANTERN_EFFECT_ALL,
    .arg_max = LANTERN_EFFECT_COLOUR_MAX,
    .one = false,
    .changed = changed,
};

bool lantern_effect_get(uint32_t *mask, uint32_t *colour) {
    return lantern_pick_get(&pick, mask, colour);
}

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

SETTINGS_STATIC_HANDLER_DEFINE(lantern_effect, "lantern/effect", NULL, on_set, on_commit, NULL);

#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)

int lantern_effect_send(uint32_t mask, uint32_t colour) {
    return lantern_pick_send(&pick, mask, colour);
}

static int battery_listener(const zmk_event_t *eh) {
    lantern_pick_resend_after(&pick, eh);
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(lantern_effect, battery_listener);
ZMK_SUBSCRIPTION(lantern_effect, zmk_peripheral_battery_state_changed);

#endif /* IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL) */

static int effect_init(void) {
    lantern_pick_init(&pick);
    return 0;
}

SYS_INIT(effect_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
