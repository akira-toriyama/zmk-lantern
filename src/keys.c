/*
 * Copyright (c) 2026 akira-toriyama
 *
 * SPDX-License-Identifier: MIT
 *
 * The key LEDs' picture: each LED shows its key's colour in the picture, by
 * default the palette slot (src/palette.c) the consumer's lantern,keys node
 * assigns to the key, so the palette's colours fall on the keyboard in bands
 * (canon t-69g7: slots 0..4 left to right across both halves; the other
 * pictures below); and the countdown to the idle dark
 * (CONFIG_LANTERN_COUNTDOWN, canon t-gsm2, t-x82q): from the half's last key
 * to its idle dark, the picture changes every CONFIG_LANTERN_COUNTDOWN_STAGE_MS,
 * a stage: the bands, then the whole half in each colour of the palette in
 * turn, then the bands again; and the effects (canon t-6ttk), a picture that
 * moves after a key, each in its own file under src/effects/, of which the
 * pick (src/effect.c, canon t-5hqx) says which draw; and the intro
 * (src/intro.c) in the boot light's place. This file is the frame's engine
 * and the strip's one writer: what it hands the effects and the intro, and
 * takes from them, is src/frame.h. A frame goes out when something changed:
 * when the rail rule lights or blanks the LEDs (src/on_usb_power.c), when
 * the palette changes while they are lit, when the countdown moves a stage,
 * at each throw of the scatter, when the boot light ends (the no picture's or
 * the intro's), and every FRAME_MS while an effect moves or the intro plays;
 * a still picture costs no tick.
 *
 * - Why the module writes the strip itself and ZMK's underglow is asserted
 *   off: rgb_underglow.c repaints every LED with its one colour and effect at
 *   each 50 ms tick while on, so a per-LED picture cannot share the strip
 *   with it, and it saves its state (on, colour) and restores it at the next
 *   boot, which the rail rule had to override every time (zmk-lantern up to
 *   2406809 lit the LEDs through it). With it off nothing else touches the
 *   strip, and a frame is written only when something changed.
 * - The picture: LED i shows the colour of one slot, base_slot(i), which the
 *   Kconfig choice LANTERN_PICTURE makes the key's slot (the bands, the
 *   default), 0 (the solid) or the LED's throw (the scatter); the gradient
 *   shows ramp()'s blend of two shades in place of a slot, and the no picture
 *   black (the four below).
 *   A slot that is 0x000000 shows slot 0 instead: the host clears the slots
 *   it does not send (lantern_palette_send()), so a palette of one colour
 *   paints every LED alike (canon t-0967 leaves dark colours out of a
 *   sprite's palette since 2026-10-20, so black arrives only as a cleared
 *   slot). Without a palette every LED is white (the no picture's black
 *   aside). Each colour of the picture
 *   (the picture, the countdown's other pictures, the stain's dye) is scaled
 *   so that its brightest channel is CONFIG_LANTERN_PICTURE_BRIGHTNESS
 *   percent of full scale: hue and saturation are the colour's, the
 *   brightness is the device's. That is CONFIG_LANTERN_BRIGHTNESS by default
 *   and never above it (asserted), the device's brightness, from which the
 *   frame's cap counts (the LEDs sit under keycaps; 16 is what ZMK's
 *   underglow drove at canon's earlier settings, brightness 25 between 5 and
 *   50); a picture dimmed below it (canon t-skdt) leaves the effects, which
 *   brighten to full scale under the same cap, as much room or more. A grey
 *   has no hue and scales to white; so does black, which has no brightest
 *   channel, when it is slot 0.
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
 *   wanted state (lit or dark, the stage, the boot light and the intro's
 *   start; atomics) and submits the one work
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
 *   own presses, and the other half's that the split central relays
 *   (lantern_keys_hit(), src/hit.c), go into a ring of the last HITS, each a
 *   key position, the press's number (the count of presses, cycling) and the
 *   low 18 bits of the press's uptime in one atomic, written while lit (the
 *   key listener on the system work queue, a relayed press on the Bluetooth
 *   receive queue, or on the system work queue on a central with key LEDs:
 *   the ring's index and the count advance by compare-and-set, so no lock; a
 *   full ring drops the oldest, which with both halves' presses in it and the
 *   crossing ring's life takes about ten presses a second) and read by the frame,
 *   which clears each hit past the picked effects' life, so an 18-bit time
 *   never wraps into a live one (cleared only at the longest built life, a
 *   hit the picked effects had finished with sat in the ring until a frame
 *   ran, and after 262 s of lit silence read as fresh: Opus review,
 *   2026-10-26); the blank clears them all, and a press that lights
 *   an idle half shows no effect. While one moves, the frame arms frame_work,
 *   a delayable item on the same queue that runs the same handler, FRAME_MS
 *   ahead; a black frame cancels it, so no frame follows the blank's. That
 *   timer and the cap below are the moving frames' (CONFIG_LANTERN_FRAMES),
 *   which the intro selects too. Which effects draw is the pick's
 *   (src/effect.h): the image carries every effect the consumer's conf turns
 *   on, and each frame reads the pick the host stored (lantern_effect_get(),
 *   an atomic), keeps the bits built here and draws those, every one of them
 *   over the last (the grammar allows several; canon sends one); with no pick
 *   stored, or none of its bits built, the frame draws the one the Kconfig
 *   choice LANTERN_EFFECT_DEFAULT names, the only pick a consumer without a
 *   host has. A hit lives the picked effects' life, the longest among those
 *   that draw, and a frame keeps moving while one lives. When the pick
 *   changes, src/effect.c clears the hits
 *   (lantern_keys_effect_changed()) and the frame clears the effects' state
 *   (the stain's dye), so a ring half-way out does not turn into the new
 *   effect's shape. An
 *   effect pulls an LED from the picture toward its colour (mix()), never
 *   adds to it: added to a bright band it would saturate to white. Its
 *   colours go up to HI_LEVEL, full scale, and then the frame's channel sum
 *   is held to CAP by scaling down only what the effects brightened above the
 *   picture, so the picture itself never dims for an effect. CAP is the most
 *   a still picture draws at the device's brightness, every LED white at
 *   LEVEL, plus CAP_KEYS LEDs white at HI_LEVEL (about 280 + 100 mA for 29
 *   LEDs at 16 %, from 60 mA a full white LED: an estimate, unmeasured): with
 *   CAP at the white picture alone, a white or grey band left an effect no
 *   room (Opus review, 2026-10-20). More keys at full scale than CAP_KEYS, on
 *   a white picture at the device's brightness, share the room and all dim
 *   alike. Integers only, for the effects and the intro alike: the low
 *   priority queue's 768-byte stack is shared with ZMK's battery sampling,
 *   and a thread that once uses the FPU keeps a larger context from then on.
 * - The effects' table (src/frame.h says what an entry is): one entry a file
 *   under src/effects/, built from LANTERN_EFFECT_LIST (src/effect.h) in its
 *   order, those the image builds. The frame paints the picture, lets each
 *   picked entry paint its lasting state into it (the stain's dye, counted
 *   as picture by the cap), keeps the picture, lets each picked entry draw
 *   the live hits over it in the table's order, and holds the frame to CAP.
 *   An entry draws for its line's bit and any it names (serves), so the
 *   thin ring's entry draws for the stain's bit too and the ring is drawn
 *   once, first, whichever is picked. The entries' state goes at a blank and when the stage or the
 *   pick moves (drawn_at, drawn_effects: the stain's dye went with the
 *   picture before the table, and goes the same way). A new effect is a
 *   line in the list, a file under src/effects/ with its entry, its Kconfig
 *   symbol and default-choice member (src/effect.h) and its CMake line:
 *   nothing here.
 * - The effects brighten to full scale because twice LEVEL, the first try
 *   (the press glow, dropped after canon t-6ttk's scores), was not seen on
 *   hardware: under the keycaps 16 % to 32 % for half a second read as no
 *   change (2026-10-21).
 * - The solid (CONFIG_LANTERN_SOLID, canon t-skdt): every LED shows slot 0,
 *   so the countdown's pictures are the whole half in each shade in turn,
 *   shade 0's first. The node
 *   still carries slots, which this picture does not read (the crossing ring
 *   and the beams do, for their colours).
 * - The gradient (CONFIG_LANTERN_GRADIENT, canon t-skdt): ramp() puts each
 *   key on one line through the shades in slot order by its x on the whole
 *   board (lantern_board(), from the coords): the leftmost key at shade 0,
 *   the rightmost at the
 *   last, and an LED between two shades shows them mixed in proportion; one
 *   on a shade's place shows that shade exactly. Both shades are taken at
 *   full scale and the mix brought to the level after, so a blend's brightest
 *   channel is the picture's level, as a shade's (mixed at that level, two
 *   hues would dim each other). The halves share the board, each showing its
 *   stretch of the one line. A shade shows pure only on a key at its place
 *   (canon: the ends always, the others by the shade count); no key sits
 *   between the halves, so a shade whose place lies there never does. The
 *   countdown's other pictures and the stain's dye are shades
 *   as with the bands. The node
 *   still carries slots, which this picture does not read (the crossing ring
 *   and the beams do, for their colours).
 * - The scatter (CONFIG_LANTERN_SCATTER, canon t-skdt): the picture is the
 *   palette's slots thrown over this half's LEDs at random in place of the
 *   bands, each slot on as many LEDs as its share of the node's
 *   scatter-weights (percent, at most 100 in all; slots 1.. take their
 *   rounded share, slot 0, the heaviest colour, the rest, so what the
 *   shares leave and a rounding remainder land on it; shares that round past
 *   the LED count shorten the last slots), and thrown again every
 *   CONFIG_LANTERN_SCATTER_MS while lit, keys or no keys, and at every light.
 *   The throw runs on the low priority queue (scatter_work, armed at the
 *   light; a tick finding the LEDs dark re-arms nothing, and the blank
 *   cancels nothing: a cancel racing the light's re-arm could leave the item
 *   neither queued nor timed, Opus review 2026-10-21), so it never races the
 *   frame. The light's throw normally queues ahead of the light's frame,
 *   behind one still queued from a blank just before, which then shows the
 *   old throw for one frame. sys_rand32_get() is Zephyr's xoshiro128++ on a
 *   half (CONFIG_XOSHIRO_RANDOM_GENERATOR; thread context, the first call
 *   seeds from the nRF RNG). The node still carries slots, which the bands,
 *   the no picture's boot light, the crossing ring and the beams read, not
 *   this picture. Each half throws for itself:
 *   the two are independent, with the same shares.
 * - The no picture (CONFIG_LANTERN_NO_PICTURE, canon t-skdt): every LED
 *   black while lit, so that only the effects show, over black keys: an
 *   effect pulls an LED from black and fades back to it, and the cap leaves
 *   the effects all its room but what a stain's dye takes (the dye stays at
 *   the picture's level, as over any picture, and the cap counts it as the
 *   picture). The boot light would then show nothing, and its point is to
 *   show that a half has power (canon t-6ttk, the user's choice of
 *   2026-10-19), so the rail rule tells this file the boot light's state at
 *   every light and again at its end with the LEDs lit
 *   (lantern_keys_boot_light(), on the system work queue; booting, an atomic
 *   the frame reads), and while it lasts the countdown's first picture (stage
 *   mod the shade count 0) is the bands, the node's slots; its end sends a
 *   frame, so on USB the keys turn black (on battery the blank follows
 *   anyway). With the countdown on, its first picture is otherwise the black
 *   one and the others the whole half in a shade, as with the bands. The
 *   node still carries slots, which the boot light reads here, and the
 *   crossing ring and the beams for their colours. The intro, when built,
 *   takes the boot light's frames instead.
 * - The intro (CONFIG_LANTERN_INTRO, src/intro.c): while the boot light
 *   lasts, the frame is the intro's, over a black picture for the cap, and
 *   keeps moving until its script ends; no effect is drawn and no press
 *   noted meanwhile (a press noted then would show at its end as a disc
 *   already in flight). The engine starts its clock when the rail rule says
 *   the boot light is on, before the flag the frame reads.
 */

