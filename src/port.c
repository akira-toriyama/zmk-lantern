/*
 * Copyright (c) 2026 akira-toriyama
 *
 * SPDX-License-Identifier: MIT
 *
 * The host's entry: a text line on any USB CDC ACM port of the split central
 * (canon's Imprint Dongle has one, its board's; canon's scripts/dongle.py
 * palette, effect and intro write it). Lines, ASCII, ended by LF or CR, each a
 * word and its arguments; the word picks the file that answers (the palette,
 * src/palette.c; the pick of the effects, src/effect.c; the pick of the intro,
 * src/intro.c), and this file only reads lines, parses them and replies:
 *
 *   palette RRGGBB [RRGGBB ...]   set slots 0.. (one to LANTERN_PALETTE_SLOTS,
 *                                 six, colours; the rest are cleared to 000000)
 *     -> palette ok RRGGBB RRGGBB RRGGBB RRGGBB RRGGBB RRGGBB   the six slots as stored
 *     -> palette error: <reason>
 *   palette  or  palette?         ask
 *     -> palette RRGGBB RRGGBB RRGGBB RRGGBB RRGGBB RRGGBB   or   palette none
 *
 *   effect <name> [<name> ...] [RRGGBB]
 *                                 pick these effects (src/effect.h's names; two
 *                                 or more draw together), in one colour, or
 *                                 without one in the palette's colours
 *     -> effect ok <name> [<name> ...] [RRGGBB]   the pick as stored, in the
 *                                   table's order, the colour when one is set
 *     -> effect error: <reason>
 *   effect  or  effect?           ask
 *     -> effect <name> [<name> ...] [RRGGBB]   or   effect none   (also for a
 *                                   stored pick whose bits this image's table
 *                                   does not know: a reply must never be a bare
 *                                   "effect", which is itself an ask)
 *
 *   intro <name>                  pick the intro the boot light plays
 *                                 (src/intro.h's names; one at a time)
 *     -> intro ok <name>
 *     -> intro error: <reason>
 *   intro  or  intro?             ask
 *     -> intro <name>   or   intro none   (as the effect's)
 *
 * The two picks share one grammar (a word, then names from the word's table,
 * then the argument where the word takes one, the effects' colour) over one
 * mechanism (src/pick.c): a pick word is a table, its instance's get and send,
 * whether it takes a colour, and its error reasons, static strings (the parse
 * runs on the system work queue, whose stack the behavior path below it
 * shares). A colour is exactly six hex digits, either case; the reply
 * writes upper case. A name is one of the table's, exactly. A line that begins
 * with none of the words is not for this file and gets no reply (the port may carry
 * anything else the host sends), and so is a line shaped like one of the
 * replies ("palette ok …", "effect none", "… error: …"): a host whose terminal
 * echoes what it receives sends every reply back, and a reply answered would
 * be answered again without end. An echoed ask reply is a set of the same
 * values, which the devices take as unchanged and whose reply the echo then
 * drops. A line longer than LINE_LIMIT is dropped up to its end. The line
 * buffer is not cleared when USB goes away: a host starts with a newline, as
 * dongle.py does, to end whatever a lost connection left. Each reply starts
 * on a line of its own (a newline first): in a logging image the log thread
 * writes its lines byte by byte and the reply may land in the middle of one.
 * "ok" means the value is stored here and the halves' invocations are queued
 * (src/palette.c says when a half gets it), not that a half has it; before
 * the settings have loaded at boot the answer is an error, so a line cannot
 * race the load.
 *
 * - Zephyr's interrupt-driven UART API on the CDC ACM device. The legacy
 *   class driver runs the callback on the USB device work queue
 *   (CONFIG_USB_WORKQUEUE, on with USB_CDC_ACM; zephyr
 *   subsys/usb/device/class/cdc_acm.c), whose stack is 1 KiB
 *   (CONFIG_USB_WORKQUEUE_STACK_SIZE): the callback only collects bytes into
 *   a line and hands a complete line, through a message queue of two, to a
 *   work item on the system work queue, where the parse, the invocation
 *   (lantern_palette_send(), lantern_effect_send(), lantern_intro_send(): the
 *   behavior, the halves' commands, the store) and the reply run. The USB work queue is cooperative
 *   at -1, so a reply from there would also be written whole, but the
 *   behavior path's frames (ZMK's split central down to the queue's wait) do
 *   not fit beside the driver's with margin. A line arriving while two wait
 *   is dropped with a warning. Nothing else in a canon image reads the port:
 *   ZMK's console (CONFIG_UART_CONSOLE, the board's) and its USB logging
 *   write with uart_poll_out() and set no callback (zephyr
 *   drivers/console/uart_console.c sets one only with CONFIG_CONSOLE_HANDLER),
 *   and zmk-beacon's 1200 baud entry is the class driver's separate rate
 *   callback. The reply goes out with uart_poll_out(), which drops bytes on a
 *   full ring and never blocks: with no host reading, the reply is lost and
 *   nothing waits.
 * - Every CDC ACM instance, as zmk-beacon's bootloader entry registers on
 *   every one: a board or a consumer's overlay may add a node, and the host
 *   may open any of the resulting ports. A device keeps one UART callback,
 *   which is why one file reads every word: a consumer that gives a port to
 *   a shell or another reader turns CONFIG_LANTERN_PORT off.
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

#include "effect.h"
#include "intro.h"
#include "palette.h"

LOG_MODULE_REGISTER(lantern_port, LOG_LEVEL_INF);

BUILD_ASSERT(IS_ENABLED(CONFIG_LANTERN_PALETTE) || IS_ENABLED(CONFIG_LANTERN_EFFECT) ||
                 IS_ENABLED(CONFIG_LANTERN_INTRO),
             "CONFIG_LANTERN_PORT has no word to answer without the palette or a pick");

#define PALETTE_WORD "palette"
#define COLOUR_DIGITS 6
/* The longest name a pick's table may carry (asserted below), and the longest
 * line worth reading and the longest reply: "palette " and six slots of six
 * digits and a space each (49 and, with "ok ", 52), or a pick's word and every
 * name of its table with a space each; the longest error reply ("not ready:
 * ...") is 58. */
