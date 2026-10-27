/*
 * Copyright (c) 2026 akira-toriyama
 *
 * SPDX-License-Identifier: MIT
 *
 * The intro (src/intro.c), for the rail rule (src/on_usb_power.c), whose boot
 * light lasts its length, and the frame engine (src/keys.c), which draws its
 * frames in the boot light's place. Layer contract: the rail rule opens the
 * boot light and reads the length; the engine starts the clock when it learns
 * the boot light is on and asks for each frame; the intro paints only the
 * frame it is handed and keeps only its clock, its seed and the depths it
 * measures.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/drivers/led_strip.h>
#include <zephyr/sys/util.h>

/* The intro's length (CONFIG_LANTERN_INTRO): the rail rule keeps the LEDs lit
 * this long from the light that starts it, the boot light's window. The
 * script that fills it is src/intro.c's, which asserts it fits. */
#define LANTERN_INTRO_MS 10000

#if IS_ENABLED(CONFIG_LANTERN_INTRO)

/* The boot light opens (src/keys.c, on the system work queue, before the
 * frame can see the boot light's state): the intro's clock starts now and the
 * glitter takes its seed. */
void lantern_intro_start(void);

/* The frame's thread: the intro's frame, now, into px (every LED), to go over
 * a black picture; true while the script runs, and false with px black from
 * LANTERN_INTRO_MS on, when the rail rule ends the boot light. */
bool lantern_intro_draw(struct led_rgb *px);

#endif /* IS_ENABLED(CONFIG_LANTERN_INTRO) */
