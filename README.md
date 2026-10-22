# zmk-lantern

A [ZMK](https://zmk.dev) module that lights a keyboard's key LEDs only while
USB powers it and it is in use, and paints them from a palette the host hands
the keyboard. While a cable powers the half, its LED rail is on and every key
shows its palette colour; on battery the LEDs are black and the rail is off,
so they cost the battery nothing; on the cable, five minutes without a key
(ZMK's idle state) turn them off until the next one, and on the way there the
half cycles through the palette's colours, the whole half in one colour for
ten seconds, then the next. At boot the LEDs light for a second, cable or
not, and then the rule applies. The palette: six
colours handed to the keyboard's split central over USB, kept by every half
across boots, and shown on the keys in the bands the consumer's overlay draws
(the palette, below).

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
stage the keys' clock said (canon t-gsm2, t-x82q).

## What it does

| While | Rail | LEDs |
| --- | --- | --- |
| USB powers the half: a host, awake or asleep, or a charger | on | lit: the palette's picture |
| the half runs on battery | off | black |
| USB powers the half and its last key is `CONFIG_LANTERN_COUNTDOWN_STAGE_MS` (10 s) or more ago (`CONFIG_LANTERN_COUNTDOWN`) | on | lit: the whole half in the palette's next colour, a new picture every stage, the bands again after the last colour |
| USB powers the half, but ZMK's activity state is idle: `CONFIG_ZMK_IDLE_TIMEOUT` ms (the consumer's line) without a key, sensor or pointing input on this half | off | black, until the next input |
| the first `CONFIG_LANTERN_BOOT_LIGHT_MS` after boot (default 1000) | on | lit, cable or not |

- Each half decides for itself, from its own USB port and its own activity;
  nothing crosses the split. Typing on one half alone leaves the other to go
  dark.
- Plugging in turns the rail on and, `settle-ms` later (100 ms on the Imprint),
  writes the picture; unplugging writes a black frame and drops the rail
  100 ms after. A reset, a suspend (the host asleep) or a resume on the cable
  changes nothing. Idle and the next input switch the LEDs the same way; the
  boot light is never cut short.
- A cable plugged into an idle half lights nothing until its next key: USB is
  no activity to ZMK, and lighting on it would leave the LEDs on with no idle
  event to follow.
- The picture ([src/keys.c](src/keys.c)): each LED shows one palette slot, the
  one the consumer's `lantern,keys` node assigns to the key under it, so the
  palette's colours fall on the keyboard in bands. A slot the host left black
  shows slot 0, so one colour paints a solid keyboard; without a palette every
  key is white. Each colour is scaled so that its brightest channel is
  `CONFIG_LANTERN_BRIGHTNESS` percent of full scale (default 16, what ZMK's
  underglow drove on canon's halves before the module painted the strip),
  which keeps its hue and saturation; a grey or black scales to white. The
  module is the strip's one writer: ZMK's underglow stays off on such a half
  (`CONFIG_ZMK_RGB_UNDERGLOW=n`, asserted), and a frame goes out only when the
  LEDs light or go dark, when the palette changes while they are lit and when
  the countdown moves a stage; nothing ticks at a fixed rate.
- The countdown (`CONFIG_LANTERN_COUNTDOWN`, on by default with the idle rule
  and the palette): from the half's last key, the picture changes every
  `CONFIG_LANTERN_COUNTDOWN_STAGE_MS` (default 10000): the bands, then the
  whole half in each colour of the palette in turn (a slot the host left
  black is slot 0's colour; colours that paint alike at the device's
  brightness count once), then the bands again, until ZMK turns idle and the
  LEDs go dark. A key brings the bands back and restarts the clock; a cable
  plugged into a half that typed a while ago lights the stage its keys say.
  The clock is the half's own keys: a sensor or pointing input counts for
  ZMK's idle, not for the stages. One colour: the bands until the dark.

## The palette

Six colours (`0xRRGGBB`, slots 0..5) that the host hands the keyboard's split
central and that the central and every half keep in their settings
(`lantern/palette/colours`); a half's key LEDs show them in the bands its
`lantern,keys` node draws, repainted when a palette arrives while they are
lit and shown at every boot. A set of fewer than six colours clears the rest
to `0x000000`, which the LEDs show as slot 0.

- The host writes a text line on the central's USB CDC ACM port (canon:
  `scripts/dongle.py palette`): `palette RRGGBB [RRGGBB ...]` (one to six)
  sets slots 0.. and clears the rest, answered with `palette ok` and the six
  slots as stored; `palette` or `palette?` is answered with the six slots, or
  `palette none` before any; a bad line gets `palette error: <reason>`
  ([src/palette_port.c](src/palette_port.c) is the grammar). The central's code
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
  `CONFIG_LANTERN_PALETTE_PORT` on a split central with a CDC ACM port. A
  split central needs `CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING=y`
  (asserted at build time). A device without a `lantern,keys` node, such as a
  dongle, keeps and forwards the palette without painting anything.

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
   picture, 0 to 5 each. A consumer with per-half overlays puts `slots` in a
   shared dtsi and each half's `positions` and `chain-length` in its own
   overlay (canon's `config/lantern_keys.dtsi`).

3. The half's conf ([config/imprint.conf](config/imprint.conf)):

   ```
   CONFIG_ZMK_RGB_UNDERGLOW=n
   CONFIG_ZMK_IDLE_TIMEOUT=300000
   ```

   `CONFIG_LANTERN_KEYS` and `CONFIG_LANTERN_ON_USB_POWER` are on by default
   wherever their nodes exist; `CONFIG_LANTERN_BRIGHTNESS` (default 16) is
   the picture's brightness; `CONFIG_LANTERN_BOOT_LIGHT_MS=0` drops the boot
   light. ZMK's underglow must be off on such a half (its tick would repaint
   the strip; Cyboard's module defaults it to y for every target). A missing
   or conflicting option fails the build with a message naming it. A target
   without the nodes (a dongle) builds none of the painting or the rail.
   `CONFIG_LANTERN_OFF_WHEN_IDLE` is on by default with the rail, and the idle
   timeout is ZMK's `CONFIG_ZMK_IDLE_TIMEOUT` (default 30000 ms; 300000 is the
   five minutes of canon's halves): the module has no timer of its own, ZMK's
   activity state already counts keys, sensors and pointing input per device.
   With `CONFIG_ZMK_SLEEP=n` the timeout moves nothing else in a half but the
   battery sampling, which ZMK pauses while idle; `CONFIG_LANTERN_OFF_WHEN_IDLE=n`
   keeps the LEDs lit on the cable whatever the activity.
   `CONFIG_LANTERN_COUNTDOWN` is on by default with the idle rule and the
   palette, a new picture every `CONFIG_LANTERN_COUNTDOWN_STAGE_MS` (10000):
   `=n` keeps the bands until the dark.

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
   that holds a whole palette for every half at once, three invocations
   each: `CONFIG_ZMK_SPLIT_BLE_CENTRAL_SPLIT_RUN_QUEUE_SIZE=6` for two halves
   (ZMK's default is 5; both asserted at build time); the port comes with the
   central's CDC ACM port (`CONFIG_USB_CDC_ACM`).

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
`lit: usb powered`, `dark: on battery`, `dark: idle`, and the countdown's
`stage N` at every move; the palette's:
`slots 0-1 RRGGBB RRGGBB stored` (or `unchanged`) on every device, and on the
central `N colour(s) from <port>` and `resent to half N`; the picture logs
only a frame the strip refused (`frame not written`).