#define PICK_NAME_MAX 15
#define PALETTE_LINE (16 + 7 * LANTERN_PALETTE_SLOTS)
#define PICK_LINE(count) (16 + 7 + (PICK_NAME_MAX + 1) * (count))
#define LINE_LIMIT                                                                                 \
    MAX(PALETTE_LINE, MAX(PICK_LINE(LANTERN_EFFECT_COUNT), PICK_LINE(LANTERN_INTRO_COUNT)))
#define REPLY_MAX MAX(64, LINE_LIMIT)
#define LINES_WAITING 2

#define NAME_FITS_(sym, bit, name, ops)                                                            \
    BUILD_ASSERT(sizeof(name) - 1 <= PICK_NAME_MAX, "a pick's name is longer than PICK_NAME_MAX");
LANTERN_EFFECT_LIST(NAME_FITS_)
LANTERN_INTRO_LIST(NAME_FITS_)
#undef NAME_FITS_

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

static void reply_error(const struct device *dev, const char *word, const char *reason) {
    char text[REPLY_MAX];
    snprintk(text, sizeof(text), "%s error: %s", word, reason);
    reply(dev, text);
}

static void reply_not_sent(const struct device *dev, const char *word, int rc) {
    if (rc == -EAGAIN) {
        reply_error(dev, word, "not ready: the settings have not loaded yet");
        return;
    }
    char reason[32];
    snprintk(reason, sizeof(reason), "not sent (%d)", rc);
    reply_error(dev, word, reason);
}

/* True when s starts with word followed by a space or the end. */
static bool starts_with_word(const char *s, const char *word) {
    size_t n = strlen(word);
    return strncmp(s, word, n) == 0 && (s[n] == ' ' || s[n] == '\0');
}

/* s past the word and the spaces after it: "" for an ask (the word alone or
 * with "?"), the arguments for a set, NULL for a line that is not a command
 * (another word, or a reply echoed back; the header). */
