/*
 * Copyright (c) 2026 akira-toriyama
 *
 * SPDX-License-Identifier: MIT
 *
 * The palette's entry from the host: a text line on any USB CDC ACM port of
 * the split central (canon's Imprint Dongle has one, its board's; canon's
 * scripts/dongle.py palette writes it). Lines, ASCII, ended by LF or CR:
 *
 *   palette RRGGBB [RRGGBB ...]   set slots 0.. (one to LANTERN_PALETTE_SLOTS,
 *                                 six, colours; the rest stay)
 *     -> palette ok RRGGBB RRGGBB RRGGBB RRGGBB RRGGBB RRGGBB   the six slots as stored
 *     -> palette error: <reason>
 *   palette  or  palette?         ask
 *     -> palette RRGGBB RRGGBB RRGGBB RRGGBB RRGGBB RRGGBB   or   palette none
 *
 * A colour is exactly six hex digits, either case; the reply writes upper
 * case. A line that does not begin with "palette" is not for this file and
 * gets no reply (the port may carry anything else the host sends), and so is
 * a line shaped like one of the replies ("palette ok …", "palette none",
 * "palette error: …"): a host whose terminal echoes what it receives sends
 * every reply back, and a reply answered would be answered again without
 * end. An echoed ask reply is a set of the same colours, which the devices
 * take as unchanged and whose reply the echo then drops. A line longer than
 * LINE_LIMIT is dropped up to its end. The line buffer is not cleared when
 * USB goes away: a host starts with a newline, as dongle.py does, to end
 * whatever a lost connection left. Each reply starts on a line of its own
 * (a newline first): in a logging image the log thread writes its lines byte
 * by byte and the reply may land in the middle of one. "ok" means the
 * palette is stored here and the halves' invocations are queued (src/palette.c
 * says when a half gets it), not that a half has it; before the settings have
 * loaded at boot the answer is an error, so a line cannot race the load.
 *
 * - Zephyr's interrupt-driven UART API on the CDC ACM device. The legacy
 *   class driver runs the callback on the USB device work queue
 *   (CONFIG_USB_WORKQUEUE, on with USB_CDC_ACM; zephyr
 *   subsys/usb/device/class/cdc_acm.c), whose stack is 1 KiB
 *   (CONFIG_USB_WORKQUEUE_STACK_SIZE): the callback only collects bytes into
 *   a line and hands a complete line, through a message queue of two, to a
 *   work item on the system work queue, where the parse, the invocation
 *   (lantern_palette_send(): the behavior, the halves' commands, the store)
 *   and the reply run. The USB work queue is cooperative at -1, so a reply
 *   from there would also be written whole, but the behavior path's frames
 *   (ZMK's split central down to the queue's wait) do not fit beside the
 *   driver's with margin. A line arriving while two wait is dropped with a
 *   warning. Nothing else in a canon image reads the port: ZMK's console
 *   (CONFIG_UART_CONSOLE, the board's) and its USB logging write with
 *   uart_poll_out() and set no callback (zephyr drivers/console/uart_console.c
 *   sets one only with CONFIG_CONSOLE_HANDLER), and zmk-beacon's 1200 baud
 *   entry is the class driver's separate rate callback. The reply goes out
 *   with uart_poll_out(), which drops bytes on a full ring and never blocks:
 *   with no host reading, the reply is lost and nothing waits.
 * - Every CDC ACM instance, as zmk-beacon's bootloader entry registers on
 *   every one: a board or a consumer's overlay may add a node, and the host
 *   may open any of the resulting ports. A device keeps one UART callback:
 *   a consumer that gives a port to a shell or another reader turns
 *   CONFIG_LANTERN_PALETTE_PORT off.
 */

#include <errno.h>
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
/* "palette " and a slot's six digits and a space each: 49 characters for six
 * slots; the reply "palette ok " and the slots 53. */
#define LINE_LIMIT (16 + 7 * LANTERN_PALETTE_SLOTS)
#define REPLY_MAX (16 + 7 * LANTERN_PALETTE_SLOTS)
#define LINES_WAITING 2

/* The USB work queue's side: bytes into a line. */
struct port {
    const struct device *dev;
    char text[LINE_LIMIT + 1];
    size_t len;
    bool overflow;
};

