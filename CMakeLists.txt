# Copyright (c) 2026 akira-toriyama
# SPDX-License-Identifier: MIT

# The key LEDs: the frame's engine, the intro and one file an effect (the
# stain is the thin ring's), each built with its Kconfig symbol (src/frame.h).
target_sources_ifdef(CONFIG_LANTERN_KEYS app PRIVATE src/keys.c)
target_sources_ifdef(CONFIG_LANTERN_INTRO app PRIVATE src/intro.c)
target_sources_ifdef(CONFIG_LANTERN_THIN_RING app PRIVATE src/effects/thin_ring.c)
target_sources_ifdef(CONFIG_LANTERN_DISC app PRIVATE src/effects/disc.c)
target_sources_ifdef(CONFIG_LANTERN_CROSS_RING app PRIVATE src/effects/cross_ring.c)
target_sources_ifdef(CONFIG_LANTERN_BEAMS app PRIVATE src/effects/beams.c)
target_sources_ifdef(CONFIG_LANTERN_WATER app PRIVATE src/effects/water.c)
target_sources_ifdef(CONFIG_LANTERN_DART app PRIVATE src/effects/dart.c)
target_sources_ifdef(CONFIG_LANTERN_ON_USB_POWER app PRIVATE src/on_usb_power.c)
target_sources_ifdef(CONFIG_LANTERN_PALETTE app PRIVATE src/palette.c)
target_sources_ifdef(CONFIG_LANTERN_PORT app PRIVATE src/port.c)
target_sources_ifdef(CONFIG_LANTERN_USB_LATENCY app PRIVATE src/latency.c)
target_sources_ifdef(CONFIG_LANTERN_HIT app PRIVATE src/hit.c)
target_sources_ifdef(CONFIG_LANTERN_EFFECT app PRIVATE src/effect.c)