static const char *arguments(const char *s, const char *word) {
    size_t n = strlen(word);
    if (strncmp(s, word, n) != 0) {
        return NULL;
    }
    s += n;
    if (*s == '\0' || (*s == '?' && s[1] == '\0')) {
        return "";
    }
    if (*s != ' ') {
        return NULL;
    }
    while (*s == ' ') {
        s++;
    }
    if (starts_with_word(s, "ok") || starts_with_word(s, "none") || starts_with_word(s, "error:")) {
        return NULL;
    }
    return s;
}

/* The value of six hex digits at s (a colour, the palette's and the effects'
 * pick's), or false. */
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

#if IS_ENABLED(CONFIG_LANTERN_PALETTE)

static void reply_palette(const struct device *dev, const char *head) {
    uint32_t colours[LANTERN_PALETTE_SLOTS];
    char text[REPLY_MAX];
    if (!lantern_palette_get(colours)) {
        snprintk(text, sizeof(text), "%s none", PALETTE_WORD);
    } else {
        int n = snprintk(text, sizeof(text), "%s%s", PALETTE_WORD, head);
        for (size_t i = 0; i < LANTERN_PALETTE_SLOTS && n < (int)sizeof(text); i++) {
            n += snprintk(text + n, sizeof(text) - n, " %06X", colours[i]);
        }
    }
    reply(dev, text);
}

static void handle_palette(const struct device *dev, const char *s) {
    if (*s == '\0') {
        reply_palette(dev, "");
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
            reply_error(dev, PALETTE_WORD, "at most " STRINGIFY(LANTERN_PALETTE_SLOTS) " colours");
            return;
        }
        if (!parse_colour(s, &colours[count]) ||
            (s[COLOUR_DIGITS] != '\0' && s[COLOUR_DIGITS] != ' ')) {
            reply_error(dev, PALETTE_WORD, "a colour is RRGGBB, six hex digits");
            return;
        }
        count++;
        s += COLOUR_DIGITS;
    }
    int rc = lantern_palette_send(colours, count);
    if (rc < 0) {
        reply_not_sent(dev, PALETTE_WORD, rc);
        return;
    }
    LOG_INF("%u colour(s) from %s", (unsigned int)count, dev->name);
    reply_palette(dev, " ok");
}

#endif /* IS_ENABLED(CONFIG_LANTERN_PALETTE) */

#if IS_ENABLED(CONFIG_LANTERN_EFFECT) || IS_ENABLED(CONFIG_LANTERN_INTRO)

/* A pick's word (the header): its table, in the table's order, and its
 * instance's get and send (src/effect.h, src/intro.h). */
struct named_bit {
    uint32_t bit;
    const char *name;
};

struct pick_word {
    const char *word;
    const struct named_bit *names;
    size_t count;
    bool (*get)(uint32_t *mask, uint32_t *arg);
    int (*send)(uint32_t mask, uint32_t arg);
    bool colour;         /* the word takes a trailing colour, the pick's argument (the effects') */
    const char *unknown; /* the error's reason for a name not in the table */
    const char *several; /* and for several names where the pick takes one; NULL where it takes any */
};

static void reply_pick(const struct device *dev, const struct pick_word *w, const char *head) {
    uint32_t mask = 0, arg = 0, known = 0;
    char text[REPLY_MAX];

    for (size_t i = 0; i < w->count; i++) {
        known |= w->names[i].bit;
    }
    if (!w->get(&mask, &arg) || (mask & known) == 0) {
        snprintk(text, sizeof(text), "%s none", w->word);
    } else {
        int n = snprintk(text, sizeof(text), "%s%s", w->word, head);
        for (size_t i = 0; i < w->count && n < (int)sizeof(text); i++) {
            if (mask & w->names[i].bit) {
                n += snprintk(text + n, sizeof(text) - n, " %s", w->names[i].name);
            }
        }
        if (w->colour && arg != 0 && n < (int)sizeof(text)) {
            snprintk(text + n, sizeof(text) - n, " %06X", arg);
        }
    }
    reply(dev, text);
}

/* The bit of the name that s starts with, ended by a space or the end, or 0. */
static uint32_t parse_name(const struct pick_word *w, const char *s, size_t *len) {
    for (size_t i = 0; i < w->count; i++) {
        if (starts_with_word(s, w->names[i].name)) {
            *len = strlen(w->names[i].name);
            return w->names[i].bit;
        }
    }
    return 0;
}

