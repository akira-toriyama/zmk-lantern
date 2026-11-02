/* Runs the real src/keys.c (included below) through one intro on the host:
 * dumps every frame as CSV and checks the cap, the end and the press rule. */
#include <stdlib.h>

#include "unity.c"

/* the pick (src/effect.c): none set, so the default draws */
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
    zmk_event_t e;
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

int main(int argc, char **argv) {
    FILE *out = argc > 1 ? fopen(argv[1], "w") : NULL;
    uint32_t step = argc > 2 ? (uint32_t)atoi(argv[2]) : 30;
    if (argc > 3) {
        fake_cycles = (uint32_t)strtoul(argv[3], NULL, 0);
    }
    uint32_t base = argc > 4 ? (uint32_t)strtoul(argv[4], NULL, 0) : 5000;

    /* as the rail rule does: the boot light's state with the LEDs dark, then the light */
    fake_now = base;
    lantern_keys_boot_light(true);
    lantern_keys_light();

    uint32_t max_sum = 0, capped = 0, frames = 0, presses = 0, mixed_star = 0;
    bool moving = true;
    for (uint32_t t = 0; t <= LANTERN_INTRO_MS + 2 * step; t += step) {
        fake_now = base + t;
        if ((t >= 2000 && t < 2000 + step) || (t >= 4000 && t < 4000 + step)) {
            press(positions[3]); /* a key while the intro plays: no hit */
            presses++;
        }
        moving = compose((size_t)atomic_get(&stage));
        led_strip_update_rgb(strip, pixels, LED_COUNT);
        frames++;
        uint32_t sum = 0, raw = 0;
        for (size_t i = 0; i < LED_COUNT; i++) {
            struct led_rgb p = strip_out[i];
            sum += p.r + p.g + p.b;
            /* one primary at a time, the star's head included (canon t-ky8x) */
            CHECK((p.r != 0) + (p.g != 0) + (p.b != 0) <= 1,
                  "t=%u LED %zu (depth %u) mixes %u,%u,%u", t, i, depth[i], p.r, p.g, p.b);
            struct led_rgb q = t < LANTERN_INTRO_MS ? intro_led(i, t, (uint32_t)intro_seed)
                                                    : (struct led_rgb){0};
            raw += q.r + q.g + q.b;
            if (out) {
                fprintf(out, "%u,%zu,%u,%u,%u,%u,%u,%u\n", t, i, depth[i],
                        coords[2 * positions[i]], coords[2 * positions[i] + 1], p.r, p.g, p.b);
            }
        }
        CHECK(sum <= CAP, "t=%u sum %u > CAP %u", t, sum, (unsigned)CAP);
        if (raw > CAP) {
            capped++;
        }
        max_sum = MAX(max_sum, sum);
        CHECK(moving == (t < LANTERN_INTRO_MS), "t=%u moving %d", t, moving);
        CHECK(live_hits() == 0, "t=%u a press during the intro left a hit", t);
        if (t >= LANTERN_INTRO_MS) {
            for (size_t i = 0; i < LED_COUNT; i++) {
                CHECK(strip_out[i].r == 0 && strip_out[i].g == 0 && strip_out[i].b == 0,
                      "t=%u LED %zu not black after the script", t, i);
            }
        }
    }
    /* the last frame of the script must already be black: nothing glows at its end */
    fake_now = base + LANTERN_INTRO_MS - 1;
    compose(0);
    for (size_t i = 0; i < LED_COUNT; i++) {
        CHECK(pixels[i].r == 0 && pixels[i].g == 0 && pixels[i].b == 0,
              "LED %zu still glows 1 ms before the end", i);
    }

    /* the boot light's end with the LEDs lit: the no picture's black, effects back */
    lantern_keys_boot_light(false);
    fake_now = base + LANTERN_INTRO_MS + 100;
    compose(0);
#if IS_ENABLED(CONFIG_LANTERN_NO_PICTURE)
    for (size_t i = 0; i < LED_COUNT; i++) {
        CHECK(pixels[i].r == 0 && pixels[i].g == 0 && pixels[i].b == 0,
              "LED %zu not black after the boot light", i);
    }
#endif
    press(positions[3]);
    CHECK(live_hits() == 1, "a press after the intro noted %d hits", live_hits());

    /* depths: 0 and DEPTH_ONE present, all in range */
    uint16_t lo = UINT16_MAX, hi = 0;
    for (size_t i = 0; i < LED_COUNT; i++) {
        lo = MIN(lo, depth[i]);
        hi = MAX(hi, depth[i]);
    }
    CHECK(lo == 0 && hi == DEPTH_ONE, "depths %u..%u", lo, hi);

    CHECK(presses == 2, "%u presses during the intro, not 2", presses);
    printf("frames %u, CAP %u, max sum %u, frames the cap held %u, mixed LEDs by the star %u, fails %d\n",
           frames, (unsigned)CAP, max_sum, capped, mixed_star, fails);
    if (out) {
        fclose(out);
    }
    return fails != 0;
}
