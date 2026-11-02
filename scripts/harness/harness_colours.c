/* The conf's colours on the host (canon t-gwsw): without a palette the frame
 * paints from CONFIG_LANTERN_COLOUR_0..5 as from a host's palette (a slot left
 * 0 shows slot 0's); a host's palette replaces them and its loss brings them
 * back; built with all six 0 (EXPECT_WHITE=1) and no palette every key is
 * white. The bands picture: slot 0 red, slot 1 green, slots 2.. cleared. */
#include <stdlib.h>

#include "unity.c"

static bool fake_pick_have;
static uint32_t fake_pick;
bool lantern_effect_get(uint32_t *mask, uint32_t *colour) {
    if (fake_pick_have) {
        *mask = fake_pick;
        if (colour != NULL) {
            *colour = fake_colour;
        }
    }
    return fake_pick_have;
}

#ifndef EXPECT_WHITE
#define EXPECT_WHITE 0
#endif

static int fails;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            fails++;                                                                               \
            fprintf(stderr, "FAIL: " __VA_ARGS__);                                                 \
            fputc('\n', stderr);                                                                   \
        }                                                                                          \
    } while (0)

static void show(void) { compose((size_t)atomic_get(&stage)); }

/* The channel pattern conf slot s paints: red, green, blue, yellow, magenta,
 * cyan for slots 0..5 in the conf build; a slot left 0 paints slot 0's. */
static const uint32_t conf[6] = {CONFIG_LANTERN_COLOUR_0, CONFIG_LANTERN_COLOUR_1,
                                 CONFIG_LANTERN_COLOUR_2, CONFIG_LANTERN_COLOUR_3,
                                 CONFIG_LANTERN_COLOUR_4, CONFIG_LANTERN_COLOUR_5};
static bool matches(struct led_rgb p, uint32_t rgb) {
    bool r = (rgb >> 16) != 0, g = ((rgb >> 8) & 0xFF) != 0, b = (rgb & 0xFF) != 0;
    return (p.r > 0) == r && (p.g > 0) == g && (p.b > 0) == b && (p.r | p.g | p.b) != 0;
}
static void expect_conf(const char *when) {
    for (size_t i = 0; i < LED_COUNT; i++) {
        struct led_rgb p = pixels[i];
        if (EXPECT_WHITE) {
            CHECK(p.r > 0 && p.r == p.g && p.g == p.b, "%s: LED %zu not white (%u %u %u)", when, i, p.r, p.g, p.b);
        } else {
            uint32_t want = conf[base_slot(i)] != 0 ? conf[base_slot(i)] : conf[0];
            CHECK(matches(p, want), "%s: LED %zu (slot %u) not %06x (%u %u %u)", when, i, base_slot(i), want, p.r, p.g, p.b);
        }
    }
}

int main(void) {
    int seen[6] = {0};
    for (size_t i = 0; i < LED_COUNT; i++) {
        seen[base_slot(i)]++;
    }

    /* 1. no palette from the host: the conf's colours */
    fake_have = false;
    show();
    expect_conf("no palette");

    /* 2. the host's palette, blue in every slot, replaces them */
    fake_have = true;
    for (size_t s = 0; s < 6; s++) {
        fake_palette[s] = 0x0000FF;
    }
    show();
    for (size_t i = 0; i < LED_COUNT; i++) {
        struct led_rgb p = pixels[i];
        CHECK(p.b > 0 && p.r == 0 && p.g == 0, "host palette: LED %zu not blue (%u %u %u)", i, p.r, p.g, p.b);
    }

    /* 3. the host's palette gone (a device flashed across a slot-count change): the conf's again */
    fake_have = false;
    show();
    expect_conf("palette gone");

    printf("colours: %d failure(s), expect_white %d, LEDs by slot %d %d %d %d %d %d\n", fails, EXPECT_WHITE,
           seen[0], seen[1], seen[2], seen[3], seen[4], seen[5]);
    return fails != 0;
}
