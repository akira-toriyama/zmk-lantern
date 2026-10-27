/*
 * Copyright (c) 2026 akira-toriyama
 *
 * SPDX-License-Identifier: MIT
 *
 * The next effect from a key (canon t-rgp5): the behavior
 * lantern,behavior-effect-next, bound to a key of the central's keymap
 * (`&effect_next`), picks on press the effect after the stored pick in
 * LANTERN_EFFECT_LIST's order (src/effect.h, lantern_effect_next()), over the
 * effects this central builds (a stock half as the central) or the whole table
 * where it builds none (a dongle, which cannot know what the halves build),
 * round again after the last, and keeps the pick's colour. The pick itself is
 * the effect behavior's (lantern_effect_send(), src/effect.c: the central
 * stores it and sends it to every half, as the host's pick), so this file
 * neither stores nor sends anything of its own. For a keyboard without a host,
 * beside `&effect <mask> <colour>` on a key for a fixed pick; canon's keymap
 * binds neither (the host picks). Runs on the central only
 * (BEHAVIOR_LOCALITY_CENTRAL, where the keymap runs, so ZMK never carries it
 * over the split: the node's name is free of the transport's 8 characters,
 * and a half builds nothing of it); nothing on release. With no pick stored
 * the halves draw LANTERN_EFFECT_DEFAULT in CONFIG_LANTERN_EFFECT_COLOUR
 * (src/keys.c), so the first press picks the effect after the default where
 * this central builds effects, and keeps that colour, read from this
 * central's own conf (a dongle builds no effects and has no default: the
 * table's first; its conf sets the colour to the halves', since it cannot
 * read theirs). Before the settings have loaded a press is refused (-EAGAIN,
 * as the port's line), and a stored colour the send refuses (-EINVAL: none,
 * src/pick.c keeps it in range) too; ZMK logs a refusal at debug level only,
 * so this file warns.
 */

#define DT_DRV_COMPAT lantern_behavior_effect_next

#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <drivers/behavior.h>
#include <zmk/behavior.h>

#include "effect.h"

LOG_MODULE_REGISTER(lantern_effect_next, LOG_LEVEL_INF);

BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) == 1,
             "CONFIG_LANTERN_EFFECT_NEXT needs exactly one lantern,behavior-effect-next node");

/* The cycle: what this device builds, or the table (the header). */
#define CYCLE ((uint32_t)LANTERN_EFFECT_BUILT != 0 ? (uint32_t)LANTERN_EFFECT_BUILT : (uint32_t)LANTERN_EFFECT_ALL)
/* No pick stored (the header): the default drawn, 0 where none is built, and
 * the conf's colour. */
#define START ((uint32_t)LANTERN_EFFECT_DEFAULT)
#define START_COLOUR ((uint32_t)CONFIG_LANTERN_EFFECT_COLOUR)

static int on_binding_pressed(struct zmk_behavior_binding *binding,
                              struct zmk_behavior_binding_event event) {
    ARG_UNUSED(binding);
    ARG_UNUSED(event);
    uint32_t mask = START, colour = START_COLOUR;

    (void)lantern_effect_get(&mask, &colour);
    int rc = lantern_effect_send(lantern_effect_next(mask, CYCLE), colour);
    if (rc < 0) {
        LOG_WRN("not sent: %d", rc);
        return rc;
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
    .locality = BEHAVIOR_LOCALITY_CENTRAL,
    .binding_pressed = on_binding_pressed,
    .binding_released = on_binding_released,
};

BEHAVIOR_DT_INST_DEFINE(0, NULL, NULL, NULL, NULL, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,
                        &api);