#define DT_DRV_COMPAT lantern_keys

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

#include <zmk/matrix.h>
#include <zmk/workqueue.h>

#if IS_ENABLED(CONFIG_LANTERN_SCATTER)
#include <zephyr/random/random.h>
#endif

#if IS_ENABLED(CONFIG_LANTERN_COUNTDOWN) || IS_ENABLED(CONFIG_LANTERN_EFFECTS)
#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>
#endif

#include "effect.h"
#include "frame.h"
#include "intro.h"
#include "keys.h"
#include "palette.h"

LOG_MODULE_REGISTER(lantern_keys, LOG_LEVEL_INF);

#define NODE DT_DRV_INST(0)
#define STRIP DT_INST_PHANDLE(0, strip)

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

/* The levels are frame.h's (the header). */
BUILD_ASSERT(PICTURE_LEVEL <= LEVEL,
             "CONFIG_LANTERN_PICTURE_BRIGHTNESS above CONFIG_LANTERN_BRIGHTNESS: a still picture "
             "could pass the frame's cap");

#if IS_ENABLED(CONFIG_LANTERN_FRAMES)
/* The moving frames' period; the levels and the cap are frame.h's. */
#define FRAME_MS 30
#endif

#if IS_ENABLED(CONFIG_LANTERN_EFFECTS)
/* The effects only: the intro, over a black picture, has all of CAP as room
 * at any brightness. */
