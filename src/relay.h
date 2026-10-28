/*
 * Copyright (c) 2026 akira-toriyama
 *
 * SPDX-License-Identifier: MIT
 *
 * The relay (src/relay.c): the split central's way to the halves for what
 * the module's code sends them, not the keymap: the hit's presses
 * (src/hit.c) and the layer's state (src/layer.c). One work queue of the
 * module's own, and the rule of whom to send to. Layer contract: the relay
 * sends nothing itself; its users queue their work on its queue and ask it
 * which halves are lit, and each sends through ZMK's split transport from
 * there. Split central only (CONFIG_LANTERN_RELAY).
 */

#pragma once

#include <stdint.h>

#include <zephyr/bluetooth/conn.h>
#include <zephyr/kernel.h>

/* The module's own work queue for what the central sends the halves, never
 * the system work queue (src/relay.c says why). */
struct k_work_q *lantern_relay_queue(void);

/* The halves whose link is connected and runs at peripheral latency 0, the
 * sign of USB power: bit s for ZMK's peripheral slot s. Any thread. */
uint32_t lantern_relay_lit(void);

/* The peripheral slot of a half's link, or a negative errno for a link that
 * is no half's. */
int lantern_relay_slot(struct bt_conn *conn);
