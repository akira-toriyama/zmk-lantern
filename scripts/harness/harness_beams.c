/* Runs the real src/keys.c (included below) through the beams on the host
 * (canon t-6ttk): a press of this half, a thumb, a press of the other half
 * relayed in (lantern_keys_hit), two presses meeting, the cap, the pick's
 * change and the blank. */
#include <stdlib.h>

#include "unity.c"

/* src/effect.c's store, faked. */
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

static int dyed(void) {
    int n = 0;
    for (size_t i = 0; i < LED_COUNT; i++) {
        n += stains[i] != 0;
    }
    return n;
}

static bool black(struct led_rgb p) { return p.r == 0 && p.g == 0 && p.b == 0; }

/* A frame at t: the strip after it, how many LEDs lit, the channel sum. */
static size_t frame(uint32_t t, bool *moving, uint32_t *sum) {
    fake_now = t;
    bool m = compose(0);
    led_strip_update_rgb(strip, pixels, LED_COUNT);
    size_t lit_n = 0;
    uint32_t s = 0;
    for (size_t i = 0; i < LED_COUNT; i++) {
        lit_n += !black(strip_out[i]);
        s += strip_out[i].r + strip_out[i].g + strip_out[i].b;
    }
    if (moving) {
        *moving = m;
    }
    if (sum) {
        *sum = s;
    }
    return lit_n;
}

static void pick(bool have, uint32_t mask) {
    fake_pick_have = have;
    fake_pick = mask;
    lantern_keys_effect_changed();
}

static int led_of(uint32_t key) {
    for (size_t i = 0; i < LED_COUNT; i++) {
        if (positions[i] == key) {
            return (int)i;
        }
    }
    return -1;
}

/* Whether key k lies on key c's row or column, by the harness's own reading
 * of coords (half a unit), and how far along. */
static bool expect_on(uint32_t k, uint32_t c, uint32_t *along) {
    int dx = abs((int)coords[2 * k] - (int)coords[2 * c]);
    int dy = abs((int)coords[2 * k + 1] - (int)coords[2 * c + 1]);
    if (dy < 50) {
        *along = (uint32_t)dx;
        return true;
    }
    if (dx < 50) {
        *along = (uint32_t)dy;
        return true;
    }
    return false;
}

/* The shade a key's band paints at full scale: the harness's palette has five
 * distinct colours, slot 5 cleared to slot 0. */
static struct led_rgb band_hi(uint32_t key) {
    uint32_t rgb = fake_palette[slots[key]] != 0 ? fake_palette[slots[key]] : fake_palette[0];
    return (struct led_rgb){.r = (rgb >> 16) & 0xFF, .g = (rgb >> 8) & 0xFF, .b = rgb & 0xFF};
}

/* One hit at key k (pressed here, or relayed from the other half) noted at
 * base, frames every 10 ms through the beams' life and past it: what lights,
 * in what order, how strong, and when it stops. Returns the peak channel sum. */
