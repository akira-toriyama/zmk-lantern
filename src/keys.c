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
 * moves after a key, of which the pick (src/effect.c, canon t-5hqx) says
 * which draw. This file is the strip's one writer. A frame goes out
 * when something changed: when the rail rule lights or blanks the LEDs
 * (src/on_usb_power.c), when the palette changes while they are lit, when the
 * countdown moves a stage, at each throw of the scatter, when the boot light
 * ends (the no picture's or the intro's), and every FRAME_MS while an effect
 * moves or the intro plays; a still picture costs no tick.
 *
 * - Why the module writes the strip itself and ZMK's underglow is asserted
 *   off: rgb_underglow.c repaints every LED with its one colour and effect at
 *   each 50 ms tick while on, so a per-LED picture cannot share the strip
 *   with it, and it saves its state (on, colour) and restores it at the next
 *   boot, which the rail rule had to override every time (zmk-lantern up to
 *   2406809 lit the LEDs through it). With it off nothing else touches the
 *   strip, and a frame is written only when something changed.
 * - The picture: LED i shows the colour of one slot, base_slot(i), which the
 *   Kconfig choice LANTERN_PICTURE makes slots[positions[i]] (the bands, the
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
 *   never wraps into a live one (cleared only at HIT_LIFE_MS, the longest
 *   built, a hit the picked effects had finished with sat in the ring until
 *   a frame ran, and after 262 s of lit silence read as fresh: Opus review,
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
 *   (lantern_keys_effect_changed()) and the frame clears the stain's dye,
 *   so a ring half-way out does not turn into the new effect's shape. An
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
 * - The effects brighten to full scale because twice LEVEL, the first try
 *   (the press glow, dropped after canon t-6ttk's scores), was not seen on
 *   hardware: under the keycaps 16 % to 32 % for half a second read as no
 *   change (2026-10-21).
 * - The thin ring (CONFIG_LANTERN_THIN_RING): from a pressed key a ring one
 *   key wide spreads at RING_SPEED key units a second and fades out over
 *   RING_MS, so it has crossed the half when it is gone; each press takes
 *   the palette's next shade (the press's number mod the shade count), at
 *   HI_LEVEL, and an LED under two rings shows the stronger one's colour at
 *   their summed strength. The idea is zmk-rgb-matrix's ripple, written from
 *   its description (its code is no clean room, its author says). Distances
 *   come from the consumer's coords, each key position's x and y in
 *   hundredths of a key unit (canon: imprint.json, keymap-drawer's physical
 *   layout, each key's top left corner; every key is one unit, so the offset
 *   to the centre cancels), so the ring is round on the board, not in the
 *   matrix. An integer square root a hit an LED a frame, 8 x 29 of them every
 *   FRAME_MS, costs a few percent of the CPU at most with eight live hits
 *   (about 0.7 ms a frame from the disassembly, Opus review 2026-10-21;
 *   unmeasured) on the preemptible low priority thread, and keeps the FPU
 *   out of it (above). The coords, the square root and the distance are
 *   CONFIG_LANTERN_GEOMETRY's, which each effect that measures the board
 *   selects, the intro for the coords and the square root, and the gradient
 *   for the coords alone.
 * - The disc (CONFIG_LANTERN_DISC): from a pressed key a rim spreads at
 *   DISC_SPEED key units a second, and each LED it has passed starts at
 *   HI_LEVEL and fades back to the picture over DISC_FADE_MS, so the disc is
 *   brightest at its rim and dark behind it; inside it the palette's shades
 *   pass in slot order, the LED's colour running once around the cycle
 *   through the distinct shades while it fades, shade 0 at the rim.
 *   QMK's SPLASH written from its idea (QMK is GPL: nothing copied). An LED
 *   under two discs shows the stronger one's colour at their summed strength,
 *   as under two rings. A hit lives DISC_MS, long enough for the rim to cross
 *   a half of the Imprint and the last LED to fade, or the whole board where
 *   the hit (src/hit.c) brings the other half's presses, whose discs come in
 *   across the split; the cap (above) holds a disc, which covers many LEDs
 *   at once, harder than it holds a ring.
 * - The stain (CONFIG_LANTERN_STAIN): the thin ring dyes the keys it passes.
 *   An LED within STAIN_REACH of the press keeps the ring's shade as its
 *   picture colour, at the picture's level like a band, from the frame in
 *   which the ring's crest reaches it, so the half is painted over in the
 *   day's colours as the user types; zmk-pk-underglow's rainbow ripple, from
 *   its idea. The reach is where the crest still has STAIN_LEAST of full
 *   strength (the ring fades as it spreads), the design's 0.6; it is judged
 *   by the crest's position, not by the strength a frame happens to sample,
 *   because the crest is narrow and frames FRAME_MS apart miss it beyond
 *   about three units (Opus review, 2026-10-21). The newest ring to have
 *   passed an LED dyes it. The dye is the frame thread's alone (stains[],
 *   written where the ring is drawn) and goes when the countdown moves the
 *   picture (the next key brings it back, and its ring dyes it afresh), when
 *   the pick changes and when the LEDs blank. In the pick the stain is the
 *   thin ring that dyes: its bit draws the ring, the thin ring's bit the ring
 *   alone, and both together the dyeing ring. It indexes the shades, so a new palette recolours
 *   the dye with it, and a dye past the new shade count shows the picture.
 * - The crossing ring (CONFIG_LANTERN_CROSS_RING): the thin ring's shape, one
 *   key wide at CROSS_SPEED key units a second, from a pressed key of either
 *   half across the whole board: a press of this half spreads out through
 *   the other half, and a press of the other half, relayed by the central
 *   (src/hit.c), comes in at the thumbs (the two clusters sit 1.5 units
 *   apart) and then the inner columns. It keeps its strength
 *   until it has left the board (CROSS_MS covers canon's Imprint, whose
 *   farthest two keys are 16.8 units apart; a wider board cuts it short),
 *   where the thin ring fades within a half, and its colour is the pressed
 *   key's band, the node's slots, so that both halves draw one press in one
 *   colour, where the thin ring's count of presses differs between them. A
 *   relayed press reaches the other half a few connection events late
 *   (7.5 ms each on canon's link, which a USB-powered half attends every one
 *   of, src/latency.c), a tenth of a key unit each at the ring's speed;
 *   nothing backdates it (canon t-6ttk: a toy, optimistic is fine).
 * - The beams (CONFIG_LANTERN_BEAMS, canon t-6ttk): from a pressed key four
 *   beams run outward at BEAMS_SPEED key units a second, two along its row
 *   and two along its column, each full at its head and gone BEAMS_WIDTH from
 *   it, fading out over BEAMS_MS as the thin ring does. A key is on the row
 *   or the column when its y or x is within BEAMS_LINE of the pressed key's:
 *   canon's rows and columns sit a unit apart, and its thumbs half a unit off
 *   the bottom row and the inner column, so the thumb clusters are two rows
 *   of three and three columns of two, and the two clusters share their
 *   rows, a unit and a half apart. QMK's cross and nexus from their idea
 *   (nothing copied). The colour is the pressed key's band, as the crossing
 *   ring's, so a beam that crosses the split keeps its colour: with the hit
 *   a press of the other half sends its row's beam in here, from an inner
 *   column at about half strength (the inner columns of canon's Imprint are
 *   6.5 units apart, 542 ms at 12; 117 of 256 at the head, less by the
 *   frame's phase), from an outer one all but spent (the outermost's 10 of
 *   255), and from a thumb nearly at full strength, the strongest crossing;
 *   the column's beams never leave their half. The design's fade of 0.6 s had the row's
 *   beam die in the gap (the inner column's reaching the other half at a
 *   tenth of its strength); a second, the thin ring's, lets it arrive. An
 *   LED under two beams takes the strongest one's colour at their summed
 *   strength. Along a line the distance is the difference of one coordinate,
 *   so the beams take no square root.
 * - The water (CONFIG_LANTERN_WATER, canon t-6ttk): from a pressed key waves
 *   spread, a front at WATER_SPEED key units a second and behind it the
 *   sine of the front's distance past each key over WATER_WAVE, fading out
 *   over WATER_MS and falling off with the distance from the key (by
 *   WATER_FALL), and the waves of every live press add up at each LED, so
 *   that two crests meeting rise higher and a crest meeting a trough stays
 *   still: zmk-advanced-underglow-effects-vfx's water, from its idea
 *   (nothing copied). The LED is pulled toward span(), the shades end to
 *   end, by the surface's height, a crest to shade 0 and a trough to the
 *   last shade, and as far as the height's size, so the still surface shows
 *   the picture (the no picture's black) and a key under the waves flickers
 *   between the two colours, once a wavelength (six times a second at the
 *   design's speed and wavelength). The design's px = G(0.5 - 0.5 w), over
 *   a lit picture, gave the still surface the middle shade; over the no
 *   picture's black the height's size is the brightness, or a still
 *   surface would glow (the one change from the design). With the hit the
 *   other half's presses start waves too, which come in across the split
 *   faint (from an inner column at about a fifth of a crest, 6.5 units
 *   off). The sine is a quarter-wave table of 65 entries folded (sine());
 *   the distance and its square root are the geometry's, as the thin
 *   ring's.
 * - The solid (CONFIG_LANTERN_SOLID, canon t-skdt): every LED shows slot 0,
 *   so the countdown's pictures are the whole half in each shade in turn,
 *   shade 0's first. The node
 *   still carries slots, which this picture does not read (the crossing ring
 *   and the beams do, for their colours).
 * - The gradient (CONFIG_LANTERN_GRADIENT, canon t-skdt): ramp() puts each
 *   key on one line through the shades in slot order by its x on the whole
 *   board (the coords): the leftmost key at shade 0, the rightmost at the
 *   last, and an LED between two shades shows them mixed in proportion; one
 *   on a shade's place shows that shade exactly. Both shades are taken at
 *   full scale and the mix brought to the level after, so a blend's brightest
 *   channel is the picture's level, as a shade's (mixed at that level, two
 *   hues would dim each other). The halves share the board, each showing its
 *   stretch of the one line. A shade shows pure only on a key at its place
 *   (canon: the ends always, the others by the shade count); no key sits
 *   between the halves, so a shade whose place lies there never does. The
 *   board's ends are measured once, at the first frame that shows the
 *   gradient. The countdown's other pictures and the stain's dye are shades
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
 *   crossing ring and the beams for their colours. The intro (below), when built, takes the
 *   boot light's frames instead.
 * - The intro (CONFIG_LANTERN_INTRO, canon t-5v1f): a show in place of the
 *   boot light's picture, LANTERN_INTRO_MS long (keys.h), which the rail
 *   rule plays at boot and whenever USB power arrives (src/on_usb_power.c),
 *   in red, blue and green up to full scale, not the palette's colours (the
 *   user's pick and order, after Pokemon's first games), and in no other
 *   colour but the star's white head: on hardware the user found the first
 *   script's end, a white burst leaving a rainbow, unrelated to the three
 *   and asked for other colours a little at most (2026-10-22). Each half
 *   plays it alone, mirrored: an LED's depth runs from 0 at the half's
 *   outermost key to DEPTH_ONE at its innermost, by its key's distance
 *   (coords) from the bottom of the board's middle, the point between the
 *   two thumb clusters, so the show moves from the outer columns' top to the
 *   inner thumb. The script, in order: the star, a white head crossing the
 *   half in STAR_MS, each key it passes glittering for GLITTER_MS, every
 *   beat a primary or none at random (the dice: an integer hash of the LED,
 *   the beat and a seed taken at the start, so the frame keeps no state for
 *   it); the waves, red, blue and green WAVE_GAP_MS apart, each front
 *   crossing in WAVE_MS with a fading tail; the core, the deepest keys, each
 *   in the colour of the newest wave to reach it, so that a wave coming into
 *   the core never meets another colour there, with a flash at each wave's
 *   arrival, then pulsing for CHARGE_MS a colour a beat, the last wave's
 *   green, red, blue, green, and swelling outwards; and the burst, a red, a
 *   blue and a green ring from the core out BURST_GAP_MS apart, each
 *   crossing in BURST_MS, the last leaving each key it passes in its third
 *   of the depths, the outer red, the middle blue and the inner green (the
 *   waves' order from the outside), held THIRDS_HOLD_MS and faded out at
 *   once over THIRDS_FADE_MS, by the end, the script's only stretch with
 *   every key lit. A key shows one primary, the strongest of the light on
 *   it (the beat's alone on the charging core's keys), light of one colour
 *   adding up to full scale, so that no retiming of the acts can mix two;
 *   the star's white goes over it, tinted where the head meets the glitter.
 *   The frame's cap over a black picture holds the whole. Each frame is
 *   composed from the time since the start, so a late frame loses nothing;
 *   the depths are measured once, at the first frame. No effect is drawn
 *   and no press noted while it plays: a press noted then would show at its
 *   end as a disc already in flight.
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

#if IS_ENABLED(CONFIG_LANTERN_SCATTER)
#include <zephyr/random/random.h>
#endif

#if IS_ENABLED(CONFIG_LANTERN_COUNTDOWN) || IS_ENABLED(CONFIG_LANTERN_EFFECTS)
#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>
#endif

#include "effect.h"
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

/* The device's brightness as a channel value, which the frame's cap counts
 * from, and the picture's, at most it (the header). */
#define LEVEL ((255 * CONFIG_LANTERN_BRIGHTNESS + 50) / 100)
#define PICTURE_LEVEL ((255 * CONFIG_LANTERN_PICTURE_BRIGHTNESS + 50) / 100)
BUILD_ASSERT(PICTURE_LEVEL <= LEVEL,
             "CONFIG_LANTERN_PICTURE_BRIGHTNESS above CONFIG_LANTERN_BRIGHTNESS: a still picture "
             "could pass the frame's cap");

#if IS_ENABLED(CONFIG_LANTERN_FRAMES)
/* The brightest channel of an effect's colour: full scale (the header says
 * why not twice the picture's). */
#define HI_LEVEL 255
#define FRAME_MS 30
/* The channel sum no frame exceeds: every LED white at LEVEL, plus CAP_KEYS
 * LEDs white at HI_LEVEL instead (the header). */
#define CAP_KEYS 2
#define CAP (3 * (LED_COUNT * LEVEL + CAP_KEYS * (HI_LEVEL - LEVEL)))
#endif

#if IS_ENABLED(CONFIG_LANTERN_EFFECTS)
/* The effects only: the intro, over a black picture, has all of CAP as room
 * at any brightness. */
BUILD_ASSERT(HI_LEVEL > LEVEL, "CONFIG_LANTERN_BRIGHTNESS 100 leaves the frame's cap no room for "
                               "an effect, which brightens to full scale");
/* The presses an effect still draws: with the hit (src/hit.c) both halves'
 * presses share the ring, and the crossing ring lives CROSS_MS, so eight held
 * only five presses a second before the oldest, still on the board, went in
 * one frame (Opus review, 2026-10-26). */
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
#endif

/* Each effect's life, how long after the press it still draws; 0 when it is
 * not built. A hit lives the picked effects' (hits_read()); HIT_LIFE_MS, the
 * longest built, bounds it (the asserts below). */
#if IS_ENABLED(CONFIG_LANTERN_THIN_RING)
#define RING_MS 1000
/* Key units a second the ring's radius grows: at 12 it has crossed a half of
 * the Imprint (8.75 units at most) in under a second. */
#define RING_SPEED 12
/* Hundredths of a key unit from the radius at which the ring is gone: full
 * on the radius, one key wide at half strength. */
#define RING_WIDTH 100
#else
#define RING_MS 0
#endif
#if IS_ENABLED(CONFIG_LANTERN_DISC)
/* Key units a second the disc's rim spreads, the ring's speed. */
#define DISC_SPEED 12
/* How long an LED the rim has passed takes to fade back to the picture. */
#define DISC_FADE_MS 500
/* A disc's life: the rim across a half of the Imprint (8.75 units, 729 ms at
 * 12) and the last LED's fade; with the hit (src/hit.c) the other half's
 * presses come in too, and the rim must cross the whole board (16.8 units,
 * 1397 ms) before the last LED fades, or the far keys went black mid-fade
 * (Opus review, 2026-10-26). */
#if IS_ENABLED(CONFIG_LANTERN_HIT)
#define DISC_MS 1900
#else
#define DISC_MS 1500
#endif
#else
#define DISC_MS 0
#endif
#if IS_ENABLED(CONFIG_LANTERN_CROSS_RING)
/* Key units a second the crossing ring's radius grows, the thin ring's
 * speed; full on the radius and gone CROSS_WIDTH hundredths of a unit from
 * it, one key wide. */
#define CROSS_SPEED 12
#define CROSS_WIDTH 100
/* How long a hit draws the crossing ring: until the ring has passed the
 * farthest key of canon's Imprint from any key (16.8 units) by its width,
 * 1.48 s at 12 (the header). */
#define CROSS_MS 1600
#else
#define CROSS_MS 0
#endif
#if IS_ENABLED(CONFIG_LANTERN_BEAMS)
/* Key units a second a beam's head runs, the thin ring's speed; full at the
 * head and gone BEAMS_WIDTH hundredths of a unit from it, the design's 0.8. */
#define BEAMS_SPEED 12
#define BEAMS_WIDTH 80
/* A key is on the pressed key's row or column within this many hundredths of
 * a unit of its y or x (the header). */
#define BEAMS_LINE 50
/* How long a hit draws the beams, fading out over it: the design's 600 had
 * the row's beam die between canon's halves (the header). */
#define BEAMS_MS 1000
#else
#define BEAMS_MS 0
#endif
#if IS_ENABLED(CONFIG_LANTERN_WATER)
/* Key units a second the water's front spreads, the thin ring's speed, and
 * the wavelength behind it in hundredths of a unit, the design's 2.0. */
#define WATER_SPEED 12
#define WATER_WAVE 200
/* How long a hit draws the water, its waves fading out over it: the
 * design's 1.5 s. */
#define WATER_MS 1500
/* The waves' fall-off with the distance from the pressed key: a wave at d
 * hundredths of a unit is 1 / (1 + WATER_FALL x d / 1000) of one at the key,
 * the design's 1 / (1 + 0.3 d) in key units. */
#define WATER_FALL 3
#else
#define WATER_MS 0
#endif
#define HIT_LIFE_MS MAX(RING_MS, MAX(DISC_MS, MAX(CROSS_MS, MAX(BEAMS_MS, WATER_MS))))
#if IS_ENABLED(CONFIG_LANTERN_STAIN)
/* The least strength, of 256, a ring's crest must still have to dye: the
 * design's 0.6. */
#define STAIN_LEAST 154
/* How far the crest keeps that strength, in hundredths of a key unit: it
 * reaches distance d at age d x 10 / RING_SPEED, with 256 x (RING_MS - age)
 * / RING_MS of its strength (the header). */
#define STAIN_REACH ((RING_MS - (RING_MS * STAIN_LEAST + 255) / 256) * RING_SPEED / 10)
BUILD_ASSERT(IS_ENABLED(CONFIG_LANTERN_THIN_RING),
             "CONFIG_LANTERN_STAIN dyes with the thin ring (its Kconfig selects it)");
BUILD_ASSERT(STAIN_REACH > 0, "STAIN_LEAST leaves the ring no reach");
#endif

#if IS_ENABLED(CONFIG_LANTERN_EFFECTS)
BUILD_ASSERT(HIT_LIFE_MS > 0, "CONFIG_LANTERN_EFFECTS without an effect: an effect sets its life");
BUILD_ASSERT(HIT_LIFE_MS <= UINT16_MAX, "a hit's age is kept in 16 bits");
BUILD_ASSERT(HIT_LIFE_MS < HIT_TIME / 2, "a hit's age must be told from a wrapped time");
/* The pick (the header): what this image draws, and what it draws without
 * one. The Kconfig choice names one built effect; the stain's bit draws the
 * thin ring too, which its Kconfig selects. */
#define EFFECTS_BUILT ((uint32_t)LANTERN_EFFECT_BUILT)
#define EFFECT_DEFAULT ((uint32_t)LANTERN_EFFECT_DEFAULT)
BUILD_ASSERT(EFFECTS_BUILT != 0, "CONFIG_LANTERN_EFFECTS without an effect in LANTERN_EFFECT_LIST");
BUILD_ASSERT(__builtin_popcount(EFFECT_DEFAULT) == 1 && (EFFECT_DEFAULT & EFFECTS_BUILT) != 0,
             "the choice LANTERN_EFFECT_DEFAULT must name one effect this image builds");
#endif

#if IS_ENABLED(CONFIG_LANTERN_GEOMETRY)
BUILD_ASSERT(DT_INST_NODE_HAS_PROP(0, coords),
             "what measures the board (CONFIG_LANTERN_GEOMETRY: the thin ring, the disc, the "
             "crossing ring, the beams, the water, the gradient or the intro) needs lantern,keys "
             "coords");
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

/* The data line low for this long after a frame, so that the LEDs latch it:
 * a WS2812B needs at least 280 us (its V5 datasheet's RES; the older part
 * 50 us), and a frame that arrives sooner reads as more pixels of the last
 * one and falls off the chain's end. The driver's own pause after a transfer
 * is the strip node's reset-delay, 8 us unless the consumer sets it (zephyr
 * drivers/led_strip/ws2812_spi.c), and a submission during the handler runs
 * it again straight away, with only that pause between the frames. */
#define LATCH_US 300

static const struct device *const strip = DEVICE_DT_GET(STRIP);
/* Without an effect the solid and the scatter read neither table and the
 * gradient positions alone; the no picture reads both for its boot light (the
 * asserts above still check both). */
static const uint8_t positions[LED_COUNT] __maybe_unused = DT_INST_PROP(0, positions);
static const uint8_t slots[KEY_COUNT] __maybe_unused = DT_INST_PROP(0, slots);
#if IS_ENABLED(CONFIG_LANTERN_GEOMETRY)
/* Each key position's x and y, in hundredths of a key unit (the header). */
static const uint16_t coords[2 * KEY_COUNT] = DT_INST_PROP(0, coords);
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
#if IS_ENABLED(CONFIG_LANTERN_INTRO)
static atomic_t intro_t0;   /* k_uptime_get_32() at the intro's start */
static atomic_t intro_seed; /* the glitter's seed, k_cycle_get_32() then */
#endif

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

#if IS_ENABLED(CONFIG_LANTERN_EFFECTS) || IS_ENABLED(CONFIG_LANTERN_GRADIENT)
/* a pulled toward b by w / 256. */
static struct led_rgb mix(struct led_rgb a, struct led_rgb b, unsigned int w) {
    return (struct led_rgb){
        .r = (uint8_t)(a.r + ((int)b.r - (int)a.r) * (int)w / 256),
        .g = (uint8_t)(a.g + ((int)b.g - (int)a.g) * (int)w / 256),
        .b = (uint8_t)(a.b + ((int)b.b - (int)a.b) * (int)w / 256),
    };
}
#endif

/* The 0xRRGGBB slot s paints: a cleared slot paints slot 0's colour, and
 * without a palette every slot paints white. */
static uint32_t slot_rgb(const uint32_t palette[LANTERN_PALETTE_SLOTS], bool have, size_t s) {
    if (!have) {
        return 0xFFFFFF;
    }
    return palette[s] != 0 ? palette[s] : palette[0];
}

/* The shades a frame is made of: the palette's colours at PICTURE_LEVEL, the
 * distinct ones in slot order (a cleared slot is slot 0's; without a palette
 * one white), each with the 0xRRGGBB it was scaled from, for an effect that
 * paints it at another level and for the gradient's full-scale blend, and
 * each slot's rank among them. */
struct shades {
    size_t n; /* 1..LANTERN_PALETTE_SLOTS */
    struct led_rgb at_level[LANTERN_PALETTE_SLOTS];
    uint32_t rgb[LANTERN_PALETTE_SLOTS];
    uint8_t rank[LANTERN_PALETTE_SLOTS];
};

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

#if IS_ENABLED(CONFIG_LANTERN_STAIN)
/* The frame's thread: each LED's dye, a shade's rank + 1, 0 for none (the
 * header), and the stage (before the modulo: with one shade the modulo would
 * never move) and the pick the last frame showed, whose move clears the dye. */
static uint8_t stains[LED_COUNT];
static size_t stained_at;
static uint32_t drawn_effects;
#endif

#if IS_ENABLED(CONFIG_LANTERN_GRADIENT)

/* The frame's thread: the leftmost key's x and the board's width to the
 * rightmost key's, measured from coords at the first frame that shows the
 * gradient. */
static bool ramp_measured;
static uint16_t ramp_left, ramp_width;

/* LED i's blend at level (the header says why the mix is at full scale);
 * q is in 1/256 shades along the shades in slot order. */
static struct led_rgb ramp(const struct shades *sh, size_t i, unsigned int level) {
    if (!ramp_measured) {
        uint16_t lo = UINT16_MAX, hi = 0;

        for (size_t k = 0; k < KEY_COUNT; k++) {
            lo = MIN(lo, coords[2 * k]);
            hi = MAX(hi, coords[2 * k]);
        }
        ramp_left = lo;
        ramp_width = hi - lo;
        ramp_measured = true;
    }
    if (sh->n == 1 || ramp_width == 0) {
        return scale(sh->rgb[0], level);
    }
    /* at most 30000 x 5 x 256, inside 32 bits */
    uint32_t q = (uint32_t)(coords[2 * positions[i]] - ramp_left) * (uint32_t)(sh->n - 1) * 256 /
                 ramp_width;
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
    return slots[positions[i]];
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

struct hit {
    uint8_t key;  /* a key position */
    uint8_t seq;  /* the press's number, mod HIT_SEQ_CYCLE */
    uint16_t age; /* ms since the press, below the picked effects' life */
};

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
 * (effects_life()), at most HIT_LIFE_MS: cleared only at HIT_LIFE_MS, a hit
 * the picked effects had finished with sat in the ring until a frame ran,
 * and after HIT_TIME ms of lit silence read as fresh (the header). The clock
 * is read after the hit, so a press noted while this runs is never later
 * than now: read before, the press could be later, and its age would wrap to
 * an expired one. */
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

/* Shade r at HI_LEVEL. */
static struct led_rgb shade_hi(const struct shades *sh, size_t r) {
    return scale(sh->rgb[r], HI_LEVEL);
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

#if IS_ENABLED(CONFIG_LANTERN_GEOMETRY)

/* The integer square root of v, rounded down, bit by bit. The gradient
 * selects the geometry for coords alone and measures no distance. */
static uint32_t __maybe_unused isqrt(uint32_t v) {
    uint32_t root = 0, bit = 1u << 30;

    while (bit > v) {
        bit >>= 2;
    }
    while (bit != 0) {
        if (v >= root + bit) {
            v -= root + bit;
            root = (root >> 1) + bit;
        } else {
            root >>= 1;
        }
        bit >>= 2;
    }
    return root;
}

/* Hundredths of a key unit between key positions a and b. */
static uint32_t __maybe_unused distance(uint8_t a, uint8_t b) {
    int32_t dx = (int32_t)coords[2 * a] - (int32_t)coords[2 * b];
    int32_t dy = (int32_t)coords[2 * a + 1] - (int32_t)coords[2 * b + 1];

    return isqrt((uint32_t)(dx * dx + dy * dy));
}

#endif /* IS_ENABLED(CONFIG_LANTERN_GEOMETRY) */

#if IS_ENABLED(CONFIG_LANTERN_THIN_RING)

/* Each hit's ring (the header): its radius RING_SPEED x the hit's age, full
 * on the radius and gone RING_WIDTH from it, fading out over RING_MS, in the
 * shade the hit's number picks. An LED under several rings takes the
 * strongest one's colour at their summed strength. With stain, the ring dyes
 * the LEDs its crest reaches (the stain's bit in the pick). */
static void thin_ring(const struct shades *sh, const struct hit live[], size_t n, bool stain) {
#if !IS_ENABLED(CONFIG_LANTERN_STAIN)
    ARG_UNUSED(stain);
#endif
    for (size_t i = 0; i < LED_COUNT; i++) {
        unsigned int w = 0, strongest = 0;
        size_t pick = 0;
#if IS_ENABLED(CONFIG_LANTERN_STAIN)
        size_t dye = 0; /* the newest hit whose crest has reached this LED within STAIN_REACH, + 1 */
#endif

        for (size_t h = 0; h < n; h++) {
            if (live[h].age >= RING_MS) {
                continue;
            }
            /* ms x units/s / 1000 x 100 hundredths */
            uint32_t r = (uint32_t)live[h].age * RING_SPEED / 10;
            uint32_t d = distance(positions[i], live[h].key);
#if IS_ENABLED(CONFIG_LANTERN_STAIN)
            if (stain && r >= d && d <= STAIN_REACH &&
                (dye == 0 || live[h].age < live[dye - 1].age)) {
                dye = h + 1;
            }
#endif
            uint32_t off = d > r ? d - r : r - d;
            if (off >= RING_WIDTH) {
                continue;
            }
            unsigned int s = (RING_WIDTH - off) * 256 / RING_WIDTH;
            s = s * (RING_MS - live[h].age) / RING_MS;
            if (s > strongest) {
                strongest = s;
                pick = h;
            }
            w += s;
        }
        if (w > 0) {
            pixels[i] = mix(pixels[i], shade_hi(sh, live[pick].seq % sh->n), MIN(w, 256));
        }
#if IS_ENABLED(CONFIG_LANTERN_STAIN)
        if (dye != 0) {
            stains[i] = (uint8_t)(live[dye - 1].seq % sh->n + 1);
        }
#endif
    }
}

#endif /* IS_ENABLED(CONFIG_LANTERN_THIN_RING) */

#if IS_ENABLED(CONFIG_LANTERN_DISC)

/* The cycle through the shades at HI_LEVEL: p, 0..255, is how far around,
 * shade 0 at 0 and again past 255, each shade an equal arc, linear between
 * neighbours. */
static struct led_rgb cycle(const struct shades *sh, unsigned int p) {
    unsigned int q = p * sh->n; /* 1/256 shades around; q >> 8 < n since p < 256 */

    return mix(shade_hi(sh, q >> 8), shade_hi(sh, ((q >> 8) + 1) % sh->n), q & 0xFF);
}

/* Each hit's disc (the header): an LED the rim, DISC_SPEED x the hit's age
 * out, reached tau ms ago is pulled toward cycle(tau / DISC_FADE_MS) with
 * strength 1 - tau / DISC_FADE_MS. An LED under several discs takes the
 * strongest one's colour at their summed strength. */
static void disc(const struct shades *sh, const struct hit live[], size_t n) {
    for (size_t i = 0; i < LED_COUNT; i++) {
        unsigned int w = 0, strongest = 0, p = 0;

        for (size_t h = 0; h < n; h++) {
            /* hundredths / (units/s x 100 / 1000) = ms the rim takes */
            uint32_t arrival = distance(positions[i], live[h].key) * 10 / DISC_SPEED;
            if (live[h].age < arrival) {
                continue;
            }
            uint32_t tau = live[h].age - arrival;
            if (tau >= DISC_FADE_MS) {
                continue;
            }
            unsigned int s = (DISC_FADE_MS - tau) * 256 / DISC_FADE_MS;
            if (s > strongest) {
                strongest = s;
                p = tau * 256 / DISC_FADE_MS;
            }
            w += s;
        }
        if (w > 0) {
            pixels[i] = mix(pixels[i], cycle(sh, p), MIN(w, 256));
        }
    }
}

#endif /* IS_ENABLED(CONFIG_LANTERN_DISC) */

#if IS_ENABLED(CONFIG_LANTERN_CROSS_RING)

/* Each hit's crossing ring (the header): its radius CROSS_SPEED x the hit's
 * age, full on the radius and gone CROSS_WIDTH from it, in the shade of the
 * pressed key's slot. An LED under several rings takes the strongest one's
 * colour at their summed strength. */
static void cross_ring(const struct shades *sh, const struct hit live[], size_t n) {
    for (size_t i = 0; i < LED_COUNT; i++) {
        unsigned int w = 0, strongest = 0;
        size_t pick = 0;

        for (size_t h = 0; h < n; h++) {
            if (live[h].age >= CROSS_MS) {
                continue;
            }
            /* ms x units/s / 1000 x 100 hundredths */
            uint32_t r = (uint32_t)live[h].age * CROSS_SPEED / 10;
            uint32_t d = distance(positions[i], live[h].key);
            uint32_t off = d > r ? d - r : r - d;
            if (off >= CROSS_WIDTH) {
                continue;
            }
            unsigned int s = (CROSS_WIDTH - off) * 256 / CROSS_WIDTH;
            if (s > strongest) {
                strongest = s;
                pick = h;
            }
            w += s;
        }
        if (w > 0) {
            pixels[i] = mix(pixels[i], shade_hi(sh, sh->rank[slots[live[pick].key]]), MIN(w, 256));
        }
    }
}

#endif /* IS_ENABLED(CONFIG_LANTERN_CROSS_RING) */

#if IS_ENABLED(CONFIG_LANTERN_BEAMS)

static uint32_t apart(uint16_t a, uint16_t b) { return a > b ? a - b : b - a; }

/* Whether key position a lies on b's row or column (the header), and then
 * how far along it, in hundredths of a key unit; b itself lies on both, at 0. */
static bool along_line(uint8_t a, uint8_t b, uint32_t *along) {
    uint32_t dx = apart(coords[2 * a], coords[2 * b]);
    uint32_t dy = apart(coords[2 * a + 1], coords[2 * b + 1]);

    if (dy < BEAMS_LINE) {
        *along = dx;
        return true;
    }
    if (dx < BEAMS_LINE) {
        *along = dy;
        return true;
    }
    return false;
}

/* Each hit's beams (the header): their heads BEAMS_SPEED x the hit's age out
 * along the pressed key's row and column, full at the head and gone
 * BEAMS_WIDTH from it, fading out over BEAMS_MS, in the shade of the pressed
 * key's slot. An LED under several beams takes the strongest one's colour at
 * their summed strength. */
static void beams(const struct shades *sh, const struct hit live[], size_t n) {
    for (size_t i = 0; i < LED_COUNT; i++) {
        unsigned int w = 0, strongest = 0;
        size_t pick = 0;

        for (size_t h = 0; h < n; h++) {
            uint32_t along;

            if (live[h].age >= BEAMS_MS || !along_line(positions[i], live[h].key, &along)) {
                continue;
            }
            /* ms x units/s / 1000 x 100 hundredths */
            uint32_t r = (uint32_t)live[h].age * BEAMS_SPEED / 10;
            uint32_t off = along > r ? along - r : r - along;
            if (off >= BEAMS_WIDTH) {
                continue;
            }
            unsigned int s = (BEAMS_WIDTH - off) * 256 / BEAMS_WIDTH;
            s = s * (BEAMS_MS - live[h].age) / BEAMS_MS;
            if (s > strongest) {
                strongest = s;
                pick = h;
            }
            w += s;
        }
        if (w > 0) {
            pixels[i] = mix(pixels[i], shade_hi(sh, sh->rank[slots[live[pick].key]]), MIN(w, 256));
        }
    }
}

#endif /* IS_ENABLED(CONFIG_LANTERN_BEAMS) */

#if IS_ENABLED(CONFIG_LANTERN_WATER)

/* 127 x sin(2 pi i / 256) for i 0..64, the first quarter of the turn;
 * sine() folds the other three onto it. */
static const int8_t sine_quarter[65] = {
    0,   3,   6,   9,   12,  16,  19,  22,  25,  28,  31,  34,  37,  40,  43,  46,  49,
    51,  54,  57,  60,  63,  65,  68,  71,  73,  76,  78,  81,  83,  85,  88,  90,  92,
    94,  96,  98,  100, 102, 104, 106, 107, 109, 111, 112, 113, 115, 116, 117, 118, 120,
    121, 122, 122, 123, 124, 125, 125, 126, 126, 126, 127, 127, 127, 127};

/* 127 x the sine of i / 256 of a turn, -127..127. */
static int32_t sine(uint32_t i) {
    i &= 0xFF;
    if (i < 128) {
        return sine_quarter[i < 64 ? i : 128 - i];
    }
    return -sine_quarter[i < 192 ? i - 128 : 256 - i];
}

/* The shades at HI_LEVEL end to end: p, 0..256, is how far from shade 0 at
 * 0 to the last shade at 256, linear between neighbours, a blend brought
 * back to full scale after the mix (mixed at that scale two hues dim each
 * other, as the gradient's ramp() says). */
static struct led_rgb span(const struct shades *sh, unsigned int p) {
    if (sh->n == 1) {
        return shade_hi(sh, 0);
    }
    /* 1/256 shades along: q >> 8 < n - 1 below p = 256, and at it the last
     * shade itself */
    unsigned int q = p * (sh->n - 1);
    size_t k = q >> 8;

    if ((q & 0xFF) == 0) {
        return shade_hi(sh, k);
    }
    struct led_rgb m = mix(shade_hi(sh, k), shade_hi(sh, k + 1), q & 0xFF);
    return scale(((uint32_t)m.r << 16) | ((uint32_t)m.g << 8) | m.b, HI_LEVEL);
}

/* Each hit's waves (the header): behind the front, WATER_SPEED x the hit's
 * age out, the surface at an LED d from the press is the sine of the front's
 * distance past it over WATER_WAVE, fading out over WATER_MS and falling off
 * with d, and the LED's height is the sum over the hits, a crest and a
 * trough cancelling. The LED is pulled toward span() of the height, a crest
 * to shade 0 and a trough to the last shade, as far as the height's size. */
static void water(const struct shades *sh, const struct hit live[], size_t n) {
    for (size_t i = 0; i < LED_COUNT; i++) {
        int32_t height = 0; /* 1/256 of a full crest, up or down */

        for (size_t h = 0; h < n; h++) {
            if (live[h].age >= WATER_MS) {
                continue;
            }
            /* ms x units/s / 1000 x 100 hundredths */
            uint32_t r = (uint32_t)live[h].age * WATER_SPEED / 10;
            uint32_t d = distance(positions[i], live[h].key);
            if (d > r) {
                continue;
            }
            /* the fade and the fall-off, each of 256; their product of 256
             * at most, so the term is within a crest, 256 */
            uint32_t fade = (WATER_MS - live[h].age) * 256 / WATER_MS;
            uint32_t fall = 256000 / (1000 + WATER_FALL * d);
            int32_t s = sine(((r - d) % WATER_WAVE) * 256 / WATER_WAVE);

            height += s * (int32_t)(fade * fall / 256) / 127;
        }
        if (height == 0) {
            continue;
        }
        height = CLAMP(height, -256, 256);
        unsigned int size = (unsigned int)(height < 0 ? -height : height);

        pixels[i] = mix(pixels[i], span(sh, (unsigned int)(256 - height) / 2), size);
    }
}

#endif /* IS_ENABLED(CONFIG_LANTERN_WATER) */

#if IS_ENABLED(CONFIG_LANTERN_INTRO)

/* An LED's depth: 0 at this half's outermost key, DEPTH_ONE at its innermost
 * (the header). */
#define DEPTH_ONE 1024

/* The intro's script (the header tells it): times in ms from its start,
 * reaches in depth, levels of 255. */
#define STAR_MS 600         /* the star's head from the outermost key to the innermost */
#define STAR_REACH 128      /* how far on either side of the head it lights */
#define GLITTER_MS 1000     /* how long a key glitters once the head has passed it */
#define GLITTER_BEAT_MS 60  /* every beat a new primary, or none */
#define GLITTER_ON 160      /* of 256: the chance that a key shows in a beat */
#define WAVE_FROM_MS 1300   /* the first wave's start; the others follow */
#define WAVE_GAP_MS 500
#define WAVE_MS 1000        /* a wave's front from the outermost key to the innermost */
#define WAVE_HEAD 64        /* how far ahead of its front a wave lights, rising */
#define WAVE_TAIL 320       /* and behind it, fading */
/* The core: the depth where it begins to glow and where it glows fully. On
 * canon's Imprint the thumb keys sit at 766 to 1024 and the next deepest key
 * at 626, so the whole thumb cluster glows, the inner keys brightest. */
#define CORE_FROM 700
#define CORE_FULL 940
#define CORE_SWELL 150      /* how much further out it begins by the burst */
#define CORE_LEVEL 170      /* its glow once a flash has faded */
/* Its flash at a wave's arrival, fading to CORE_LEVEL; the last wave's fades
 * to CHARGE_LOW within the charge's first beat, so that every later beat
 * pulses from the low alike. */
#define POP_MS 300
#define CHARGE_MS 600       /* the core's pulses, a colour a beat, and its swell */
#define CHARGE_BEAT_MS 150
#define CHARGE_LOW 80       /* the pulses' low */
#define BURST_FROM_MS (WAVE_FROM_MS + 2 * WAVE_GAP_MS + WAVE_MS + CHARGE_MS)
#define BURST_GAP_MS 150    /* between the rings' starts */
#define BURST_MS 450        /* a ring's front from the innermost key to the outermost */
#define BURST_REACH 128     /* how far on either side of its front a ring lights */
#define THIRDS_HOLD_MS 350  /* every key in its third, once the last ring's front is out */
#define THIRDS_FADE_MS 1000 /* then every key fading out at once */
/* The thirds' level: on canon's 29 LEDs every key at it, 4640, fits the
 * frame's cap at the device's brightness (4851 at 16 %), so the hold shows at
 * its level. */
#define THIRDS_LEVEL 160
BUILD_ASSERT(BURST_FROM_MS + 2 * BURST_GAP_MS + BURST_MS + THIRDS_HOLD_MS + THIRDS_FADE_MS <=
                 LANTERN_INTRO_MS,
             "the intro's script runs past LANTERN_INTRO_MS, where the rail rule ends it");
BUILD_ASSERT(CORE_SWELL < CORE_FROM && CORE_FROM < CORE_FULL && CORE_FULL <= DEPTH_ONE,
             "the core's depths are out of order");

/* The frame's thread: each LED's depth, measured from coords at the intro's
 * first frame. */
static bool depth_measured;
static uint16_t depth[LED_COUNT];

/* Hundredths of a key unit from LED i's key to (mid, bottom). */
static uint32_t from_middle(size_t i, int32_t mid, int32_t bottom) {
    int32_t dx = (int32_t)coords[2 * positions[i]] - mid;
    int32_t dy = (int32_t)coords[2 * positions[i] + 1] - bottom;

    return isqrt((uint32_t)(dx * dx + dy * dy));
}

/* The bottom of the board's middle: halfway between its leftmost and
 * rightmost keys, level with its lowest (y grows downwards); the depths run
 * from this half's farthest LED from it to its nearest. */
static void depth_measure(void) {
    uint16_t left = UINT16_MAX, right = 0, bottom = 0;

    for (size_t k = 0; k < KEY_COUNT; k++) {
        left = MIN(left, coords[2 * k]);
        right = MAX(right, coords[2 * k]);
        bottom = MAX(bottom, coords[2 * k + 1]);
    }
    int32_t mid = ((int32_t)left + right) / 2;
    uint32_t far = 0, near = UINT32_MAX;

    for (size_t i = 0; i < LED_COUNT; i++) {
        uint32_t r = from_middle(i, mid, bottom);
        far = MAX(far, r);
        near = MIN(near, r);
    }
    for (size_t i = 0; i < LED_COUNT; i++) {
        depth[i] = far == near ? DEPTH_ONE
                               : (uint16_t)((far - from_middle(i, mid, bottom)) * DEPTH_ONE /
                                            (far - near));
    }
    depth_measured = true;
}

/* The glitter's dice: a 32-bit integer hash, the improved lowbias32 constants
 * [16 21f0aaad 15 d35a2d97 15] listed in the README of Chris Wellons'
 * hash-prospector (public domain). */
static uint32_t dice(uint32_t x) {
    x ^= x >> 16;
    x *= 0x21f0aaadu;
    x ^= x >> 15;
    x *= 0xd35a2d97u;
    x ^= x >> 15;
    return x;
}

/* The intro's three colours in their order, red, blue, green, the order the
 * user wrote them (canon t-5v1f), as indexes into intro_led()'s c[]: the
 * waves', the rings' and the thirds' from the outside. */
static const uint8_t primary[3] = {0, 2, 1};

/* LED i's colour t ms into the intro, before the cap: the strongest primary
 * of the light on it, c[0] red, c[1] green, c[2] blue, and the star's white
 * over it (the header). */
static struct led_rgb intro_led(size_t i, uint32_t t, uint32_t seed) {
    int32_t d = depth[i];
    uint32_t c[3] = {0, 0, 0};
    uint32_t star = 0;

    /* the star */
    int32_t off = (int32_t)(t * DEPTH_ONE / STAR_MS) - d;
    if (off < 0) {
        off = -off;
    }
    if (off < STAR_REACH) {
        star = (uint32_t)(STAR_REACH - off) * 255 / STAR_REACH;
    }
    uint32_t passed = (uint32_t)d * STAR_MS / DEPTH_ONE;
    if (t >= passed && t - passed < GLITTER_MS) {
        uint32_t age = t - passed;
        uint32_t roll = dice(seed ^ ((uint32_t)i << 8) ^ (age / GLITTER_BEAT_MS));

        if ((roll & 0xFF) < GLITTER_ON) {
            c[(roll >> 8) % 3] += 255 * (GLITTER_MS - age) / GLITTER_MS;
        }
    }

    /* the waves; reached counts those whose light has come to this LED */
    size_t reached = 0;
    for (size_t w = 0; w < 3; w++) {
        uint32_t from = WAVE_FROM_MS + w * WAVE_GAP_MS;
        if (t < from) {
            break;
        }
        int32_t behind = (int32_t)((t - from) * DEPTH_ONE / WAVE_MS) - d;
        if (behind <= -WAVE_HEAD) {
            break;
        }
        reached = w + 1;
        if (behind < 0) {
            c[primary[w]] += (uint32_t)(WAVE_HEAD + behind) * 255 / WAVE_HEAD;
        } else if (behind < WAVE_TAIL) {
            c[primary[w]] += (uint32_t)(WAVE_TAIL - behind) * 255 / WAVE_TAIL;
        }
    }

    /* the core, until the burst */
    if (t < BURST_FROM_MS) {
        size_t arrived = 0;
        uint32_t last = 0;

        for (size_t w = 0; w < 3; w++) {
            uint32_t at = WAVE_FROM_MS + w * WAVE_GAP_MS + WAVE_MS;
            if (t >= at) {
                arrived = w + 1;
                last = at;
            }
        }
        if (arrived > 0) {
            uint32_t since = t - last;
            uint32_t low = arrived == 3 ? CHARGE_LOW : CORE_LEVEL;
            uint32_t pop = arrived == 3 ? CHARGE_BEAT_MS : POP_MS;
            uint32_t level = low;
            int32_t from = CORE_FROM;
            /* the newest wave to reach this key; an arrived wave has reached
             * every key, so reached >= arrived >= 1 */
            size_t colour = reached - 1;

            if (since < pop) {
                level += (255 - low) * (pop - since) / pop;
            }
            if (arrived == 3) {
                /* the last arrival is CHARGE_MS before the burst, so since < CHARGE_MS */
                uint32_t beat = since % CHARGE_BEAT_MS;
                uint32_t tri = (beat < CHARGE_BEAT_MS / 2 ? beat : CHARGE_BEAT_MS - beat) * 512 /
                               CHARGE_BEAT_MS;

                level = MAX(level, CHARGE_LOW + (255 - CHARGE_LOW) * MIN(tri, 256) / 256);
                from -= (int32_t)(CORE_SWELL * since / CHARGE_MS);
                /* a colour a beat: the last wave's, then the waves' order round again */
                colour = (2 + since / CHARGE_BEAT_MS) % 3;
            }
            if (d > from) {
                uint32_t k = d >= CORE_FULL ? 256 : (uint32_t)(d - from) * 256 / (CORE_FULL - from);

                if (arrived == 3) {
                    /* the last wave's fading tail would outshine a beat at its
                     * low on the deepest keys */
                    uint32_t own = c[primary[colour]];

                    c[0] = c[1] = c[2] = 0;
                    c[primary[colour]] = own;
                }
                c[primary[colour]] += level * k / 256;
            }
        }
    }

    /* the burst: the rings, and behind the last the thirds */
    if (t >= BURST_FROM_MS) {
        uint32_t since = t - BURST_FROM_MS;

        for (size_t j = 0; j < 3 && since >= j * BURST_GAP_MS; j++) {
            int32_t front =
                DEPTH_ONE - (int32_t)((since - j * BURST_GAP_MS) * DEPTH_ONE / BURST_MS);
            off = front > d ? front - d : d - front;
            if (off < BURST_REACH) {
                c[primary[j]] += (uint32_t)(BURST_REACH - off) * 255 / BURST_REACH;
            }
        }
        passed = 2 * BURST_GAP_MS + (uint32_t)(DEPTH_ONE - d) * BURST_MS / DEPTH_ONE;
        uint32_t fade = 2 * BURST_GAP_MS + BURST_MS + THIRDS_HOLD_MS;
        size_t third = MIN((uint32_t)d * 3 / DEPTH_ONE, 2u);

        if (since >= passed && since < fade) {
            c[primary[third]] += THIRDS_LEVEL;
        } else if (since >= fade && since - fade < THIRDS_FADE_MS) {
            c[primary[third]] += THIRDS_LEVEL * (THIRDS_FADE_MS - (since - fade)) / THIRDS_FADE_MS;
        }
    }

    size_t top = c[1] > c[0] ? 1 : 0;
    if (c[2] > c[top]) {
        top = 2;
    }
    uint32_t p = MIN(c[top], 255u);

    return (struct led_rgb){
        .r = (uint8_t)MIN((top == 0 ? p : 0) + star, 255u),
        .g = (uint8_t)MIN((top == 1 ? p : 0) + star, 255u),
        .b = (uint8_t)MIN((top == 2 ? p : 0) + star, 255u),
    };
}

/* The frame's thread: the intro's frame, t ms from its start, over a black
 * picture for the cap; true until LANTERN_INTRO_MS, when the rail rule ends
 * the boot light (the script's last light is out before, asserted). Not
 * inlined: inside paint() its locals would sit in paint()'s frame under the
 * strip driver's calls too, nearer the end of the 768-byte stack: 24 bytes
 * for the first script, 40 for the current one on a model build (Opus
 * reviews, 2026-10-21 and -22). */
static __noinline bool intro_compose(void) {
    uint32_t t = k_uptime_get_32() - (uint32_t)atomic_get(&intro_t0);

    if (!depth_measured) {
        depth_measure();
    }
    memset(picture, 0, sizeof(picture));
    if (t >= LANTERN_INTRO_MS) {
        memset(pixels, 0, sizeof(pixels));
        return false;
    }
    uint32_t seed = (uint32_t)atomic_get(&intro_seed);

    for (size_t i = 0; i < LED_COUNT; i++) {
        pixels[i] = intro_led(i, t, seed);
    }
    hold_to_cap();
    return true;
}

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

/* How long after a press the effects in fx still draw. */
static uint32_t effects_life(uint32_t fx) {
    uint32_t life = 0;

    if (fx & (LANTERN_EFFECT_THIN_RING | LANTERN_EFFECT_STAIN)) {
        life = MAX(life, (uint32_t)RING_MS);
    }
    if (fx & LANTERN_EFFECT_DISC) {
        life = MAX(life, (uint32_t)DISC_MS);
    }
    if (fx & LANTERN_EFFECT_CROSS_RING) {
        life = MAX(life, (uint32_t)CROSS_MS);
    }
    if (fx & LANTERN_EFFECT_BEAMS) {
        life = MAX(life, (uint32_t)BEAMS_MS);
    }
    if (fx & LANTERN_EFFECT_WATER) {
        life = MAX(life, (uint32_t)WATER_MS);
    }
    return life;
}

#endif /* IS_ENABLED(CONFIG_LANTERN_EFFECTS) */

/* The frame: the picture of stage at, the picked effects over it and the cap,
 * or the intro's frame while it plays; true while a picked effect still moves
 * or the intro plays. The low priority work queue only: it writes
 * frame_shades, pixels, picture and the stain's state and clears spent hits
 * (the palette, the pick and the hits are atomics, read from there). */
static bool compose(size_t at) {
#if IS_ENABLED(CONFIG_LANTERN_INTRO)
    if (intro_plays()) {
        return intro_compose();
    }
#endif
    uint32_t palette[LANTERN_PALETTE_SLOTS] = {0};
    bool have = IS_ENABLED(CONFIG_LANTERN_PALETTE) && lantern_palette_get(palette);

    shades(palette, have, &frame_shades);
#if IS_ENABLED(CONFIG_LANTERN_EFFECTS)
    uint32_t fx = active_effects();
#endif
#if IS_ENABLED(CONFIG_LANTERN_STAIN)
    if (at != stained_at || fx != drawn_effects) {
        memset(stains, 0, sizeof(stains));
        stained_at = at;
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
                    : boot ? frame_shades.at_level[frame_shades.rank[slots[positions[i]]]]
                           : (struct led_rgb){0};
#else
        pixels[i] = frame_shades.at_level[at > 0 ? at : frame_shades.rank[base_slot(i)]];
#endif
    }

#if IS_ENABLED(CONFIG_LANTERN_STAIN)
    for (size_t i = 0; i < LED_COUNT; i++) {
        if (stains[i] != 0 && stains[i] <= frame_shades.n) {
            pixels[i] = frame_shades.at_level[stains[i] - 1];
        }
    }
#endif

#if IS_ENABLED(CONFIG_LANTERN_EFFECTS)
    struct hit live[HITS];
    size_t n = hits_read(live, effects_life(fx));

    memcpy(picture, pixels, sizeof(pixels));
#if IS_ENABLED(CONFIG_LANTERN_THIN_RING)
    if (fx & (LANTERN_EFFECT_THIN_RING | LANTERN_EFFECT_STAIN)) {
        thin_ring(&frame_shades, live, n, (fx & LANTERN_EFFECT_STAIN) != 0);
    }
#endif
#if IS_ENABLED(CONFIG_LANTERN_DISC)
    if (fx & LANTERN_EFFECT_DISC) {
        disc(&frame_shades, live, n);
    }
#endif
#if IS_ENABLED(CONFIG_LANTERN_CROSS_RING)
    if (fx & LANTERN_EFFECT_CROSS_RING) {
        cross_ring(&frame_shades, live, n);
    }
#endif
#if IS_ENABLED(CONFIG_LANTERN_BEAMS)
    if (fx & LANTERN_EFFECT_BEAMS) {
        beams(&frame_shades, live, n);
    }
#endif
#if IS_ENABLED(CONFIG_LANTERN_WATER)
    if (fx & LANTERN_EFFECT_WATER) {
        water(&frame_shades, live, n);
    }
#endif
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
#if IS_ENABLED(CONFIG_LANTERN_STAIN)
        memset(stains, 0, sizeof(stains));
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

/* Any thread (keys.h): the hits go now, the dye at the frame this sends. */
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
        atomic_set(&intro_t0, (atomic_val_t)k_uptime_get_32());
        atomic_set(&intro_seed, (atomic_val_t)k_cycle_get_32());
    }
#endif
    atomic_set(&booting, on ? 1 : 0);
    if (atomic_get(&lit)) {
        submit();
    }
}

#endif
