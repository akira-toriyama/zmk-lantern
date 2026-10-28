# zmk-lantern

A [ZMK](https://zmk.dev) module for a keyboard's per-key LEDs. It lights them
while USB powers the keyboard and the keyboard is in use, keeps them dark on
battery and after the idle timeout, plays an intro at boot and whenever a
cable is plugged in, paints the keys from a palette of six colours the host
hands the keyboard over USB, and draws an effect from every key you press: a
ring, a disc, a stain, a ring crossing both halves, beams, water or a dart,
in the palette's colours or in one colour of the host's choosing; or, while
you hold a layer, lights the keys that layer gives a meaning, in its colour.
On a split keyboard each half lights for itself, the central relays the key
presses, the held layer and the host's choices to the halves, and a half
that was away catches up when it
is back; the host's channel is the split central's USB port, so a keyboard
that is not a split has the conf's defaults and a key's pick, without a
palette. A keyboard whose LED supply has an enable pin can hand that pin to
the module, which then cuts the LEDs' power whenever they are dark.

The module paints the strip itself: ZMK's underglow stays off on a keyboard
that carries it. The name: a beacon shows others where you are; a lantern
lights what is at hand. The sibling module
[zmk-beacon](https://github.com/akira-toriyama/zmk-beacon) is the beacon.

## Using it in a ZMK config

### The least

1. `config/west.yml`: the module as a project, pinned by commit:

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

2. The keyboard's overlay (a split: each half's, or a dtsi both include): the
   strip and the key under each LED.

   ```dts
   &led_strip {
       chain-length = <21>;
   };

   / {
       lantern_keys: lantern_keys {
           compatible = "lantern,keys";
           strip = <&led_strip>;
           positions = < 0 1 2 3 4 5 /* ... one entry per LED, in chain order: the key position under it */ >;
       };
   };
   ```

   `strip` is the board's LED strip node (the one ZMK's `zmk,underglow`
   chosen names), with `chain-length` set to the real LED count: the driver
   sends and buffers the whole chain at every frame, and `positions` has one
   entry per LED (asserted). `positions` is measured on the hardware, one LED
   at a time (light one, see which key it sits under); an LED under no key
   takes the nearest key's position. The key positions are the keymap's,
   global across a split, and the key count is the keymap's
   (`ZMK_KEYMAP_LEN`: the longest physical layout's map where the keyboard
   has layouts, else the chosen matrix transform's; a Corne that chooses
   its 5-column layout still counts 42). The module takes each key's place
   from the keyboard's chosen `zmk,physical-layout` (the key's centre, in
   the layout's hundredths of a key unit), so a keyboard whose layout has
   `keys` needs nothing more for the effects; one without writes `coords`
   (below). The places follow the chosen layout: a layout selected at run
   time instead (ZMK Studio) numbers the keys differently, and the LEDs and
   the effects land a key off until the chosen one is selected again. The
   check build of ZMK's Corne is this least plus the split's nodes and
   every effect: [config/corne.overlay](config/corne.overlay),
   [config/corne.conf](config/corne.conf).

