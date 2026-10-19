# Copyright (c) 2026 akira-toriyama
# SPDX-License-Identifier: MIT

target_sources_ifdef(CONFIG_LANTERN_ON_USB_POWER app PRIVATE src/on_usb_power.c)
