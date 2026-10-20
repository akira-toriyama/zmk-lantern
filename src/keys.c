/*
 * Copyright (c) 2026 akira-toriyama
 *
 * SPDX-License-Identifier: MIT
 *
 * The key LEDs' picture: each LED shows one palette slot (src/palette.c), the
 * one the consumer's lantern,keys node assigns to the key under it, so the
 * palette's colours fall on the keyboard in bands (canon t-69g7: slots 0..4
 * left to right across both halves); and the countdown to the idle dark
 * (CONFIG_LANTERN_COUNTDOWN, canon t-gsm2, t-x82q): from the half's last key
 * to its idle dark, the picture changes every CONFIG_LANTERN_COUNTDOWN_STAGE_MS,
 * a stage: the bands, then the whole half in each colour of the palette in
 * turn, then the bands again; and the effects (canon t-6ttk), a picture that
 * moves after a key. This file is the strip's one writer. A frame goes out
 * when something changed: when the rail rule lights or blanks the LEDs
 * (src/on_usb_power.c), when the palette changes while they are lit, when the
 * countdown moves a stage, and every FRAME_MS while an effect moves; a still
 * picture costs no tick.
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
 * - The countdown: the pictures are the bands and then the palette's shades
 *   in slot order, N of them (a cleared slot is slot 0's; two colours that
 *   scale to one shade, a hue at two brightnesses or two greys, are one:
 *   canon's sender fills its five slots from fewer colours by repeating
 *   them, and a picture nobody could tell from the last would only be a
 *   skipped beat). Stage k, from k x CONFIG_LANTERN_COUNTDOWN_STAGE_MS after
 *   the last key, shows picture k mod N, so the pictures cycle until ZMK
 *   turns idle and the rail rule blanks the LEDs; one shade is the bands
 *   alone. Up to 750f413 a stage was CONFIG_ZMK_IDLE_TIMEOUT / N and the last
 *   shade held until the dark, so that the colour read how far the dark was;
 *   on hardware a minute a colour read as still, and the user chose a ten
 *   second cycle (canon t-x82q, 2026-10-20). The clock is this half's key
 *   positions (zmk_position_state_changed, which a split peripheral raises
 *   for its own keys, on the system work queue), kept while dark too, not
 *   ZMK's activity: ZMK keeps the time of its last input to itself and
 *   raises only the ACTIVE/IDLE transitions. A light does not restart the
 *   clock, so a cable plugged into a half that typed a while ago lights it
 *   into the stage its keys say (Opus review, 2026-10-20); only a clock
 *   already past the idle timeout restarts at a light, since then something
 *   this clock does not count woke ZMK, a sensor or pointing input (canon
 *   t-gsm2: the Imprint's trackballs are unused), and ZMK's clock restarted
 *   too. A key brings the bands back and restarts the clock.
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
 *   blank cancels it. A key while the bands show costs the step and no frame
 *   of the countdown's (an effect draws its own, below).
 * - The rail rule's order holds: lantern_keys_blank() has the black frame on
 *   the queue when it returns, and the rail drops BLANK_MS later
 *   (src/on_usb_power.c); lantern_keys_light() is called with the rail up and
 *   settled, so the frame meets powered LEDs.
 * - Two frames in a row: the handler ends with the data line low for LATCH_US
 *   (the code says why), so a frame that follows the last straight away, as
 *   the three pairs of a palette do, is latched and not lost (Opus review,
 *   2026-10-20).
 * - Effects (CONFIG_LANTERN_EFFECTS, which each effect selects): the half's
 *   own presses go into a ring of the last HITS, each a key position and the
 *   low 24 bits of the press's uptime in one atomic, written on the system
 *   work queue (the key listener, while lit; a full ring drops the oldest)
 *   and read by the frame, which clears each hit that has run its course, so
 *   a 24-bit time never wraps into a live one; the blank clears them all, and
 *   a press that lights an idle half shows no effect. While one moves, the
 *   frame arms frame_work, a delayable item on the same queue that runs the
 *   same handler, FRAME_MS ahead; a black frame cancels it, so no frame
 *   follows the blank's. An effect pulls an LED from the picture toward its
 *   colour (mix()), never adds to it: added to a bright band it would
 *   saturate to white. Its colours go up to HI_LEVEL, full scale, and then
 *   the frame's channel sum is held to CAP by scaling down only what the
 *   effects brightened above the picture, so the picture itself never dims
 *   for an effect. CAP is the most a still picture draws, every LED white at
 *   LEVEL, plus CAP_KEYS LEDs white at HI_LEVEL (about 280 + 100 mA for 29
 *   LEDs at 16 %, from 60 mA a full white LED: an estimate, unmeasured): with
 *   CAP at the white picture alone, a white or grey band left an effect no
 *   room (Opus review, 2026-10-20). More keys at full scale than CAP_KEYS,
 *   on a white picture, share the room and all dim alike. Integers only: the
 *   low priority queue's 768-byte stack is shared with ZMK's battery
 *   sampling, and a thread that once uses the FPU keeps a larger context
 *   from then on.
 * - The press glow (CONFIG_LANTERN_PRESS_GLOW): a pressed key's LED jumps to
 *   its band's colour at HI_LEVEL and falls back to the picture over GLOW_MS,
 *   QMK's SOLID_REACTIVE_SIMPLE written from its idea (QMK is GPL: nothing
 *   copied). Pressing every key of a half shows the chain order (positions)
 *   key by key, which the bands show only band by band. Full scale because
 *   twice LEVEL, the first try, was not seen on hardware: under the keycaps
 *   16 % to 32 % for half a second read as no change (canon t-6ttk,
 *   2026-10-21).
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

#if IS_ENABLED(CONFIG_LANTERN_COUNTDOWN) || IS_ENABLED(CONFIG_LANTERN_EFFECTS)
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

#if IS_ENABLED(CONFIG_LANTERN_EFFECTS)
/* The brightest channel of an effect's colour: full scale (the header). */
#define HI_LEVEL 255
BUILD_ASSERT(HI_LEVEL > LEVEL, "CONFIG_LANTERN_BRIGHTNESS 100 leaves an effect no headroom: an "
                               "effect brightens to full scale");
