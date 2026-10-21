/*
 * Copyright (c) 2026 akira-toriyama
 *
 * SPDX-License-Identifier: MIT
 *
 * The key LEDs' picture (src/keys.c), for the rail rule (src/on_usb_power.c)
 * and the palette (src/palette.c). Layer contract: this file is the strip's
 * one writer; callers say lit or dark, and when the boot light lasts, and
 * never touch a pixel. The frame is written on ZMK's low priority work queue;
 * the countdown's clock runs on the system work queue, so the light, the
 * blank and the boot light are called from there (the rail rule's thread),
 * the refresh from any thread.
 */

#pragma once

#include <stdbool.h>

#include <zephyr/sys/util.h>

/* Paint the palette's picture (white without a palette; the no picture's
 * black stays black) and start the countdown. Call with the rail on and
 * settled, on the system work queue. */
void lantern_keys_light(void);

/* Paint every LED black and stop the countdown. The frame is on its way when
 * this returns, not out: keep the rail on until it is (src/on_usb_power.c
 * waits BLANK_MS). System work queue. */
void lantern_keys_blank(void);

/* The palette changed: repaint while lit, nothing while dark. Any thread. */
void lantern_keys_refresh(void);

/* The boot light's state, before every light (on only for the boot light's)
 * and again at the boot light's end with the LEDs lit (off): only the no
 * picture, which paints every key black, shows a picture of its own for it,
 * the bands, and sends a frame when it ends (src/keys.c); for every other
 * picture this compiles to nothing. System work queue. */
#if IS_ENABLED(CONFIG_LANTERN_NO_PICTURE)
void lantern_keys_boot_light(bool on);
#else
static inline void lantern_keys_boot_light(bool on) { ARG_UNUSED(on); }
#endif
