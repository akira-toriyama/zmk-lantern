/*
 * Copyright (c) 2026 akira-toriyama
 *
 * SPDX-License-Identifier: MIT
 *
 * Peripheral latency 0 on the link to the split central while USB powers
 * this half, and the link's own latency again on battery (canon t-6ttk).
 *
 * - Why: ZMK's central opens the link at CONFIG_ZMK_SPLIT_BLE_PREF_LATENCY
 *   (30) and never changes it, and Zephyr's link layer lets a half with
 *   nothing to send skip that many connection events, so what the central
 *   sends a waiting half (a relayed key press, a palette) waits up to 31
 *   intervals, 232.5 ms at 7.5 ms. On USB the half's LEDs are lit, and
 *   listening at every event costs the battery nothing; on battery the LEDs
 *   are dark and the half keeps the central's latency. canon t-6ttk compared
 *   the two by eye on hardware (2026-10-20, the central setting both links):
 *   at 30 a relayed blink came a little late, at 0 at once.
 * - How: the half asks with bt_conn_le_param_update(), the interval and the
 *   supervision timeout left as the link has them. Both ends' controllers do
 *   the link layer's connection parameter request (BT_CTLR_CONN_PARAM_REQ),
 *   so Zephyr sends an LE Connection Update, not the L2CAP request. The
 *   central's host accepts a valid request unless an le_param_req callback
 *   refuses it, and neither ZMK nor canon's modules register one. The
 *   battery latency is the one the link had at connection, the central's
 *   (the PREF_* symbols exist on a central only), lowered if the link's
 *   interval and timeout have since come to forbid it.
 * - The host's hold: Zephyr keeps a peripheral's request until
 *   CONFIG_BT_CONN_PARAM_UPDATE_TIMEOUT (5 s) after the connection and then
 *   sends the latest one from its own deferred work on the system work queue
 *   (subsys/bluetooth/host/conn.c); later requests go out at once. A request
 *   held for the old power state would still go out, so the last value asked
 *   is remembered and asked again over it when the power changes, even while
 *   the link already shows the new value.
 * - Threads: the USB event (system work queue) and the connection callbacks
 *   (the Bluetooth receive work queue) only note their input and schedule
 *   step() on this file's own work queue: a request that is not held is a
 *   synchronous HCI command (bt_conn_le_conn_update()), which this file keeps
 *   off the system work queue, where ZMK processes key events. step() asks
 *   when the link's latency or the last value asked differs from the wanted
 *   one, and looks again CHECK_MS later (past the hold), RETRY_MS after an
 *   error, or RETRY_MS after the link's parameters change. A refused request
 *   reports the unchanged parameters (hci_core.c), which schedule nothing.
 * - Giving up: on USB, after MAX_ASKS requests without an agreement, until
 *   the next power change or connection; latency 0 is a nicety. On battery
 *   never: after MAX_ASKS the half asks every BACKOFF_MS, since a half left
 *   at latency 0 on battery listens at every event (the dongle's link layer
 *   runs one parameter procedure at a time across its links and refuses an
 *   overlapping one, Opus review 2026-10-20).
 */

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/bluetooth/conn.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

#include <zmk/event_manager.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/usb.h>

LOG_MODULE_REGISTER(lantern_latency, LOG_LEVEL_INF);

/* The deepest path, step() down to the HCI command, is about 0.7 KB (Opus
 * review 2026-10-20, from the disassembly; deferred logging adds 100-200 B). */
#define STACK_SIZE 1536
#define CHECK_MS 7000
#define RETRY_MS 1000
#define BACKOFF_MS 60000
#define MAX_ASKS 5
/* The most peripheral latency a link may have (BT Core, Vol 6, Part B, 4.5.2). */
#define MOST_LATENCY 499

BUILD_ASSERT(CHECK_MS > CONFIG_BT_CONN_PARAM_UPDATE_TIMEOUT,
             "look again only after the host has sent a held request");

K_THREAD_STACK_DEFINE(latency_stack, STACK_SIZE);
static struct k_work_q latency_q;

static atomic_t usb_powered;
/* The link's latency at connection; -1 before the first. */
static atomic_t battery_latency = ATOMIC_INIT(-1);
/* The last latency asked on this connection; -1 for none. */
static atomic_t asked = ATOMIC_INIT(-1);
/* Requests since the last agreement, power change or connection. */
static atomic_t asks;

/* The Bluetooth receive work queue only: the link's parameters as last
 * reported, so that a refused request's report changes nothing. */
static struct {
    uint16_t interval, latency, timeout;
} seen;

static void find_link(struct bt_conn *conn, void *data) {
    struct bt_conn **link = data;
    struct bt_conn_info info;

    if (*link == NULL && bt_conn_get_info(conn, &info) == 0 &&
        info.role == BT_CONN_ROLE_PERIPHERAL && info.state == BT_CONN_STATE_CONNECTED) {
        *link = bt_conn_ref(conn);
    }
}