#define FRAME_MS 30
#define HITS 8
/* The channel sum no frame exceeds: every LED white at LEVEL, plus CAP_KEYS
 * LEDs white at HI_LEVEL instead (the header). */
#define CAP_KEYS 2
#define CAP (3 * (LED_COUNT * LEVEL + CAP_KEYS * (HI_LEVEL - LEVEL)))
/* A hit's time: the low 24 bits of k_uptime_get_32(), in ms. */
#define HIT_TIME 0xFFFFFFu
BUILD_ASSERT(KEY_COUNT <= 255, "lantern,keys: a hit stores its key position + 1 in a byte");
#endif

#if IS_ENABLED(CONFIG_LANTERN_PRESS_GLOW)
#define GLOW_MS 500
#endif

/* How long a hit lives: the longest effect's. */
#if IS_ENABLED(CONFIG_LANTERN_PRESS_GLOW)
#define HIT_LIFE_MS GLOW_MS
#endif

#if IS_ENABLED(CONFIG_LANTERN_EFFECTS)
#ifndef HIT_LIFE_MS
#error "CONFIG_LANTERN_EFFECTS without an effect: an effect sets HIT_LIFE_MS"
#endif
BUILD_ASSERT(HIT_LIFE_MS <= UINT16_MAX, "a hit's age is kept in 16 bits");
#endif

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
static atomic_t stage; /* stages since the last key; the picture is stage mod the shade count */

/* The 0xRRGGBB scaled so that its brightest channel is level; black to white. */
static struct led_rgb scale(uint32_t rgb, unsigned int level) {
    unsigned int r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
    unsigned int max = MAX(r, MAX(g, b));
    if (max == 0) {
        return (struct led_rgb){.r = level, .g = level, .b = level};
    }
    return (struct led_rgb){
        .r = (uint8_t)((r * level + max / 2) / max),
        .g = (uint8_t)((g * level + max / 2) / max),
        .b = (uint8_t)((b * level + max / 2) / max),
    };
}