BUILD_ASSERT(HI_LEVEL > LEVEL, "CONFIG_LANTERN_BRIGHTNESS 100 leaves the frame's cap no room for "
                               "an effect, which brightens to full scale");
/* The presses an effect still draws: with the hit (src/hit.c) both halves'
 * presses share the ring, and with the crossing ring's 1.6 s life eight held
 * only five presses a second before the oldest, still on the board, went in
 * one frame (Opus review, 2026-10-26). The limit is HITS over the longest
 * picked life, both halves together: ten presses a second on canon's halves
 * with the crossing ring, eight with the disc, and fewer on a larger board,
 * whose lives are longer (frame.h: lantern_life()); past it the oldest
 * press's effect goes in one frame. A larger ring costs RAM and, through
 * live[HITS] in compose(), the frame thread's stack (canon t-gwsw carries
 * the review of its size). */
#define HITS 16
/* A hit in one atomic: (position + 1) << 24 | seq << HIT_SEQ_SHIFT | the
 * press's time, the low 18 bits of k_uptime_get_32() in ms (262 s). seq is
 * the count of presses mod HIT_SEQ_CYCLE: an effect that gives each press the
 * palette's next shade reads seq mod the shade count, which skips no shade at
 * the wrap while the cycle is a multiple of every count 1..6. */
