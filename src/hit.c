/*
 * Copyright (c) 2026 akira-toriyama
 *
 * SPDX-License-Identifier: MIT
 *
 * The hit: a key press carried from the device that pressed it to the other
 * halves through the split central, so that an effect can spread from the
 * other half's key too (canon t-6ttk: the crossing ring and the beams,
 * src/effects/). ZMK's
 * halves know nothing of each other's keys: a half sends its key positions
 * to the central, where the keymap runs, and nothing comes back to a half
 * but what a behavior sends it.
 *
 * - One behavior, lantern,behavior-hit, the node named hit on every device of
 *   the split (the consumer's keymap or an overlay, as the palette's node):
 *   the central sends it to a half with zmk_split_central_invoke_behavior(),
 *   one invocation a press, param1 = the key position (the keymap's, global
 *   across the split), param2 = 0, press only; ZMK's split transport carries
 *   it by the node's name (src/palette.c says how, and why the name is at
 *   most 8 characters). On the half the handler, on the Bluetooth receive
 *   work queue, hands the position to src/keys.c (lantern_keys_hit()), which
 *   notes it as a hit like the half's own press while the LEDs are lit and
 *   never moves the countdown's clock: the other half's keys are not this
 *   half's activity (a stock half as the central counts them through its
 *   own listener, as ZMK's idle does there). The central never invokes the
 *   behavior through zmk_behavior_invoke_binding(), and nothing binds it to
 *   a key.
 * - The central's side (CONFIG_ZMK_SPLIT_ROLE_CENTRAL): a listener on
 *   zmk_position_state_changed takes every press, a half's (source = its
 *   slot) or the central's own (a stock half as the central), and relays it
 *   to every other half. It runs on the system work queue, where ZMK raises
 *   the events, and only drops a repeat (a combo that captured presses raises
 *   the second and later ones again from the first listener, combo.c's
 *   release_pressed_keys(), with the timestamp split/central.c stamped once:
 *   the last SEEN pairs of position and timestamp are remembered, as
 *   zmk-beacon's status broadcaster does; a second press of one key raised
 *   within the same millisecond, in a burst of notifications raised at once,
 *   is dropped with it, one ring the fewer) and queues the press for the
 *   relay's work queue (src/relay.c, which says why not the system work
 *   queue), which sends it. A burst beyond PRESSES_WAITING drops presses,
 *   never waits.
 * - To whom: every other half the relay calls lit, one whose link runs at
 *   peripheral latency 0, the sign of USB power (lantern_relay_lit(), read
 *   at each press; src/relay.c says why): a half on battery is dark and gets
 *   nothing, and a half just connected gets nothing until Zephyr sends its
 *   held request for latency 0 (the connection's first 5 s).
 * - The hits share ZMK's run queue with the palette and the picks
 *   (CONFIG_ZMK_SPLIT_BLE_CENTRAL_SPLIT_RUN_QUEUE_SIZE, which src/pick.c
 *   sizes for a whole palette and every pick a half). A lit half that vanishes
 *   (unplugged, reflashed) keeps its link, at latency 0, until the
 *   supervision timeout (ZMK's 4 s), and the hits relayed to it meanwhile
 *   can fill the queue once the TX pool is dry: a palette set in that window
 *   waits 100 ms a pair and may lose the oldest commands, the healthy half's;
 *   its next battery report resends it (canon: a toy).
 * - Delivery is not confirmed, as the palette's (src/palette.c): the
 *   transport drops a command for a half still in discovery or when its run
 *   queue overflows, and a half on an image without the node logs an error a
 *   press and draws nothing. A ring not drawn is a ring not drawn.
 * - A central with key LEDs of its own (a stock half as the central) notes a
 *   half's press locally too (lantern_keys_hit()); its own keys reach
 *   src/keys.c's listener as on any half.
 */

#define DT_DRV_COMPAT lantern_behavior_hit

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <drivers/behavior.h>
#include <zmk/behavior.h>

#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/split/central.h>

#include "relay.h"
#endif

