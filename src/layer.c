/*
 * Copyright (c) 2026 akira-toriyama
 *
 * SPDX-License-Identifier: MIT
 *
 * The layer: which keys the keymap's layer of the moment binds, carried from
 * the split central to the halves for the layer keys
 * (src/effects/layer_keys.c, canon t-6ttk): while a layer is held, the keys
 * it gives a meaning of its own light in its colour. ZMK's halves know
 * nothing of the keymap or its layers: both live on the central, where the
 * keymap runs, and nothing comes back to a half but what a behavior sends it
 * (src/hit.c says the same of the presses).
 *
 * - One behavior, lantern,behavior-layer, the node named layer on every
 *   device of the split (the consumer's keymap or an overlay, as the hit's):
 *   the central sends it to a half with zmk_split_central_invoke_behavior(),
 *   one invocation a chunk of the state, press only, from the relay's work
 *   queue (src/relay.c); ZMK's split transport carries it by the node's name
 *   (src/palette.c says how, and why the name is at most 8 characters). On a
 *   half the handler, on the Bluetooth receive work queue, hands each chunk
 *   to the layer keys (lantern_layer_keys_set()). Nothing binds it to a key.
 * - The state is absolute (src/layer.h, the wire format): the key positions
 *   shown, a bit each, and the palette slot of their colour; no key is no
 *   layer shown. It goes in chunks of 61 key positions, the chunk's number in
 *   the invocation's event position (ZMK's split transport carries it as a
 *   byte) and the last chunk marked there, all of a state's chunks in order
 *   to a half, which takes the state whole at the marked one: one invocation
 *   for a keymap of up to 61 key positions (canon's 58), two up to 122. The
 *   mark lets a half whose keymap counts other chunks than the central's (the
 *   module's check build: the stock dongle's 82 key positions, its halves'
 *   76) take the states whole all the same, keeping the chunks its keymap
 *   holds. A half holds no table of layers: the central computes everything,
 *   and the halves' images need not change with the property below.
 * - Which layer shows: the node's layers property, pairs of a layer and a
 *   palette slot (the consumer's; canon's config/imprint_behaviors.dtsi),
 *   names the layers that show and the colour of each; only the central
 *   reads it. The highest active layer it names shows, an active layer it
 *   does not name is passed over (canon's arrow layers, which an arrow key's
 *   hold-tap raises for the length of a mere tap: canon t-6ttk's research,
 *   2.4), and with none of them active nothing shows. The walk goes down to
 *   the default layer, as ZMK's own resolution does, which shows only where
 *   the property names it. A layer is ZMK's layer id, its index in the keymap
 *   at build time (canon: config/layers.h), walked by index from the top, so
 *   a keymap reordered at run time (ZMK Studio) keeps each named layer's
 *   colour and shows the one on top.
 * - Which keys: those whose binding on the shown layer is neither a &none
 *   nor a &trans (every zmk,behavior-none and zmk,behavior-transparent node)
 *   and, on a layer other than the default, not the default layer's at the
 *   same position (its behavior and both parameters): the keys the layer
 *   gives a meaning of its own, where a layer that repeats the default
 *   layer's thumbs (canon's FOOTER_TD) would light them too. A &trans that
 *   falls through to another held layer counts as nothing (canon has none).
 *   Read through ZMK's zmk_keymap_get_layer_binding_at_idx() at each state,
 *   so a binding ZMK Studio changes is the one read.
 * - When: a layer coming into view shows SETTLE_MS after the change that
 *   brought it, each change meanwhile restarting the wait, so a layer that a
 *   hold-tap or a macro raises for a short tap only does not flash (canon
 *   t-6ttk's research: 60 ms); a change that leaves no layer in view clears
 *   at once. Nothing cuts a wait short: a run of the work that finds a wait
 *   pending (a change came after the run was queued: Zephyr's reschedule
 *   leaves a queued run queued, kernel/work.c) sends nothing and leaves the
 *   state to the wait's own run, and a half that comes to latency 0 meanwhile
 *   is sent the state then (Opus review 2026-10-28). The listener runs on the
 *   system work queue, where ZMK raises zmk_layer_state_changed (for a real
 *   change only, keymap.c's set_layer_state()), and only schedules the
 *   relay's work, which reads the layer state and the keymap when it runs,
 *   and sends. The state is computed once a second after boot too, for a
 *   central's own LEDs when the property names the default layer.
 * - To whom: the halves the relay calls lit (src/relay.c), each sent the
 *   state when it differs from the last it was sent. A half that was not
 *   lit meanwhile (on battery, away) is sent the state of the moment when
 *   its link comes to peripheral latency 0, the sign that USB power arrived
 *   (Zephyr's le_param_updated, raised on the central for a change the half
 *   asked for too, src/latency.c). A half drops its state when its link to
 *   the central goes and when its USB power goes, the moment the central
 *   stops sending to it, and takes none while unpowered (one still on its way
 *   then is the past's): a cable plugged back in shows no layer until the
 *   central's state at latency 0 arrives, under the boot light the plug-in
 *   opens, and none at all where the half never comes to latency 0
 *   (src/latency.c gives up after refused requests on USB). Nothing persists,
 *   since the state is a held key's, not a setting, and no battery report
 *   resends it (the run queue is sized for the palette's and the picks'
 *   resends, src/pick.c).
 * - Threads on a half: the handler on the Bluetooth receive work queue, the
 *   link's end and the USB event on the system work queue (Zephyr's conn.c
 *   reports a disconnection from its deferred work there), all cooperative,
 *   so their stores never interleave (src/effects/layer_keys.c).
 * - The run queue is shared with the palette, the picks and the hits
 *   (src/hit.c says what a vanished half costs): a state is a chunk's
 *   invocation a lit half a change. Delivery is not confirmed, as the
 *   palette's: a state lost on its way, or a chunk of it, shows until the
 *   layer's next change, or until the half's link next comes to latency 0,
 *   and a half on an image without the node logs an error a chunk and draws
 *   nothing.
 * - A central with key LEDs of its own (a stock half as the central) hands
 *   each state to its own layer keys too.
 */

