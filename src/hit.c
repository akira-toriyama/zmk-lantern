/*
 * Copyright (c) 2026 akira-toriyama
 *
 * SPDX-License-Identifier: MIT
 *
 * The hit: a key press carried from the device that pressed it to the other
 * halves through the split central, so that an effect can spread from the
 * other half's key too (canon t-6ttk: the crossing ring, src/keys.c). ZMK's
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
 *   module's own work queue, which sends it. Why its own queue: a put into
 *   ZMK's run queue waits 100 ms or more when the queue is full
 *   (app/src/split/bluetooth/central.c), and the system work queue also
 *   carries the host's Bluetooth TX in this Zephyr and the halves' key
 *   events (canon t-6ttk's research): a wait there would hold key
 *   processing. A burst beyond PRESSES_WAITING drops presses, never waits.
 * - To whom: a half whose link runs at peripheral latency 0. A half asks for
 *   that while USB powers it (src/latency.c, CONFIG_LANTERN_USB_LATENCY),
 *   the only time its LEDs are lit past the boot light; a half on battery
 *   keeps the central's latency (ZMK's 30) and is dark, and writes to it
 *   would each wait up to 31 connection events in the controller, a run of
 *   them drying the TX pool both links share, on which the run queue's drain
 *   then blocks with the lit half's writes behind. The latency is read from
 *   each link (bt_conn_get_info()) at each press, and a link's slot from
 *   ZMK's peripheral_slot_index_for_conn() (app/src/split/bluetooth/central.c;
 *   it has no header, so it is declared here: a rename or a static in ZMK
 *   fails the link, loudly, where a changed signature would not, C linking
 *   by name alone). A half just connected shows the central's latency until
 *   Zephyr sends its held request (the connection's first 5 s), and gets
 *   nothing until then.
 * - The hits share ZMK's run queue with the palette (CONFIG_ZMK_SPLIT_BLE_CENTRAL_SPLIT_RUN_QUEUE_SIZE,
 *   which src/palette.c sizes for a whole palette). A lit half that vanishes
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
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <drivers/behavior.h>
#include <zmk/behavior.h>

#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
#include <zephyr/bluetooth/conn.h>
#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/split/central.h>
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

/* The deepest path, relay() down to the run queue's put, was about 0.9 KB in
 * canon t-6ttk's trial of this relay (Opus review 2026-10-20, from the
 * disassembly). */
#define STACK_SIZE 2048
#define PRESSES_WAITING 8
#define SEEN 8
/* The relay has nothing to beat: the lowest preemptible priority, which a
 * central without key LEDs may lack ZMK's low priority thread's symbol for. */
#define PRIORITY K_LOWEST_APPLICATION_THREAD_PRIO

/* Both have external linkage (Zephyr's macros), hence the module's prefix. */
K_THREAD_STACK_DEFINE(lantern_hit_stack, STACK_SIZE);
static struct k_work_q hit_q;

struct press {
    uint8_t from; /* the slot of the half that pressed, or ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL */
    uint8_t position;
};

K_MSGQ_DEFINE(lantern_hit_presses, sizeof(struct press), PRESSES_WAITING, 1);

/* ZMK's, app/src/split/bluetooth/central.c: the slot of a half's link, or
 * -EINVAL (the header says why it is declared here). */
int peripheral_slot_index_for_conn(struct bt_conn *conn);

/* Each slot's link latency, -1 without a connected link. */
struct links {
    int16_t latency[ZMK_SPLIT_CENTRAL_PERIPHERAL_COUNT];
};

static void link_latency(struct bt_conn *conn, void *data) {
    struct links *l = data;
    struct bt_conn_info info;
    int slot = peripheral_slot_index_for_conn(conn);

    if (slot < 0 || slot >= ZMK_SPLIT_CENTRAL_PERIPHERAL_COUNT ||
        bt_conn_get_info(conn, &info) < 0 || info.state != BT_CONN_STATE_CONNECTED) {
        return;
    }
    l->latency[slot] = (int16_t)info.le.latency;
}

/* hit_q only: each queued press to every other half whose link is at
 * latency 0 (the header). */
static void relay(struct k_work *work) {
    ARG_UNUSED(work);
    struct press p;

    while (k_msgq_get(&lantern_hit_presses, &p, K_NO_WAIT) == 0) {
        struct links l;

        for (size_t s = 0; s < ZMK_SPLIT_CENTRAL_PERIPHERAL_COUNT; s++) {
            l.latency[s] = -1;
        }
        bt_conn_foreach(BT_CONN_TYPE_LE, link_latency, &l);
        for (uint8_t s = 0; s < ZMK_SPLIT_CENTRAL_PERIPHERAL_COUNT; s++) {
            if (s == p.from || l.latency[s] != 0) {
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
        k_work_submit_to_queue(&hit_q, &relay_work);
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

/* POST_KERNEL, as src/latency.c's queue: started before ZMK's Bluetooth and
 * keys come up at APPLICATION, so no press is ever queued onto a queue not
 * yet running. */
static int hit_init(void) {
    static const struct k_work_queue_config config = {.name = "lantern_hit"};

    k_work_queue_start(&hit_q, lantern_hit_stack, K_THREAD_STACK_SIZEOF(lantern_hit_stack),
                       PRIORITY, &config);
    return 0;
}

SYS_INIT(hit_init, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);

#endif /* IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL) */