#if IS_ENABLED(CONFIG_LANTERN_KEYS)
#include "keys.h"
#endif

LOG_MODULE_REGISTER(lantern_hit, LOG_LEVEL_INF);

BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) == 1,
             "CONFIG_LANTERN_HIT needs exactly one lantern,behavior-hit node");

#define BEHAVIOR_NAME DEVICE_DT_NAME(DT_DRV_INST(0))

/* A relayed press, on a half (the Bluetooth receive work queue). */
static int on_binding_pressed(struct zmk_behavior_binding *binding,
                              struct zmk_behavior_binding_event event) {
    ARG_UNUSED(event);
    if (binding->param1 > UINT8_MAX) {
        LOG_WRN("no key position %u", binding->param1);
        return -EINVAL;
    }
#if IS_ENABLED(CONFIG_LANTERN_KEYS)
    lantern_keys_hit((uint8_t)binding->param1);
#endif
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

#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)

#define PRESSES_WAITING 8
#define SEEN 8

struct press {
    uint8_t from; /* the slot of the half that pressed, or ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL */
    uint8_t position;
};

/* External linkage (Zephyr's macro), hence the module's prefix. */
K_MSGQ_DEFINE(lantern_hit_presses, sizeof(struct press), PRESSES_WAITING, 1);

/* The relay's queue only: each queued press to every other half the relay
 * calls lit (the header). */
static void relay(struct k_work *work) {
    ARG_UNUSED(work);
    struct press p;

    while (k_msgq_get(&lantern_hit_presses, &p, K_NO_WAIT) == 0) {
        uint32_t lit = lantern_relay_lit();

        for (uint8_t s = 0; s < ZMK_SPLIT_CENTRAL_PERIPHERAL_COUNT; s++) {
            if (s == p.from || !(lit & BIT(s))) {
                continue;
            }
            struct zmk_behavior_binding binding = {
                .behavior_dev = BEHAVIOR_NAME,
                .param1 = p.position,
                .param2 = 0,
            };
            struct zmk_behavior_binding_event event = {
                .layer = 0,
                .position = p.position,
                .timestamp = k_uptime_get(),
                .source = ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL,
            };
            int err = zmk_split_central_invoke_behavior(s, &binding, event, true);
            if (err < 0) {
                LOG_WRN("hit to half %u not sent: %d", s, err);
            }
        }
    }
}

static K_WORK_DEFINE(relay_work, relay);

struct seen_press {
    uint32_t position;
    int64_t timestamp;
};

/* System work queue only. */
static struct seen_press seen[SEEN];
static size_t seen_next;

/* True for a press already taken (the header); a new one is remembered. */
static bool seen_before(const struct zmk_position_state_changed *ev) {
    for (size_t i = 0; i < SEEN; i++) {
        if (seen[i].position == ev->position && seen[i].timestamp == ev->timestamp) {
            return true;
        }
    }
    seen[seen_next] = (struct seen_press){.position = ev->position, .timestamp = ev->timestamp};
    seen_next = (seen_next + 1) % SEEN;
    return false;
}

static int press_listener(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *ev = as_zmk_position_state_changed(eh);

    if (ev == NULL || !ev->state || ev->position > UINT8_MAX || seen_before(ev)) {
        return ZMK_EV_EVENT_BUBBLE;
    }
    struct press p = {.from = ev->source, .position = (uint8_t)ev->position};

    if (k_msgq_put(&lantern_hit_presses, &p, K_NO_WAIT) == 0) {
        k_work_submit_to_queue(lantern_relay_queue(), &relay_work);
    } else {
        LOG_WRN("a press not relayed: %d waiting", PRESSES_WAITING);
    }
#if IS_ENABLED(CONFIG_LANTERN_KEYS)
    /* a half's press, for this central's own LEDs (the header) */
    if (ev->source != ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL) {
        lantern_keys_hit(p.position);
    }
#endif
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(lantern_hit, press_listener);
ZMK_SUBSCRIPTION(lantern_hit, zmk_position_state_changed);

#endif /* IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL) */