/* A complete line, on its way to the system work queue. */
struct line {
    const struct device *dev;
    char text[LINE_LIMIT + 1];
};

K_MSGQ_DEFINE(lines, sizeof(struct line), LINES_WAITING, 4);

static void reply(const struct device *dev, const char *text) {
    uart_poll_out(dev, '\n');
    for (const char *c = text; *c != '\0'; c++) {
        uart_poll_out(dev, (unsigned char)*c);
    }
    uart_poll_out(dev, '\n');
}

static void reply_palette(const struct device *dev, const char *head) {
    uint32_t colours[LANTERN_PALETTE_SLOTS];
    char text[REPLY_MAX];
    if (!lantern_palette_get(colours)) {
        snprintk(text, sizeof(text), "%s none", WORD);
    } else {
        int n = snprintk(text, sizeof(text), "%s%s", WORD, head);
        for (size_t i = 0; i < LANTERN_PALETTE_SLOTS && n < (int)sizeof(text); i++) {
            n += snprintk(text + n, sizeof(text) - n, " %06X", colours[i]);
        }
    }
    reply(dev, text);
}

static void reply_error(const struct device *dev, const char *reason) {
    char text[REPLY_MAX];
    snprintk(text, sizeof(text), "%s error: %s", WORD, reason);
    reply(dev, text);
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

/* True when s starts with word followed by a space or the end. */
static bool starts_with_word(const char *s, const char *word) {
    size_t n = strlen(word);
    return strncmp(s, word, n) == 0 && (s[n] == ' ' || s[n] == '\0');
}

static void handle(const struct line *l) {
    const struct device *dev = l->dev;
    const char *s = l->text;
    if (strncmp(s, WORD, strlen(WORD)) != 0) {
        return;
    }
    s += strlen(WORD);
    if (*s == '\0' || (*s == '?' && s[1] == '\0')) {
        reply_palette(dev, "");
        return;
    }
    if (*s != ' ') {
        return;
    }
    while (*s == ' ') {
        s++;
    }
    /* One of the replies, echoed back (the header). */
    if (starts_with_word(s, "ok") || starts_with_word(s, "none") || starts_with_word(s, "error:")) {
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
            reply_error(dev, "at most " STRINGIFY(LANTERN_PALETTE_SLOTS) " colours");
            return;
        }
        if (!parse_colour(s, &colours[count]) ||
            (s[COLOUR_DIGITS] != '\0' && s[COLOUR_DIGITS] != ' ')) {
            reply_error(dev, "a colour is RRGGBB, six hex digits");
            return;
        }
        count++;
        s += COLOUR_DIGITS;
    }
    if (count == 0) {
        reply_error(dev, "no colour given");
        return;
    }

    int rc = lantern_palette_send(colours, count);
    if (rc == -EAGAIN) {
        reply_error(dev, "not ready: the settings have not loaded yet");
        return;
    }
    if (rc < 0) {
        char reason[32];
        snprintk(reason, sizeof(reason), "not sent (%d)", rc);
        reply_error(dev, reason);
        return;
    }
    LOG_INF("%u colour(s) from %s", (unsigned int)count, dev->name);
    reply_palette(dev, " ok");
}

/* System work queue. */
static void handle_lines(struct k_work *work) {
    ARG_UNUSED(work);
    struct line l;
    while (k_msgq_get(&lines, &l, K_NO_WAIT) == 0) {
        handle(&l);
    }
}

static K_WORK_DEFINE(lines_work, handle_lines);

/* USB work queue: a complete line to the system work queue. */
static void complete(struct port *p) {
    struct line l = {.dev = p->dev};
    memcpy(l.text, p->text, p->len);
    l.text[p->len] = '\0';
    if (k_msgq_put(&lines, &l, K_NO_WAIT) < 0) {
        LOG_WRN("%s: a line dropped, %d already waiting", p->dev->name, LINES_WAITING);
        return;
    }
    k_work_submit(&lines_work);
}

static void feed(struct port *p, uint8_t c) {
    if (c == '\n' || c == '\r') {
        if (p->overflow) {
            p->overflow = false;
        } else if (p->len > 0) {
            complete(p);
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
    p->text[p->len++] = (char)c;
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