3. The keyboard's conf:

   ```
   CONFIG_ZMK_RGB_UNDERGLOW=n
   ```

   The module is the strip's one writer; ZMK's underglow would repaint it at
   every tick (asserted at build time). Everything else is on by default
   where its node exists: `CONFIG_LANTERN_KEYS` from the `lantern,keys` node,
   `CONFIG_LANTERN_ON_USB_POWER` with it (the rule: lit while USB powers the
   keyboard and it is in use, dark otherwise), `CONFIG_LANTERN_OFF_WHEN_IDLE`
   (dark after ZMK's `CONFIG_ZMK_IDLE_TIMEOUT`, the consumer's line, 30 s by
   ZMK's default), `CONFIG_LANTERN_INTRO_BANDS` (the boot light: a second of
   the palette's bands at boot and when a cable is plugged in). With no more
   than this, every key is white (the palette needs the split's palette
   node, below; with it, the palette's first colour), and no effect is
   built: the effects and the other pictures are conf lines, below.

### Then, as you want them

- **The LED rail.** A keyboard whose LED supply has an enable pin (the pin
  the board's `EXT_POWER` node drives as `control-gpios`) hands it to the
  module with a `lantern,rail` node, and the module turns the supply off
  whenever the LEDs are dark, so on battery they draw nothing at all. ZMK's
  `ext_power` driver must then let go of the pin: the node stays disabled,
  and a gpio hog holds the pin low from early boot until the module takes it
  (the driver would turn the rail on at init and restore a stored state).
  Without a rail node the supply stays the board's and dark is a black
  frame.

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

  / {
      lantern_rail: lantern_rail {
          compatible = "lantern,rail";
          gpios = <&gpio1 2 GPIO_ACTIVE_HIGH>;
          settle-ms = <100>;
      };
  };
  ```

  `settle-ms` is the LEDs' power-up time before the first frame, the
  `EXT_POWER` node's `init-delay-ms`. `CONFIG_LANTERN_RAIL` is on by default
  where the node exists. No `&ext_power` key in the keymap: the driver is
  out of the build.

- **The bands: `slots`.** One palette slot, 0 to 5, per key position, in
  position order (the whole keymap, both halves of a split), the colour the
  key shows in the bands, the default picture, and the colour of the
  crossing ring and the beams from that key. A table written in the keymap's
  shape reads as the bands. Without it every key is slot 0.

  ```dts
  &lantern_keys {
      slots = < 0 0 0 1 1 1 /* ... one entry per key position */ >;
  };
  ```

- **The places: `coords`**, for a keyboard without a physical layout (or to
  override it): an x and a y per key position, in hundredths of a key unit
  on the whole board (both halves of a split, so a distance reads the same
  from either), at most 30000 each; keymap-drawer's physical layout times
  100 is one source. What measures the board (every effect but the layer
  keys, the gradient, the star waves) asserts one or the other at build
  time.

  ```dts
  &lantern_keys {
      coords = < 0 200 100 200 /* ... an x and a y per key position */ >;
  };
  ```

- **The scatter's shares: `scatter-weights`**, a percentage per palette slot
  (six, at most 100 in all), only with `CONFIG_LANTERN_SCATTER`.

- **The pictures, the effects and the intros** (what they are: What it does,
  below). The picture is one Kconfig choice, `LANTERN_PICTURE`, the bands
  (`CONFIG_LANTERN_BANDS`) by default: `CONFIG_LANTERN_SOLID=y` paints every
  key in slot 0, `CONFIG_LANTERN_GRADIENT=y` blends the colours across the
  board by the keys' x, `CONFIG_LANTERN_SCATTER=y` throws the slots over the
  keys at random, `CONFIG_LANTERN_NO_PICTURE=y` leaves every key black so
  that only the effects light. `CONFIG_LANTERN_COUNTDOWN` (on by default with
  the idle rule and the palette) changes the picture every
  `CONFIG_LANTERN_COUNTDOWN_STAGE_MS` (10000) from the last key until the
  idle dark; `=n` keeps the picture. The effects are built one by one:
  `CONFIG_LANTERN_THIN_RING=y`, `CONFIG_LANTERN_DISC=y`,
  `CONFIG_LANTERN_STAIN=y` (the ring that dyes), `CONFIG_LANTERN_CROSS_RING=y`
  (it brings the hit, below), `CONFIG_LANTERN_BEAMS=y`,
  `CONFIG_LANTERN_WATER=y`, `CONFIG_LANTERN_DART=y`,
  `CONFIG_LANTERN_LAYER_KEYS=y` (it brings the layer, below); which of the built ones
  draw is the pick's (below), and without a pick the choice
  `LANTERN_EFFECT_DEFAULT` (`CONFIG_LANTERN_EFFECT_DEFAULT_<EFFECT>=y`; the
  stain when built, else the first built) in `CONFIG_LANTERN_EFFECT_COLOUR`
  (`0xRRGGBB`; 0, the default, the effects' own colours from the palette).
  The intros: `CONFIG_LANTERN_INTRO_BANDS` (on by default) and
  `CONFIG_LANTERN_INTRO_STAR_WAVES=y`; which plays is the pick's, and
  without one the choice `LANTERN_INTRO_DEFAULT` (the star waves when built,
  else the bands); `CONFIG_LANTERN_INTRO_BANDS=n` with no other intro built
  drops the boot light. `CONFIG_LANTERN_BRIGHTNESS` (default 16, percent of
  full scale) sets the frame's cap and, by default, the picture's
  brightness, which `CONFIG_LANTERN_PICTURE_BRIGHTNESS` (at most it) lowers
  alone; the effects go to full scale under the cap. A missing or
  conflicting option fails the build with a message naming it.

- **The split.** The palette and the picks travel from the central to the
  halves as ZMK behaviors, carried by the split transport under the node's
  name, so each node exists on every device of the split under the one name
  (the keymap's behaviors, or an overlay every device includes; the module's
  own check build includes [config/palette.dtsi](config/palette.dtsi),
  [config/effect.dtsi](config/effect.dtsi), [config/intro.dtsi](config/intro.dtsi),
  [config/hit.dtsi](config/hit.dtsi) and [config/layer.dtsi](config/layer.dtsi)
  from every overlay):

  ```dts
  / {
      behaviors {
          palette: palette {
              compatible = "lantern,behavior-palette";
              #binding-cells = <2>;
          };
          effect: effect {
              compatible = "lantern,behavior-effect";
              #binding-cells = <2>;
          };
          intro: intro {
              compatible = "lantern,behavior-intro";
              #binding-cells = <2>;
          };
          hit: hit {
              compatible = "lantern,behavior-hit";
              #binding-cells = <2>;
          };
          layer: layer {
              compatible = "lantern,behavior-layer";
              #binding-cells = <2>;
              layers = <1 0  2 1>; /* layer 1 in slot 0's colour, layer 2 in slot 1's */
          };
      };
  };
  ```

  `CONFIG_LANTERN_PALETTE`, `CONFIG_LANTERN_EFFECT`, `CONFIG_LANTERN_INTRO`,
  `CONFIG_LANTERN_HIT` and `CONFIG_LANTERN_LAYER` are on wherever their node
  exists; the hit (the central relaying each half's key presses to the other
  half) is what the crossing ring and the beams need, and every effect but
  the layer keys uses; the layer (the central telling the halves which keys
  the held layer binds) is what the layer keys need. Its `layers` pairs each
  layer to show, its index in the keymap, with a palette slot, 0 to 5; the
  central's node needs them (asserted) and only the central reads them, so
  a half's node may leave them out and a change of them is the central's
  image alone. The split
  central's conf needs `CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING=y`
  (a half's battery report is what tells the central the half is back, and
  the palette and the picks are resent to it then) and a run queue that
  holds a whole palette and both picks for every half at once, five
  invocations each: `CONFIG_ZMK_SPLIT_BLE_CENTRAL_SPLIT_RUN_QUEUE_SIZE=10` for
  two halves (ZMK's default is 5, enough for one; both asserted at build
  time). A device without a `lantern,keys` node, such as a dongle, keeps and
  forwards the palette and the picks without painting anything. On a split
  peripheral `CONFIG_LANTERN_USB_LATENCY` is on by default with the rule:
  while USB powers the half it asks the central for peripheral latency 0, so
  what the central relays shows at once (The split link, below); `=n` leaves
  the link at the central's latency.

- **The host.** With a USB CDC ACM port on the split central
  (`CONFIG_USB_CDC_ACM`), `CONFIG_LANTERN_PORT` is on by default, and a host
  sets the palette and the picks by writing a text line on that port (The
  palette and The picks, below). The port and the palette are a split
  central's: a keyboard that is not a split has neither. Without a host the
  keyboard has the Kconfig defaults, and a key of the central's keymap can
  pick (Without a host, below).

## What it does

| While | Rail (with one) | LEDs |
| --- | --- | --- |
| USB powers the half: a host, awake or asleep, or a charger | on | lit: the palette's picture |
| the half runs on battery | off | black |
| USB powers the half and its last key is `CONFIG_LANTERN_COUNTDOWN_STAGE_MS` (10 s) or more ago (`CONFIG_LANTERN_COUNTDOWN`) | on | lit: the whole half in the palette's next colour, a new picture every stage, the bands again after the last colour |
| USB powers the half, but ZMK's activity state is idle: `CONFIG_ZMK_IDLE_TIMEOUT` ms (the consumer's line) without a key, sensor or pointing input on this half | off | black, until the next input |
| the boot light: from boot and from each arrival of USB power, for the picked intro's length (the bands a second, the star waves ten) | on | the intro, cable or not |

- Each half decides for itself, from its own USB port and its own activity;
  nothing crosses the split. Typing on one half alone leaves the other to go
  dark.
- Plugging in turns the rail on (with one) and, `settle-ms` later, plays the
  intro and then writes the picture; unplugging writes a black frame and
  drops the rail 100 ms after. A reset, a suspend (the host asleep) or a
  resume on the cable changes nothing. Idle and the next input switch the
  LEDs the same way; the boot light is never cut short.
- A cable plugged into an idle half plays the intro and goes dark again until
  its next key: USB is no activity to ZMK, and lighting on it for longer would
  leave the LEDs on with no idle event to follow.
- The picture ([src/keys.c](src/keys.c)): each LED shows its key's colour in
  the picture, by default (the bands) the palette slot the `lantern,keys`
  node assigns to the key under it, so the palette's colours fall on the
  keyboard in bands. A slot the host left black shows slot 0, so one colour
  paints every key alike; without a palette every key is white (black in the
  no picture). Each colour is scaled so that its brightest channel is, by
  default, `CONFIG_LANTERN_BRIGHTNESS` percent of full scale (default 16),
  which keeps its hue and saturation; a grey or black scales to white. The
  picture's colours, with the countdown's other pictures and the stain's dye,
  can be dimmed below the brightness (`CONFIG_LANTERN_PICTURE_BRIGHTNESS`, the
  same by default) while the effects keep full scale and the frame's cap stays
  counted from the brightness. A frame goes out only when the LEDs light or
  go dark, when the palette changes while they are lit, when the countdown
  moves a stage, at each throw of the scatter, when the boot light ends, and
  every 30 ms while an effect moves or an intro plays; a still picture ticks
  at no rate.
- The solid (`CONFIG_LANTERN_SOLID`): instead of the bands, every key in slot
  0, the whole half in one colour; the countdown's pictures are then the
  whole half in each distinct colour in turn, slot 0's first.
- The gradient (`CONFIG_LANTERN_GRADIENT`): instead of the bands, the
  palette's colours blended across the whole board by each key's x: the
  leftmost key in the first distinct colour (colours that paint alike count
  once), the rightmost in the last, each key between them the two colours
  around its place mixed in proportion, so no band edges; a blend's brightest
  channel is the picture's brightness, as a colour's. The halves share the
  board: the left half shows the first stretch, the right half the last. A
  colour shows pure only on a key at its place; no key sits between the
  halves, so a colour whose place lies there never does.
- The scatter (`CONFIG_LANTERN_SCATTER`): instead of the bands, the palette's
  slots thrown over the half's keys at random, each slot on as many keys as
  its share of the node's `scatter-weights` (percent per slot, at most 100 in
  all; slot 0 takes what the others leave), thrown again every
  `CONFIG_LANTERN_SCATTER_MS` (default 10000) while the LEDs are lit and at
  every light. Each half throws for itself.
- The no picture (`CONFIG_LANTERN_NO_PICTURE`): every key black while the
  LEDs are lit, so that only the effects light, over black keys (the stain's
  dye stays, as over any picture). The boot light shows the picked intro, by
  default the bands, so that a half still shows it has power; on the cable
  the keys turn black when it ends. With the countdown on, its first picture
  is the black one and the others the whole half in a colour.
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
- The effects: a picture that moves after a key, the seven below, and the
  layer keys, which follow the held layer instead (below the dart). The conf
  builds the ones it wants into the image, and the pick says which of them
  draw (The picks, below): the host's, kept across boots, or without one the
  effect the Kconfig choice `LANTERN_EFFECT_DEFAULT` names, in the pick's
  colour or the effects' own. An effect pulls an LED toward its colour at
  full brightness and never dims the picture; a frame draws at most what
  every LED white plus two keys white at full brightness would (an estimate
  of 280 + 100 mA for 29 LEDs at 16 %, unmeasured), so more keys lit at once
  by an effect on a white picture share that and dim alike. The brightness
  must stay below 100 % with an effect (asserted). An effect lives its
  design's time after a press, or on a larger board the time its shape needs
  to cross it (a ring until it has left the board, the thin ring its half, a
  bar until it has run the board's width; measured from the keys' places at
  boot), so a wider board keeps it in sight; the beams keep the design's
  fade, which answers to the gap between the halves. A frame keeps moving
  while a press is within the picked effects' life. The presses an effect
  still draws sit in a ring of sixteen, both halves together: more presses
  than that within the longest picked life (ten a second on a 29-LED half
  with the crossing ring, fewer on a larger board) drop the oldest press's
  effect in one frame.
- The thin ring (`CONFIG_LANTERN_THIN_RING`; the pick's `thin-ring`): from
  each key this half presses (and, with the hit below, from the other half's
  keys too, in across the split) a ring one key wide spreads at twelve key
  units a second and fades out over a second, so it has crossed the half when
  it is gone; each press takes the palette's next colour, at full brightness
  within the frame's cap (above), and a key under two rings shows the
  stronger one's colour.
- The disc (`CONFIG_LANTERN_DISC`; the pick's `disc`): from each key this
  half presses (and, with the hit below, from the other half's keys too, the
  disc then living long enough to cross the whole board) a rim spreads at
  twelve key units a second, and a key it has passed lights at full
  brightness within the frame's cap (above), which holds a disc harder than a
  ring, and fades back to the picture over half a second, its colour running
  once through the palette's colours in order as it fades: brightest at the
  rim, dark behind it, the palette's colours passing inside. A key under two
  discs shows the stronger one's colour.
- The stain (`CONFIG_LANTERN_STAIN`; the pick's `stain`, the thin ring that
  dyes, and it brings the thin ring): a key within about 4.8 key units of the
  press, where the ring's crest still has 0.6 of its full strength, keeps the
  ring's colour at the picture's brightness from the moment the crest reaches
  it (where two rings have passed, the newer press's colour), so the half is
  painted over in the palette's colours as you type, each press in the next
  colour (a ring in from the other half dyes the keys within its reach too).
  The dye goes when the countdown changes the picture (the next key brings
  the picture back and its ring dyes it afresh), when the pick changes and
  whenever the LEDs light; without the countdown it stays until the LEDs go
  dark.
- The crossing ring (`CONFIG_LANTERN_CROSS_RING`; the pick's `cross-ring`, and
  it brings the hit, below): the thin ring's shape, one key wide at twelve key
  units a second, from a pressed key of either half across the whole board:
  a press of this half spreads out through the other half, and a press of
  the other half, relayed by the split central, comes in from the gap. It
  keeps its strength until it has left the board, at full brightness within
  the frame's cap (above), and its colour is the pressed key's slot (the
  node's `slots`), so both halves draw one press in one colour. A key under
  two rings shows the stronger one's colour.
- The beams (`CONFIG_LANTERN_BEAMS`; the pick's `beams`): from each pressed
  key four beams run outward at twelve key units a second, two along its row
  and two along its column, each four fifths of a key wide and fading out
  over a second, so the row's have left the half well before they are gone,
  and with the hit a press of the other half sends its row's beam in across
  the split. A key is on the row or the column when its y or x is within half
  a key unit of the pressed key's, so a thumb cluster half a unit off the
  grid makes rows and columns of its own. The colour is the pressed key's
  slot, as the crossing ring's, at full brightness within the frame's cap; a
  key under two beams shows the stronger one's colour.
- The water (`CONFIG_LANTERN_WATER`; the pick's `water`): each pressed key
  starts waves, a front spreading at twelve key units a second with a
  wavelength of three key units behind it, fading out over a second and a
  half and weaker the farther from the key (with the hit the other half's
  presses start waves too, which come in across the split low). The waves of
  every press add up: where two crests meet the surface rises higher, where
  a crest meets a trough it stays still. A key is pulled from the picture
  along the palette's colours by the surface's height, a full crest to the
  first, a full trough to the last, a lower wave to the colours between, and
  as far as the surface is from still, at full brightness within the frame's
  cap: the still surface shows the picture (black with the no picture), the
  pressed key's first waves flicker between the first and last colours four
  times a second, and lower waves, later or farther out, between colours
  nearer the middle.
- The dart (`CONFIG_LANTERN_DART`; the pick's `dart`): from each pressed key
  a bar runs across the rows toward the far side of the board, at sixteen
  key units a second (a key left of the board's middle sends it right, one
  right of it left), full at its head and gone four fifths of a unit from
  it, full on the pressed key's row and two fifths as strong on the other
  rows, fading out over 1.2 s, in the palette's first colour at the key and
  sliding along the colours as it runs, the last colour a board's width
  away, at full brightness within the frame's cap (with the hit the bar runs
  on through the other half, and a press of the other half brings it in
  across the split); a key under two darts shows the stronger one's colour.
- The layer keys (`CONFIG_LANTERN_LAYER_KEYS`; the pick's `layer-keys`, and
  it brings the layer, below): while you hold a layer that the `layer` node's
  `layers` names, the keys that layer gives a meaning of its own light in
  its colour, the palette slot `layers` gives it, at full brightness within
  the frame's cap (above), and the rest stay as the picture and the other
  effects leave them. A key of its own is one whose binding on that layer is
  neither `&none` nor `&trans` and differs from the default layer's at that
  key, so thumbs a layer repeats from the default layer stay dark. The
  highest held layer that `layers` names shows; a held layer it does not name
  is passed over (keep out a layer a tap raises, such as one behind a
  hold-tap that holds while undecided). A layer shows 60 ms after it comes
  into view, so a short tap does not flash, and goes as soon as no named
  layer is held. A press draws nothing of it; picked with other effects it
  draws over them and shares the frame's cap with them, so its keys dim
  while a press's effect lives, and a layer of many keys or a whitish
  colour shows dimmer than one of few. A pick's colour paints every layer
  in it, so layers told apart by colour alone then look alike. A layer of
  one half's keys lights that half alone, and only while that half is lit:
  a half gone dark for idle shows it from its own next input.
- The pick's colour: with one, every picked effect draws in that one colour
  in place of its own source above (the stain dyes in it too); the picture
  keeps the palette's colours.
- The intros: what the boot light plays, the two below. The conf builds the
  ones it wants into the image (`CONFIG_LANTERN_INTRO_BANDS`, on by default;
  `CONFIG_LANTERN_INTRO_STAR_WAVES`), and the pick says which plays (The
  picks, below): the host's, kept across boots, or without one the intro the
  Kconfig choice `LANTERN_INTRO_DEFAULT` names (the star waves when built,
  else the bands). The boot light opens at boot, cable or not, and again
  whenever USB power arrives, lasts the picked intro's length, and then the
  rule applies; nothing cuts it short, power arriving while it plays does not
  restart it, and keys pressed meanwhile show no effect. A pick that arrives
  while one plays shows at the next boot light.
- The bands (`CONFIG_LANTERN_INTRO_BANDS`; the pick's `bands`): the palette's
  bands for a second, each key its slot's colour at the picture's brightness,
  whatever the picture, so that a half shows it has power.
- The star waves (`CONFIG_LANTERN_INTRO_STAR_WAVES`; the pick's
  `star-waves`): a ten-second show in red, blue and green up to full
  brightness within the frame's cap, not the palette's colours. Each half
  plays it alone, mirrored, from its outermost key to its innermost thumb and
  back: a star crosses the half, red over the outer third of the depths, blue
  over the middle and green over the inner (the thirds the burst ends in),
  and leaves the keys glittering in random primaries; a red, a blue and a
  green wave follow it in; the thumb cluster, its inner keys brightest,
  takes each wave's colour as it arrives, red, then blue, then green, and
  pulses in the three by turns; a red, a blue and a green ring burst back
  out, and the last leaves every key in its third of the half by depth, the
  outer red, the middle blue and the inner green, which hold a moment; the
  three bands then deepen into crests and flow outward at the waves' pace
  for three seconds, each key dark between one colour and the next, and
  stand again, until a star runs back out from the thumbs over them, the
  keys glittering after it as at the start, the last of it out at the end.
  No key mixes two of the colours, and no other colour shows, white
  included.

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
half as one invocation of the node (param1 the key position,
[src/hit.c](src/hit.c)), from a work queue of the module's own, the relay's
([src/relay.c](src/relay.c)), and only to a half whose
link runs at latency 0, the sign that USB powers it and its LEDs may be lit; a
half on battery is dark and gets nothing. A half notes a relayed press as a
hit for its effects while its LEDs are lit, as one of its own keys, and its
countdown's clock ignores it (a stock half as the central counts the other
half's keys through its own listener, as ZMK's idle does there). The crossing
ring draws it from the other half's key; the beams, the thin ring, the
disc, the water and the dart do too, in their own colours (the beams' row
comes in across the split, the water's waves low, the dart's bar on its
way), the stain dyes with it, and it counts in the press count the thin
ring's colours follow. Delivery is not confirmed, as the palette's, and the
hits share the central's run queue with the palette (the source says what a
half that vanishes while lit costs).

The layer (`CONFIG_LANTERN_LAYER`, on wherever the `lantern,behavior-layer`
node exists on a BLE split, named `layer` on every device like the hit's):
the split central, where the keymap runs, sends every lit half which keys
the layer that shows binds to something of its own and the palette slot of
its colour, whenever that changes ([src/layer.c](src/layer.c)), from the
same work queue as the hit and to the same halves, those whose link runs at
latency 0 ([src/relay.c](src/relay.c)). A half whose link comes to latency 0,
as USB power arrives, is sent the state of the moment. A half forgets the
state when its link to the central goes and when its USB power goes, and
takes none on battery, so a cable plugged back in shows no layer until the
central's state arrives (under the intro the plug-in opens) rather than one
from before. Nothing is kept in the settings: the state is a held key's. A
change is one invocation a lit half for a keymap of up to 61 key positions,
two up to 122, each device counting from its own keymap; the last is marked,
so a half whose keymap counts other chunks than the central's still takes
each state whole.

## The palette

Six colours (`0xRRGGBB`, slots 0..5) that the host hands the keyboard's split
central and that the central and every half keep in their settings
(`lantern/palette/colours`); a half's key LEDs show them in the picture
(by default the bands its `lantern,keys` node draws), repainted when a
palette arrives while they are lit and shown at every boot. A set of fewer
than six colours clears the rest to `0x000000`, which the LEDs show as slot
0.

- The host writes a text line on the central's USB CDC ACM port: `palette
  RRGGBB [RRGGBB ...]` (one to six) sets slots 0.. and clears the rest,
  answered with `palette ok` and the six slots as stored; `palette` or
  `palette?` is answered with the six slots, or `palette none` before any; a
  bad line gets `palette error: <reason>` ([src/port.c](src/port.c) is the
  grammar). The central's code invokes the `lantern,behavior-palette` node,
  which ZMK's split transport carries to every half by the node's name, one
  pair of slots per invocation ([src/palette.c](src/palette.c)); nothing is
  bound to a key.
- A half that was away when the host wrote, or that lost its settings, gets
  the palette when it is back: the central resends it a second after each
  battery report of a half, the first of which follows every (re)connection.
  A half writes its flash only when a colour changed. A stored palette of
  another slot count (a device flashed across a change of the count) is
  ignored at load: `palette none` until the host sends again.

## The picks

Two values the host chooses (or a key of the central's keymap: Without a
host, below), which the central and every half keep in their settings and
the central resends to a half that is back, one mechanism
([src/pick.c](src/pick.c)) with an instance each: the pick of the effects,
which of the effects built into a half's image draw and in which colour
(`lantern/effect/mask`: the mask, then the colour; [src/effect.c](src/effect.c)),
so that the effect changes without a flash, and the pick of the intro, which
of the intros built in the boot light plays (`lantern/intro/mask`,
[src/intro.c](src/intro.c)). The image carries every effect and intro the
conf builds; a half draws the picked effects it has, every one of them over
the last when the pick names several, and with no pick stored, or none of its
effects built, the one the Kconfig choice `LANTERN_EFFECT_DEFAULT` names
(`CONFIG_LANTERN_EFFECT_DEFAULT_CROSS_RING` and so on, one member per built
effect; without a line the stain when it is built, else the first built
effect); it plays the picked intro when it has it, else the one
`LANTERN_INTRO_DEFAULT` names (`CONFIG_LANTERN_INTRO_DEFAULT_BANDS`,
`CONFIG_LANTERN_INTRO_DEFAULT_STAR_WAVES`; without a line the star waves when
built, else the bands): the only picks a keyboard without a host has.

- The host writes a text line on the central's USB CDC ACM port: `effect
  <name> [<name> ...] [RRGGBB]` picks these effects on every half and the
  central, in the one colour when one is given (every picked effect then
  draws in it, the stain dyes in it, and the picture keeps the palette's
  colours; `000000` is none) or in the effects' own colours from the palette
  without, answered with `effect ok`, the pick's names in the table's order
  and the colour when set; `intro <name>` picks the intro the same way, one
  name (two get `intro error: one intro at a time`), answered with `intro
  ok` and the name; `effect` or `effect?`, `intro` or `intro?` are answered
  with the names (and the colour), or `effect none` / `intro none` before
  any; a name not in the word's table gets `effect error: no such effect`
  (or the intro's) and changes nothing ([src/port.c](src/port.c) is the
  grammar). The names, and the bit each carries on the wire: the effects'
  `thin-ring`, `disc`, `stain`, `cross-ring`, `beams`, `water`, `dart`,
  `layer-keys` ([src/effect.h](src/effect.h)'s table; bit 7 is no
  effect's), the intros' `bands`, `star-waves`
  ([src/intro.h](src/intro.h)'s). The central's code invokes the
  `lantern,behavior-effect` or `lantern,behavior-intro` node, which ZMK's
  split transport carries to every half by the node's name; a key of the
  central's keymap may invoke the effect's the same way (Without a host,
  below).
- A half that was away when the host wrote gets both picks with the palette,
  a second after each of its battery reports; a half writes its flash only
  when a pick changed. A change of the effects' pick clears the effects' state
  (a ring half-way out, the stain's dye), so the new effect starts clean; a
  change of the intro's shows at the next boot light. A device stores the
  host's bits as sent, the ones its image lacks included, so the reply names
  the pick as made (with the names this image knows; `effect none` or `intro
  none` when it knows none of the stored bits) and an image with more effects
  or intros shows it. The colour is measured in the host harness (every
  effect in one colour, and the frames without one unchanged to the byte);
  what ran on hardware is in canon's CLAUDE.md (Invariants, the effect
  paragraph).

### Without a host

A keyboard whose central has no host on its port picks in its conf and its
keymap. The conf: `CONFIG_LANTERN_EFFECT_DEFAULT_<EFFECT>=y` names the effect
drawn while no pick is stored (one of the built; without the line the stain
when built, else the first built), `CONFIG_LANTERN_EFFECT_COLOUR=0xRRGGBB` its
colour (default 0, the palette's colours), `CONFIG_LANTERN_INTRO_DEFAULT_<INTRO>=y`
the intro. The keymap, on the central: `&effect <mask> <colour>` on a key picks
on press, the mask a sum of the effects' bits in [src/effect.h](src/effect.h)'s
order (`thin-ring` 1, `disc` 2, `stain` 4, `cross-ring` 8, `beams` 16, `water`
32, `dart` 64, `layer-keys` 256; several draw together) and the colour
`0xRRGGBB` or 0 for the palette's (`&effect 4 0xFF0000`: the stain in red);
`&effect_next` on a key
picks the next effect in the table's order, round again after the last, and
keeps the colour (the node `lantern,behavior-effect-next`, in the keymap's
behaviors with `#binding-cells = <0>`, on the central's keymap;
`CONFIG_LANTERN_EFFECT_NEXT` is on wherever it exists on a split central,
and a split central it must be: the send is the central's). With no pick
stored its first press picks the effect after `LANTERN_EFFECT_DEFAULT` where
the central builds effects (a dongle builds none: the table's first) and
keeps `CONFIG_LANTERN_EFFECT_COLOUR` as read on the central, so a dongle's
conf sets it to the halves' value (it cannot read theirs). Both store the
pick as the host's line does, on the central and every half, and a stored
pick outlives a boot. The two bindings compile in this repository's check
build ([config/pick_keys.dtsi](config/pick_keys.dtsi)) and are unverified on
hardware: canon's keymap binds neither, its host picks.

## Building this repository

```sh
./scripts/build.sh              # every target of build.yaml, firmware/<shield>.uf2
./scripts/build.sh corne_left   # one
./scripts/build.sh --logging    # with USB logging; the module's lines are INFO
./scripts/build.sh --help       # every option
```

Docker, a west workspace under `~/.cache/zmk-lantern`, the images in
`firmware/`. They are build checks, not a keyboard's firmware: both halves
of the Cyboard Imprint (Cyboard's stock config plus the consumer-side lines
of [config/imprint.overlay](config/imprint.overlay), with the rail) and its
`imprint_dongle` shield, the split central with the port, and both halves of
ZMK's Corne on the nice!nano ([config/corne.overlay](config/corne.overlay):
no rail, no `slots`, no `coords`, the places from the shield's physical
layout). A consumer's keymap and roles are its own, and the check builds'
`positions` are the chain in key order, not a measurement. CI builds the same
targets on every pull request and weekly against the moving branches. Every
push to `main` refreshes one rolling draft release of the notes, with nothing
attached; publishing the draft by hand creates the tag.

Log lines (a `--logging` image, through any serial reader at 115200): `start:
usb powered, boot light on` (or `none`), `lit: boot light 10000 ms` (the
picked intro's length), `lit: usb powered`, `boot light: usb powered` when
power arrives, `dark: on battery`, `dark: idle`, and the countdown's `stage
N` at every move; the palette's: `slots 0-1 RRGGBB RRGGBB stored` (or
`unchanged`) on every device, and on the central `N colour(s) from <port>`
and `resent to half N`; the picks': `effect: 0000000X rrggbb stored` (or
`unchanged`; `intro: 0000000X stored` without a colour) on every device, and
on the central `effect: pick 0000000X rrggbb from <port>` and `effect: resent
to half N` (the intro's without the colour: `intro: 0000000X stored`, `intro:
pick 0000000X from <port>`); the picture logs only a frame the strip refused
(`frame not written`).

The one keyboard this module has run on is the Cyboard Imprint of
[akira-toriyama/canon](https://github.com/akira-toriyama/canon), whose config
carries the real overlays and confs; what was seen on its hardware, and when,
is in [CLAUDE.md](CLAUDE.md).