#define HIT_TIME 0x3FFFFu
#define HIT_SEQ_SHIFT 18
#define HIT_SEQ_MASK 0x3Fu
#define HIT_SEQ_CYCLE 60
BUILD_ASSERT(KEY_COUNT <= 255, "lantern,keys: a hit stores its key position + 1 in a byte");
BUILD_ASSERT(HIT_SEQ_CYCLE <= HIT_SEQ_MASK + 1, "a hit's seq is stored in 6 bits");
BUILD_ASSERT(LANTERN_PALETTE_SLOTS == 6 && HIT_SEQ_CYCLE % 60 == 0,
             "HIT_SEQ_CYCLE must be a multiple of every shade count, 1 to LANTERN_PALETTE_SLOTS");
BUILD_ASSERT(!IS_ENABLED(CONFIG_LANTERN_STAIN) || IS_ENABLED(CONFIG_LANTERN_THIN_RING),
             "CONFIG_LANTERN_STAIN dyes with the thin ring (its Kconfig selects it)");
/* No effect lives past LANTERN_EFFECT_LIFE_MAX_MS (frame.h; each asserts its
 * own), which the ring's fields hold. */
BUILD_ASSERT(LANTERN_EFFECT_LIFE_MAX_MS <= UINT16_MAX, "a hit's age is kept in 16 bits");
BUILD_ASSERT(LANTERN_EFFECT_LIFE_MAX_MS < HIT_TIME / 2,
             "a hit's age must be told from a wrapped time");
/* The pick (the header): what this image draws, and what it draws without
 * one. The Kconfig choice names one built effect; the stain's bit draws the
 * thin ring too, which its Kconfig selects. */
#define EFFECTS_BUILT ((uint32_t)LANTERN_EFFECT_BUILT)
#define EFFECT_DEFAULT ((uint32_t)LANTERN_EFFECT_DEFAULT)
BUILD_ASSERT(EFFECTS_BUILT != 0, "CONFIG_LANTERN_EFFECTS without an effect in LANTERN_EFFECT_LIST");
BUILD_ASSERT(__builtin_popcount(EFFECT_DEFAULT) == 1 && (EFFECT_DEFAULT & EFFECTS_BUILT) != 0,
             "the choice LANTERN_EFFECT_DEFAULT must name one effect this image builds");

/* The table (the header): the effects this image builds, in
 * LANTERN_EFFECT_LIST's order, each its line's bit and its file's entry
 * (src/effects/). */
struct effect_row {
    uint32_t bit;
    const struct lantern_effect_entry *entry;
};
#define LANTERN_EFFECT_EXTERN_(sym, bit, name, ops)                                                \
    extern const struct lantern_effect_entry lantern_effect_##ops;
