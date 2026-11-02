/*
 * Copyright (c) 2026 akira-toriyama
 *
 * SPDX-License-Identifier: MIT
 *
 * What the split central's sends of the palette (src/palette.c) and of the
 * picks (src/pick.c) share: the event their invocations carry, and the
 * resend to a half that is back, the battery report that asks for it and the
 * wait before it (src/palette.c says why that report and why the wait).
 * Split central only; those two files alone include it.
 */

#pragma once

#include <stddef.h>

#include <zephyr/kernel.h>

#include <zmk/behavior.h>
#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/position_state_changed.h>

/* From a half's battery report to the resend to it. */
#define LANTERN_RESEND_DELAY_MS 1000

/* The event of an invocation the central's own code makes, a send or a
 * resend: a key of the central's own, at position 0. */
static inline struct zmk_behavior_binding_event lantern_local_event(void) {
    return (struct zmk_behavior_binding_event){
        .layer = 0,
        .position = 0,
        .timestamp = k_uptime_get(),
        .source = ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL,
    };
}

/* The half a resend goes to after the event eh: the slot of a half's battery
 * report that carries a level (ZMK reports 0 at a disconnection), below
 * count, the caller's resends; -1 for any other report or event. */
static inline int lantern_resend_slot(const zmk_event_t *eh, size_t count) {
    const struct zmk_peripheral_battery_state_changed *ev =
        as_zmk_peripheral_battery_state_changed(eh);

    if (ev == NULL || ev->source >= count || ev->state_of_charge == 0) {
        return -1;
    }
    return ev->source;
}
