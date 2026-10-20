/*
 * Copyright (c) 2026 akira-toriyama
 *
 * SPDX-License-Identifier: MIT
 *
 * The key LEDs' picture (src/keys.c), for the rail rule (src/on_usb_power.c)
 * and the palette (src/palette.c). Layer contract: this file is the strip's
 * one writer; callers say lit or dark and never touch a pixel. Every call may
 * come from any thread; the frame is written on ZMK's low priority work
 * queue.
 */

#pragma once

/* Paint the palette's picture (white without a palette). Call with the rail
 * on and settled. */
void lantern_keys_light(void);

/* Paint every LED black. The frame is on its way when this returns, not out:
 * keep the rail on until it is (src/on_usb_power.c waits BLANK_MS). */
void lantern_keys_blank(void);

/* The palette changed: repaint while lit, nothing while dark. */
void lantern_keys_refresh(void);
