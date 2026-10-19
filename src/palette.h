/*
 * Copyright (c) 2026 akira-toriyama
 *
 * SPDX-License-Identifier: MIT
 *
 * The palette's in-memory copy and the central's entry point (src/palette.c),
 * for the port (src/palette_port.c). Layer contract: the port parses and
 * replies, and never stores or applies a colour itself; every change goes
 * through the palette behavior, on the central and the halves alike.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LANTERN_PALETTE_SLOTS 4
#define LANTERN_PALETTE_RGB_MAX 0xFFFFFF

/* The four colours (0xRRGGBB, slot 0 first). False while no palette has been
 * set on this device, and colours[] is then left alone. */
bool lantern_palette_get(uint32_t colours[LANTERN_PALETTE_SLOTS]);

/* Split central only: set slots 0..count-1 on every half and on this device;
 * the slots from count on keep their colours. count is 1..LANTERN_PALETTE_SLOTS
 * and every colour at most LANTERN_PALETTE_RGB_MAX, else -EINVAL. 0 means the
 * invocations are queued for the halves, not that they arrived: a half that is
 * away gets the palette when it is back (src/palette.c). */
int lantern_palette_send(const uint32_t *colours, size_t count);
