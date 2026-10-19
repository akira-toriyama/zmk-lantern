# CLAUDE.md

Claude Code notes: what breaks, where the reasoning lives (the source header),
how to build. Usage: [README.md](README.md). English only.

## What this repository is

- A ZMK module for the key LEDs of a split half: lit while USB powers the half
  and ZMK's activity state is active, dark and unpowered on battery and while
  idle, a boot light (`CONFIG_LANTERN_ON_USB_POWER`, `CONFIG_LANTERN_OFF_WHEN_IDLE`,
  `CONFIG_LANTERN_BOOT_LIGHT_MS`; [src/on_usb_power.c](src/on_usb_power.c), binding
  `lantern,rail`), and their colour: a palette of six colours the host hands
  the split central over its USB CDC ACM port, which the central and every
  half keep in their settings and whose slot 0 the underglow paints
  (`CONFIG_LANTERN_PALETTE`, `CONFIG_LANTERN_PALETTE_PORT`;
  [src/palette.c](src/palette.c), [src/palette_port.c](src/palette_port.c),
  binding `lantern,behavior-palette`). Each source's header carries its design
  and every reason.
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
  `config/palette.dtsi`). With Cyboard's stock roles the left half is a split
  central (canon's is a peripheral): the check build compiles the palette's
  central side on the left half and the dongle, its half side on both halves.
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
  `CONFIG_LANTERN_ON_USB_POWER=y` and `CONFIG_LANTERN_PALETTE=y` in `.config`,
  a `lantern_rail` node with `compatible = "lantern,rail"` and a `palette` node
  with `compatible = "lantern,behavior-palette"` in `zephyr.dts`, and
  `on_usb_power.c.obj` and `palette.c.obj` in `zmk.map`; a split central's
  (the dongle, the stock left half) `CONFIG_LANTERN_PALETTE_PORT=y` and
  `palette_port.c.obj` too, the dongle's nothing of the rail.
- Kconfig merge order, later wins: board defconfig → ZMK `app/prj.conf` → the
  shield's confs → the consumer's `config/<prefix>.conf` files →
  `EXTRA_CONF_FILE` fragments (build.sh: logging, `--kconfig`) → `-DCONFIG_*`.
  ZMK's `app/Kconfig.defaults` sets `ZMK_RGB_UNDERGLOW_EXT_POWER=y` whenever
  the underglow is on: the consumer's conf must say `=n`.
- ZMK tracks `main` and Cyboard's module `zephyr-4.1`, as canon does (its
  CLAUDE.md says why); the weekly run of `build.yml` warns early of an
  upstream break.
- Against canon's exact tree (its ZMK pin and patches): canon's
  `./scripts/build-zmk.sh imprint --module <this checkout>` once canon's
  scripts take a second module (canon t-66ww); until then a `west build` in
  canon's workspace with `-DZMK_EXTRA_MODULES=<this checkout>` and an extra
  conf and overlay (done 2026-10-19 for both halves).