static uint32_t one_press(uint32_t k, uint32_t base, bool relayed, size_t expect_lit) {
    uint32_t first[LED_COUNT], peak[LED_COUNT], sum_max = 0;
    bool moving = true;
    size_t on_line = 0;

    for (size_t i = 0; i < LED_COUNT; i++) {
        first[i] = UINT32_MAX;
        peak[i] = 0;
    }
    fake_now = base;
    if (relayed) {
        lantern_keys_hit((uint8_t)k);
    } else {
        press(k);
    }
    for (uint32_t t = 0; t <= BEAMS_MS + 200; t += 10) {
        uint32_t s;
        bool m;
        frame(base + t, &m, &s);
        sum_max = MAX(sum_max, s);
        for (size_t i = 0; i < LED_COUNT; i++) {
            uint32_t along;
            bool on = expect_on(positions[i], k, &along);
            struct led_rgb px = strip_out[i];
            if (!black(px)) {
                CHECK(on, "key %u at %u ms: LED %zu (key %u) lit off the row and column", k, t, i,
                      positions[i]);
                CHECK(t < BEAMS_MS, "key %u: LED %zu lit at %u ms, past the beams' life", k, i, t);
                if (first[i] == UINT32_MAX) {
                    first[i] = t;
                }
                /* the pressed key's band, pulled from black: one hue, every
                 * channel in the same proportion */
                struct led_rgb b = band_hi(k);
                unsigned int ch = MAX(px.r, MAX(px.g, px.b));
                CHECK((b.r == 0) == (px.r == 0) && (b.g == 0) == (px.g == 0) &&
                          (b.b == 0) == (px.b == 0),
                      "key %u at %u ms: LED %zu is %u,%u,%u, not the band's hue %u,%u,%u", k, t, i,
                      px.r, px.g, px.b, b.r, b.g, b.b);
                peak[i] = MAX(peak[i], ch);
            }
        }
        if (t == BEAMS_MS) {
            CHECK(!m, "key %u: still moving at the beams' life", k);
        } else if (t == BEAMS_MS - 10) {
            CHECK(m, "key %u: not moving at %u ms", k, t);
        }
        moving = m;
    }
    CHECK(!moving, "key %u: moving after the life", k);
    for (size_t i = 0; i < LED_COUNT; i++) {
        uint32_t along;
        if (!expect_on(positions[i], k, &along)) {
            continue;
        }
        on_line++;
        /* the head reaches it at along x 10 / 12 ms; it lights BEAMS_WIDTH before */
        uint32_t arrive = along * 10 / BEAMS_SPEED;
        uint32_t lights = along > BEAMS_WIDTH ? (along - BEAMS_WIDTH) * 10 / BEAMS_SPEED : 0;
        if (lights < BEAMS_MS) {
            CHECK(first[i] != UINT32_MAX, "key %u: LED %zu (key %u, along %u) never lit", k, i,
                  positions[i], along);
            CHECK(first[i] >= lights && first[i] <= lights + 20,
                  "key %u: LED %zu (along %u) first lit at %u ms, expected about %u", k, i, along,
                  first[i], lights);
            /* the peak: full at the head, fading with age; 10 ms frames miss
             * the exact head by up to 12 hundredths (38 of 256) */
            if (arrive < BEAMS_MS) {
                unsigned int want = 255 * (BEAMS_MS - arrive) / BEAMS_MS;
                CHECK(peak[i] + 45 >= want && peak[i] <= want + 2,
                      "key %u: LED %zu (along %u) peaked at %u, expected about %u", k, i, along,
                      peak[i], want);
            }
        } else {
            CHECK(first[i] == UINT32_MAX, "key %u: LED %zu (along %u) lit though the beam dies first",
                  k, i, along);
        }
        /* the order: farther along, later */
        for (size_t j = 0; j < LED_COUNT; j++) {
            uint32_t along_j;
            if (j != i && expect_on(positions[j], k, &along_j) && along_j > along &&
                first[j] != UINT32_MAX) {
                CHECK(first[i] != UINT32_MAX && first[i] <= first[j],
                      "key %u: LED %zu (along %u) lit after LED %zu (along %u)", k, i, along, j,
                      along_j);
            }
        }
    }
    CHECK(on_line == expect_lit, "key %u: %zu LEDs on its row and column, expected %zu", k, on_line,
          expect_lit);
    CHECK(sum_max <= CAP, "key %u: sum %u > CAP %u", k, sum_max, (unsigned)CAP);
    CHECK(dyed() == 0, "key %u: %d dyed", k, dyed());
    return sum_max;
}

