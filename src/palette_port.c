/*
 * Copyright (c) 2026 akira-toriyama
 *
 * SPDX-License-Identifier: MIT
 *
 * The palette's entry from the host: a text line on any USB CDC ACM port of
 * the split central (canon's Imprint Dongle has one, its board's; canon's
 * scripts/dongle.py palette writes it). Lines, ASCII, ended by LF or CR:
 *
 *   palette RRGGBB [RRGGBB [RRGGBB [RRGGBB]]]   set slots 0.. (the rest stay)
 *     -> palette ok RRGGBB RRGGBB RRGGBB RRGGBB   the four slots as stored
 *     -> palette error: <reason>
 *   palette  or  palette?                       ask
 *     -> palette RRGGBB RRGGBB RRGGBB RRGGBB   or   palette none
 *
 * A colour is exactly six hex digits, either case; the reply writes upper
 * case. A line that does not begin with "palette" is not for this file and
 * gets no reply (the port may carry anything else the host sends); a line
 * longer than LINE_LIMIT is dropped up to its end. "ok" means the palette is
 * stored here and queued for the halves (src/palette.c says when a half gets
 * it), not that a half has it.
 *
 * - Zephyr's interrupt-driven UART API on the CDC ACM device: the legacy class
 *   driver runs the callback on the system work queue and hands over the bytes
 *   its endpoint received (zephyr subsys/usb/device/class/cdc_acm.c), and the
 *   line's handling runs right there: lantern_palette_send() invokes the
 *   behavior, which queues the halves' commands and stores the colours, and
 *   blocks on nothing longer than ZMK's own key handling does. Nothing else in
 *   a canon image reads the port: ZMK's console (CONFIG_UART_CONSOLE, the
 *   board's) and its USB logging write with uart_poll_out() and set no
 *   callback (zephyr drivers/console/uart_console.c sets one only with
 *   CONFIG_CONSOLE_HANDLER), and zmk-beacon's 1200 baud entry is the class
 *   driver's separate rate callback. The reply goes out with uart_poll_out(),
 *   which drops bytes on a full ring and never blocks: with no host reading,
 *   the reply is lost and nothing waits.
 * - Every CDC ACM instance, as zmk-beacon's bootloader entry registers on
 *   every one: a board or a consumer's overlay may add a node, and the host
 *   may open any of the resulting ports.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#include "palette.h"

LOG_MODULE_REGISTER(lantern_palette_port, LOG_LEVEL_INF);

#define WORD "palette"
#define COLOUR_DIGITS 6
/* "palette RRGGBB RRGGBB RRGGBB RRGGBB" is 35 characters. */
#define LINE_LIMIT 48
#define REPLY_MAX 64

struct port {
    const struct device *dev;
    char line[LINE_LIMIT + 1];
    size_t len;
    bool overflow;
};

static void reply(struct port *p, const char *text) {
    for (const char *c = text; *c != '\0'; c++) {
        uart_poll_out(p->dev, (unsigned char)*c);
    }
    uart_poll_out(p->dev, '\n');
}

static void reply_palette(struct port *p, const char *head) {
    uint32_t colours[LANTERN_PALETTE_SLOTS];
    char text[REPLY_MAX];
    if (!lantern_palette_get(colours)) {
        snprintk(text, sizeof(text), "%s none", WORD);
    } else {
        snprintk(text, sizeof(text), "%s%s %06X %06X %06X %06X", WORD, head, colours[0], colours[1],
                 colours[2], colours[3]);
    }
    reply(p, text);
}

static void reply_error(struct port *p, const char *reason) {
    char text[REPLY_MAX];
    snprintk(text, sizeof(text), "%s error: %s", WORD, reason);
    reply(p, text);
}

/* The value of six hex digits, or false. */
static bool parse_colour(const char *s, uint32_t *out) {
    uint32_t v = 0;
    for (size_t i = 0; i < COLOUR_DIGITS; i++) {
        char c = s[i];
        int digit;
        if (c >= '0' && c <= '9') {
            digit = c - '0';
        } else if (c >= 'a' && c <= 'f') {
            digit = 10 + c - 'a';
        } else if (c >= 'A' && c <= 'F') {
            digit = 10 + c - 'A';
        } else {
            return false;
        }
        v = (v << 4) | (uint32_t)digit;
    }
    *out = v;
    return true;
}

static void handle(struct port *p) {
    const char *s = p->line;
    if (strncmp(s, WORD, strlen(WORD)) != 0) {
        return;
    }
    s += strlen(WORD);
    if (*s == '\0' || (*s == '?' && s[1] == '\0')) {
        reply_palette(p, "");
        return;
    }
    if (*s != ' ') {
        return;
    }

    uint32_t colours[LANTERN_PALETTE_SLOTS];
    size_t count = 0;
    while (*s != '\0') {
        if (*s == ' ') {
            s++;
            continue;
        }
        if (count == LANTERN_PALETTE_SLOTS) {
            reply_error(p, "at most four colours");
            return;
        }
        if (!parse_colour(s, &colours[count]) ||
            (s[COLOUR_DIGITS] != '\0' && s[COLOUR_DIGITS] != ' ')) {
            reply_error(p, "a colour is RRGGBB, six hex digits");
            return;
        }
        count++;
        s += COLOUR_DIGITS;
    }
    if (count == 0) {
        reply_error(p, "no colour given");
        return;
    }

    int rc = lantern_palette_send(colours, count);
    if (rc < 0) {
        char reason[32];
        snprintk(reason, sizeof(reason), "not sent (%d)", rc);
        reply_error(p, reason);
        return;
    }
    LOG_INF("%u colour(s) from %s", (unsigned int)count, p->dev->name);
    reply_palette(p, " ok");
}

static void feed(struct port *p, uint8_t c) {
    if (c == '\n' || c == '\r') {
        if (p->overflow) {
            p->overflow = false;
        } else if (p->len > 0) {
            p->line[p->len] = '\0';
            handle(p);
        }
        p->len = 0;
        return;
    }
    if (p->overflow) {
        return;
    }
    if (p->len == LINE_LIMIT) {
        p->overflow = true;
        p->len = 0;
        return;
    }
    p->line[p->len++] = (char)c;
}

static void on_rx(const struct device *dev, void *user_data) {
    struct port *p = user_data;
    uint8_t chunk[16];

    if (uart_irq_update(dev) <= 0) {
        return;
    }
    while (uart_irq_rx_ready(dev) > 0) {
        int n = uart_fifo_read(dev, chunk, sizeof(chunk));
        if (n <= 0) {
            break;
        }
        for (int i = 0; i < n; i++) {
            feed(p, chunk[i]);
        }
    }
}

#define PORT_OF(node_id) {.dev = DEVICE_DT_GET(node_id)},

static struct port ports[] = {DT_FOREACH_STATUS_OKAY(zephyr_cdc_acm_uart, PORT_OF)};

static int palette_port_init(void) {
    for (size_t i = 0; i < ARRAY_SIZE(ports); i++) {
        const struct device *dev = ports[i].dev;
        if (!device_is_ready(dev)) {
            LOG_WRN("%s not ready, no palette command on it", dev->name);
            continue;
        }
        int rc = uart_irq_callback_user_data_set(dev, on_rx, &ports[i]);
        if (rc < 0) {
            LOG_WRN("%s rejected the callback (%d), no palette command on it", dev->name, rc);
            continue;
        }
        uart_irq_rx_enable(dev);
    }
    return 0;
}

SYS_INIT(palette_port_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