static bool same(const struct led_rgb *a, const struct led_rgb *b) {
    return a->r == b->r && a->g == b->g && a->b == b->b;
}

/* The 0xRRGGBB slot s paints: a cleared slot paints slot 0's colour, and
 * without a palette every slot paints white. */
static uint32_t slot_rgb(const uint32_t palette[LANTERN_PALETTE_SLOTS], bool have, size_t s) {
    if (!have) {
        return 0xFFFFFF;
    }
    return palette[s] != 0 ? palette[s] : palette[0];
}

/* The shades a frame is made of: the palette's colours at LEVEL, the distinct
 * ones in slot order, and each slot's rank among them (a cleared slot ranks as
 * slot 0; without a palette one white). Returns how many,
 * 1..LANTERN_PALETTE_SLOTS. */
static size_t shades(const uint32_t palette[LANTERN_PALETTE_SLOTS], bool have,
                     struct led_rgb shade[LANTERN_PALETTE_SLOTS],
                     uint8_t rank[LANTERN_PALETTE_SLOTS]) {
    size_t n = 0;

    for (size_t s = 0; s < LANTERN_PALETTE_SLOTS; s++) {
        struct led_rgb c = scale(slot_rgb(palette, have, s), LEVEL);
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

#if IS_ENABLED(CONFIG_LANTERN_EFFECTS)

/* The ring of the last HITS presses (the header): (position + 1) << 24 | the
 * press's HIT_TIME; 0 is no hit. Set on the system work queue; the frame
 * clears a spent hit with a compare-and-set. */
static atomic_t hits[HITS];
static size_t hit_next;

struct hit {
    uint8_t key;  /* a key position */
    uint16_t age; /* ms since the press, below HIT_LIFE_MS */
};

/* System work queue. A full ring drops the oldest. */
static void hit_note(uint32_t position) {
    uint32_t v = ((position + 1) << 24) | (k_uptime_get_32() & HIT_TIME);
    atomic_set(&hits[hit_next], (atomic_val_t)v);
    hit_next = (hit_next + 1) % HITS;
}

/* System work queue. */
static void hits_clear(void) {
    for (size_t j = 0; j < HITS; j++) {
        atomic_set(&hits[j], 0);
    }
}

/* The frame's thread. The hits still alive, into live; returns how many.
 * One that has run its course is cleared, unless a newer press took its
 * place meanwhile (the compare-and-set). The clock is read after the hit, so
 * a press noted while this runs is never later than now: read before, the
 * press could be later, and its age would wrap to an expired one. */
static size_t hits_read(struct hit live[HITS]) {
    size_t n = 0;

    for (size_t j = 0; j < HITS; j++) {
        atomic_val_t v = atomic_get(&hits[j]);
        if (v == 0) {
            continue;
        }
        uint32_t now = k_uptime_get_32() & HIT_TIME;
        uint32_t age = (now - ((uint32_t)v & HIT_TIME)) & HIT_TIME;
        if (age >= HIT_LIFE_MS) {
            (void)atomic_cas(&hits[j], v, 0);
            continue;
        }
        live[n++] = (struct hit){.key = (uint8_t)(((uint32_t)v >> 24) - 1), .age = (uint16_t)age};
    }
    return n;
}

/* a pulled toward b by w / 256. */
static struct led_rgb mix(struct led_rgb a, struct led_rgb b, unsigned int w) {
    return (struct led_rgb){
        .r = (uint8_t)(a.r + ((int)b.r - (int)a.r) * (int)w / 256),
        .g = (uint8_t)(a.g + ((int)b.g - (int)a.g) * (int)w / 256),
        .b = (uint8_t)(a.b + ((int)b.b - (int)a.b) * (int)w / 256),
    };
}

/* The frame's thread: the picture under the frame's effects, for the cap. */
static struct led_rgb picture[LED_COUNT];

static uint32_t above(uint8_t px, uint8_t pic) { return px > pic ? (uint32_t)(px - pic) : 0; }

/* px with what it has above pic scaled by room / over. */
static uint8_t held(uint8_t px, uint8_t pic, uint32_t room, uint32_t over) {
    return px > pic ? (uint8_t)(pic + (uint32_t)(px - pic) * room / over) : px;
}

/* Hold the frame's channel sum to CAP, taking the excess out of what the
 * effects brightened above the picture alone (the header). The frame without
 * that brightening is at most the picture, which is at most CAP, so room is
 * never negative; the floor keeps the sum at most CAP. */
static void hold_to_cap(void) {
    uint32_t sum = 0, over = 0;

    for (size_t i = 0; i < LED_COUNT; i++) {
        sum += pixels[i].r + pixels[i].g + pixels[i].b;
        over += above(pixels[i].r, picture[i].r) + above(pixels[i].g, picture[i].g) +
                above(pixels[i].b, picture[i].b);
    }
    if (sum <= CAP) {
        return;
    }
    uint32_t room = CAP - (sum - over);
    for (size_t i = 0; i < LED_COUNT; i++) {
        pixels[i].r = held(pixels[i].r, picture[i].r, room, over);
        pixels[i].g = held(pixels[i].g, picture[i].g, room, over);
        pixels[i].b = held(pixels[i].b, picture[i].b, room, over);
    }
}

#endif /* IS_ENABLED(CONFIG_LANTERN_EFFECTS) */

#if IS_ENABLED(CONFIG_LANTERN_PRESS_GLOW)

/* Each pressed key's LED pulled toward its band's colour at HI_LEVEL, all the
 * way at the press and not at all GLOW_MS later; the freshest press of a key
 * wins. */
static void press_glow(const uint32_t palette[LANTERN_PALETTE_SLOTS], bool have,
                       const struct hit live[], size_t n) {
    for (size_t i = 0; i < LED_COUNT; i++) {
        unsigned int w = 0;
        for (size_t h = 0; h < n; h++) {
            if (live[h].key == positions[i] && live[h].age < GLOW_MS) {
                w = MAX(w, 256 - (unsigned int)live[h].age * 256 / GLOW_MS);
            }
        }
        if (w > 0) {
            struct led_rgb hi = scale(slot_rgb(palette, have, slots[positions[i]]), HI_LEVEL);
            pixels[i] = mix(pixels[i], hi, w);
        }
    }
}

#endif /* IS_ENABLED(CONFIG_LANTERN_PRESS_GLOW) */

/* The frame: the picture of stage at, the effects over it and the cap; true
 * while an effect still moves. The low priority work queue only: it writes
 * pixels and picture and clears spent hits (the palette and the hits are
 * atomics, read from there). */
static bool compose(size_t at) {
    uint32_t palette[LANTERN_PALETTE_SLOTS] = {0};
    bool have = IS_ENABLED(CONFIG_LANTERN_PALETTE) && lantern_palette_get(palette);
    struct led_rgb shade[LANTERN_PALETTE_SLOTS];
    uint8_t rank[LANTERN_PALETTE_SLOTS];
    size_t n = shades(palette, have, shade, rank);

    at %= n;
    for (size_t i = 0; i < LED_COUNT; i++) {
        pixels[i] = shade[at > 0 ? at : rank[slots[positions[i]]]];
    }

#if IS_ENABLED(CONFIG_LANTERN_EFFECTS)
    struct hit live[HITS];
    size_t moving = hits_read(live);

    memcpy(picture, pixels, sizeof(pixels));
#if IS_ENABLED(CONFIG_LANTERN_PRESS_GLOW)
    press_glow(palette, have, live, moving);
#endif
    hold_to_cap();
    return moving > 0;
#else
    return false;
#endif
}

static void paint(struct k_work *work);

static K_WORK_DEFINE(paint_work, paint);
#if IS_ENABLED(CONFIG_LANTERN_EFFECTS)
static K_WORK_DELAYABLE_DEFINE(frame_work, paint);
#endif

/* The low priority work queue only: paint_work and frame_work, which never
 * run at once. */
static void paint(struct k_work *work) {
    ARG_UNUSED(work);
    bool moving = false;

    if (atomic_get(&lit)) {
        moving = compose((size_t)atomic_get(&stage));
    } else {
        memset(pixels, 0, sizeof(pixels));
    }
    int rc = led_strip_update_rgb(strip, pixels, LED_COUNT);
    if (rc < 0) {
        LOG_ERR("frame not written: %d", rc);
    }
    k_usleep(LATCH_US);

#if IS_ENABLED(CONFIG_LANTERN_EFFECTS)
    if (moving) {
        k_work_reschedule_for_queue(zmk_workqueue_lowprio_work_q(), &frame_work,
                                    K_MSEC(FRAME_MS));
    } else {
        (void)k_work_cancel_delayable(&frame_work);
    }
#else
    (void)moving;
#endif
}

static void submit(void) { k_work_submit_to_queue(zmk_workqueue_lowprio_work_q(), &paint_work); }

#if IS_ENABLED(CONFIG_LANTERN_COUNTDOWN)

/* System work queue only (the header): k_uptime_get_32() at this half's last
 * key; 0 from boot, as ZMK's own clock starts at its init. */
static uint32_t since;

static void tick(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(tick_work, tick);

/* System work queue. Store the stage the clock gives now and arm the tick
 * for the next boundary; true when the stage moved. */
static bool countdown_step(void) {
    uint32_t elapsed = k_uptime_get_32() - since;
    size_t at = elapsed / CONFIG_LANTERN_COUNTDOWN_STAGE_MS;

    k_work_reschedule(&tick_work, K_MSEC((at + 1) * CONFIG_LANTERN_COUNTDOWN_STAGE_MS - elapsed));
    if ((size_t)atomic_set(&stage, (atomic_val_t)at) == at) {
        return false;
    }
    LOG_INF("stage %u", (unsigned int)at);
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

#else

static void countdown_start(void) {}
static void countdown_stop(void) {}
static void countdown_recount(void) {}

#endif /* IS_ENABLED(CONFIG_LANTERN_COUNTDOWN) */

#if IS_ENABLED(CONFIG_LANTERN_COUNTDOWN) || IS_ENABLED(CONFIG_LANTERN_EFFECTS)

/* System work queue, where a split peripheral raises its own keys' events, on
 * press and on release. */
static int key_listener(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *ev = as_zmk_position_state_changed(eh);
    if (ev == NULL) {
        return ZMK_EV_EVENT_BUBBLE;
    }
    bool frame = false;

#if IS_ENABLED(CONFIG_LANTERN_COUNTDOWN)
    since = k_uptime_get_32();
    frame = atomic_get(&lit) && countdown_step();
#endif
#if IS_ENABLED(CONFIG_LANTERN_EFFECTS)
    /* This half's own presses only: on a split central (Cyboard's stock left
     * half) the other half's keys arrive here too, and no LED here shows them. */
    if (ev->state && ev->source == ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL &&
        ev->position < KEY_COUNT && atomic_get(&lit)) {
        hit_note(ev->position);
        frame = true;
    }
#endif
    if (frame) {
        submit();
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(lantern_keys, key_listener);
ZMK_SUBSCRIPTION(lantern_keys, zmk_position_state_changed);

#endif

void lantern_keys_light(void) {
    atomic_set(&lit, 1);
    countdown_start();
    submit();
}

void lantern_keys_blank(void) {
    atomic_set(&lit, 0);
    countdown_stop();
#if IS_ENABLED(CONFIG_LANTERN_EFFECTS)
    hits_clear();
#endif
    submit();
}

void lantern_keys_refresh(void) {
    if (atomic_get(&lit)) {
        countdown_recount();
        submit();
    }
}
