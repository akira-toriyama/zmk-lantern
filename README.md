# zmk-lantern

A [ZMK](https://zmk.dev) module that lights a keyboard's key LEDs only while
USB powers it and it is in use. While a cable powers the half, its LED rail is
on and ZMK's underglow lights every key; on battery the underglow is off and
the rail is off, so the LEDs cost the battery nothing; on the cable, five
minutes without a key (ZMK's idle state) turn them off until the next one. At
boot the LEDs light for a second, cable or not, and then the rule applies.
The colour comes from the host: a palette of four colours handed to the
keyboard's split central over USB, kept by every half across boots, whose
slot 0 the LEDs show (the palette, below).

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
and the Imprint Dongle: one `dongle.py palette` changed both halves over the
split (each logged `stored` and `applied`); a half rebooted on the cable
applied slot 0 from its settings at 0.3 s and took the central's resend at
1.8 s; a half flashed fresh, with no palette stored, had it 1.7 s after boot
from the resend; and the central's reboot resent it to both halves within 2 s.

## What it does

| While | Rail | LEDs |
| --- | --- | --- |
| USB powers the half: a host, awake or asleep, or a charger | on | lit, with ZMK's underglow colour and effect |
| the half runs on battery | off | dark |
| USB powers the half, but ZMK's activity state is idle: `CONFIG_ZMK_IDLE_TIMEOUT` ms (the consumer's line) without a key, sensor or pointing input on this half | off | dark, until the next input |
| the first `CONFIG_LANTERN_BOOT_LIGHT_MS` after boot (default 1000) | on | lit, cable or not |

- Each half decides for itself, from its own USB port and its own activity;
  nothing crosses the split. Typing on one half alone leaves the other to go
  dark.
- Plugging in turns the rail on and, `settle-ms` later (100 ms on the Imprint),
  the underglow; unplugging turns the underglow off and drops the rail 100 ms
  after. A reset, a suspend (the host asleep) or a resume on the cable changes
  nothing. Idle and the next input switch the LEDs the same way; the boot
  light is never cut short.
- A cable plugged into an idle half lights nothing until its next key: USB is
  no activity to ZMK, and lighting on it would leave the LEDs on with no idle
  event to follow.
- The colour, saturation, brightness and effect are ZMK's underglow settings
  (`CONFIG_ZMK_RGB_UNDERGLOW_{HUE,SAT,BRT,EFF}_START`). ZMK saves the
  underglow's state a minute after each change; the rule overrides it at every
  boot, and a half that boots on battery after a plugged minute shows the
  saved colour for a tenth of a second while the module takes over.
- Not reconciled: an underglow change the module did not make. A `&rgb_ug`
  key pressed on battery, or ZMK's idle auto-off (asserted off), would run the
  strip with the rail off; keep such keys off a half that runs on battery.

## The palette

Four colours (`0xRRGGBB`, slots 0..3) that the host hands the keyboard's split
central and that the central and every half keep in their settings
(`lantern/palette/colours`); slot 0 is the hue and saturation ZMK's underglow
paints the key LEDs with on a half, applied when it arrives and at every boot,
at the brightness the half has (the consumer's
`CONFIG_ZMK_RGB_UNDERGLOW_BRT_START`): a grey or black slot 0 paints white.
The other slots change nothing yet; effects to come read them.

- The host writes a text line on the central's USB CDC ACM port (canon:
  `scripts/dongle.py palette`): `palette RRGGBB [RRGGBB [RRGGBB [RRGGBB]]]`
  sets slots 0.. and leaves the rest, answered with `palette ok` and the four
  slots as stored; `palette` or `palette?` is answered with the four slots, or
  `palette none` before any; a bad line gets `palette error: <reason>`
  ([src/palette_port.c](src/palette_port.c) is the grammar). The central's code
  invokes the `lantern,behavior-palette` node, which ZMK's split transport
  carries to every half by the node's name, one pair of slots per invocation
  ([src/palette.c](src/palette.c)); nothing is bound to a key.
