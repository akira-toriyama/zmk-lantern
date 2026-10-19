# CLAUDE.md

Claude Code notes: what breaks, where the reasoning lives (the source header),
how to build. Usage: [README.md](README.md). English only.

## What this repository is

- A ZMK module for the key LEDs of a split half: lit while USB powers the half,
  dark and unpowered on battery, a boot light (`CONFIG_LANTERN_ON_USB_POWER`,
  `CONFIG_LANTERN_BOOT_LIGHT_MS`). One source, [src/on_usb_power.c](src/on_usb_power.c),
  whose header carries the design and every reason; one binding,
  `lantern,rail` ([dts/bindings/lantern,rail.yaml](dts/bindings/lantern,rail.yaml)).
  Device names follow canon's
  [docs/glossary.md](https://github.com/akira-toriyama/canon/blob/main/docs/glossary.md):
  Cyboard Imprint (the halves), Imprint Dongle, Prospector Dongle.
- Two roles in one tree ([config/west.yml](config/west.yml)): a Zephyr module
  (`zephyr/module.yml` with `dts_root`, `dts/bindings/`, `src/`, `Kconfig`,
  `CMakeLists.txt`; no shield, no board) and the zmk-config (`config/`,
  `build.yaml`) that CI and [scripts/build.sh](scripts/build.sh) build:
  Cyboard's stock Imprint halves plus the consumer-side lines a half needs
  (`config/imprint.conf`, `config/imprint.overlay`).
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
  `zephyr/zmk.map`, `zephyr/zmk.elf`. A build that carries the module shows
  `CONFIG_LANTERN_ON_USB_POWER=y` in `.config`, a `lantern_rail` node with
  `compatible = "lantern,rail"` in `zephyr.dts` and `on_usb_power.c.obj` in
  `zmk.map`; a dongle build shows none of the three.
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
- **All state lives on the system work queue** (the USB event, the steps, the
  first evaluation); a new entry point submits work there, never touches the
  state.
- A target without a `lantern,rail` node (canon's dongles) builds none of the
  module: `CONFIG_LANTERN_ON_USB_POWER` depends on
  `DT_HAS_LANTERN_RAIL_ENABLED`. Exactly one node per target (BUILD_ASSERT).
- Remove or rename a Kconfig symbol together with the canon change that stops
  setting it: Kconfig aborts on an assignment to an undefined symbol.
- Hardware verification is canon's: flash with canon's `scripts/flash-watch.sh`,
  read a `--logging` image's lines with its `dongle.py log`. Hardware claims
  here, in README and in task bodies carry a date; the module is unverified
  on hardware as of 2026-10-19 (README "Status").

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
