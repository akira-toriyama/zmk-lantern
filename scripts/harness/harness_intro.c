/* The intro pick on the host (canon t-qvgv): which intro the boot light plays
 * and for how long, from the pick the central's host stored: none (the Kconfig
 * default), the bands, a bit this image does not build (the default again),
 * two bits (the first in the table's order), a change between two boot lights,
 * and the press rule during each (no hit while it plays, a hit after). Built
 * with the product flags; EXPECT_DEFAULT_BANDS=1 when the choice names the
 * bands (-DCONFIG_LANTERN_INTRO_DEFAULT_BANDS=1 in place of the star waves'). */
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

#ifndef EXPECT_DEFAULT_BANDS
#define EXPECT_DEFAULT_BANDS 0
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

static void press(uint32_t position) {
    static struct zmk_position_state_changed ev;
    ev = (struct zmk_position_state_changed){
        .source = ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL, .position = position, .state = true};
    fake_event = &ev;
    zmk_event_t e = {0};
    key_listener(&e);
    fake_event = NULL;
}

static int live_hits(void) {
    int n = 0;
    for (size_t j = 0; j < HITS; j++) {
        n += hits[j] != 0;
    }
    return n;
}

static bool frame_black(void) {
    for (size_t i = 0; i < LED_COUNT; i++) {
        if (strip_out[i].r || strip_out[i].g || strip_out[i].b) {
            return false;
        }
    }
    return true;
}

/* The frame is the bands: each LED its key's slot's shade at the picture's level. */
static bool frame_bands(void) {
    for (size_t i = 0; i < LED_COUNT; i++) {
        struct led_rgb want = frame_shades.at_level[frame_shades.rank[key_slot(led_key(i))]];
        if (!same(&strip_out[i], &want)) {
            return false;
        }
    }
    return true;
}

static bool show(uint32_t t) {
    fake_now = t;
    bool m = compose((size_t)atomic_get(&stage));
    led_strip_update_rgb(strip, pixels, LED_COUNT);
    return m;
}

/* One boot light from base, as the rail rule drives it: the state on (the
 * length the engine hands back), the light, frames through it, the state off,
 * the picture's frame, a blank. */
static void boot_light(const char *name, uint32_t base, uint32_t expect_ms, bool expect_bands) {
    fake_now = base;
    uint32_t ms = lantern_keys_boot_light(true);
    CHECK(ms == expect_ms, "%s: length %u, not %u", name, ms, expect_ms);
    lantern_keys_light();
    bool moving = show(base);
    CHECK(moving == !expect_bands, "%s: moving %d at the start", name, moving);
    CHECK(frame_bands() == expect_bands, "%s: bands %d at the start", name, frame_bands());
    press(positions[3]);
    CHECK(live_hits() == 0, "%s: a press during the intro left a hit", name);
    moving = show(base + ms / 2);
    CHECK(moving == !expect_bands, "%s: moving %d half-way", name, moving);
    CHECK(frame_bands() == expect_bands, "%s: bands %d half-way", name, frame_bands());
    CHECK(!frame_black(), "%s: black half-way", name);
    if (!expect_bands) {
        moving = show(base + ms);
        CHECK(!moving && frame_black(), "%s: the script's end is not black and still (moving %d)",
              name, moving);
    }
    (void)lantern_keys_boot_light(false);
    show(base + ms + 5);
    CHECK(frame_black(), "%s: the no picture is not black after the boot light", name);
    press(positions[3]);
    CHECK(live_hits() == 1, "%s: a press after the intro noted %d hits", name, live_hits());
    fake_now = base + ms + 10;
    lantern_keys_blank();
    paint(NULL);
}

int main(void) {
    fake_have = true;
    fake_palette[0] = 0xFF0000;
    fake_palette[1] = 0x00FF00;
    fake_palette[2] = 0x0000FF;
    fake_palette[3] = 0xFFFF00;
    fake_palette[4] = 0x00FFFF;
    fake_palette[5] = 0;
    fake_cycles = 0x1234567u;

    const uint32_t bands_ms = lantern_intro_bands.length_ms, star_ms = lantern_intro_star_waves.length_ms;
    CHECK(bands_ms == 1000 && star_ms == 10000, "lengths %u / %u", bands_ms, star_ms);
    const uint32_t default_ms = EXPECT_DEFAULT_BANDS ? bands_ms : star_ms;

    fake_intro_have = false;
    boot_light("no pick", 5000, default_ms, EXPECT_DEFAULT_BANDS);
    fake_intro_have = true;
    fake_intro = LANTERN_INTRO_BANDS;
    boot_light("bands", 30000, bands_ms, true);
    fake_intro = LANTERN_INTRO_STAR_WAVES;
    boot_light("star waves", 50000, star_ms, false);
    fake_intro = BIT(7);
    boot_light("unbuilt bit", 80000, default_ms, EXPECT_DEFAULT_BANDS);
    fake_intro = LANTERN_INTRO_BANDS | LANTERN_INTRO_STAR_WAVES;
    boot_light("two bits", 100000, bands_ms, true);
    /* a change while one plays shows at the next boot light */
    fake_intro = LANTERN_INTRO_STAR_WAVES;
    fake_now = 120000;
    uint32_t ms = lantern_keys_boot_light(true);
    lantern_keys_light();
    fake_intro = LANTERN_INTRO_BANDS;
    bool moving = show(120000 + 500);
    CHECK(ms == star_ms && moving && !frame_bands(), "a pick mid-intro changed the playing intro");
    (void)lantern_keys_boot_light(false);
    lantern_keys_blank();
    paint(NULL);
    boot_light("after the change", 140000, bands_ms, true);
    /* a pick stored as none again: the default */
    fake_intro_have = false;
    boot_light("pick gone", 160000, default_ms, EXPECT_DEFAULT_BANDS);

    printf("intro pick: default %u ms, fails %d\n", default_ms, fails);
    return fails != 0;
}
