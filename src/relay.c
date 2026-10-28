/*
 * Copyright (c) 2026 akira-toriyama
 *
 * SPDX-License-Identifier: MIT
 *
 * The relay (src/relay.h): what the split central's code sends the halves,
 * the hit's presses (src/hit.c) and the layer's state (src/layer.c), goes
 * out from one work queue of the module's own, to the halves whose LEDs may
 * be lit. ZMK's split transport carries a behavior invocation to a half by
 * the node's name (src/palette.c says how), and nothing else of the module
 * reaches a half.
 *
 * - Why its own queue: a put into ZMK's run queue waits 100 ms or more when
 *   the queue is full (app/src/split/bluetooth/central.c), and the system
 *   work queue also carries the host's Bluetooth TX in this Zephyr and the
 *   halves' key events (canon t-6ttk's research): a wait there would hold
 *   key processing. One queue for both users, so what they send goes out in
 *   the order it was queued, and a burst of one waits behind the other only
 *   for the run queue's drain, a GATT write a command.
 * - To whom (lantern_relay_lit()): a half whose link runs at peripheral
 *   latency 0. A half asks for that while USB powers it (src/latency.c,
 *   CONFIG_LANTERN_USB_LATENCY), the only time its LEDs are lit past the
 *   boot light; a half on battery keeps the central's latency (ZMK's 30) and
 *   is dark, and writes to it would each wait up to 31 connection events in
 *   the controller, a run of them drying the TX pool both links share, on
 *   which the run queue's drain then blocks with the lit half's writes
 *   behind. The latency is read from each link (bt_conn_get_info()) at each
 *   call, and a link's slot from ZMK's peripheral_slot_index_for_conn()
 *   (app/src/split/bluetooth/central.c; it has no header, so it is declared
 *   here: a rename or a static in ZMK fails the link, loudly, where a
 *   changed signature would not, C linking by name alone). A half just
 *   connected shows the central's latency until Zephyr sends its held
 *   request (the connection's first 5 s), and is not lit until then.
 * - The stack: the deepest path, a user's send down to the run queue's put,
 *   was about 0.9 KB in canon t-6ttk's trial of the hit's relay (Opus review
 *   2026-10-20, from the disassembly); the layer's walk of the keymap
 *   (src/layer.c) runs above that put, not under it.
 */

#include <errno.h>
#include <stdint.h>

#include <zephyr/bluetooth/conn.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include <zmk/split/central.h>

#include "relay.h"

BUILD_ASSERT(ZMK_SPLIT_CENTRAL_PERIPHERAL_COUNT <= 32, "lantern_relay_lit() keeps a slot a bit");

#define STACK_SIZE 2048
/* The relay has nothing to beat: the lowest preemptible priority, which a
 * central without key LEDs may lack ZMK's low priority thread's symbol for. */
#define PRIORITY K_LOWEST_APPLICATION_THREAD_PRIO

/* Both have external linkage (Zephyr's macros), hence the module's prefix. */
K_THREAD_STACK_DEFINE(lantern_relay_stack, STACK_SIZE);
static struct k_work_q relay_q;

/* ZMK's, app/src/split/bluetooth/central.c: the slot of a half's link, or
 * -EINVAL (the header says why it is declared here). */
int peripheral_slot_index_for_conn(struct bt_conn *conn);

struct k_work_q *lantern_relay_queue(void) { return &relay_q; }

int lantern_relay_slot(struct bt_conn *conn) {
    int slot = peripheral_slot_index_for_conn(conn);

    return slot >= 0 && slot < ZMK_SPLIT_CENTRAL_PERIPHERAL_COUNT ? slot : -EINVAL;
}

static void lit_link(struct bt_conn *conn, void *data) {
    uint32_t *lit = data;
    struct bt_conn_info info;
    int slot = lantern_relay_slot(conn);

    if (slot < 0 || bt_conn_get_info(conn, &info) < 0 || info.state != BT_CONN_STATE_CONNECTED ||
        info.le.latency != 0) {
        return;
    }
    *lit |= BIT(slot);
}

uint32_t lantern_relay_lit(void) {
    uint32_t lit = 0;

    bt_conn_foreach(BT_CONN_TYPE_LE, lit_link, &lit);
    return lit;
}

/* POST_KERNEL, as src/latency.c's queue: started before ZMK's Bluetooth,
 * keymap and keys come up at APPLICATION, so nothing is ever queued onto a
 * queue not yet running. */
static int relay_init(void) {
    static const struct k_work_queue_config config = {.name = "lantern_relay"};

    k_work_queue_start(&relay_q, lantern_relay_stack, K_THREAD_STACK_SIZEOF(lantern_relay_stack),
                       PRIORITY, &config);
    return 0;
}

SYS_INIT(relay_init, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);