LANTERN_EFFECT_LIST(LANTERN_EFFECT_EXTERN_)
#undef LANTERN_EFFECT_EXTERN_
#define LANTERN_EFFECT_ROW_(sym, bit, name, ops)                                                   \
    COND_CODE_1(CONFIG_LANTERN_##sym, ({BIT(bit), &lantern_effect_##ops}, ), ())
static const struct effect_row effects[] = {LANTERN_EFFECT_LIST(LANTERN_EFFECT_ROW_)};
#undef LANTERN_EFFECT_ROW_
BUILD_ASSERT(ARRAY_SIZE(effects) == __builtin_popcount(EFFECTS_BUILT),
             "the effects' table: a row per built effect");

/* The pick's bits row e draws for: its line's and those its entry names. */
static uint32_t row_bits(size_t e) { return effects[e].bit | effects[e].entry->serves; }
#endif

#if IS_ENABLED(CONFIG_LANTERN_GEOMETRY)
BUILD_ASSERT(DT_INST_NODE_HAS_PROP(0, coords),
             "what measures the board (CONFIG_LANTERN_GEOMETRY: an effect that measures it, "
             "the gradient or the intro) needs lantern,keys coords");
BUILD_ASSERT(COND_CODE_1(DT_INST_NODE_HAS_PROP(0, coords), (DT_INST_PROP_LEN(0, coords)), (0)) ==
                 2 * KEY_COUNT,
             "lantern,keys: coords needs an x and a y per key position of slots");
/* 30000 keeps a squared distance, 2 x 30000^2, inside 32 bits. */
#define COORD_IN_RANGE(node_id, prop, idx)                                                        \
    BUILD_ASSERT(DT_PROP_BY_IDX(node_id, prop, idx) <= 30000,                                     \
                 "lantern,keys: a coords entry is at most 30000 hundredths of a key unit");
COND_CODE_1(DT_INST_NODE_HAS_PROP(0, coords), (DT_FOREACH_PROP_ELEM(NODE, coords, COORD_IN_RANGE)),
            ())
#endif

#if IS_ENABLED(CONFIG_LANTERN_GEOMETRY)

/* The board (frame.h), measured at the first call (board_init() below, at
 * boot), the spans on squared distances and rooted once; the frame's thread
 * reads it. */
const struct lantern_board *lantern_board(void) {
    static struct lantern_board board;
    static bool measured;

    if (measured) {
        return &board;
    }
    uint16_t left = UINT16_MAX, right = 0, bottom = 0;
    uint32_t span2 = 0, half2 = 0;

    for (size_t k = 0; k < KEY_COUNT; k++) {
        left = MIN(left, key_x(k));
        right = MAX(right, key_x(k));
        bottom = MAX(bottom, key_y(k));
        for (size_t j = k + 1; j < KEY_COUNT; j++) {
            span2 = MAX(span2, distance2((uint8_t)k, (uint8_t)j));
        }
    }
    for (size_t i = 0; i < LED_COUNT; i++) {
        for (size_t j = i + 1; j < LED_COUNT; j++) {
            half2 = MAX(half2, distance2(led_key(i), led_key(j)));
        }
    }
    board = (struct lantern_board){
        .left = left,
        .right = right,
        .bottom = bottom,
        .width = (uint32_t)(right - left),
        .span = isqrt(span2),
        .half_span = isqrt(half2),
    };
    measured = true;
    return &board;
}

/* The measure at boot, on the main thread's stack: the frame's thread (768
 * bytes, shared with ZMK's battery sampling) then only reads it, and the
 * pair loops never sit under an effect's frame. lantern_board() still
 * measures at its first call where no init ran (the host harness). */
static int board_init(void) {
    (void)lantern_board();
    return 0;
}

SYS_INIT(board_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

#endif /* IS_ENABLED(CONFIG_LANTERN_GEOMETRY) */

/* The data line low for this long after a frame, so that the LEDs latch it:
 * a WS2812B needs at least 280 us (its V5 datasheet's RES; the older part
 * 50 us), and a frame that arrives sooner reads as more pixels of the last
 * one and falls off the chain's end. The driver's own pause after a transfer
 * is the strip node's reset-delay, 8 us unless the consumer sets it (zephyr
 * drivers/led_strip/ws2812_spi.c), and a submission during the handler runs
 * it again straight away, with only that pause between the frames. */
#define LATCH_US 300

static const struct device *const strip = DEVICE_DT_GET(STRIP);
/* The node's tables (frame.h declares them for the effects and the intro). */
const uint8_t lantern_positions[LED_COUNT] = DT_INST_PROP(0, positions);
const uint8_t lantern_slots[KEY_COUNT] = DT_INST_PROP(0, slots);
#if IS_ENABLED(CONFIG_LANTERN_GEOMETRY)
/* Each key position's x and y, in hundredths of a key unit (frame.h). */
const uint16_t lantern_coords[2 * KEY_COUNT] = DT_INST_PROP(0, coords);
#endif
#if IS_ENABLED(CONFIG_LANTERN_SCATTER)
BUILD_ASSERT(DT_INST_NODE_HAS_PROP(0, scatter_weights),
             "CONFIG_LANTERN_SCATTER needs lantern,keys scatter-weights: a percentage per slot");
BUILD_ASSERT(COND_CODE_1(DT_INST_NODE_HAS_PROP(0, scatter_weights),
                         (DT_INST_PROP_LEN(0, scatter_weights)), (0)) == LANTERN_PALETTE_SLOTS,
             "lantern,keys: scatter-weights needs one percentage per palette slot");
BUILD_ASSERT(COND_CODE_1(DT_INST_NODE_HAS_PROP(0, scatter_weights),
                         (DT_INST_FOREACH_PROP_ELEM_SEP(0, scatter_weights, DT_PROP_BY_IDX, (+))),
                         (0)) <= 100,
             "lantern,keys: scatter-weights sum to more than 100 (slot 0 takes what the others leave)");
/* Each slot's share of the LEDs, in percent (the header). */
static const uint8_t scatter_weights[LANTERN_PALETTE_SLOTS] = DT_INST_PROP(0, scatter_weights);
/* The frame's thread: the slot each LED shows, thrown by scatter_roll(). */
static uint8_t scatter[LED_COUNT];
#endif
static struct led_rgb pixels[LED_COUNT];
static atomic_t lit;
static atomic_t stage; /* stages since the last key; the picture is stage mod the shade count */
#if IS_ENABLED(CONFIG_LANTERN_NO_PICTURE) || IS_ENABLED(CONFIG_LANTERN_INTRO)
/* the boot light lasts: the intro plays, or the no picture shows the bands (the header) */
static atomic_t booting;
#endif

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

/* The shades of the frame (frame.h): the palette's colours at PICTURE_LEVEL,
 * the distinct ones in slot order. */
static void shades(const uint32_t palette[LANTERN_PALETTE_SLOTS], bool have, struct shades *sh) {
    sh->n = 0;
    for (size_t s = 0; s < LANTERN_PALETTE_SLOTS; s++) {
        uint32_t rgb = slot_rgb(palette, have, s);
        struct led_rgb c = scale(rgb, PICTURE_LEVEL);
        size_t r = 0;
        while (r < sh->n && !same(&sh->at_level[r], &c)) {
            r++;
        }
        if (r == sh->n) {
            sh->at_level[sh->n] = c;
            sh->rgb[sh->n] = rgb;
            sh->n++;
        }
        sh->rank[s] = (uint8_t)r;
    }
}

/* The frame's thread: the shades of the frame being composed. */
static struct shades frame_shades;

#if IS_ENABLED(CONFIG_LANTERN_EFFECTS)
/* The frame's thread: the stage (before the modulo: with one shade the modulo
 * would never move) and the pick the last frame drew, whose move clears the
 * effects' state (the header). */
static size_t drawn_at;
static uint32_t drawn_effects;
#endif

#if IS_ENABLED(CONFIG_LANTERN_GRADIENT)

/* LED i's blend at level (the header says why the mix is at full scale);
 * q is in 1/256 shades along the shades in slot order, by the key's x over
 * the board's width (lantern_board()). */
static struct led_rgb ramp(const struct shades *sh, size_t i, unsigned int level) {
    const struct lantern_board *b = lantern_board();

    if (sh->n == 1 || b->width == 0) {
        return scale(sh->rgb[0], level);
    }
    /* at most 30000 x 5 x 256, inside 32 bits */
    uint32_t q = (uint32_t)(key_x(led_key(i)) - b->left) * (uint32_t)(sh->n - 1) * 256 / b->width;
    size_t k = q >> 8;

    if (k >= sh->n - 1) {
        return scale(sh->rgb[sh->n - 1], level);
    }
    if ((q & 0xFF) == 0) {
        /* the shade itself: the mix at full scale would round it twice */
        return scale(sh->rgb[k], level);
    }
    struct led_rgb m = mix(scale(sh->rgb[k], 255), scale(sh->rgb[k + 1], 255), q & 0xFF);
    return scale(((uint32_t)m.r << 16) | ((uint32_t)m.g << 8) | m.b, level);
}

#elif !IS_ENABLED(CONFIG_LANTERN_NO_PICTURE)

/* The slot LED i shows in the picture: its throw, slot 0, or its key's band. */
static uint8_t base_slot(size_t i) {
#if IS_ENABLED(CONFIG_LANTERN_SCATTER)
    return scatter[i];
#elif IS_ENABLED(CONFIG_LANTERN_SOLID)
    ARG_UNUSED(i);
    return 0;
#elif IS_ENABLED(CONFIG_LANTERN_BANDS)
    return key_slot(led_key(i));
#else
#error "base_slot(): a LANTERN_PICTURE member without its picture here"
#endif
}

#endif /* IS_ENABLED(CONFIG_LANTERN_GRADIENT); the no picture builds no base_slot() */

#if IS_ENABLED(CONFIG_LANTERN_SCATTER)

/* The low priority work queue only (scatter_work). Slots 1.. take their
 * rounded share of the LEDs, slot 0 the rest (the header); then a
 * Fisher-Yates shuffle. */
static void scatter_roll(void) {
    size_t n = 0;

    for (size_t s = 1; s < LANTERN_PALETTE_SLOTS; s++) {
        size_t share = (LED_COUNT * scatter_weights[s] + 50) / 100;
        while (share-- > 0 && n < LED_COUNT) {
            scatter[n++] = (uint8_t)s;
        }
    }
    while (n < LED_COUNT) {
        scatter[n++] = 0;
    }
    for (size_t i = LED_COUNT - 1; i > 0; i--) {
        size_t j = sys_rand32_get() % (i + 1);
        uint8_t t = scatter[i];
        scatter[i] = scatter[j];
        scatter[j] = t;
    }
}

#endif /* IS_ENABLED(CONFIG_LANTERN_SCATTER) */

#if IS_ENABLED(CONFIG_LANTERN_EFFECTS)

/* The ring of the last HITS presses (the header), packed as HIT_TIME says; 0
 * is no hit. Written from any thread: the next index and the count of
 * presses advance by compare-and-set, and the frame clears a spent hit the
 * same way. */
static atomic_t hits[HITS];
static atomic_t hit_next;
static atomic_t hit_seq; /* the count of presses noted, mod HIT_SEQ_CYCLE */

/* counter's value, and counter advanced to the next, mod cycle, by
 * compare-and-set: two threads never take one value. */
static uint32_t take_next(atomic_t *counter, uint32_t cycle) {
    atomic_val_t v;

    do {
        v = atomic_get(counter);
    } while (!atomic_cas(counter, v, (v + 1) % (atomic_val_t)cycle));
    return (uint32_t)v;
}

/* Any thread. A full ring drops the oldest. */
static void hit_note(uint32_t position) {
    uint32_t seq = take_next(&hit_seq, HIT_SEQ_CYCLE);
    uint32_t v = ((position + 1) << 24) | (seq << HIT_SEQ_SHIFT) | (k_uptime_get_32() & HIT_TIME);

    atomic_set(&hits[take_next(&hit_next, HITS)], (atomic_val_t)v);
}

/* Any thread. */
static void hits_clear(void) {
    for (size_t j = 0; j < HITS; j++) {
        atomic_set(&hits[j], 0);
    }
}

/* The frame's thread. The hits within life ms of their press, into live;
 * returns how many. One past it is cleared, unless a newer press took its
 * place meanwhile (the compare-and-set). life is the picked effects'
 * (effects_life()), at most LANTERN_EFFECT_LIFE_MAX_MS: cleared only at the
 * longest built life, a hit the picked effects had finished with sat in the
 * ring until a frame ran, and after HIT_TIME ms of lit silence read as fresh
 * (the header). The clock is read after the hit, so a press noted while this
 * runs is never later than now: read before, the press could be later, and
 * its age would wrap to an expired one. */
static size_t hits_read(struct hit live[HITS], uint32_t life) {
    size_t n = 0;

    for (size_t j = 0; j < HITS; j++) {
        atomic_val_t v = atomic_get(&hits[j]);
        if (v == 0) {
            continue;
        }
        uint32_t now = k_uptime_get_32() & HIT_TIME;
        uint32_t age = (now - ((uint32_t)v & HIT_TIME)) & HIT_TIME;
        if (age >= life) {
            (void)atomic_cas(&hits[j], v, 0);
            continue;
        }
        live[n++] = (struct hit){.key = (uint8_t)(((uint32_t)v >> 24) - 1),
                                 .seq = (uint8_t)(((uint32_t)v >> HIT_SEQ_SHIFT) & HIT_SEQ_MASK),
                                 .age = (uint16_t)age};
    }
    return n;
}

#endif /* IS_ENABLED(CONFIG_LANTERN_EFFECTS) */

#if IS_ENABLED(CONFIG_LANTERN_FRAMES)

/* The frame's thread: the picture under the frame's effects, for the cap
 * (black under the intro). */
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

#endif /* IS_ENABLED(CONFIG_LANTERN_FRAMES) */

#if IS_ENABLED(CONFIG_LANTERN_INTRO)

/* The whole boot light, the black after the script included: compose draws
 * the intro's frame, and a press notes no hit (the header). */
static bool intro_plays(void) { return atomic_get(&booting) != 0; }

#else

static inline bool intro_plays(void) { return false; }

#endif /* IS_ENABLED(CONFIG_LANTERN_INTRO) */

#if IS_ENABLED(CONFIG_LANTERN_EFFECTS)

/* The effects this frame draws (the header): the pick's bits built here, or
 * the default. */
static uint32_t active_effects(void) {
    uint32_t pick;

    if (lantern_effect_get(&pick) && (pick & EFFECTS_BUILT) != 0) {
        return pick & EFFECTS_BUILT;
    }
    return EFFECT_DEFAULT;
}

/* How long after a press the effects in fx still draw: the longest life among
 * the entries that draw for them, each at most LANTERN_EFFECT_LIFE_MAX_MS
 * (frame.h: lantern_life() holds it; the ring's fields are sized for it). */
static uint32_t effects_life(uint32_t fx) {
    uint32_t life = 0;

    for (size_t e = 0; e < ARRAY_SIZE(effects); e++) {
        if (row_bits(e) & fx) {
            uint32_t own = effects[e].entry->life();

            life = MAX(life, MIN(own, LANTERN_EFFECT_LIFE_MAX_MS));
        }
    }
    return life;
}

/* The frame's thread: every entry's state goes (the header). */
static void effects_clear(void) {
    for (size_t e = 0; e < ARRAY_SIZE(effects); e++) {
        if (effects[e].entry->clear != NULL) {
            effects[e].entry->clear();
        }
    }
}

#endif /* IS_ENABLED(CONFIG_LANTERN_EFFECTS) */

/* The frame: the picture of stage at, the picked effects over it and the cap,
 * or the intro's frame while it plays; true while a picked effect still moves
 * or the intro plays. The low priority work queue only: it writes
 * frame_shades, pixels, picture and the effects' state and clears spent hits
 * (the palette, the pick and the hits are atomics, read from there). */
static bool compose(size_t at) {
#if IS_ENABLED(CONFIG_LANTERN_INTRO)
    if (intro_plays()) {
        memset(picture, 0, sizeof(picture));
        bool more = lantern_intro_draw(pixels);
        hold_to_cap();
        return more;
    }
#endif
    uint32_t palette[LANTERN_PALETTE_SLOTS] = {0};
    bool have = IS_ENABLED(CONFIG_LANTERN_PALETTE) && lantern_palette_get(palette);

    shades(palette, have, &frame_shades);
#if IS_ENABLED(CONFIG_LANTERN_EFFECTS)
    uint32_t fx = active_effects();

    if (at != drawn_at || fx != drawn_effects) {
        effects_clear();
        drawn_at = at;
        drawn_effects = fx;
    }
#endif
    at %= frame_shades.n;
#if IS_ENABLED(CONFIG_LANTERN_NO_PICTURE)
    /* the intro, when built, has the boot light's frames (above) */
    bool boot = !IS_ENABLED(CONFIG_LANTERN_INTRO) && atomic_get(&booting) != 0;
#endif
    for (size_t i = 0; i < LED_COUNT; i++) {
#if IS_ENABLED(CONFIG_LANTERN_GRADIENT)
        pixels[i] = at > 0 ? frame_shades.at_level[at] : ramp(&frame_shades, i, PICTURE_LEVEL);
#elif IS_ENABLED(CONFIG_LANTERN_NO_PICTURE)
        /* black, but the bands while the boot light lasts (the header) */
        pixels[i] = at > 0  ? frame_shades.at_level[at]
                    : boot ? frame_shades.at_level[frame_shades.rank[key_slot(led_key(i))]]
                           : (struct led_rgb){0};
#else
        pixels[i] = frame_shades.at_level[at > 0 ? at : frame_shades.rank[base_slot(i)]];
#endif
    }

#if IS_ENABLED(CONFIG_LANTERN_EFFECTS)
    for (size_t e = 0; e < ARRAY_SIZE(effects); e++) {
        if ((row_bits(e) & fx) && effects[e].entry->picture != NULL) {
            effects[e].entry->picture(pixels, &frame_shades);
        }
    }

    struct hit live[HITS];
    size_t n = hits_read(live, effects_life(fx));

    memcpy(picture, pixels, sizeof(pixels));
    for (size_t e = 0; e < ARRAY_SIZE(effects); e++) {
        if ((row_bits(e) & fx) && effects[e].entry->draw != NULL) {
            effects[e].entry->draw(pixels, &frame_shades, live, n, fx);
        }
    }
    hold_to_cap();
    /* every live hit is within the picked effects' life (hits_read) */
    return n > 0;
#else
    return false;
#endif
}

static void paint(struct k_work *work);

static K_WORK_DEFINE(paint_work, paint);
#if IS_ENABLED(CONFIG_LANTERN_FRAMES)
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
#if IS_ENABLED(CONFIG_LANTERN_EFFECTS)
        effects_clear();
#endif
    }
    int rc = led_strip_update_rgb(strip, pixels, LED_COUNT);
    if (rc < 0) {
        LOG_ERR("frame not written: %d", rc);
    }
    k_usleep(LATCH_US);

#if IS_ENABLED(CONFIG_LANTERN_FRAMES)
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

#if IS_ENABLED(CONFIG_LANTERN_SCATTER)

static void scatter_tick(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(scatter_work, scatter_tick);

/* The low priority work queue only: a new throw and a frame, and the next
 * throw CONFIG_LANTERN_SCATTER_MS on, while lit; a tick that finds the LEDs
 * dark re-arms nothing, so the item runs at most twice after a blank, a
 * queued run and a timed one (the header says why the blank does not cancel
 * it). */
static void scatter_tick(struct k_work *work) {
    ARG_UNUSED(work);
    if (!atomic_get(&lit)) {
        return;
    }
    scatter_roll();
    k_work_reschedule_for_queue(zmk_workqueue_lowprio_work_q(), &scatter_work,
                                K_MSEC(CONFIG_LANTERN_SCATTER_MS));
    submit();
}

/* The light: a fresh throw, normally ahead of the light's frame on the low
 * priority queue (behind a frame still queued from a blank just before). */
static void scatter_start(void) {
    k_work_reschedule_for_queue(zmk_workqueue_lowprio_work_q(), &scatter_work, K_NO_WAIT);
}

#else

static void scatter_start(void) {}

#endif /* IS_ENABLED(CONFIG_LANTERN_SCATTER) */

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
     * half) the other half's keys arrive here too, and no LED here shows them;
     * none while the intro plays (the header). */
    if (ev->state && ev->source == ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL &&
        ev->position < KEY_COUNT && atomic_get(&lit) && !intro_plays()) {
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

#if IS_ENABLED(CONFIG_LANTERN_EFFECTS)

/* Any of ZMK's threads (keys.h): a relayed press is a hit as the half's own,
 * under the same rule as the listener's, and no key of the countdown's clock.
 * ZMK's threads are cooperative: a preemptible caller blanked between the
 * check and the note could leave a hit for the next light. */
void lantern_keys_hit(uint8_t position) {
    if (position < KEY_COUNT && atomic_get(&lit) && !intro_plays()) {
        hit_note(position);
        submit();
    }
}

/* Any thread (keys.h): the hits go now, the effects' state at the frame this
 * sends. */
void lantern_keys_effect_changed(void) {
    hits_clear();
    if (atomic_get(&lit)) {
        submit();
    }
}

#endif

void lantern_keys_light(void) {
    atomic_set(&lit, 1);
    countdown_start();
    scatter_start();
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

#if IS_ENABLED(CONFIG_LANTERN_NO_PICTURE) || IS_ENABLED(CONFIG_LANTERN_INTRO)

/* System work queue (the rail rule): before every light, the boot light's
 * state (on only for the boot light's, whose frames then play the intro or
 * show the no picture's bands; dark then, so no frame here), and off again
 * at the boot light's end with the LEDs lit, which sends a frame. */
void lantern_keys_boot_light(bool on) {
#if IS_ENABLED(CONFIG_LANTERN_INTRO)
    if (on) {
        /* before the flag, so that a frame that sees the flag sees this start */
        lantern_intro_start();
    }
#endif
    atomic_set(&booting, on ? 1 : 0);
    if (atomic_get(&lit)) {
        submit();
    }
}

#endif
