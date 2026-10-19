# zmk-lantern

A [ZMK](https://zmk.dev) module that lights a keyboard's key LEDs only while
USB powers it and it is in use. While a cable powers the half, its LED rail is
on and ZMK's underglow lights every key; on battery the underglow is off and
the rail is off, so the LEDs cost the battery nothing; on the cable, five
minutes without a key (ZMK's idle state) turn them off until the next one. At
boot the LEDs light for a second, cable or not, and then the rule applies.

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
(`CONFIG_LANTERN_OFF_WHEN_IDLE`): not yet seen on hardware (2026-10-19).

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

## Building this repository

```sh
./scripts/build.sh              # both halves, firmware/imprint_{left,right}.uf2
./scripts/build.sh --logging    # with USB logging; the module's lines are INFO
./scripts/build.sh --help       # every option
```

Docker, a west workspace under `~/.cache/zmk-lantern`, the images in
`firmware/`. They are the build check of Cyboard's stock Imprint config plus
the module, not a keyboard's firmware: a consumer's keymap and roles are its
own. CI builds the same targets on every pull request and weekly against the
moving branches.

Log lines (a `--logging` image, through canon's `dongle.py log` or any serial
reader at 115200): `start: usb powered, boot light 1000 ms`, `lit: boot light`,
`lit: usb powered`, `dark: on battery`, `dark: idle`.