#define DT_DRV_COMPAT lantern_behavior_layer

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/bluetooth/conn.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

#include <drivers/behavior.h>
#include <zmk/behavior.h>

#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
#include <zmk/event_manager.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/keymap.h>
#include <zmk/matrix.h>
#include <zmk/split/central.h>

#include "relay.h"
#elif IS_ENABLED(CONFIG_LANTERN_LAYER_KEYS)
#include <zmk/event_manager.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/usb.h>
#endif

#include "layer.h"
#include "palette.h"

LOG_MODULE_REGISTER(lantern_layer, LOG_LEVEL_INF);

BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) == 1,
             "CONFIG_LANTERN_LAYER needs exactly one lantern,behavior-layer node");

#define NODE DT_DRV_INST(0)
#define BEHAVIOR_NAME DEVICE_DT_NAME(NODE)

/* A chunk of a state, on a half (the Bluetooth receive work queue): its
 * number and the last mark in the event's position (the header). */
static int on_binding_pressed(struct zmk_behavior_binding *binding,
                              struct zmk_behavior_binding_event event) {
    if (event.position > UINT8_MAX) {
        LOG_WRN("no chunk position %u", event.position);
        return -EINVAL;
    }
    if (lantern_layer_slot(binding->param2) >= LANTERN_PALETTE_SLOTS) {
        LOG_WRN("no palette slot %u", lantern_layer_slot(binding->param2));
        return -EINVAL;
    }
#if IS_ENABLED(CONFIG_LANTERN_LAYER_KEYS)
#if !IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    if (!zmk_usb_is_powered()) {
        return ZMK_BEHAVIOR_OPAQUE; /* the past's (the header) */
    }
#endif
    lantern_layer_keys_set(event.position & LANTERN_LAYER_CHUNK_MASK,
                           (event.position & LANTERN_LAYER_LAST) != 0, binding->param1,
                           binding->param2);
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

#if IS_ENABLED(CONFIG_LANTERN_LAYER_KEYS) && !IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)

/* The half's link to the central went (the system work queue: Zephyr's
 * deferred disconnect work): its state goes with it (the header). A split
 * peripheral's one link is the central's. */
static void disconnected(struct bt_conn *conn, uint8_t reason) {
    ARG_UNUSED(conn);
    ARG_UNUSED(reason);
    lantern_layer_keys_clear();
}

BT_CONN_CB_DEFINE(lantern_layer_link) = {
    .disconnected = disconnected,
};

/* USB power came or went (the system work queue, where ZMK raises it): gone,
 * the state goes too (the header). */
