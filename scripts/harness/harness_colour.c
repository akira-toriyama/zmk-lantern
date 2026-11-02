/* The effects' colour on the host (canon t-rgp5): with a pick's colour every
 * effect draws in that one colour over the picture, which keeps the palette's
 * (the bands) or black (the no picture); the stain dyes in it; a colour change
 * clears the effects' state; without a pick CONFIG_LANTERN_EFFECT_COLOUR rules
 * (EXPECT_DEFAULT_RED=1 when built with it red). The palette is pure green, so
 * a red colour shows as r > 0 with b == 0 everywhere and g never above the
 * picture's. Without a colour nothing changes: run.sh equiv. */
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

#ifndef EXPECT_DEFAULT_RED
#define EXPECT_DEFAULT_RED 0
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

#ifdef HALF_LEFT
static const uint32_t other = 18;
#else
static const uint32_t other = 17;
#endif

static void press(uint32_t position) {
    static struct zmk_position_state_changed ev;
    ev = (struct zmk_position_state_changed){
        .source = ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL, .position = position, .state = true};
    fake_event = &ev;
    zmk_event_t e = {0};
    key_listener(&e);
    fake_event = NULL;
}

static void pick(bool have, uint32_t mask, uint32_t colour) {
    fake_pick_have = have;
    fake_pick = mask;
    fake_colour = colour;
    lantern_keys_effect_changed();
}

static void show(uint32_t t) {
    fake_now = t;
    compose((size_t)atomic_get(&stage));
    led_strip_update_rgb(strip, pixels, LED_COUNT);
}

/* Frames every 30 ms from a to b, as the engine ticks: the dye a ring writes
 * in one frame shows in the picture from the next. */
static void run(uint32_t a, uint32_t b) {
    for (uint32_t t = a; t <= b; t += 30) {
        show(t);
    }
}

static int dyed(void) {
    int n = 0;
    for (size_t i = 0; i < LED_COUNT; i++) {
        n += stains[i] != 0;
    }
    return n;
}

/* A light at base with the pick as set, a press of this half and a relayed one,
 * frames every 30 ms to 1500 ms: the colour red shows (r above the picture's
 * somewhere), blue never, green never above the picture's. */
static void red_effect(const char *name, uint32_t base) {
    struct led_rgb pic[LED_COUNT];
    bool reddened = false;

    fake_now = base;
    lantern_keys_light();
    paint(NULL);
    memcpy(pic, strip_out, sizeof(pic));
    press(positions[3]);
    lantern_keys_hit(other);
    for (uint32_t t = 0; t <= 1500; t += 30) {
        show(base + t);
        for (size_t i = 0; i < LED_COUNT; i++) {
            CHECK(strip_out[i].b == 0, "%s: t=%u LED %zu blue %u", name, t, i, strip_out[i].b);
            CHECK(strip_out[i].g <= pic[i].g, "%s: t=%u LED %zu green %u above the picture's %u",
                  name, t, i, strip_out[i].g, pic[i].g);
            if (strip_out[i].r > pic[i].r) {
                reddened = true;
            }
        }
    }
    CHECK(reddened, "%s: no pixel reddened", name);
    fake_now = base + 1600;
    lantern_keys_blank();
    paint(NULL);
}

int main(void) {
    fake_have = true;
    for (size_t s = 0; s < 6; s++) {
        fake_palette[s] = 0x00FF00;
    }
    uint32_t base = 5000;

    /* every effect, picked one at a time, in red */
    static const struct {
        const char *name;
        uint32_t mask;
    } one[] = {
        {"thin ring", LANTERN_EFFECT_THIN_RING}, {"disc", LANTERN_EFFECT_DISC},
        {"stain", LANTERN_EFFECT_STAIN},         {"cross ring", LANTERN_EFFECT_CROSS_RING},
        {"beams", LANTERN_EFFECT_BEAMS},         {"water", LANTERN_EFFECT_WATER},
        {"dart", LANTERN_EFFECT_DART},           {"all seven", 0x7f},
    };
    for (size_t k = 0; k < ARRAY_SIZE(one); k++) {
        pick(true, one[k].mask, 0xFF0000);
        red_effect(one[k].name, base);
        base += 5000;
    }

    /* the stain's dye in the colour, and gone at a colour change */
    pick(true, LANTERN_EFFECT_STAIN, 0xFF0000);
    fake_now = base;
    lantern_keys_light();
    paint(NULL);
    press(positions[3]);
    run(base, base + 600);
    CHECK(dyed() > 0, "no dye after the stain's ring");
    struct led_rgb want = scale(0xFF0000, PICTURE_LEVEL);
    bool red_dye = false;
    for (size_t i = 0; i < LED_COUNT; i++) {
        if (stains[i] != 0) {
            red_dye = same(&strip_out[i], &want) || red_dye;
        }
    }
    CHECK(red_dye, "no dyed LED at the colour's picture level (%u,%u,%u)", want.r, want.g, want.b);
    pick(true, LANTERN_EFFECT_STAIN, 0x0000FF);
    show(base + 630);
    CHECK(dyed() == 0, "the dye survived a colour change (%d)", dyed());
    fake_now = base + 630;
    press(positions[3]);
    run(base + 630, base + 630 + 600);
    want = scale(0x0000FF, PICTURE_LEVEL);
    bool blue_dye = false;
    for (size_t i = 0; i < LED_COUNT; i++) {
        if (stains[i] != 0) {
            blue_dye = same(&strip_out[i], &want) || blue_dye;
        }
    }
    CHECK(dyed() > 0 && blue_dye, "no blue dye after the change");
    /* back to the palette's colours: the dye goes again, the next is green */
    pick(true, LANTERN_EFFECT_STAIN, 0);
    show(base + 1300);
    CHECK(dyed() == 0, "the dye survived the change back to the palette (%d)", dyed());
    fake_now = base + 1300;
    press(positions[3]);
    run(base + 1300, base + 1900);
    want = scale(0x00FF00, PICTURE_LEVEL);
    bool green_dye = false;
    for (size_t i = 0; i < LED_COUNT; i++) {
        if (stains[i] != 0) {
            green_dye = same(&strip_out[i], &want) || green_dye;
        }
    }
    CHECK(dyed() > 0 && green_dye, "no green dye with the palette's colours back");
    fake_now = base + 2000;
    lantern_keys_blank();
    paint(NULL);
    base += 5000;

    /* no pick: the Kconfig default colour (red when built so, else the palette's green) */
    pick(false, 0, 0);
    fake_now = base;
    lantern_keys_light();
    paint(NULL);
    press(positions[3]);
    bool any_red = false, any_blue = false;
    for (uint32_t t = 0; t <= 1500; t += 30) {
        show(base + t);
        for (size_t i = 0; i < LED_COUNT; i++) {
            any_red = strip_out[i].r > 0 || any_red;
            any_blue = strip_out[i].b > 0 || any_blue;
        }
    }
    CHECK(any_red == (EXPECT_DEFAULT_RED != 0), "no pick: red %d, expected %d", any_red,
          EXPECT_DEFAULT_RED);
    CHECK(!any_blue, "no pick: blue");
    lantern_keys_blank();
    paint(NULL);

    printf("colour: built %08x, default colour %06x, fails %d\n", (unsigned)EFFECTS_BUILT,
           (unsigned)CONFIG_LANTERN_EFFECT_COLOUR, fails);
    return fails != 0;
}
