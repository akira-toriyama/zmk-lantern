/*
 * Copyright (c) 2026 akira-toriyama
 *
 * SPDX-License-Identifier: MIT
 *
 * The key LEDs' picture: each LED shows one palette slot (src/palette.c), the
 * one the consumer's lantern,keys node assigns to the key under it, so the
 * palette's colours fall on the keyboard in bands (canon t-69g7: slots 0..4
 * left to right across both halves); and the countdown to the idle dark
 * (CONFIG_LANTERN_COUNTDOWN, canon t-gsm2): from the half's last key, the lit
 * time is cut into as many stages as the palette has colours, stage 0 the
 * bands and stage k the whole half in colour k, so that the colour reads how
 * far the dark is. This file is the strip's one writer. A frame goes out when
 * something changed, never on a fixed tick: when the rail rule lights or
 * blanks the LEDs (src/on_usb_power.c), when the palette changes while they
 * are lit, and when the countdown moves a stage; effects to come (canon
 * t-6ttk) add their tick here.
 *
 * - Why the module writes the strip itself and ZMK's underglow is asserted
 *   off: rgb_underglow.c repaints every LED with its one colour and effect at
 *   each 50 ms tick while on, so a per-LED picture cannot share the strip
 *   with it, and it saves its state (on, colour) and restores it at the next
 *   boot, which the rail rule had to override every time (zmk-lantern up to
 *   2406809 lit the LEDs through it). With it off nothing else touches the
 *   strip, and a frame is written only when something changed.
 * - The picture: LED i shows the colour of slot slots[positions[i]]. A slot
 *   that is 0x000000 shows slot 0 instead: the host clears the slots it does
 *   not send (lantern_palette_send()), so a palette of one colour is a solid
 *   one (canon t-0967 leaves dark colours out of a sprite's palette since
 *   2026-10-20, so black arrives only as a cleared slot). Without a palette
 *   every LED is white. Each colour is scaled so that its brightest channel
 *   is CONFIG_LANTERN_BRIGHTNESS percent of full scale: hue and saturation
 *   are the colour's, the brightness is the device's (the LEDs sit under
 *   keycaps; 16 is what ZMK's underglow drove at canon's earlier settings,
 *   brightness 25 between 5 and 50). A grey has no hue and scales to white;
 *   so does black, which has no brightest channel, when it is slot 0.
 * - The countdown: the palette's shades in slot order are the stages, N of
 *   them (a cleared slot is slot 0's; two colours that scale to one shade, a
 *   hue at two brightnesses or two greys, are one: canon's sender fills its
 *   five slots from fewer colours by repeating them, and a stage nobody could
 *   tell from the last would only shorten the others), and a stage lasts
 *   CONFIG_ZMK_IDLE_TIMEOUT / N, so the last one ends where ZMK turns idle
 *   and the rail rule blanks the LEDs (ZMK checks once a second, so the last
 *   stage holds up to a second longer). The clock is this half's key
 *   positions (zmk_position_state_changed, which a split peripheral raises
 *   for its own keys, on the system work queue), kept while dark too, not
 *   ZMK's activity: ZMK keeps the time of its last input to itself and
 *   raises only the ACTIVE/IDLE transitions. A light does not restart the
 *   clock, so a cable plugged into a half that typed a while ago lights it
 *   into the stage its keys say, in step with ZMK's idle (Opus review,
 *   2026-10-20); only a clock already past the timeout restarts at a light,
 *   since then something this clock does not count woke ZMK, a sensor or
 *   pointing input (canon t-gsm2: the Imprint's trackballs are unused), and
 *   ZMK's clock restarted too. Such input while lit moves ZMK's idle alone,
 *   so the last stage then holds until ZMK idles. A key brings the bands back
 *   and restarts the clock. One shade, one stage: the bands until the dark.
 * - Threads: the frame is composed and written on ZMK's low priority work
 *   queue (zmk_workqueue_lowprio_work_q(), the one rgb_underglow.c used; the
 *   Kconfig selects it). led_strip_update_rgb() blocks for the SPI transfer
 *   (the WS2812 SPI driver sends the node's whole chain-length, 24 bytes an
 *   LED at 4 MHz: 1.4 ms for 29 LEDs, 7 ms for Cyboard's placeholder 150,
 *   hence the consumer sets the real count) and stays off the system work
 *   queue, where ZMK processes key events, and off the Bluetooth receive
 *   queue, where a half runs the palette behavior. A caller only sets the
 *   wanted state (lit or dark, the stage; atomics) and submits the one work
 *   item; the handler reads the state and the palette's atomics when it runs,
 *   so two submissions coalesce, the last state wins, a submission during the
 *   handler runs it again, and nothing holds a lock. The countdown's clock and
 *   its tick, a delayable work item, live on the system work queue, where ZMK
 *   raises the position events and where the rail rule lights and blanks
 *   (src/on_usb_power.c), so they need no lock either: one step computes the
 *   stage from the clock and the palette, arms the tick for the next
 *   boundary and says whether the stage moved, which alone costs a frame; the
 *   tick, a key and the light take the step there and then, a palette change
 *   runs the tick at once (k_work_reschedule works from any thread), the
 *   blank cancels it. A key while the bands show costs the step and no frame.
 * - The rail rule's order holds: lantern_keys_blank() has the black frame on
 *   the queue when it returns, and the rail drops BLANK_MS later
 *   (src/on_usb_power.c); lantern_keys_light() is called with the rail up and
 *   settled, so the frame meets powered LEDs.
 * - Two frames in a row: the handler ends with the data line low for LATCH_US
 *   (the code says why), so a frame that follows the last straight away, as
 *   the three pairs of a palette do, is latched and not lost (Opus review,
 *   2026-10-20).
 */