- CI: `build.yml` → the local reusable `zmk-build.yml` (it says why not ZMK's);
  `shellcheck.yml`; the fleet gates. No release workflow: the module ships no
  image, consumers pin a commit.

## Invariants

- **Never give the LED rail back to ZMK's `ext_power` driver**: the consumer's
  EXT_POWER node stays `status = "disabled"`, with
  `CONFIG_ZMK_RGB_UNDERGLOW_EXT_POWER=n`. The driver turns the rail on at init
  and restores a stored `ext_power/state/EXT_POWER` at settings load (a stored
  off kept canon's left half dark, 2026-10-19), and with `CONFIG_ZMK_SLEEP=n`
  nothing turns the rail off again. The module drives the node's `gpios`
  itself; the hog holds the pin low from POST_KERNEL until it does.
- **`CONFIG_ZMK_RGB_UNDERGLOW_AUTO_OFF_USB`, `_AUTO_OFF_IDLE` and `_ON_START`
  stay off** (the source asserts the three, and the three prerequisites): the
  first calls the same on/off and whichever listener runs last wins, the
  second runs the strip on activity against an unpowered rail on battery, the
  third drives the strip at init with the rail off. The boot light is the
  module's. `&rgb_ug` keys on a battery half are the same hazard, unasserted.
- **The first evaluation runs from the settings commit handler (commit priority
  1), never from SYS_INIT**: ZMK's saved `rgb/underglow/state` is restored in
  `settings_load()` after every init level and would undo an earlier decision.
  Every `zmk_rgb_underglow_on()/off()` saves the state 60 s later; the module
  overrides it at every boot and powers the rail before the restored tick's
  first frames reach the LEDs (the only way to DARK is through BLANKING), so
  the saved value is harmless and never trusted.
- **All state lives on the system work queue** (the USB and activity events,
  the steps, the first evaluation); a new entry point submits work there,
  never touches the state.
- **The idle timeout is ZMK's `CONFIG_ZMK_IDLE_TIMEOUT`, set by the consumer;
  the module keeps no timer**: ZMK's activity state already counts a half's
  keys, sensors and pointing input, and the consumer's later effects module
  (canon t-6ttk) reads the same state. A cable plugged into an idle half
  lights nothing until its next key, by design (the source says why).
- A target without a `lantern,rail` node (canon's dongles) builds none of the
  rail: `CONFIG_LANTERN_ON_USB_POWER` depends on `DT_HAS_LANTERN_RAIL_ENABLED`;
  one without a `lantern,behavior-palette` node (canon's Prospector Dongle)
  none of the palette. Exactly one node of each per target (BUILD_ASSERT).
- **The palette's node is named `palette` on every device of a split, and the
  two parameters of an invocation are a wire format**: ZMK's split transport
  carries a behavior by its node name (at most 8 characters: the Bluetooth
  payload's `ZMK_SPLIT_RUN_BEHAVIOR_DEV_LEN` is 9 with the terminator) and
  param1/param2, so the central and the halves must agree on the name and on
  the packing (`src/palette.c`: param1 = pair << 24 | the first slot's
  0xRRGGBB, param2 = the second's). A change to either is a change on every
  device at once (canon flashes all four targets at a pin bump).
- **Every palette change goes through the behavior**, on the central too: the
  port only parses and replies (`lantern_palette_send()`), and the central
  stores its copy by the same handler the halves run. Slot 0 reaches the
  underglow from that handler and from the subtree's settings commit at
  commit priority 1 (after ZMK restored its own `rgb/underglow/state`);
  only hue and saturation change, the brightness stays the device's.
- **The central's run queue holds a whole palette for every half at once**
  (`CONFIG_ZMK_SPLIT_BLE_CENTRAL_SPLIT_RUN_QUEUE_SIZE` at least three
  invocations a half, 6 for two halves, asserted in `src/palette.c`): a set
  from the port queues every half's three in one go, ZMK's own thread
  drains them with one GATT write each, and a put into a full queue waits
  up to 100 ms for the drain and then drops the oldest queued command. With
  ZMK's 5, a drain stalled by a half that stopped acknowledging would cost
  the first half's slots 0-1, the ones seen; with the palette fitting, a
  set never waits and a stall drops only what was queued before it (the
  source header; on an idle link 5 drops nothing either). A change of
  `LANTERN_PALETTE_SLOTS` moves the requirement and the settings value's
  size, which a device flashed across it ignores until the host sends again.
- **The resend rides on the halves' battery reports**
  (`zmk_peripheral_battery_state_changed`, hence the asserted
  `CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING`): ZMK's split central
  raises no event of its own for a half's connection, and the report after a
  (re)connection is the one signal it does raise. The send waits a second for
  the discovery to finish; a half saves only a changed palette.
- The port's line is central-only and never touches the port's rate: 1200
  baud is zmk-beacon's bootloader entry, 2400 its screen dump, both the CDC
  driver's rate callback; this file is the UART interrupt callback, which
  nothing else in a canon image sets. **The callback runs on the USB device
  work queue (1 KiB of stack) and only collects bytes**: a complete line goes
  through a message queue to the system work queue, where the parse, the
  behavior and the reply run (Opus review 2026-10-19: the behavior path's
  frames do not fit the USB queue's stack with margin). Replies are never
  commands: a line shaped like a reply is ignored, so an echoing host cannot
  start a loop; before the settings have loaded a command gets an error.
- Remove or rename a Kconfig symbol together with the canon change that stops
  setting it: Kconfig aborts on an assignment to an undefined symbol.
- Hardware verification is canon's: flash with canon's `scripts/flash-dongle.sh
  imprint_left` / `imprint_right` (a data cable, USB power), read a `--logging`
  image's lines with its `dongle.py log`, set and ask the palette with its
  `dongle.py palette`. Hardware claims here, in README and
  in task bodies carry a date; README "Status" holds what was seen (the USB
  rule and the boot light on both halves, canon #263, 2026-10-19; the idle
  rule: the right half, canon's logging build with a 20 s timeout, 2026-10-19).

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