/* The battery latency the link's interval and timeout allow now: Zephyr's
 * bt_le_conn_params_valid() wants (1 + latency) x interval below 4 x timeout. */
static uint16_t battery(atomic_val_t base, const struct bt_conn_info *info) {
    uint32_t q = (4U * info->le.timeout - 1U) / info->le.interval;
    uint32_t most = MIN(q > 0 ? q - 1U : 0U, (uint32_t)MOST_LATENCY);

    return (uint16_t)MIN((uint32_t)base, most);
}

static void step(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(step_work, step);

static void later(int32_t ms) {
    k_work_reschedule_for_queue(&latency_q, &step_work, K_MSEC(ms));
}

static void ask(struct bt_conn *link, const struct bt_conn_info *info, uint16_t want) {
    atomic_val_t before = atomic_inc(&asks);

    if (want == 0 && before >= MAX_ASKS) {
        if (before == MAX_ASKS) {
            LOG_WRN("latency 0 not reached in %d requests; waiting for a change", MAX_ASKS);
        }
        return;
    }
    int rc = bt_conn_le_param_update(
        link, BT_LE_CONN_PARAM(info->le.interval, info->le.interval, want, info->le.timeout));
    if (rc < 0) {
        LOG_WRN("latency %u not asked: %d", want, rc);
    } else {
        atomic_set(&asked, want);
        LOG_INF("asked for latency %u", want);
    }
    if (before + 1 >= MAX_ASKS) {
        later(BACKOFF_MS);
    } else {
        later(rc < 0 ? RETRY_MS : CHECK_MS);
    }
}

static void step(struct k_work *work) {
    ARG_UNUSED(work);
    struct bt_conn *link = NULL;
    struct bt_conn_info info;

    bt_conn_foreach(BT_CONN_TYPE_LE, find_link, &link);
    if (link == NULL) {
        return; /* connected() schedules the next step */
    }
    atomic_val_t base = atomic_get(&battery_latency);
    if (base >= 0 && bt_conn_get_info(link, &info) == 0) {
        uint16_t want = atomic_get(&usb_powered) ? 0 : battery(base, &info);
        atomic_val_t last = atomic_get(&asked);
        if (info.le.latency == want && (last < 0 || last == want)) {
            atomic_set(&asks, 0);
        } else {
            ask(link, &info, want);
        }
    }
    bt_conn_unref(link);
}

static void connected(struct bt_conn *conn, uint8_t err) {
    struct bt_conn_info info;

    if (err != 0 || bt_conn_get_info(conn, &info) < 0 || info.role != BT_CONN_ROLE_PERIPHERAL) {
        return;
    }
    seen.interval = info.le.interval;
    seen.latency = info.le.latency;
    seen.timeout = info.le.timeout;
    atomic_set(&battery_latency, info.le.latency);
    atomic_set(&asked, -1);
    atomic_set(&asks, 0);
    later(0);
}

static void param_updated(struct bt_conn *conn, uint16_t interval, uint16_t latency,
                          uint16_t timeout) {
    struct bt_conn_info info;

    if (bt_conn_get_info(conn, &info) < 0 || info.role != BT_CONN_ROLE_PERIPHERAL ||
        (interval == seen.interval && latency == seen.latency && timeout == seen.timeout)) {
        return;
    }
    seen.interval = interval;
    seen.latency = latency;
    seen.timeout = timeout;
    LOG_INF("link: interval %u, latency %u, timeout %u", interval, latency, timeout);
    later(RETRY_MS);
}

BT_CONN_CB_DEFINE(lantern_latency_conn) = {
    .connected = connected,
    .le_param_updated = param_updated,
};

static int usb_listener(const zmk_event_t *eh) {
    if (as_zmk_usb_conn_state_changed(eh) == NULL) {
        return ZMK_EV_EVENT_BUBBLE;
    }
    bool powered = zmk_usb_is_powered();
    if ((bool)atomic_set(&usb_powered, powered) != powered) {
        atomic_set(&asks, 0);
        later(0);
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(lantern_latency, usb_listener);
ZMK_SUBSCRIPTION(lantern_latency, zmk_usb_conn_state_changed);

/* POST_KERNEL: the queue runs before ZMK's USB and Bluetooth come up at
 * APPLICATION, so no input is ever scheduled onto a queue not yet started. */
static int latency_init(void) {
    static const struct k_work_queue_config config = {.name = "lantern_latency"};

    k_work_queue_start(&latency_q, latency_stack, K_THREAD_STACK_SIZEOF(latency_stack),
                       CONFIG_ZMK_LOW_PRIORITY_THREAD_PRIORITY, &config);
    return 0;
}

SYS_INIT(latency_init, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);