static void handle_pick(const struct device *dev, const struct pick_word *w, const char *s) {
    if (*s == '\0') {
        reply_pick(dev, w, "");
        return;
    }
    uint32_t mask = 0, colour = 0, c;
    while (*s != '\0') {
        if (*s == ' ') {
            s++;
            continue;
        }
        size_t len;
        uint32_t bit = parse_name(w, s, &len);
        if (bit != 0) {
            mask |= bit;
            s += len;
            continue;
        }
        /* a colour, where the word takes one: six hex digits to a space or the end */
        if (w->colour && parse_colour(s, &c) &&
            (s[COLOUR_DIGITS] == '\0' || s[COLOUR_DIGITS] == ' ')) {
            colour = c;
            s += COLOUR_DIGITS;
            continue;
        }
        reply_error(dev, w->word, w->unknown);
        return;
    }
    if (mask == 0) {
        /* a colour alone names no effect */
        reply_error(dev, w->word, w->unknown);
        return;
    }
    int rc = w->send(mask, colour);
    if (rc == -E2BIG && w->several != NULL) {
        reply_error(dev, w->word, w->several);
        return;
    }
    if (rc < 0) {
        reply_not_sent(dev, w->word, rc);
        return;
    }
    LOG_INF("%s: pick %08x %06x from %s", w->word, mask, colour, dev->name);
    reply_pick(dev, w, " ok");
}

#define NAMED_BIT_(sym, bit, name, ops) {BIT(bit), name},

#endif /* IS_ENABLED(CONFIG_LANTERN_EFFECT) || IS_ENABLED(CONFIG_LANTERN_INTRO) */

#if IS_ENABLED(CONFIG_LANTERN_EFFECT)

static const struct named_bit effect_names[] = {LANTERN_EFFECT_LIST(NAMED_BIT_)};
static const struct pick_word effect_word = {
    .word = "effect",
    .names = effect_names,
    .count = ARRAY_SIZE(effect_names),
    .get = lantern_effect_get,
    .send = lantern_effect_send,
    .colour = true,
    .unknown = "no such effect (the names: effect?)",
    .several = NULL,
};

#endif /* IS_ENABLED(CONFIG_LANTERN_EFFECT) */

#if IS_ENABLED(CONFIG_LANTERN_INTRO)

static const struct named_bit intro_names[] = {LANTERN_INTRO_LIST(NAMED_BIT_)};
static const struct pick_word intro_word = {
    .word = "intro",
    .names = intro_names,
    .count = ARRAY_SIZE(intro_names),
    .get = lantern_intro_get,
    .send = lantern_intro_send,
    .colour = false,
    .unknown = "no such intro (the names: intro?)",
    .several = "one intro at a time",
};

#endif /* IS_ENABLED(CONFIG_LANTERN_INTRO) */

static void handle(const struct line *l) {
    const char *s;
#if IS_ENABLED(CONFIG_LANTERN_PALETTE)
    s = arguments(l->text, PALETTE_WORD);
    if (s != NULL) {
        handle_palette(l->dev, s);
        return;
    }
#endif
#if IS_ENABLED(CONFIG_LANTERN_EFFECT)
    s = arguments(l->text, effect_word.word);
    if (s != NULL) {
        handle_pick(l->dev, &effect_word, s);
        return;
    }
#endif
#if IS_ENABLED(CONFIG_LANTERN_INTRO)
    s = arguments(l->text, intro_word.word);
    if (s != NULL) {
        handle_pick(l->dev, &intro_word, s);
        return;
    }
#endif
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

static int port_init(void) {
    for (size_t i = 0; i < ARRAY_SIZE(ports); i++) {
        const struct device *dev = ports[i].dev;
        if (!device_is_ready(dev)) {
            LOG_WRN("%s not ready, no command on it", dev->name);
            continue;
        }
        int rc = uart_irq_callback_user_data_set(dev, on_rx, &ports[i]);
        if (rc < 0) {
            LOG_WRN("%s rejected the callback (%d), no command on it", dev->name, rc);
            continue;
        }
        uart_irq_rx_enable(dev);
    }
    return 0;
}

SYS_INIT(port_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
