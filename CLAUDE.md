# CLAUDE.md

Claude Code notes: what breaks, where the reasoning lives (the source header),
how to build. Usage: [README.md](README.md). English only.

## What this repository is

- A ZMK module for the key LEDs of a split half: lit while USB powers the half
  and ZMK's activity state is active, dark and unpowered on battery and while
  idle, a boot light at boot and when USB power arrives
  (`CONFIG_LANTERN_ON_USB_POWER`, `CONFIG_LANTERN_OFF_WHEN_IDLE`;
  [src/on_usb_power.c](src/on_usb_power.c), binding `lantern,rail`); their
  picture, painted by the module itself, each LED the
  palette slot the consumer's node assigns to the key under it, and the
  countdown to the idle dark: the pictures cycling every ten seconds, the
  bands and then each colour of the palette (`CONFIG_LANTERN_KEYS`,
  `CONFIG_LANTERN_BRIGHTNESS`, `CONFIG_LANTERN_PICTURE_BRIGHTNESS`,
  `CONFIG_LANTERN_COUNTDOWN`,
  `CONFIG_LANTERN_COUNTDOWN_STAGE_MS`; [src/keys.c](src/keys.c), binding
  `lantern,keys`); the pictures in place of the bands, one Kconfig choice
  (`LANTERN_PICTURE`, the bands `CONFIG_LANTERN_BANDS` by default while
  canon t-skdt tries the others one image at a time; src/keys.c): the solid,
  every key in slot 0 (`CONFIG_LANTERN_SOLID`), the gradient, the colours
  blended across the board by the keys' x in `coords`
  (`CONFIG_LANTERN_GRADIENT`, which selects `CONFIG_LANTERN_GEOMETRY`), the
  scatter, the slots thrown over the keys at random by the node's
  `scatter-weights` and thrown again every ten seconds
  (`CONFIG_LANTERN_SCATTER`, `CONFIG_LANTERN_SCATTER_MS`), and the no
  picture, every key black under the effects and the bands for the boot
  light (`CONFIG_LANTERN_NO_PICTURE`); the
  effects that move the picture after a key, the thin ring, the disc, the
  stain, the crossing ring, the beams, the water and the dart
  (`CONFIG_LANTERN_THIN_RING`, `CONFIG_LANTERN_DISC`, `CONFIG_LANTERN_STAIN`,
  `CONFIG_LANTERN_CROSS_RING`, `CONFIG_LANTERN_BEAMS`, `CONFIG_LANTERN_WATER`,
  `CONFIG_LANTERN_DART`, each built into the image by the consumer's conf,
  off by default;
  one file an effect under src/effects/ on the engine of src/keys.c, the
  table and the node's `coords` through src/frame.h and
  `CONFIG_LANTERN_GEOMETRY`), and the
  pick of which built effects draw and in which colour, the host's over the
  central's port or a key's (`&effect <mask> <colour>`), kept across boots,
  or the Kconfig choice `LANTERN_EFFECT_DEFAULT` and `CONFIG_LANTERN_EFFECT_COLOUR`
  (`CONFIG_LANTERN_EFFECT`; [src/effect.c](src/effect.c), the effects' table
  [src/effect.h](src/effect.h), binding `lantern,behavior-effect`), and the
  next effect from a key (`CONFIG_LANTERN_EFFECT_NEXT`;
  [src/effect_next.c](src/effect_next.c), binding
  `lantern,behavior-effect-next`, the central only); the
  hit, each half's key presses relayed to the other half by the split
  central for the effects that cross the split (`CONFIG_LANTERN_HIT`;
  [src/hit.c](src/hit.c), binding `lantern,behavior-hit`); the
  intros, what the boot light plays, one file an intro under src/intros/ on
  the same engine and table shape as the effects (the table
  [src/intro.h](src/intro.h), `CONFIG_LANTERN_INTROS`): the bands, a second
  of the palette's bands (`CONFIG_LANTERN_INTRO_BANDS`, on by default), and
  the star waves, a ten-second show in red, blue and green from the outer
  keys to the thumbs and back (`CONFIG_LANTERN_INTRO_STAR_WAVES`, off by
  default; canon t-5v1f), and the pick of which plays, the host's over the
  central's port, kept across boots, or the Kconfig choice
  `LANTERN_INTRO_DEFAULT` (`CONFIG_LANTERN_INTRO`; [src/intro.c](src/intro.c),
  binding `lantern,behavior-intro`; canon t-qvgv); the two picks' one
  mechanism, the store, the save and the resend ([src/pick.c](src/pick.c),
  `CONFIG_LANTERN_PICK`); the split link's peripheral
  latency, 0 while USB powers a split peripheral and the central's on battery
  (`CONFIG_LANTERN_USB_LATENCY`; [src/latency.c](src/latency.c)); the palette: six
  colours the host hands the
  split central, which the central and every half
  keep in their settings (`CONFIG_LANTERN_PALETTE`;
  [src/palette.c](src/palette.c), binding `lantern,behavior-palette`); and
  the host's port, the split central's USB CDC ACM port reading the
  palette's and the pick's lines (`CONFIG_LANTERN_PORT`; [src/port.c](src/port.c)).
  Each source's header carries its design and every reason.
  Device names follow canon's
  [docs/glossary.md](https://github.com/akira-toriyama/canon/blob/main/docs/glossary.md):
  Cyboard Imprint (the halves), Imprint Dongle, Prospector Dongle.
- Two roles in one tree ([config/west.yml](config/west.yml)): a Zephyr module
  (`zephyr/module.yml` with `dts_root`, `dts/bindings/`, `src/`, `Kconfig`,
  `CMakeLists.txt`; no shield, no board) and the zmk-config (`config/`,
  `build.yaml`) that CI and [scripts/build.sh](scripts/build.sh) build:
  Cyboard's stock Imprint halves and its `imprint_dongle` shield plus the
  consumer-side lines each needs (`config/imprint.conf`, `config/imprint.overlay`,
  `config/imprint_left.conf`, `config/imprint_dongle.{conf,overlay}`,
  `config/palette.dtsi`, `config/hit.dtsi`, `config/effect.dtsi`). With Cyboard's stock roles the left half is a split
  central (canon's is a peripheral): the check build compiles the palette's
  central side on the left half and the dongle, its half side on both halves.
  Its `lantern,keys` node takes the chain in key order (`positions`), a
  placeholder a consumer replaces with its measurement.
- The one consumer, [canon](https://github.com/akira-toriyama/canon), pins
  this repository by commit SHA in its `config/west.yml` and carries the real
  overlays and confs; a merge here reaches the halves with canon's pin bump,
  which moves every canon image's firmware stamp (canon's CLAUDE.md).
- The sibling module [zmk-beacon](https://github.com/akira-toriyama/zmk-beacon)
  (the status advertisement) is the model for this repository's shape; the two
  stay separate on purpose (canon t-26y4, 2026-10-19).
- Tasks: on canon's board for now (`furrow list -r canon`, epic e-s8c7).

## Build

- `./scripts/build.sh [shield | board:shield] [--logging] [--kconfig CONFIG_X=V]...
  [--tag <name>] [--update]` (flags: its header). Docker, workspace `$ZMK_WS`
  (default `~/.cache/zmk-lantern`), images `firmware/<shield>[-logging][-kconfig][-<tag>].uf2`,
  then a summary (sha256 prefix, FLASH, RAM, revisions). `--update` moves
  zmk@main and zmk-keyboards@zephyr-4.1.
- The west topdir is never the module root: `$RUNNER_TEMP/ws` in CI, `$ZMK_WS/ws`
  locally, the repository passed as `-DZMK_EXTRA_MODULES` (as ZMK's
  `build-user-config.yml` does). Never `west init` inside the repository.
- Per build directory (`$ZMK_WS/ws/build/<image name>/`): `build.log` (west's
  output, FLASH/RAM at its end), `zephyr/.config`, `zephyr/zephyr.dts`,
  `zephyr/zmk.map`, `zephyr/zmk.elf`. A half's build shows
  `CONFIG_LANTERN_KEYS=y`, `CONFIG_LANTERN_ON_USB_POWER=y`,
  `CONFIG_LANTERN_COUNTDOWN=y`, `CONFIG_LANTERN_COUNTDOWN_STAGE_MS=10000` and
  `CONFIG_LANTERN_PALETTE=y` in `.config` and no `CONFIG_ZMK_RGB_UNDERGLOW`,
  a `lantern_keys` node with `compatible = "lantern,keys"`, a `lantern_rail`
  node with `compatible = "lantern,rail"` and a `palette` node with
  `compatible = "lantern,behavior-palette"` in `zephyr.dts`, the strip's
  `chain-length = < 0x1d >` (29), and `keys.c.obj`, `on_usb_power.c.obj` and
  `palette.c.obj` in `zmk.map`; a split peripheral half's (canon's both, the
  stock right) `CONFIG_LANTERN_USB_LATENCY=y` and `latency.c.obj` too; a split
  central's (the dongle, the stock left half) never the latency, and with a
  CDC ACM port (the dongle's board has one, the stock left half none)
  `CONFIG_LANTERN_PORT=y` and `port.c.obj`; the dongle's nothing of the rail
  or the keys; a half's `CONFIG_LANTERN_INTROS=y`, `CONFIG_LANTERN_INTRO_BANDS=y`,
  `CONFIG_LANTERN_INTRO_DEFAULT_BANDS=y` and `bands.c.obj`; every target
  `CONFIG_LANTERN_EFFECT=y`, `CONFIG_LANTERN_INTRO=y`, `CONFIG_LANTERN_PICK=y`
  and `effect.c.obj`, `intro.c.obj`, `pick.c.obj` from the effect and intro
  nodes (`config/effect.dtsi`, `config/intro.dtsi`, on every target); a split
  central's (the dongle, the stock left half) `CONFIG_LANTERN_EFFECT_NEXT=y`
  and `effect_next.c.obj` from the effect-next node (`config/effect_next.dtsi`,
  on every target, a driver on the central only), and with the effects or
  the effect-next node `CONFIG_LANTERN_EFFECT_COLOUR=0` (`.config` writes a hex symbol's default
  as `0`, autoconf.h as `0x0`); `config/pick_keys.dtsi` binds
  `&effect 1 0xFF0000` and `&effect_next` in a macro never invoked, so the
  bindings compile as a consumer's keymap would write them.
  `--kconfig CONFIG_LANTERN_THIN_RING=y`, `CONFIG_LANTERN_DISC=y`,
  `CONFIG_LANTERN_BEAMS=y`, `CONFIG_LANTERN_WATER=y` or
  `CONFIG_LANTERN_DART=y` adds
  `CONFIG_LANTERN_EFFECTS=y` and `CONFIG_LANTERN_FRAMES=y`,
  `CONFIG_LANTERN_GEOMETRY=y` and, the choice `LANTERN_EFFECT_DEFAULT` with
  no line of its own, `CONFIG_LANTERN_EFFECT_DEFAULT_<the first built
  effect>=y` unless a line names another;
  `CONFIG_LANTERN_STAIN=y` adds `CONFIG_LANTERN_THIN_RING=y`;
  `CONFIG_LANTERN_CROSS_RING=y` adds `CONFIG_LANTERN_EFFECTS=y`,
  `CONFIG_LANTERN_GEOMETRY=y` and `CONFIG_LANTERN_HIT=y`, which the hit node
  (`config/hit.dtsi`, on every target) turns on by itself, with `hit.c.obj`
  in zmk.map;
  `CONFIG_LANTERN_INTRO_STAR_WAVES=y` adds `CONFIG_LANTERN_FRAMES=y`,
  `CONFIG_LANTERN_GEOMETRY=y` and `CONFIG_LANTERN_INTRO_DEFAULT_STAR_WAVES=y`
  (the choice `LANTERN_INTRO_DEFAULT` with no line of its own, in place of
  the bands') and puts `star_waves.c.obj` in zmk.map, as each built effect
  puts its file's
  (`thin_ring.c.obj`, the stain's too, `disc.c.obj`, `cross_ring.c.obj`,
  `beams.c.obj`, `water.c.obj`, `dart.c.obj`);
  a half shows `CONFIG_LANTERN_BANDS=y`, or `CONFIG_LANTERN_SOLID=y`,
  `CONFIG_LANTERN_GRADIENT=y`, `CONFIG_LANTERN_SCATTER=y` or
  `CONFIG_LANTERN_NO_PICTURE=y` in its place,
  the gradient with `CONFIG_LANTERN_GEOMETRY=y`, the scatter with
  `CONFIG_LANTERN_SCATTER_MS=10000` (the choice `LANTERN_PICTURE` has no
  line of its own: a `--kconfig CONFIG_LANTERN_PICTURE=…` fails the build).
- Kconfig merge order, later wins: board defconfig → ZMK `app/prj.conf` → the
  shield's confs → the consumer's `config/<prefix>.conf` files →
  `EXTRA_CONF_FILE` fragments (build.sh: logging, `--kconfig`) → `-DCONFIG_*`.
  Cyboard's `boards/shields/imprint/Kconfig.defconfig` defaults
  `ZMK_RGB_UNDERGLOW=y` for every target in the manifest: the consumer's conf
  must say `=n` (`src/keys.c` asserts it on a half).
- ZMK tracks `main` and Cyboard's module `zephyr-4.1`, as canon does (its
  CLAUDE.md says why); the weekly run of `build.yml` warns early of an
  upstream break.
- Against canon's exact tree (its ZMK pin and patches): canon's
  `./scripts/build-zmk.sh imprint --module <this checkout>` (canon #267,
  2026-10-19): the images carry `-lantern` and a stamp of canon's pinned
  inputs plus that variant, which `dongle.py check` reports as `debug`.
- CI: `build.yml` → the local reusable `zmk-build.yml` (it says why not ZMK's);
  `shellcheck.yml`; the fleet gates. `release.yml` keeps a rolling draft
  release of the notes alone (glyph's reusable, verdict-only: the module ships
  no image), tagged when published by hand; consumers still pin a commit.

## Invariants

- **Never give the LED rail back to ZMK's `ext_power` driver**: the consumer's
  EXT_POWER node stays `status = "disabled"`. The driver turns the rail on at
  init and restores a stored `ext_power/state/EXT_POWER` at settings load (a
  stored off kept canon's left half dark, 2026-10-19), and with
  `CONFIG_ZMK_SLEEP=n` nothing turns the rail off again. The module drives the
  node's `gpios` itself; the hog holds the pin low from POST_KERNEL until it
  does.
- **ZMK's underglow stays off on a half (`CONFIG_ZMK_RGB_UNDERGLOW=n`, asserted
  in `src/keys.c`): the module is the strip's one writer.** `rgb_underglow.c`
  repaints every LED with its one colour and effect at each 50 ms tick while
  on, and saves and restores its on/colour state across boots, so a per-LED
  picture cannot share the strip with it (up to 2406809 the module lit the
  LEDs through it and overrode that saved state at every boot). With it off
  a frame is written only when something changed, from one work item on ZMK's
  low priority work queue (`src/keys.c`); `&rgb_ug` keys have no driver and
  do nothing.
- **The first evaluation runs from the settings commit handler (commit priority
  1), never from SYS_INIT**: `settings_load()` (ZMK `app/src/main.c`) runs
  after every init level and restores the palette in its set phase, so a boot
  light started from the commit phase paints the palette's picture; from
  SYS_INIT it would paint white and the palette's own commit would have to
  repaint. The frame reads the palette's atomics when it is composed, so the
  order of the two commit handlers does not matter.
- **All rail state lives on the system work queue** (the USB and activity
  events, the steps, the first evaluation); a new entry point submits work
  there, never touches the state. **The picture is composed and written on
  ZMK's low priority work queue** (`src/keys.c`: one work item, the wanted
  state an atomic, two submissions coalesce): `led_strip_update_rgb()` blocks
  for the SPI transfer and stays off the system work queue, where ZMK
  processes key events, and off the Bluetooth receive queue, where a half
  runs the palette behavior. Callers say lit, dark or changed, and when the
  boot light lasts (`lantern_keys_boot_light()`, which the intros build,
  `CONFIG_LANTERN_INTROS`: told on, it resolves the pick, starts the intro
  and returns its length), and never touch a pixel. The boot light opens at
  boot and when USB power arrives (`src/on_usb_power.c`'s listener, on the
  system work queue with the rest of the rail state) and lasts the picked
  intro's length; a moving intro's script asserts it fits its length
  (`src/intros/star_waves.c`). **The
  countdown's clock and tick live on the system work queue**
  too (`src/keys.c`: the time of the half's last key, a delayable work item
  that computes the stage and arms itself for the next boundary; the stage
  crosses to the painter as one atomic), so the light, the blank and the
  boot light are system work queue calls, the refresh any thread's.
- **The idle timeout is ZMK's `CONFIG_ZMK_IDLE_TIMEOUT`, set by the consumer,
  and the dark is ZMK's idle state**: it already counts a half's keys,
  sensors and pointing input. The countdown (`CONFIG_LANTERN_COUNTDOWN`)
  changes the picture every `CONFIG_LANTERN_COUNTDOWN_STAGE_MS` (10 s) from
  a clock of its own, the half's key positions
  (`zmk_position_state_changed`), kept while dark too: ZMK keeps the time of
  its last input to itself. A light does not restart that clock (a cable
  plugged into a half that typed a while ago lights the stage its keys say;
  Opus review 2026-10-20) unless it is already past the idle timeout, the
  sign that a sensor or pointing input woke ZMK; such input moves ZMK's idle
  and not this clock (canon t-gsm2: the Imprint's trackballs are unused). Up
  to 750f413 a stage was the timeout over the colour count and the last
  colour held until the dark; the user chose the ten-second cycle on
  hardware (t-x82q). A cable plugged into an idle half plays the boot
  light's intro and goes dark at its end, a timed light that needs no idle
  event, and lights nothing else until its next key, by design (the source
  says why).
- A target without a `lantern,keys` node builds none of the painting
  (`CONFIG_LANTERN_KEYS` depends on `DT_HAS_LANTERN_KEYS_ENABLED`) and none of
  the rail (`CONFIG_LANTERN_ON_USB_POWER` depends on `LANTERN_KEYS` and on
  `DT_HAS_LANTERN_RAIL_ENABLED`: the rail rule has nothing to show without
  the picture); one without a `lantern,behavior-palette` node (canon's
  Prospector Dongle) none of the palette. Exactly one node of each per target
  (BUILD_ASSERT).
- **`positions` and `chain-length` are the consumer's measurement, `slots` its
  bands, `coords` its geometry**: `src/keys.c` asserts the lengths (`positions`
  as many as the strip's `chain-length`, `slots` as many as `ZMK_KEYMAP_LEN`,
  `coords` twice that where something measures the board, an effect, the
  gradient or the intro, `CONFIG_LANTERN_GEOMETRY`, present only then)
  and the ranges (a position below the key count, a slot below
  `LANTERN_PALETTE_SLOTS`, a coordinate at most 30000 so that a squared
  distance fits 32 bits), and nothing about the order or the places: a wrong
  order paints the right colours on the wrong keys, a wrong table a ring that
  is round in the matrix and not on the board, which only the hardware shows
  (canon t-69g7 measures the order one LED at a time; the coords are
  imprint.json times 100). The driver sends and buffers the whole
  `chain-length` at every frame (Cyboard's overlay says 150, a placeholder),
  so the consumer sets the real count.
- **The palette's node is named `palette` on every device of a split, and the
  two parameters of an invocation are a wire format**: ZMK's split transport
  carries a behavior by its node name (at most 8 characters: the Bluetooth
  payload's `ZMK_SPLIT_RUN_BEHAVIOR_DEV_LEN` is 9 with the terminator) and
  param1/param2, so the central and the halves must agree on the name and on
  the packing (`src/palette.c`: param1 = pair << 24 | the first slot's
  0xRRGGBB, param2 = the second's). A change to either is a change on every
  device at once (canon flashes all four targets at a pin bump). **The hit's
  node is named `hit` on every device, its invocation a wire format too**
  (`src/hit.c`: param1 = the key position, param2 = 0, press only): the
  central sends it with `zmk_split_central_invoke_behavior()` from the
  module's own work queue, never the system work queue (a put into ZMK's run
  queue waits up to 100 ms or more when it is full, and the system work
  queue carries the host's Bluetooth TX and the halves' key events), and
  only to a half whose link is at peripheral latency 0, the sign of USB
  power (`src/latency.c`); a half on battery gets nothing. **The effect's and
  the intro's nodes are named `effect` and `intro` on every device, their
  invocations wire formats too** (`src/pick.c` for both: param1 = the pick, a
  mask of `src/effect.h`'s or `src/intro.h`'s bits, param2 = the effect's
  colour or the intro's 0; the release does nothing, a key's too),
  **and so are the bits and the names of `LANTERN_EFFECT_LIST` and
  `LANTERN_INTRO_LIST`** (the settings values, the port's words, canon's
  `dongle.py effect` and `intro`): never renumber or rename a line, append
  one. The effect's param2 is the colour (`0xRRGGBB`, 0 for the palette's
  colours; stored after the mask under `lantern/effect/mask`, one value, so
  a power loss never leaves a mask with another pick's colour; the mask
  alone, from an image before the colour, reads as colour 0), the intro's is
  0. A key of the central's
  keymap may bind `&effect <mask> <colour>` (ZMK runs the keymap on the
  central and carries a GLOBAL behavior to every half before running it
  there, `app/src/behavior.c`) and `&effect_next` (`src/effect_next.c`,
  `BEHAVIOR_LOCALITY_CENTRAL`: never carried over the split, so its node's
  name is free of the 8 characters; it computes the next bit of the stored
  pick over the effects this central builds, or the whole table where it
  builds none, and sends it through `lantern_effect_send()`); canon's keymap
  binds neither, and neither is verified on hardware (the check build's
  `config/pick_keys.dtsi` compiles the bindings). The hits share
  the run queue with the palette: a lit half that vanishes keeps its link
  for the supervision timeout, and hits relayed to it meanwhile can fill
  the queue (the source header). The slot of a link comes from ZMK's
  `peripheral_slot_index_for_conn()`, declared in `src/hit.c` since ZMK has
  no header for it: a rename or a static there fails the link, loudly, a
  changed signature silently (C links by name). A half notes a relayed
  press from the Bluetooth receive queue (`lantern_keys_hit()`:
  `src/keys.c`'s hit ring advances by compare-and-set, so any of ZMK's
  cooperative threads may note; `keys.h` lists the hit among the callers)
  and never moves the countdown's clock with it. With the hit built, the
  beams, the thin ring, the disc, the water and the dart draw the other
  half's presses too, and the disc's life covers the whole board
  (`disc_life()`), not a half.
- **Every palette change goes through the behavior**, on the central too: the
  port only parses and replies (`lantern_palette_send()`, which clears the
  slots it is not given), and the central stores its copy by the same handler
  the halves run. The handler repaints the key LEDs when a pair changed, only
  while they are lit; at boot the first frame reads the palette itself. A
  black slot other than 0 paints slot 0; a grey or black slot 0 paints white.
- **The central's run queue holds a whole palette and every pick for every
  half at once** (`CONFIG_ZMK_SPLIT_BLE_CENTRAL_SPLIT_RUN_QUEUE_SIZE` at least
  five invocations a half with both picks, 10 for two halves, asserted in
  `src/pick.c`; `src/palette.c` asserts the palette's three alone): a set
  from the port queues every half's three in one go, ZMK's own thread
  drains them with one GATT write each, and a put into a full queue waits
  up to 100 ms for the drain and then drops the oldest queued command. With
  ZMK's 5, a drain stalled by a half that stopped acknowledging would cost
  the first half's slots 0-1, the ones seen; with the palette fitting, a
  set never waits and a stall drops only what was queued before it (the
  source header; on an idle link 5 drops nothing either). A change of
  `LANTERN_PALETTE_SLOTS` moves the requirement and the settings value's
  size, which a device flashed across it ignores until the host sends again.
- **The resends ride on the halves' battery reports**
  (`zmk_peripheral_battery_state_changed`, hence the asserted
  `CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING`): ZMK's split central
  raises no event of its own for a half's connection, and the report after a
  (re)connection is the one signal it does raise. The palette's and the
  picks' sends each wait a second for the discovery to finish; a half saves
  only a changed palette or pick.
- **A pick picks among what is built, and never hides what the host sent**
  (`src/pick.c`, the mechanism; `src/effect.c` and `src/intro.c`, the
  instances): a half draws the effects' pick's bits its image builds, every
  one over the last, and the default (`LANTERN_EFFECT_DEFAULT`, one built
  effect, asserted; without a conf line the stain when built, else the first
  built in the choice's order) when no pick is stored or none of its bits is
  built; it plays the first built bit of the intro's pick in the table's
  order, else the choice `LANTERN_INTRO_DEFAULT` (without a conf line the
  star waves when built, else the bands); a device stores the host's bits as
  sent, bits its table does not know included (a newer image's), so the port
  answers the pick as made with the names it knows (`effect none` or `intro
  none` when it knows none: a bare word would be an ask, and an echoing host
  would loop) and an image flashed with more shows it. Refused, on every
  device and with nothing stored: an empty pick, and a colour above
  `0xFFFFFF` (the argument's range is wire format too, unlike the mask's
  bits: a wider one from a newer central would drop the whole pick here);
  ignored at load: a stored value of another size, an empty mask, a colour
  above the range. The central refuses the intro's pick with two bits (`one
  intro at a time`). A failed save is logged, not retried: the next change
  saves, and until then a boot returns to the previous pick, whole (as the
  mask before the colour). A change of the effects' pick clears the hits
  (`lantern_keys_effect_changed()`) and the stain's dye (the frame compares
  the pick and the colour it drew), never the palette; a change of the
  intro's pick does nothing until the next boot light, which reads it. The
  stain's bit draws the thin ring with the dye, the thin ring's the ring
  alone. **The pick's colour reaches every effect as the shades it draws
  from** (`src/keys.c`: with a colour, `effect_shades` is that one colour
  alone, at the picture's level for the stain's dye, handed to the entries'
  `picture` and `draw` in place of the frame's shades, which the picture
  keeps; with 0, the frame's shades, so the colour-less frames are the old
  ones): an effect never reads the colour itself, and a new effect needs
  nothing for it. Without a pick the colour is `CONFIG_LANTERN_EFFECT_COLOUR`
  (default 0).
- The port (`src/port.c`) is central-only, reads every word (the palette's,
  the picks') in one file because a device keeps one UART callback, and never
  touches the port's rate: 1200 baud is zmk-beacon's bootloader entry, 2400
  its screen dump, both the CDC driver's rate callback; this file is the UART
  interrupt callback, which nothing else in a canon image sets. **The
  callback runs on the USB device work queue (1 KiB of stack) and only
  collects bytes**: a complete line goes through a message queue to the
  system work queue, where the parse, the behavior and the reply run (Opus
  review 2026-10-19: the behavior path's frames do not fit the USB queue's
  stack with margin). Replies are never commands: a line shaped like a reply
  is ignored, so an echoing host cannot start a loop; before the settings
  have loaded a command gets an error.
- **A new effect is a line in `LANTERN_EFFECT_LIST` (`src/effect.h`), its
  Kconfig symbol `LANTERN_<SYMBOL>` (depends on `LANTERN_KEYS`, selects
  `LANTERN_EFFECTS` and, when it measures the board, `LANTERN_GEOMETRY`) and
  its member of the choice `LANTERN_EFFECT_DEFAULT`, a file under
  `src/effects/` defining its table entry (`src/frame.h`'s
  `struct lantern_effect_entry`: its life, a function returning the design's
  floor or the board's measure when longer, `lantern_life()` over
  `src/keys.c`'s `lantern_board()`, under `LANTERN_EFFECT_LIFE_MAX_MS`; its
  draw; for a lasting state its picture and clear hooks; and in `serves` any
  other line's bit it draws for) and that file's line in `CMakeLists.txt`,
  never a change to
  `src/keys.c`**: the engine builds its table from the list, in the list's
  order, pairing each line's bit with its entry, and dispatches on that bit
  and the entry's `serves` (the thin ring's entry names the stain's bit, so
  the dyeing ring is one draw, first). An effect writes only the frame it is handed
  and its own state, keeps every constant it needs in its file, and asserts
  its design's life against `LANTERN_EFFECT_LIFE_MAX_MS`, under which
  `lantern_life()` holds the board's; the board is measured once at boot on
  the main thread (`src/keys.c`'s `board_init()`), and the frame's thread
  reads it. The beams are the one life without a board term: their fade
  answers to the gap between the halves.
- **A new intro is a line in `LANTERN_INTRO_LIST` (`src/intro.h`), its
  Kconfig symbol `LANTERN_INTRO_<SYMBOL>` (depends on
  `LANTERN_ON_USB_POWER`, selects `LANTERN_INTROS` and, when it moves,
  `LANTERN_FRAMES`, and `LANTERN_GEOMETRY` when it measures the board) and
  its member of the choice `LANTERN_INTRO_DEFAULT`, a file under `src/intros/`
  defining its table entry (`src/frame.h`'s `struct lantern_intro_entry`: its
  length, which the rail rule keeps the LEDs lit for; a start, for a seed; its
  draw, handed the time since the boot light opened and the frame's shades,
  true while it moves and black from a moving one's end) and that file's line
  in `CMakeLists.txt`, never a change to `src/keys.c` or
  `src/on_usb_power.c`**: the engine resolves the pick when the rail rule
  opens the boot light (the first built bit of the stored pick in the table's
  order, else the default; on the system work queue), starts the intro and
  hands the rail rule its length, and the frame's thread reads the row. The
  bands (`src/intros/bands.c`) are a second of the palette's bands whatever
  the picture (the boot light of before the table for the no picture and the
  bands picture; the solid, the gradient and the scatter showed their own
  picture then), one still frame; the star waves (`src/intros/star_waves.c`) the show, whose
  thirds show at the design's level or what the cap allows with every key
  lit, whichever is less, which at a brightness of 15 % or below on canon's
  halves (or more LEDs) is less than before, where the cap held the whole
  frame instead. No intro built (`CONFIG_LANTERN_INTRO_BANDS=n` and no
  other) is no boot light.
- **An effect never dims the picture, and a frame draws at most CAP**
  (`src/keys.c`): an effect pulls an LED from the picture toward its colour,
  and the cap takes any excess out of the effects' brightening alone. CAP is
  the white picture plus two keys white at full scale (`CAP_KEYS`), about
  380 mA by estimate: a cap at the white picture alone left a white or grey
  band no room (Opus review, 2026-10-20). The cap counts from
  `CONFIG_LANTERN_BRIGHTNESS`; the picture's own brightness,
  `CONFIG_LANTERN_PICTURE_BRIGHTNESS`, is at most that (Kconfig's range and
  a BUILD_ASSERT), since a brighter still picture could pass the cap and
  leave the room negative. An effect goes to full scale
  because twice the picture's brightness was not seen under the keycaps on
  hardware (canon t-6ttk, 2026-10-21, the press glow, since dropped). The
  intro draws under the same cap,
  over a black picture (the timer and the cap are `CONFIG_LANTERN_FRAMES`'s,
  which the effects' engine and the intro select). Effects and the intro
  run on ZMK's low priority queue, whose 768-byte stack ZMK's battery
  sampling shares and no MPU guard protects on a half: integers only (the
  effects' distances and the intro's depths through an integer square
  root), and anything large static. A hit carries the press's number (mod 60, a multiple of every
  shade count) so that an effect can give each press the palette's next
  shade without skipping one at the wrap. **The stain's dye is the frame
  thread's alone** (`stains[]`, written where the ring is drawn, applied to
  the picture before the effects, cleared when the stage or the pick moves
  and at a blank): nothing else reads or writes it. **The scatter's throw runs on the
  low priority queue too** (`scatter_work`, a delayable item the light arms
  and that re-arms itself while lit; the blank cancels nothing, since a
  cancel racing the light's re-arm could leave it neither queued nor timed,
  Opus review 2026-10-21; a tick that finds the LEDs dark just returns), so
  the frame never reads a half-thrown table; the light's throw normally
  queues ahead of the light's frame, behind one still queued from a blank
  just before.
- **The split link's latency is the half's rule, never ZMK's `PREF_*` on a
  half** (`src/latency.c`): those symbols exist on a central only, so the
  battery latency is the one the link had at connection. A request may be a
  synchronous HCI command and runs on the module's own work queue, never the
  system work queue. On USB the half gives up after a few refused requests
  (latency 0 is a nicety); on battery it keeps asking until the link agrees,
  since a half left at latency 0 on battery listens at every event. A
  request Zephyr holds through a connection's first 5 s is asked over when
  the power changes meanwhile.
- Remove or rename a Kconfig symbol together with the canon change that stops
  setting it: Kconfig aborts on an assignment to an undefined symbol (the
  five `CONFIG_ZMK_RGB_UNDERGLOW_*` lines the halves carried up to 2406809
  left canon's confs with the switch to `src/keys.c`; `CONFIG_LANTERN_BOOT_LIGHT_MS`
  went with the intros' table, canon t-qvgv). A symbol that keeps its name and
  changes its meaning only warns: `CONFIG_LANTERN_INTRO=y` meant the show up
  to that change and means the pick since, so an old line builds the bands
  alone, with a Kconfig warning where the node is missing.
- Hardware verification is canon's: flash with canon's `scripts/flash-dongle.sh
  imprint_left` / `imprint_right` (a data cable, USB power), read a `--logging`
  image's lines with its `dongle.py log`, set and ask the palette with its
  `dongle.py palette` and the picks with its `dongle.py effect` and `intro`.
  Hardware claims here, in README and
  in task bodies carry a date; README "Status" holds what was seen (the USB
  rule and the boot light on both halves, canon #263, 2026-10-19; the idle
  rule: the right half, canon's logging build with a 20 s timeout,
  2026-10-19; the palette through the underglow, 2026-10-19; the bands on
  both halves, canon #271, 2026-10-20; the countdown on both halves, canon's
  trial images, 2026-10-20).

## Commits, pull requests, fleet files

- glyph: `<:code:>[(scope)]<sigil> <subject>`, English
  ([CONTRIBUTING.md](https://github.com/akira-toriyama/.github/blob/main/CONTRIBUTING.md),
  codes: `glyph emoji`); `glyph lint --range origin/main..HEAD` before pushing,
  `glyph hook install` once per clone. PR bodies end with the `SetStatus-task:` line.
- Public repository on free Actions minutes: PR gates are deterministic checks
  only; never add a workflow that calls a paid API.
- Fleet-managed, never edited here (fleet-sync in akira-toriyama/.github
  overwrites them from its `fleet/`):
  `.github/workflows/{actionlint,commit-lint,repo-policy,taplo,task-status,version-preview,zizmor}.yml`,
  `.github/zizmor.yml`, `.github/dependabot.yml`, `docs/commit-convention.md`.
  `glyph.toml` came from `glyph init --gemoji`: edit it, never regenerate it.