#define DT_DRV_COMPAT lantern_keys

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

#include <zmk/matrix.h>
#include <zmk/workqueue.h>

#if IS_ENABLED(CONFIG_LANTERN_COUNTDOWN)
#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>
#endif

#include "keys.h"
#include "palette.h"

LOG_MODULE_REGISTER(lantern_keys, LOG_LEVEL_INF);

#define NODE DT_DRV_INST(0)
#define STRIP DT_INST_PHANDLE(0, strip)
#define LED_COUNT DT_INST_PROP_LEN(0, positions)
#define KEY_COUNT DT_INST_PROP_LEN(0, slots)

BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) == 1,
             "CONFIG_LANTERN_KEYS needs exactly one lantern,keys node");
BUILD_ASSERT(!IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW),
             "CONFIG_ZMK_RGB_UNDERGLOW must be n with CONFIG_LANTERN_KEYS: its tick would repaint "
             "the strip this module paints (the header says why)");
BUILD_ASSERT(LED_COUNT == DT_PROP(STRIP, chain_length),
             "lantern,keys: positions needs one entry per LED, the strip's chain-length");
BUILD_ASSERT(KEY_COUNT == ZMK_KEYMAP_LEN,
             "lantern,keys: slots needs one entry per key position of the keymap");
BUILD_ASSERT(KEY_COUNT <= 256, "lantern,keys: a key position is stored in a byte");
BUILD_ASSERT(IS_ENABLED(CONFIG_LANTERN_ON_USB_POWER),
             "CONFIG_LANTERN_KEYS needs CONFIG_LANTERN_ON_USB_POWER: nothing else lights the LEDs");
#if IS_ENABLED(CONFIG_LANTERN_COUNTDOWN)
BUILD_ASSERT(CONFIG_ZMK_IDLE_TIMEOUT >= LANTERN_PALETTE_SLOTS,
             "CONFIG_LANTERN_COUNTDOWN needs CONFIG_ZMK_IDLE_TIMEOUT of at least a millisecond a "
             "palette slot: a stage is the timeout over the colour count");
#endif