int main(void) {
#ifdef HALF_RIGHT
    const uint32_t mid = 20, thumb = 50, far_in = 17, far_out = 0, left_k = 18, right_k = 22;
#else
    const uint32_t mid = 14, thumb = 47, far_in = 18, far_out = 23, left_k = 12, right_k = 16;
#endif
    uint32_t base = 5000;

    fake_have = true;
    fake_palette[0] = 0xFF0000;
    fake_palette[1] = 0x00FF00;
    fake_palette[2] = 0x0000FF;
    fake_palette[3] = 0xFFFF00;
    fake_palette[4] = 0x00FFFF;
    fake_palette[5] = 0;

    CHECK((EFFECTS_BUILT & LANTERN_EFFECT_BEAMS) != 0, "built %08x lacks the beams", (unsigned)EFFECTS_BUILT);
    CHECK(LANTERN_EFFECT_COUNT == 9, "count %d", (int)LANTERN_EFFECT_COUNT);
    CHECK(LANTERN_EFFECT_BEAMS == BIT(4), "bit %08x", (unsigned)LANTERN_EFFECT_BEAMS);
    CHECK(EFFECT_DEFAULT == LANTERN_EFFECT_CROSS_RING, "default %08x", (unsigned)EFFECT_DEFAULT);
    CHECK(HIT_LIFE_MS == DISC_MS && HIT_LIFE_MS == 1900, "hit life %u", (unsigned)HIT_LIFE_MS);
    CHECK(led_of(mid) >= 0 && led_of(thumb) >= 0 && led_of(far_in) < 0 && led_of(far_out) < 0 &&
              led_of(left_k) >= 0 && led_of(right_k) >= 0,
          "the harness's keys are on the wrong half");

    lantern_keys_light();
    frame(base, NULL, NULL);
    base += 100;
    pick(true, LANTERN_EFFECT_BEAMS);

    /* a key of the main block: its row's six and its column's four, nine LEDs */
    one_press(mid, base, false, 9);
    CHECK(live_hits() == 0, "mid: %d hits after the beams' life", live_hits());
    base += 10000;

    /* a thumb: the cluster's row of three and the thumb below, the bottom row
     * half a unit off never */
    one_press(thumb, base, false, 4);
    base += 10000;

    /* the hit goes at the picked effects' life, not at HIT_LIFE_MS (the
     * disc's 1900): live at 990, gone at 1000, and a frame 262 s later draws
     * nothing of it */
    fake_now = base;
    press(mid);
    frame(base + 990, NULL, NULL);
    CHECK(live_hits() == 1, "mid: %d hits at 990 ms", live_hits());
    frame(base + BEAMS_MS, NULL, NULL);
    CHECK(live_hits() == 0, "mid: %d hits at the beams' life", live_hits());
    fake_now = base;
    press(mid);
    frame(base + 1020, NULL, NULL);
    size_t phantom = frame(base + 1020 + HIT_TIME + 1 + 300, NULL, NULL);
    CHECK(phantom == 0 && live_hits() == 0, "wrap: %zu lit, %d hits after HIT_TIME", phantom, live_hits());
    base += 10000;

    /* the other half's inner column, relayed: its row comes in here from the
     * inner column out, at about half strength, in that key's band */
    one_press(far_in, base, true, 6);
    base += 10000;
    {
        int inner = -1;
        uint32_t along, nearest = UINT32_MAX;
        for (size_t i = 0; i < LED_COUNT; i++) {
            if (expect_on(positions[i], far_in, &along) && along < nearest) {
                nearest = along;
                inner = (int)i;
            }
        }
        CHECK(nearest == 650, "the inner columns are %u apart, not 650", nearest);
        fake_now = base;
        lantern_keys_hit((uint8_t)far_in);
        frame(base + 540, NULL, NULL);
        struct led_rgb px = strip_out[inner], b = band_hi(far_in);
        unsigned int ch = MAX(px.r, MAX(px.g, px.b));
        CHECK(ch >= 105 && ch <= 120, "relayed inner column: %u of 255 at 540 ms, expected about 113", ch);
        CHECK((b.r == 0) == (px.r == 0) && (b.g == 0) == (px.g == 0) && (b.b == 0) == (px.b == 0),
              "relayed: %u,%u,%u, not the band's hue %u,%u,%u", px.r, px.g, px.b, b.r, b.g, b.b);
        pick(true, LANTERN_EFFECT_BEAMS);
        base += 10000;
    }

    /* the other half's outer column, relayed: spent on the way, a few of 255 at most */
    {
        fake_now = base;
        lantern_keys_hit((uint8_t)far_out);
        unsigned int most = 0;
        for (uint32_t t = 0; t <= BEAMS_MS; t += 10) {
            frame(base + t, NULL, NULL);
            for (size_t i = 0; i < LED_COUNT; i++) {
                most = MAX(most, MAX(strip_out[i].r, MAX(strip_out[i].g, strip_out[i].b)));
            }
        }
        CHECK(most <= 12, "relayed outer column: %u of 255 here, expected a few", most);
        pick(true, LANTERN_EFFECT_BEAMS);
        base += 10000;
    }

    /* two presses two units apart, 50 ms apart: the key between them takes
     * the first beam's colour, then the second's */
    {
        int between = led_of(mid);
        fake_now = base;
        press(right_k);
        fake_now = base + 50;
        press(left_k);
        frame(base + 167, NULL, NULL);
        struct led_rgb px = strip_out[between], want = band_hi(right_k);
        CHECK(px.r == want.r && px.g == want.g && px.b == want.b,
              "meeting at 167 ms: %u,%u,%u, expected the first press's %u,%u,%u", px.r, px.g, px.b,
              want.r, want.g, want.b);
        frame(base + 217, NULL, NULL);
        px = strip_out[between];
        want = band_hi(left_k);
        CHECK(px.r == want.r && px.g == want.g && px.b == want.b,
              "meeting at 217 ms: %u,%u,%u, expected the second press's %u,%u,%u", px.r, px.g, px.b,
              want.r, want.g, want.b);
        pick(true, LANTERN_EFFECT_BEAMS);
        base += 10000;
    }

    /* the cap: eight keys pressed at once without a palette, white at full
     * scale, more than CAP allows */
    {
        fake_have = false;
        fake_now = base;
        for (size_t i = 0; i < 8; i++) {
            press(positions[i]);
        }
        uint32_t s;
        size_t lit_n = frame(base, NULL, &s);
        CHECK(lit_n == 8, "cap: %zu lit, expected 8", lit_n);
        CHECK(s <= CAP && s > CAP - 3 * 8, "cap: sum %u, CAP %u", s, (unsigned)CAP);
        fake_have = true;
        pick(true, LANTERN_EFFECT_BEAMS);
        base += 10000;
    }

    /* the beams with the crossing ring: more lit than the beams alone, the
     * frame moving until CROSS_MS */
    {
        pick(true, LANTERN_EFFECT_BEAMS | LANTERN_EFFECT_CROSS_RING);
        fake_now = base;
        press(mid);
        bool m;
        size_t both = frame(base + 100, &m, NULL);
        CHECK(both > 5 && m, "both: %zu lit at 100 ms, moving %d", both, m);
        frame(base + 1400, &m, NULL);
        CHECK(m, "both: not moving at 1400 ms");
        pick(true, LANTERN_EFFECT_BEAMS);
        base += 10000;
    }

    /* the pick changes mid-beam: the hits go now */
    fake_now = base;
    press(mid);
    size_t before = frame(base + 100, NULL, NULL);
    CHECK(before > 1, "repick: %zu lit before", before);
    pick(true, LANTERN_EFFECT_DISC);
    CHECK(live_hits() == 0, "repick: %d hits left", live_hits());
    bool mv;
    size_t after = frame(base + 130, &mv, NULL);
    CHECK(after == 0 && !mv, "repick: lit %zu moving %d", after, mv);
    base += 10000;

    /* the blank clears the hits */
    pick(true, LANTERN_EFFECT_BEAMS);
    fake_now = base;
    press(mid);
    frame(base + 100, NULL, NULL);
    lantern_keys_blank();
    paint(NULL);
    CHECK(live_hits() == 0, "blank: %d hits", live_hits());
    size_t dark = frame(base + 200, NULL, NULL);
    CHECK(dark == 0 || !atomic_get(&lit), "blank: %zu lit", dark);

    printf("beams: built %08x, %d submits, fails %d\n", (unsigned)EFFECTS_BUILT, submits, fails);
    return fails != 0;
}