- A half that was away when the host wrote, or that lost its settings, gets
  the palette when it is back: the central resends it a second after each
  battery report of a half, the first of which follows every (re)connection.
  A half writes its flash only when a colour changed.
- `CONFIG_LANTERN_PALETTE` is on wherever the node exists (the consumer's
  keymap or an overlay, named `palette` on every device of the split, the
  central included: that name is what the transport carries) and
  `CONFIG_LANTERN_PALETTE_PORT` on a split central with a CDC ACM port. A
  split central needs `CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING=y`
  (asserted at build time). `CONFIG_ZMK_RGB_UNDERGLOW=n` on a device, such as
  a dongle, keeps and forwards the palette without painting anything.

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
         revision: <commit sha>
         path: modules/zmk-lantern
   ```

2. The half's overlay ([config/imprint.overlay](config/imprint.overlay) is the
   Imprint's): take the LED rail away from ZMK's `ext_power` driver and give it
   to the module.

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

   `gpios` is the pin the board's `EXT_POWER` node has as `control-gpios`,
   `settle-ms` its `init-delay-ms` (the LEDs' power-up time before the first
   frame). The hog holds the rail off from early boot until the module takes
   the pin; the disabled node keeps ZMK's driver, which would turn the rail on
   at init and restore a stored state, out of the build.

3. The half's conf ([config/imprint.conf](config/imprint.conf)):

   ```
   CONFIG_ZMK_RGB_UNDERGLOW=y
   CONFIG_ZMK_RGB_UNDERGLOW_EXT_POWER=n
   CONFIG_ZMK_RGB_UNDERGLOW_AUTO_OFF_USB=n
   CONFIG_ZMK_RGB_UNDERGLOW_AUTO_OFF_IDLE=n
   CONFIG_ZMK_RGB_UNDERGLOW_ON_START=n
   CONFIG_ZMK_IDLE_TIMEOUT=300000
   ```

   `CONFIG_LANTERN_ON_USB_POWER` is on by default wherever the node exists;
   `CONFIG_LANTERN_BOOT_LIGHT_MS=0` drops the boot light. A missing or
   conflicting option fails the build with a message naming it. A target
   without the node (a dongle) builds none of the module.
   `CONFIG_LANTERN_OFF_WHEN_IDLE` is on by default with it, and the idle
   timeout is ZMK's `CONFIG_ZMK_IDLE_TIMEOUT` (default 30000 ms; 300000 is the
   five minutes of canon's halves): the module has no timer of its own, ZMK's
   activity state already counts keys, sensors and pointing input per device.
   With `CONFIG_ZMK_SLEEP=n` the timeout moves nothing else in a half but the
   battery sampling, which ZMK pauses while idle; `CONFIG_LANTERN_OFF_WHEN_IDLE=n`
   keeps the LEDs lit on the cable whatever the activity.

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
   `CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING=y`; the port comes
   with the central's CDC ACM port (`CONFIG_USB_CDC_ACM`).

## Building this repository

```sh
./scripts/build.sh              # both halves and the dongle, firmware/<shield>.uf2
./scripts/build.sh --logging    # with USB logging; the module's lines are INFO
./scripts/build.sh --help       # every option
```

Docker, a west workspace under `~/.cache/zmk-lantern`, the images in
`firmware/`. They are the build check of Cyboard's stock Imprint config (both
halves and the `imprint_dongle` shield) plus the module, not a keyboard's
firmware: a consumer's keymap and roles are its own. CI builds the same targets on every pull request and weekly against the
moving branches.

Log lines (a `--logging` image, through canon's `dongle.py log` or any serial
reader at 115200): `start: usb powered, boot light 1000 ms`, `lit: boot light`,
`lit: usb powered`, `dark: on battery`, `dark: idle`; the palette's:
`slots 0-1 RRGGBB RRGGBB stored` (or `unchanged`), `slot 0 RRGGBB applied: hue H
sat S brt B` on a half, and on the central `N colour(s) from <port>` and
`resent to half N`.