#define POSITION_IN_RANGE(node_id, prop, idx)                                                     \
    BUILD_ASSERT(DT_PROP_BY_IDX(node_id, prop, idx) < KEY_COUNT,                                  \
                 "lantern,keys: a positions entry is no key position of slots");
DT_FOREACH_PROP_ELEM(NODE, positions, POSITION_IN_RANGE)

#define SLOT_IN_RANGE(node_id, prop, idx)                                                         \
    BUILD_ASSERT(DT_PROP_BY_IDX(node_id, prop, idx) < LANTERN_PALETTE_SLOTS,                      \
                 "lantern,keys: a slots entry is no palette slot (0 to 5)");
DT_FOREACH_PROP_ELEM(NODE, slots, SLOT_IN_RANGE)

/* The brightest channel of every colour painted. */
#define LEVEL ((255 * CONFIG_LANTERN_BRIGHTNESS + 50) / 100)

/* The data line low for this long after a frame, so that the LEDs latch it:
 * a WS2812B needs at least 280 us (its V5 datasheet's RES; the older part
 * 50 us), and a frame that arrives sooner reads as more pixels of the last
 * one and falls off the chain's end. The driver's own pause after a transfer
 * is the strip node's reset-delay, 8 us unless the consumer sets it (zephyr
 * drivers/led_strip/ws2812_spi.c), and a submission during the handler runs
 * it again straight away, with only that pause between the frames. */
#define LATCH_US 300

static const struct device *const strip = DEVICE_DT_GET(STRIP);
static const uint8_t positions[LED_COUNT] = DT_INST_PROP(0, positions);
static const uint8_t slots[KEY_COUNT] = DT_INST_PROP(0, slots);
static struct led_rgb pixels[LED_COUNT];
static atomic_t lit;
static atomic_t stage; /* 0 the bands; k the whole half in shade k (clamped when painted) */

/* The 0xRRGGBB scaled so that its brightest channel is LEVEL; black to white. */
static struct led_rgb scaled(uint32_t rgb) {
    unsigned int r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
    unsigned int max = MAX(r, MAX(g, b));
    if (max == 0) {
        return (struct led_rgb){.r = LEVEL, .g = LEVEL, .b = LEVEL};
    }
    return (struct led_rgb){
        .r = (uint8_t)((r * LEVEL + max / 2) / max),
        .g = (uint8_t)((g * LEVEL + max / 2) / max),
        .b = (uint8_t)((b * LEVEL + max / 2) / max),
    };
}

static bool same(const struct led_rgb *a, const struct led_rgb *b) {
    return a->r == b->r && a->g == b->g && a->b == b->b;
}

/* The shades a frame is made of: the palette's colours at LEVEL, the distinct
 * ones in slot order, and each slot's rank among them (a cleared slot ranks as
 * slot 0; without a palette one white). Returns how many,
 * 1..LANTERN_PALETTE_SLOTS. Any thread: the palette is atomics. */
static size_t shades(struct led_rgb shade[LANTERN_PALETTE_SLOTS],
                     uint8_t rank[LANTERN_PALETTE_SLOTS]) {
    uint32_t palette[LANTERN_PALETTE_SLOTS];
    bool have = IS_ENABLED(CONFIG_LANTERN_PALETTE) && lantern_palette_get(palette);
    size_t n = 0;

    for (size_t s = 0; s < LANTERN_PALETTE_SLOTS; s++) {
        uint32_t rgb = 0xFFFFFF;
        if (have) {
            rgb = palette[s] != 0 ? palette[s] : palette[0];
        }
        struct led_rgb c = scaled(rgb);
        size_t r = 0;
        while (r < n && !same(&shade[r], &c)) {
            r++;
        }
        if (r == n) {
            shade[n++] = c;
        }
        rank[s] = (uint8_t)r;
    }
    return n;
}

