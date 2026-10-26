# zmk-lantern

A [ZMK](https://zmk.dev) module that lights a keyboard's key LEDs only while
USB powers it and it is in use, and paints them from a palette the host hands
the keyboard. While a cable powers the half, its LED rail is on and every key
shows its colour in the palette's picture; on battery the LEDs are black and
the rail is off, so they cost the battery nothing; on the cable, five minutes
without a key (ZMK's idle state) turn them off until the next one, and on the
way there the half cycles through the palette's colours, the whole half in one
colour for ten seconds, then the next. At boot the LEDs light for a second,
cable or not, and then the rule applies. The palette: six colours handed to
the keyboard's split central over USB, kept by every half across boots, and
shown on the keys in the picture the module paints, by default the bands the
consumer's overlay draws (the palette, below).

Built for the **Cyboard Imprint** of
[akira-toriyama/canon](https://github.com/akira-toriyama/canon), whose halves
have kept their LED rail off since the rail alone drew 10-18 mA a half at rest
(canon #207, 2026-09-15). Device names follow canon's
[glossary](https://github.com/akira-toriyama/canon/blob/main/docs/glossary.md).
The name: a beacon shows others where you are; a lantern lights what is at
hand. The sibling module [zmk-beacon](https://github.com/akira-toriyama/zmk-beacon)
is the beacon.

## Status

Builds for both Imprint halves against ZMK `main` and Cyboard's `zephyr-4.1`
(CI, and canon's own tree with its patches, 2026-10-19). On hardware, both
halves of canon's Cyboard Imprint (canon #263, 2026-10-19): the boot light on
battery, then dark; lit on plugging in and dark on unplugging, each half for
itself; lit through a boot on the cable. The idle rule
(`CONFIG_LANTERN_OFF_WHEN_IDLE`): seen on the right half on 2026-10-19, in
canon's logging build with a 20 s `CONFIG_ZMK_IDLE_TIMEOUT`: `dark: idle` once
the half had sat untouched on the cable, `lit: usb powered` at the next key;
the five-minute product images are canon's (its t-0y3a). The palette, on
hardware 2026-10-19 with canon's logging images of this module on both halves
and the Imprint Dongle (slot 0 then went through ZMK's underglow): one
`dongle.py palette` changed both halves over the split (each logged `stored`
and `applied`); a half rebooted on the cable applied slot 0 from its settings
at 0.3 s and took the central's resend at 1.8 s; a half flashed fresh, with no
palette stored, had it 1.7 s after boot from the resend; and the central's
reboot resent it to both halves within 2 s. The picture the module paints
itself (the bands, `src/keys.c`): both halves on 2026-10-20 with canon #271's
images, five bands from a palette the host set while the halves ran on
battery, shown when each was plugged in (canon t-0967). The countdown
(`CONFIG_LANTERN_COUNTDOWN`, `src/keys.c`): both halves on 2026-10-20 with
canon's trial images (a palette of five colours): a new picture every
10.000 s from the last key, the bands after every fifth, the bands back at a
key, `dark: idle` at ZMK's timeout; a cable plugged in after a boot lit the
stage the keys' clock said (canon t-gsm2, t-x82q). The effects, on both
halves with the Imprint Dongle relaying (canon's trial images, one effect at
a time, 2026-10-21 to 2026-10-26, then its product images with the pick,
2026-10-26): the thin ring, the disc, the stain, the crossing ring in across
the split, and the beams (2026-10-26, canon's product conf plus
`CONFIG_LANTERN_BEAMS=y`, the pick `beams` sent from the dongle's port), each
the user's OK on sight (canon t-6ttk, t-5hqx).

## What it does

| While | Rail | LEDs |
| --- | --- | --- |
| USB powers the half: a host, awake or asleep, or a charger | on | lit: the palette's picture |
| the half runs on battery | off | black |
| USB powers the half and its last key is `CONFIG_LANTERN_COUNTDOWN_STAGE_MS` (10 s) or more ago (`CONFIG_LANTERN_COUNTDOWN`) | on | lit: the whole half in the palette's next colour, a new picture every stage, the bands again after the last colour |
| USB powers the half, but ZMK's activity state is idle: `CONFIG_ZMK_IDLE_TIMEOUT` ms (the consumer's line) without a key, sensor or pointing input on this half | off | black, until the next input |
| the first `CONFIG_LANTERN_BOOT_LIGHT_MS` after boot (default 1000) | on | lit, cable or not |
| with `CONFIG_LANTERN_INTRO`, the boot light: six seconds from boot and from each arrival of USB power | on | the intro, cable or not |

- Each half decides for itself, from its own USB port and its own activity;
  nothing crosses the split. Typing on one half alone leaves the other to go
  dark.
- Plugging in turns the rail on and, `settle-ms` later (100 ms on the Imprint),
  writes the picture; unplugging writes a black frame and drops the rail
  100 ms after. A reset, a suspend (the host asleep) or a resume on the cable
  changes nothing. Idle and the next input switch the LEDs the same way; the
  boot light is never cut short. With the intro, plugging in plays it first.
- A cable plugged into an idle half lights nothing until its next key: USB is
  no activity to ZMK, and lighting on it would leave the LEDs on with no idle
  event to follow. With the intro it plays the intro, six seconds, and goes
  dark again.
- The picture ([src/keys.c](src/keys.c)): each LED shows its key's colour in
  the picture, by default (the bands) the palette slot the consumer's
  `lantern,keys` node assigns to the key under it, so the palette's colours
  fall on the keyboard in bands. A slot the host left black shows slot 0, so
  one colour paints every key alike; without a palette every key is white
  (black in the no picture).
  Each colour is scaled so that its brightest channel is, by default,
  `CONFIG_LANTERN_BRIGHTNESS` percent of full scale (default 16, what ZMK's
  underglow drove on canon's halves before the module painted the strip),
  which keeps its hue and saturation; a grey or black scales to white. The
  picture's colours, with the countdown's other pictures and the stain's dye,
  can be dimmed below the brightness (`CONFIG_LANTERN_PICTURE_BRIGHTNESS`, the
  same by default) while the effects keep full scale and the frame's cap stays
  counted from the brightness. The module is the strip's one writer: ZMK's
  underglow stays off on such a half (`CONFIG_ZMK_RGB_UNDERGLOW=n`, asserted),
  and a frame goes out only when the LEDs light or go dark, when the palette
  changes while they are lit, when the countdown moves a stage, at each throw
  of the scatter, when the boot light ends (the no picture's or the intro's),
  and every 30 ms while an effect moves or the intro plays; a still picture
  ticks at no rate.
- The solid (`CONFIG_LANTERN_SOLID`, off by default while canon tries the
  pictures one at a time): instead of the bands, every key in slot 0, the
  whole half in one colour; the countdown's pictures are then the whole half
  in each distinct colour in turn, slot 0's first. The bands, the solid,
  the gradient, the scatter and the no picture are one Kconfig choice,
  `LANTERN_PICTURE`, set through its members.
- The gradient (`CONFIG_LANTERN_GRADIENT`, off by default while canon tries
  the pictures one at a time): instead of the bands, the palette's colours
  blended across the whole board by each key's x (the node's `coords`): the
  leftmost key in the first distinct colour (colours that paint alike count
  once), the rightmost in the last, each key between them the two colours
  around its place mixed in proportion, so no band edges; a blend's brightest
  channel is the picture's brightness, as a colour's. The halves share the
  board: the left half shows the first stretch, the right half the last. A
  colour shows pure only on a key at its place; no key sits between the
  halves, so a colour whose place lies there never does.
- The scatter (`CONFIG_LANTERN_SCATTER`, off by default while canon tries the
  pictures one at a time): instead of the bands, the palette's slots thrown
  over the half's keys at random, each slot on as many keys as its share of
  the node's `scatter-weights` (percent per slot, at most 100 in all; canon:
  50 20 10 5 5 0, and slot 0 takes what the others leave), thrown again every
  `CONFIG_LANTERN_SCATTER_MS` (default 10000) while the LEDs are lit and at
  every light. Each half throws for itself.
- The no picture (`CONFIG_LANTERN_NO_PICTURE`, off by default while canon
  tries the pictures one at a time): every key black while the LEDs are lit,
  so that only the effects light, over black keys (the stain's dye stays, as
  over any picture). The boot light shows the bands instead, so that a half
  still shows it has power, unless the intro plays in its place; on the
  cable the keys turn black when it ends.
  With the countdown on, its first picture is the black one and the others
  the whole half in a colour.
- The countdown (`CONFIG_LANTERN_COUNTDOWN`, on by default with the idle rule
  and the palette): from the half's last key, the picture changes every
  `CONFIG_LANTERN_COUNTDOWN_STAGE_MS` (default 10000): the bands, then the
  whole half in each colour of the palette in turn (a slot the host left
  black is slot 0's colour; colours that paint alike at the picture's
  brightness count once), then the bands again, until ZMK turns idle and the
  LEDs go dark. A key brings the bands back and restarts the clock; a cable
  plugged into a half that typed a while ago lights the stage its keys say.
  The clock is the half's own keys: a sensor or pointing input counts for
  ZMK's idle, not for the stages. One colour: the bands until the dark.
- The effects: a picture that moves after a key, the six below. The
  consumer's conf builds the ones it wants into the image
  (`CONFIG_LANTERN_THIN_RING`, `CONFIG_LANTERN_DISC`, `CONFIG_LANTERN_STAIN`,
  `CONFIG_LANTERN_CROSS_RING`, `CONFIG_LANTERN_BEAMS`, `CONFIG_LANTERN_WATER`,
  each off by default), and the pick says which
  of them draw (the pick, below): the host's, kept across boots, or without
  one the effect the Kconfig choice `LANTERN_EFFECT_DEFAULT` names. An effect
  pulls an LED toward its colour at full brightness and never dims the
  picture; a frame draws at most what every LED white plus two keys white at
  full brightness would (an estimate of 280 + 100 mA at 16 %, unmeasured), so
  more keys lit at once by an effect on a white picture share that and dim
  alike. The brightness must stay below 100 % with an effect (asserted).
- The thin ring (`CONFIG_LANTERN_THIN_RING`; the pick's `thin-ring`): from
  each key this half presses (and, with the hit below, from the other half's
  keys too, in across the split) a ring one key wide spreads at twelve key units
  a second and fades out over a second, so it has crossed the half when it
  is gone; each press takes the palette's next colour, at full brightness
  within the frame's cap (above), and a key under two rings shows the
  stronger one's colour. It measures the board from the node's `coords`,
  each key's x and y in hundredths of a key unit (canon's `imprint.json`
  times 100).
- The disc (`CONFIG_LANTERN_DISC`; the pick's `disc`): from each key
  this half presses (and, with the hit below, from the other half's keys
  too, the disc then living long enough to cross the whole board) a rim
  spreads at twelve key units a second, and a key it
  has passed lights at full brightness within the frame's cap (above), which
  holds a disc harder than a ring, and fades back to the picture over half a
  second, its colour running once through the palette's colours in
  order as it fades: brightest at the rim, dark behind it, the day's colours
  passing inside. A key under two discs shows the stronger one's colour. It
  measures the board as the thin ring does.
- The stain (`CONFIG_LANTERN_STAIN`; the pick's `stain`, the thin ring that
  dyes, and it brings the thin ring): a key within about 4.8 key units of the press, where the ring's
  crest still has 0.6 of its full strength, keeps the ring's colour at the
  picture's brightness from the moment the crest reaches it (where two rings
  have passed, the newer press's colour), so the half is painted over in the
  day's colours as you type, each press in the next colour (a ring in from
  the other half dyes the keys within its reach too, the thumbs first). The dye goes when
  the countdown changes the picture (the next key
  brings the picture back and its ring dyes it afresh), when the pick changes
  and whenever the LEDs light; without the countdown it stays until the LEDs
  go dark.
- The crossing ring (`CONFIG_LANTERN_CROSS_RING`; the pick's `cross-ring`, and
  it brings the hit, below): the thin ring's shape, one key wide at twelve key
  units a second, from a pressed key of either half across the whole board:
  a press of this half spreads out through the other half, and a press of
  the other half, relayed by the split central, comes in at the thumbs and
  then the inner columns. It keeps its strength until it has left the board
  (a second and a half on the Imprint), at full brightness within the
  frame's cap (above), and its colour is the pressed key's band (the node's
  `slots`), so both halves draw one press in one colour. A key under two
  rings shows the stronger one's colour. It measures the board as the thin
  ring does.
- The beams (`CONFIG_LANTERN_BEAMS`; the pick's `beams`): from each pressed
  key four beams run outward at twelve key units a second, two along its row
  and two along its column, each four fifths of a key wide and fading out
  over a second, so the row's have left the half well before they are gone,
  and with the hit a press of the other half sends its row's beam in across
  the split (from an inner column at about half strength, from an outer one
  all but spent, from a thumb nearly at full strength). A key is on the row
  or the column when its y or x is within half a key unit of the pressed
  key's, so canon's thumb clusters, half a unit off the bottom row and the
  inner column, make rows and columns of their own, two rows of three and
  three columns of two, and the two clusters share their rows. The
  colour is the pressed key's band, as the crossing ring's, at full
  brightness within the frame's cap; a key under two beams shows the
  stronger one's colour. It measures the board from `coords` as the thin
  ring does, along one axis at a time.
- The water (`CONFIG_LANTERN_WATER`; the pick's `water`): each pressed key
  starts waves, a front spreading at twelve key units a second with a
  wavelength of three key units behind it, fading out over a second and a
  half and weaker the farther from the key (with the hit the other half's
  presses start waves too, which come in across the split low: about a
  fifth of a crest from an inner column, three fifths from a thumb). The
  waves of every press add up: where two crests meet the surface rises
  higher, where a crest meets a trough it stays still. A key is pulled from
  the picture along the palette's colours by the surface's height, a full
  crest to the first, a full trough to the last, a lower wave to the colours
  between, and as far as the surface is from still, at full brightness
  within the frame's cap: the still surface shows the picture (black with
  the no picture), the pressed key's first waves flicker between the first
  and last colours four times a second, and lower waves, later or farther
  out, between colours nearer the middle. It measures the board as the thin
  ring does.
- The intro (`CONFIG_LANTERN_INTRO`, off by default while canon tries it):
  the boot light plays a six-second show instead of the picture, in red,
  blue and green up to full brightness within the frame's cap, not the
  palette's colours, and opens at boot and again whenever USB power arrives;
  then the rule applies as after any boot light. Each half plays it alone,
  mirrored, from its outermost key to its innermost thumb (it measures the
  board from `coords`): a white star crosses the half and leaves the keys
  glittering in random primaries; a red, a blue and a green wave follow it
  in; the thumb cluster, its inner keys brightest, takes each wave's colour
  as it arrives, red, then blue, then green, and pulses in the three by
  turns; a red, a blue and a green ring burst back out, and the last leaves
  every key in its third of the half by depth, the outer red, the middle
  blue and the inner green (on the Imprint, the thumb cluster), which hold
  a moment and fade. No key mixes two of the colours; the only other is the
  star's white head, tinted where it meets the glitter. Nothing cuts it
  short, power arriving while it plays does not restart it, and keys
  pressed meanwhile show no effect.

## The split link

While USB powers a half, the half asks the split central for peripheral
latency 0 on their link; on battery it asks for the link's own latency again,
the central's (ZMK's 30). A half with latency 30 may skip 30 connection
events while it has nothing to send, so whatever the central sends it waits
up to 232.5 ms at ZMK's 7.5 ms interval; at 0 it arrives at the next event.
The LEDs are lit only on USB, where listening at every event costs the
battery nothing, so what the central relays to a lit half shows at once.
`CONFIG_LANTERN_USB_LATENCY` is on by default on a split peripheral whose
LEDs light on USB power ([src/latency.c](src/latency.c)); the interval and
the supervision timeout stay the central's. Zephyr holds a peripheral's
request for the first 5 s of a connection, and the central's host accepts it
(neither ZMK nor this module registers a callback that could refuse it).

The hit (`CONFIG_LANTERN_HIT`, on wherever the `lantern,behavior-hit` node
exists on a BLE split, named `hit` on every device like the palette's node):
the split central sends each key press, a half's or its own, to every other
half as one invocation of the node (param1 the key position), from a work
queue of the module's own ([src/hit.c](src/hit.c)), and only to a half whose
link runs at latency 0, the sign that USB powers it and its LEDs may be lit; a
half on battery is dark and gets nothing. A half notes a relayed press as a
hit for its effects while its LEDs are lit, as one of its own keys, and its
countdown's clock ignores it (a stock half as the central counts the other
half's keys through its own listener, as ZMK's idle does there). The crossing
ring draws it from the other half's key; the beams, the thin ring, the
disc and the water do too, in their own colours (the beams' row comes in
across the split, the water's waves low), the stain dyes with it, and it
counts in the press
count the thin ring's colours follow. Delivery is not confirmed, as the
palette's, and the hits share the central's run queue with the palette (the
source says what a half that vanishes while lit costs).

## The palette

Six colours (`0xRRGGBB`, slots 0..5) that the host hands the keyboard's split
central and that the central and every half keep in their settings
(`lantern/palette/colours`); a half's key LEDs show them in the picture
(by default the bands its `lantern,keys` node draws), repainted when a
palette arrives while they are lit and shown at every boot. A set of fewer
than six colours clears the rest to `0x000000`, which the LEDs show as slot
0.

- The host writes a text line on the central's USB CDC ACM port (canon:
  `scripts/dongle.py palette`): `palette RRGGBB [RRGGBB ...]` (one to six)
  sets slots 0.. and clears the rest, answered with `palette ok` and the six
  slots as stored; `palette` or `palette?` is answered with the six slots, or
  `palette none` before any; a bad line gets `palette error: <reason>`
  ([src/port.c](src/port.c) is the grammar). The central's code
  invokes the `lantern,behavior-palette` node, which ZMK's split transport
  carries to every half by the node's name, one pair of slots per invocation
  ([src/palette.c](src/palette.c)); nothing is bound to a key.
- A half that was away when the host wrote, or that lost its settings, gets
  the palette when it is back: the central resends it a second after each
  battery report of a half, the first of which follows every (re)connection.
  A half writes its flash only when a colour changed. A stored palette of
  another slot count (a device flashed across the change from four slots to
  six) is ignored at load: `palette none` until the host sends again.
- `CONFIG_LANTERN_PALETTE` is on wherever the node exists (the consumer's
  keymap or an overlay, named `palette` on every device of the split, the
  central included: that name is what the transport carries) and
  `CONFIG_LANTERN_PORT`, the port that reads the palette's and the pick's
  lines, on a split central with a CDC ACM port. A
  split central needs `CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING=y`
  (asserted at build time). A device without a `lantern,keys` node, such as a
  dongle, keeps and forwards the palette without painting anything.

## The pick

Which of the effects built into a half's image draw, chosen by the host and
kept by the central and every half in their settings (`lantern/effect/mask`),
so that the effect changes without a flash (canon's effect of the day,
t-5hqx). The image carries every effect the consumer's conf builds; a half
draws the picked ones it has, every one of them over the last when the pick
names several, and with no pick stored, or none of its effects built, the one
the Kconfig choice `LANTERN_EFFECT_DEFAULT` names (`CONFIG_LANTERN_EFFECT_DEFAULT_CROSS_RING`
and so on, one member per built effect; without a line the stain when it is
built, else the first built effect): the only pick a keyboard without a host
has.

- The host writes a text line on the central's USB CDC ACM port (canon:
  `scripts/dongle.py effect`): `effect <name> [<name> ...]` picks these
  effects on every half and the central, answered with `effect ok` and the
  pick's names in the table's order; `effect` or `effect?` is answered with
  the names, or `effect none` before any; a name not in the table gets
  `effect error: no such effect` and changes nothing
  ([src/port.c](src/port.c) is the grammar). The names, and the bit each
  carries on the wire: `thin-ring`, `disc`, `stain`, `cross-ring`, `beams`,
  `water`
  ([src/effect.h](src/effect.h)'s table; a new effect is a line there, its
  Kconfig and its draw in src/keys.c). The central's code invokes the
  `lantern,behavior-effect` node, which ZMK's split transport carries to every
  half by the node's name ([src/effect.c](src/effect.c)); nothing is bound to
  a key.
- A half that was away when the host wrote gets the pick with the palette, a
  second after each of its battery reports; a half writes its flash only when
  the pick changed. A change of the pick clears the effects' state (a ring
  half-way out, the stain's dye), so the new effect starts clean. A device
  stores the host's bits as sent, the ones its image lacks included, so the
  reply names the pick as made (with the names this image knows; `effect
  none` when it knows none of the stored bits) and an image with more
  effects shows it.
- `CONFIG_LANTERN_EFFECT` is on wherever the node exists (named `effect` on
  every device of the split, as the palette's). The central's run queue must
  hold the palette's three invocations and the pick's one for every half at
  once: `CONFIG_ZMK_SPLIT_BLE_CENTRAL_SPLIT_RUN_QUEUE_SIZE=8` for two halves
  (asserted at build time).

## Using it in a ZMK config

1. `config/west.yml`: the module as a project, pinned by commit (canon's way:
   a change here reaches the keyboard with a pin bump, without a release):

   ```yaml
   manifest:
     remotes:
       - name: akira-toriyama
         url-base: https://github.com/akira-toriyama
     projects:
       - name: zmk-lantern
         remote: akira-toriyama
         revision: <commit sha> # or a release tag
         path: modules/zmk-lantern
   ```

2. The half's overlay ([config/imprint.overlay](config/imprint.overlay) is the
   Imprint's): take the LED rail away from ZMK's `ext_power` driver and give it
   to the module, and name the strip and the keys.

   ```dts
   #include <zephyr/dt-bindings/gpio/gpio.h>

   &{/EXT_POWER} {
       status = "disabled";
   };

   &gpio1 {
       lantern_rail_hog {
           gpio-hog;
           gpios = <2 GPIO_ACTIVE_HIGH>;
           output-low;
       };
   };

   &led_strip {
       chain-length = <29>;
   };

   / {
       lantern_rail: lantern_rail {
           compatible = "lantern,rail";
           gpios = <&gpio1 2 GPIO_ACTIVE_HIGH>;
           settle-ms = <100>;
       };

       lantern_keys: lantern_keys {
           compatible = "lantern,keys";
           strip = <&led_strip>;
           positions = < 17 29 5 16 28 40 /* ... one entry per LED, in chain order: the key under it */ >;
           slots = < 0 0 0 1 1 1 /* ... one entry per key position: the slot its LED shows */ >;
           coords = < 0 200 100 200 /* ... an x and a y per key position, hundredths of a key unit */ >;
           scatter-weights = <50 20 10 5 5 0>; /* a percentage per slot, for the scatter */
       };
   };
   ```

   `gpios` is the pin the board's `EXT_POWER` node has as `control-gpios`,
   `settle-ms` its `init-delay-ms` (the LEDs' power-up time before the first
   frame). The hog holds the rail off from early boot until the module takes
   the pin; the disabled node keeps ZMK's driver, which would turn the rail on
   at init and restore a stored state, out of the build. `strip` is the
   board's LED strip node (Cyboard's `ws2812@0`, ZMK's chosen `zmk,underglow`)
   with `chain-length` set to the real LED count (29 a half on the Imprint;
   Cyboard's 150 is a placeholder): the driver sends and buffers the whole
   chain at every frame, and `positions` has one entry per LED (asserted).
   `positions` is measured on the hardware, one LED at a time (canon's t-69g7
   probe); `slots` has one entry per key position of the chosen matrix
   transform (asserted), so a table in the keymap's shape reads as the
   bands, 0 to 5 each; `coords` has an x and a y per key position, in
   hundredths of a key unit on the whole board (canon: `imprint.json` times
   100), for what measures the board (the thin ring, the disc, the
   gradient, the intro), which asserts it. A consumer
   with per-half overlays puts `slots`, `coords` and `scatter-weights` in a shared dtsi and each
   half's `positions` and `chain-length` in its own overlay (canon's
   `config/lantern_keys.dtsi`).

3. The half's conf ([config/imprint.conf](config/imprint.conf)):

   ```
   CONFIG_ZMK_RGB_UNDERGLOW=n
   CONFIG_ZMK_IDLE_TIMEOUT=300000
   ```

   `CONFIG_LANTERN_KEYS` and `CONFIG_LANTERN_ON_USB_POWER` are on by default
   wherever their nodes exist; `CONFIG_LANTERN_BRIGHTNESS` (default 16) sets
   the frame's cap and, by default, the picture's brightness, which
   `CONFIG_LANTERN_PICTURE_BRIGHTNESS` (at most it) lowers alone;
   `CONFIG_LANTERN_BOOT_LIGHT_MS=0` drops the boot light, and
   `CONFIG_LANTERN_INTRO=y` makes it play the intro, at boot and when USB
   power arrives (it needs `coords`). ZMK's underglow must
   be off on such a half (its tick would repaint the strip; Cyboard's module
   defaults it to y for every target). A missing or conflicting option fails
   the build with a message naming it, except
   `CONFIG_LANTERN_BOOT_LIGHT_MS` with the intro, which Kconfig only warns
   about and drops. A target without the nodes (a dongle)
   builds none of the painting or the rail. `CONFIG_LANTERN_OFF_WHEN_IDLE` is
   on by default with the rail, and the idle timeout is ZMK's
   `CONFIG_ZMK_IDLE_TIMEOUT` (default 30000 ms; 300000 is the five minutes of
   canon's halves): the module has no timer of its own, ZMK's activity state
   already counts keys, sensors and pointing input per device. With
   `CONFIG_ZMK_SLEEP=n` the timeout moves nothing else in a half but the
   battery sampling, which ZMK pauses while idle;
   `CONFIG_LANTERN_OFF_WHEN_IDLE=n` keeps the LEDs lit on the cable whatever
   the activity. `CONFIG_LANTERN_COUNTDOWN` is on by default with the idle
   rule and the palette, a new picture every
   `CONFIG_LANTERN_COUNTDOWN_STAGE_MS` (10000): `=n` keeps the picture until
   the dark (the scatter still throws anew every `CONFIG_LANTERN_SCATTER_MS`).
   The picture is one Kconfig choice, `LANTERN_PICTURE`, the bands
   (`CONFIG_LANTERN_BANDS`) by default: `CONFIG_LANTERN_SOLID=y` paints every
   key in slot 0, `CONFIG_LANTERN_GRADIENT=y` blends the colours across the
   board by the keys' x (it needs `coords`), `CONFIG_LANTERN_SCATTER=y` throws
   the slots over the keys at random (it needs `scatter-weights`),
   `CONFIG_LANTERN_NO_PICTURE=y` leaves every key black for the effects (the
   boot light shows the bands unless the intro plays; not with the press
   glow).
   `CONFIG_LANTERN_THIN_RING=y` builds the thin ring into the image,
   `CONFIG_LANTERN_DISC=y` the disc, `CONFIG_LANTERN_STAIN=y` the stain, the
   ring that dyes, `CONFIG_LANTERN_CROSS_RING=y` the crossing ring,
   `CONFIG_LANTERN_BEAMS=y` the beams and `CONFIG_LANTERN_WATER=y` the water
   (the six need `coords`, the crossing ring the hit node too, 4.); which of
   them draw is
   the pick's (the effect node, 4., and `dongle.py effect`), and without one
   the choice `LANTERN_EFFECT_DEFAULT` (`CONFIG_LANTERN_EFFECT_DEFAULT_CROSS_RING=y`
   on canon's halves; the first built effect unless a line says otherwise).
   On a split peripheral
   `CONFIG_LANTERN_USB_LATENCY` is on by
   default with the rail; `=n` leaves the link at the central's latency.

4. The palette's node, on every device of the split under the one name
   (canon: in its keymap's behaviors; [config/palette.dtsi](config/palette.dtsi)
   here, included from every overlay of the check build):

   ```dts
   / {
       behaviors {
           palette: palette {
               compatible = "lantern,behavior-palette";
               #binding-cells = <2>;
           };
       };
   };
   ```

   The split central's conf needs
   `CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING=y` and a run queue
   that holds a whole palette and the pick for every half at once, four
   invocations each: `CONFIG_ZMK_SPLIT_BLE_CENTRAL_SPLIT_RUN_QUEUE_SIZE=8` for
   two halves (ZMK's default is 5; both asserted at build time); the port
   comes with the central's CDC ACM port (`CONFIG_USB_CDC_ACM`).

   The effect's node, likewise on every device under the one name
   ([config/effect.dtsi](config/effect.dtsi) here), for the pick of the
   effects; `CONFIG_LANTERN_EFFECT` is on wherever it exists:

   ```dts
   / {
       behaviors {
           effect: effect {
               compatible = "lantern,behavior-effect";
               #binding-cells = <2>;
           };
       };
   };
   ```

   The hit's node, likewise on every device under the one name
   ([config/hit.dtsi](config/hit.dtsi) here), for the effects that cross the
   split (the crossing ring, the beams); `CONFIG_LANTERN_HIT` is on wherever
   it exists:

   ```dts
   / {
       behaviors {
           hit: hit {
               compatible = "lantern,behavior-hit";
               #binding-cells = <2>;
           };
       };
   };
   ```

## Building this repository

```sh
./scripts/build.sh              # both halves and the dongle, firmware/<shield>.uf2
./scripts/build.sh --logging    # with USB logging; the module's lines are INFO
./scripts/build.sh --help       # every option
```

Docker, a west workspace under `~/.cache/zmk-lantern`, the images in
`firmware/`. They are the build check of Cyboard's stock Imprint config (both
halves and the `imprint_dongle` shield) plus the module, not a keyboard's
firmware: a consumer's keymap and roles are its own, and the check build's
`positions` are the chain in key order, not a measurement. CI builds the same
targets on every pull request and weekly against the moving branches. Every
push to `main` refreshes one rolling draft release of the notes, with nothing
attached; publishing the draft by hand creates the tag.

Log lines (a `--logging` image, through canon's `dongle.py log` or any serial
reader at 115200): `start: usb powered, boot light 1000 ms`, `lit: boot light`,
`lit: usb powered`, `dark: on battery`, `dark: idle` (with the intro
`start: usb powered, intro 6000 ms`, `lit: intro`, and `intro: usb powered`
when power arrives), and the countdown's
`stage N` at every move; the palette's:
`slots 0-1 RRGGBB RRGGBB stored` (or `unchanged`) on every device, and on the
central `N colour(s) from <port>` and `resent to half N`; the pick's: `effect
0000000X stored` (or `unchanged`) on every device, and on the central `pick
0000000X from <port>` and `resent to half N`; the picture logs only a frame
the strip refused (`frame not written`).
