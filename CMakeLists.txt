# Copyright (c) 2026 akira-toriyama
# SPDX-License-Identifier: MIT

target_sources_ifdef(CONFIG_LANTERN_KEYS app PRIVATE src/keys.c)
target_sources_ifdef(CONFIG_LANTERN_ON_USB_POWER app PRIVATE src/on_usb_power.c)
target_sources_ifdef(CONFIG_LANTERN_PALETTE app PRIVATE src/palette.c)
target_sources_ifdef(CONFIG_LANTERN_PALETTE_PORT app PRIVATE src/palette_port.c)