static void compose(size_t at) {
    struct led_rgb shade[LANTERN_PALETTE_SLOTS];
    uint8_t rank[LANTERN_PALETTE_SLOTS];
    size_t n = shades(shade, rank);

    at = MIN(at, n - 1);
    for (size_t i = 0; i < LED_COUNT; i++) {
        size_t r = rank[slots[positions[i]]];
        if (IS_ENABLED(CONFIG_LANTERN_COUNTDOWN_ROTATE)) {
            r = (r + at) % n;
        } else if (at > 0) {
            r = at;
        }
        pixels[i] = shade[r];
    }
}

static void paint(struct k_work *work) {
    ARG_UNUSED(work);
    if (atomic_get(&lit)) {
        compose((size_t)atomic_get(&stage));
    } else {
        memset(pixels, 0, sizeof(pixels));
    }
    int rc = led_strip_update_rgb(strip, pixels, LED_COUNT);
    if (rc < 0) {
        LOG_ERR("frame not written: %d", rc);
    }
    k_usleep(LATCH_US);
}

static K_WORK_DEFINE(paint_work, paint);

static void submit(void) { k_work_submit_to_queue(zmk_workqueue_lowprio_work_q(), &paint_work); }

#if IS_ENABLED(CONFIG_LANTERN_COUNTDOWN)

/* System work queue only (the header): k_uptime_get_32() at this half's last
 * key; 0 from boot, as ZMK's own clock starts at its init. */
static uint32_t since;

static void tick(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(tick_work, tick);

/* System work queue. Store the stage the clock and the palette give now and
 * arm the tick for the next boundary; true when the stage moved. */
static bool countdown_step(void) {
    struct led_rgb shade[LANTERN_PALETTE_SLOTS];
    uint8_t rank[LANTERN_PALETTE_SLOTS];
    size_t n = shades(shade, rank);
    uint32_t period = CONFIG_ZMK_IDLE_TIMEOUT / n;
    uint32_t elapsed = k_uptime_get_32() - since;
    size_t at = MIN(elapsed / period, n - 1);

    if (at + 1 < n) {
        k_work_reschedule(&tick_work, K_MSEC((at + 1) * period - elapsed));
    }
    if ((size_t)atomic_set(&stage, (atomic_val_t)at) == at) {
        return false;
    }
    LOG_INF("stage %u of %u", (unsigned int)at, (unsigned int)n);
    return true;
}

static void tick(struct k_work *work) {
    ARG_UNUSED(work);
    if (atomic_get(&lit) && countdown_step()) {
        submit();
    }
}

/* The light: a clock past the timeout restarts, as ZMK's did (the header);
 * the stage is stored before the caller's frame reads it. */
static void countdown_start(void) {
    uint32_t now = k_uptime_get_32();
    if (now - since >= CONFIG_ZMK_IDLE_TIMEOUT) {
        since = now;
    }
    (void)countdown_step();
}

static void countdown_stop(void) { k_work_cancel_delayable(&tick_work); }

static void countdown_recount(void) { k_work_reschedule(&tick_work, K_NO_WAIT); }

static int key_listener(const zmk_event_t *eh) {
    if (as_zmk_position_state_changed(eh) == NULL) {
        return ZMK_EV_EVENT_BUBBLE;
    }
    since = k_uptime_get_32();
    if (atomic_get(&lit) && countdown_step()) {
        submit();
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(lantern_keys, key_listener);
ZMK_SUBSCRIPTION(lantern_keys, zmk_position_state_changed);

#else

static void countdown_start(void) {}
static void countdown_stop(void) {}
static void countdown_recount(void) {}

#endif /* IS_ENABLED(CONFIG_LANTERN_COUNTDOWN) */

void lantern_keys_light(void) {
    atomic_set(&lit, 1);
    countdown_start();
    submit();
}

void lantern_keys_blank(void) {
    atomic_set(&lit, 0);
    countdown_stop();
    submit();
}

void lantern_keys_refresh(void) {
    if (atomic_get(&lit)) {
        countdown_recount();
        submit();
    }
}
