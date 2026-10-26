# Copyright (c) 2026 akira-toriyama
# SPDX-License-Identifier: MIT

target_sources_ifdef(CONFIG_LANTERN_KEYS app PRIVATE src/keys.c)
target_sources_ifdef(CONFIG_LANTERN_ON_USB_POWER app PRIVATE src/on_usb_power.c)
target_sources_ifdef(CONFIG_LANTERN_PALETTE app PRIVATE src/palette.c)
target_sources_ifdef(CONFIG_LANTERN_PORT app PRIVATE src/port.c)
target_sources_ifdef(CONFIG_LANTERN_USB_LATENCY app PRIVATE src/latency.c)
target_sources_ifdef(CONFIG_LANTERN_HIT app PRIVATE src/hit.c)
target_sources_ifdef(CONFIG_LANTERN_EFFECT app PRIVATE src/effect.c)