static int usb_listener(const zmk_event_t *eh) {
    if (as_zmk_usb_conn_state_changed(eh) != NULL && !zmk_usb_is_powered()) {
        lantern_layer_keys_clear();
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(lantern_layer, usb_listener);
ZMK_SUBSCRIPTION(lantern_layer, zmk_usb_conn_state_changed);

#endif

#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)

/* How long a layer must stay in view before it shows (the header). */
#define SETTLE_MS 60

/* The node's layers property (the header): a layer and a palette slot, pair
 * by pair. */
BUILD_ASSERT(DT_NODE_HAS_PROP(NODE, layers),
             "lantern,behavior-layer: the split central's node needs layers, the layers to show");
BUILD_ASSERT(DT_PROP_LEN_OR(NODE, layers, 0) % 2 == 0,
             "lantern,behavior-layer: layers pairs each layer with a palette slot");
#define PAIR_IN_RANGE(node_id, prop, idx)                                                          \
    BUILD_ASSERT(DT_PROP_BY_IDX(node_id, prop, idx) <                                              \
                     ((idx) % 2 == 0 ? ZMK_KEYMAP_LAYERS_LEN : LANTERN_PALETTE_SLOTS),             \
                 "lantern,behavior-layer: a layers pair is a layer of the keymap and a palette "   \
                 "slot (0 to 5)");
COND_CODE_1(DT_NODE_HAS_PROP(NODE, layers), (DT_FOREACH_PROP_ELEM(NODE, layers, PAIR_IN_RANGE)), ())
BUILD_ASSERT(LANTERN_LAYER_CHUNKS <= LANTERN_LAYER_CHUNK_MASK + 1,
             "a chunk's number rides in the event position's low bits (src/layer.h)");

static const uint8_t shown[] =
    COND_CODE_1(DT_NODE_HAS_PROP(NODE, layers), (DT_PROP(NODE, layers)), ({0, 0}));

/* The behaviors that give a key nothing of its own (the header): every &none
 * and &trans node, by the device name a binding carries. */
#define INERT_NAME(node_id) DEVICE_DT_NAME(node_id),
static const char *const inert[] = {
    DT_FOREACH_STATUS_OKAY(zmk_behavior_none, INERT_NAME)
        DT_FOREACH_STATUS_OKAY(zmk_behavior_transparent, INERT_NAME) NULL,
};

static bool inert_binding(const struct zmk_behavior_binding *b) {
    if (b == NULL || b->behavior_dev == NULL) {
        return true;
    }
    for (size_t i = 0; inert[i] != NULL; i++) {
        if (strcmp(b->behavior_dev, inert[i]) == 0) {
            return true;
        }
    }
    return false;
}

static bool same_binding(const struct zmk_behavior_binding *a,
                         const struct zmk_behavior_binding *b) {
    return a != NULL && b != NULL && a->behavior_dev != NULL && b->behavior_dev != NULL &&
           strcmp(a->behavior_dev, b->behavior_dev) == 0 && a->param1 == b->param1 &&
           a->param2 == b->param2;
}

/* The pair of the layer that shows (the header), its index in shown[], or -1:
 * the highest active layer the property names, down to the default layer.
 * The layer state is a word ZMK writes on the system work queue; the relay's
 * queue reads it as it stands. */
static int shown_pair(void) {
    zmk_keymap_layer_id_t base = zmk_keymap_layer_default();

    for (int index = ZMK_KEYMAP_LAYERS_LEN - 1; index >= 0; index--) {
        zmk_keymap_layer_id_t layer = zmk_keymap_layer_index_to_id((zmk_keymap_layer_index_t)index);

        if (layer == ZMK_KEYMAP_LAYER_ID_INVAL) {
            continue;
        }
        if (zmk_keymap_layer_active(layer)) {
            for (size_t p = 0; p < ARRAY_SIZE(shown); p += 2) {
                if (shown[p] == layer) {
                    return (int)p;
                }
            }
        }
        if (layer == base) {
            break;
        }
    }
    return -1;
}

/* A state (src/layer.h): each chunk's two parameters, chunk c's at 2c and
 * 2c + 1. */
struct state {
    uint32_t w[LANTERN_LAYER_WORDS];
};

/* The relay's queue: the state of the moment (the header), the keys of the
 * layer that shows and its slot in every chunk, or zeros. */
static void state_now(struct state *s) {
    int p = shown_pair();

    memset(s, 0, sizeof(*s));
    if (p < 0) {
        return;
    }
    zmk_keymap_layer_id_t layer = shown[p], base = zmk_keymap_layer_default();
    bool any = false;

    for (uint16_t k = 0; k < ZMK_KEYMAP_LEN; k++) {
        const struct zmk_behavior_binding *b = zmk_keymap_get_layer_binding_at_idx(layer, k);

        if (inert_binding(b) ||
            (layer != base && same_binding(b, zmk_keymap_get_layer_binding_at_idx(base, k)))) {
            continue;
        }
        uint32_t c = k / LANTERN_LAYER_CHUNK_KEYS, off = k % LANTERN_LAYER_CHUNK_KEYS;

        if (off < 32) {
            s->w[2 * c] |= BIT(off);
        } else {
            s->w[2 * c + 1] |= BIT(off - 32);
        }
        any = true;
    }
    if (!any) {
        return; /* nothing of its own: no layer shown */
    }
    for (size_t c = 0; c < LANTERN_LAYER_CHUNKS; c++) {
        s->w[2 * c + 1] |= (uint32_t)shown[p + 1] << LANTERN_LAYER_SLOT_SHIFT;
    }
}

/* The relay's queue only: the state each half was last sent, while it stayed
 * lit. */
static struct {
    struct state state;
    bool valid;
} sent[ZMK_SPLIT_CENTRAL_PERIPHERAL_COUNT];
/* The halves to send the state of the moment whatever they were last sent,
 * a bit a slot: their link came to latency 0 (any thread sets, the relay's
 * queue takes). */
static atomic_t again;

static void send_state(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(state_work, send_state);

/* The relay's queue only: every chunk of the state, in order, the last
 * marked, to half s; 0 or the first error. */
static int send_to(uint8_t s, const struct state *now) {
    for (uint32_t c = 0; c < LANTERN_LAYER_CHUNKS; c++) {
        struct zmk_behavior_binding binding = {
            .behavior_dev = BEHAVIOR_NAME,
            .param1 = now->w[2 * c],
            .param2 = now->w[2 * c + 1],
        };
        struct zmk_behavior_binding_event event = {
            .layer = 0,
            .position = c | (c == LANTERN_LAYER_CHUNKS - 1 ? LANTERN_LAYER_LAST : 0),
            .timestamp = k_uptime_get(),
            .source = ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL,
        };
        int err = zmk_split_central_invoke_behavior(s, &binding, event, true);

        if (err < 0) {
            return err;
        }
    }
    return 0;
}

/* The relay's queue only: the state of the moment to every lit half that
 * needs it (the header). A wait pending leaves it to the wait's run. */
static void send_state(struct k_work *work) {
    ARG_UNUSED(work);
    if (k_work_delayable_busy_get(&state_work) & K_WORK_DELAYED) {
        return;
    }
    struct state now;
    uint32_t lit = lantern_relay_lit();
    uint32_t force = (uint32_t)atomic_clear(&again);

    state_now(&now);
    for (uint8_t s = 0; s < ZMK_SPLIT_CENTRAL_PERIPHERAL_COUNT; s++) {
        if (!(lit & BIT(s))) {
            sent[s].valid = false;
            continue;
        }
        if (!(force & BIT(s)) && sent[s].valid &&
            memcmp(&sent[s].state, &now, sizeof(now)) == 0) {
            continue;
        }
        int err = send_to(s, &now);

        if (err < 0) {
            LOG_WRN("layer to half %u not sent: %d", s, err);
        }
        sent[s].state = now;
        sent[s].valid = err >= 0;
    }
#if IS_ENABLED(CONFIG_LANTERN_LAYER_KEYS)
    /* this central's own key LEDs (the header) */
    for (uint32_t c = 0; c < LANTERN_LAYER_CHUNKS; c++) {
        lantern_layer_keys_set(c, c == LANTERN_LAYER_CHUNKS - 1, now.w[2 * c], now.w[2 * c + 1]);
    }
#endif
}

/* System work queue, where ZMK raises the layers' changes (the header): a
 * layer in view waits SETTLE_MS from this change; none in view goes at once,
 * the wait cancelled. */
static int layer_listener(const zmk_event_t *eh) {
    if (as_zmk_layer_state_changed(eh) == NULL) {
        return ZMK_EV_EVENT_BUBBLE;
    }
    k_work_reschedule_for_queue(lantern_relay_queue(), &state_work,
                                shown_pair() < 0 ? K_NO_WAIT : K_MSEC(SETTLE_MS));
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(lantern_layer, layer_listener);
ZMK_SUBSCRIPTION(lantern_layer, zmk_layer_state_changed);

/* A link's parameters changed (the Bluetooth receive work queue): a half at
 * latency 0 is lit now, and is sent the state of the moment, at once or at a
 * pending wait's end (the header). */
static void param_updated(struct bt_conn *conn, uint16_t interval, uint16_t latency,
                          uint16_t timeout) {
    ARG_UNUSED(interval);
    ARG_UNUSED(timeout);
    int slot = lantern_relay_slot(conn);

    if (latency != 0 || slot < 0) {
        return;
    }
    atomic_or(&again, (atomic_val_t)BIT(slot));
    k_work_schedule_for_queue(lantern_relay_queue(), &state_work, K_NO_WAIT);
}

BT_CONN_CB_DEFINE(lantern_layer_links) = {
    .le_param_updated = param_updated,
};

/* The state once at boot (the header), BOOT_MS on, past every init: ZMK
 * fills the keymap's position map at APPLICATION too
 * (zmk_physical_layouts_init()), and an init that blocks before it, as
 * Bluetooth's does on the controller, would let the relay's queue run first. */
#define BOOT_MS 1000

static int layer_init(void) {
    k_work_schedule_for_queue(lantern_relay_queue(), &state_work, K_MSEC(BOOT_MS));
    return 0;
}

SYS_INIT(layer_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

#endif /* IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL) */
