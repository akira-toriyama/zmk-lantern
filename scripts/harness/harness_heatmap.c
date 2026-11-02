/* Runs the real src/keys.c (unity.c) through the heatmap on the host (canon
 * t-6ttk): one press against the design in floating point (the key, its
 * neighbours by distance, the cooling, the brightness ramp and the colour
 * along the palette), the other half's press relayed in, repeated presses
 * heating a key to the top, a burst past the engine's ring, the press count's
 * wrap, a stage move, the cap, the pick's change, the blank, the clock's
 * wraps, the skewed clock, and the pick's colour. */
#include <math.h>
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

/* The design (canon t-6ttk, the recommendation, QMK's proportions): a press
 * adds PRESS to its key and NEAR x (1 - d / REACH) to a key d units away
 * under REACH, every key sheds COOL a second, the top is 1, the brightness
 * is full from FULL on, and the colour runs the palette from its last shade
 * cold to its first at the top. */
#define M_PRESS 0.125
#define M_NEAR 0.06
#define M_REACH 2.9
#define M_COOL 0.15
#define M_FULL (1.0 / 3)

/* The model: each LED's heat, the presses in time order (this half's keys
 * only: a relayed press warms nothing), and the time it stands at. */
static double mh[LED_COUNT];
static uint32_t model_now;
static struct {
    uint32_t key, when;
} mp[256];
static size_t mp_n, mp_done;

static void model_reset(uint32_t t) {
    memset(mh, 0, sizeof(mh));
    model_now = t;
    mp_n = mp_done = 0;
}

/* in key units, rounded down to the hundredth as the module's integer square
 * root does (frame.h's distance()): over tens of presses the rounding adds up
 * on a neighbour that never cools to nothing */
static double dist(uint32_t a, uint32_t b) {
    double dx = (double)coords[2 * a] - coords[2 * b], dy = (double)coords[2 * a + 1] - coords[2 * b + 1];
    return floor(sqrt(dx * dx + dy * dy)) / 100.0;
}

static void model_add(uint32_t key) {
    for (size_t i = 0; i < LED_COUNT; i++) {
        double add;
        if (positions[i] == key) {
            add = M_PRESS;
        } else {
            double d = dist(positions[i], key);
            if (d >= M_REACH) {
                continue;
            }
            add = M_NEAR * (1 - d / M_REACH);
        }
        mh[i] = fmin(1.0, mh[i] + add);
    }
}

static void model_cool_to(uint32_t t) {
    double dt = (uint32_t)(t - model_now) / 1000.0;
    for (size_t i = 0; i < LED_COUNT; i++) {
        mh[i] = fmax(0.0, mh[i] - M_COOL * dt);
    }
    model_now = t;
}

/* The model at t: every press at or before t applied at its time. */
static void model_to(uint32_t t) {
    while (mp_done < mp_n && (uint32_t)(t - mp[mp_done].when) <= 0x7FFFFFFFu) {
        model_cool_to(mp[mp_done].when);
        model_add(mp[mp_done].key);
        mp_done++;
    }
    model_cool_to(t);
}

static int led_of(uint32_t key) {
    for (size_t i = 0; i < LED_COUNT; i++) {
        if (positions[i] == key) {
            return (int)i;
        }
    }
    return -1;
}

/* the count of presses noted, the harness's own */
static unsigned presses;

static void press(uint32_t position) {
    static struct zmk_position_state_changed ev;
    ev = (struct zmk_position_state_changed){
        .source = ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL, .position = position, .state = true};
    fake_event = &ev;
    zmk_event_t e = {0};
    key_listener(&e);
    fake_event = NULL;
    presses++;
    if (led_of(position) >= 0 && mp_n < ARRAY_SIZE(mp)) {
        mp[mp_n].key = position;
        mp[mp_n].when = fake_now;
        mp_n++;
    }
}

static void relayed(uint32_t position) {
    lantern_keys_hit((uint8_t)position);
    presses++;
}

/* the race's two presses, noted from the clock hook (stubs_skew.h) */
static uint32_t race_v, race_w;
static void press(uint32_t position);
static void race_hook(void) {
    press(race_v);
    press(race_w);
}

static int live_hits(void) {
    int n = 0;
    for (size_t j = 0; j < HITS; j++) {
        n += hits[j] != 0;
    }
    return n;
}

static bool black(struct led_rgb p) { return p.r == 0 && p.g == 0 && p.b == 0; }

/* A frame at t: the strip after it, how many LEDs lit, the channel sum; the
 * heatmap's own clock read lands skew ms after the engine's readings (one a
 * live hit: stubs_skew.h). */
static size_t frame_skew(uint32_t t, uint32_t skew, bool *moving, uint32_t *sum) {
    fake_now = t;
    clock_calls = 0;
    skew_after = skew ? (uint32_t)live_hits() : 0xFFFFFFFFu;
    skew_ms = skew;
    bool m = compose(0);
    skew_after = 0xFFFFFFFFu;
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
static size_t frame(uint32_t t, bool *moving, uint32_t *sum) { return frame_skew(t, 0, moving, sum); }

static void pick(bool have, uint32_t mask) {
    fake_pick_have = have;
    fake_pick = mask;
    lantern_keys_effect_changed();
}

/* The palettes: two shades, red (0, the hot end) and blue (1, the cold end);
 * one shade, green; none (white). */
enum pal { PAL_TWO, PAL_ONE, PAL_NONE, PAL_PICK };
static enum pal palette;

static void palette_two(void) {
    fake_have = true;
    fake_palette[0] = 0xFF0000;
    fake_palette[1] = 0x0000FF;
    for (size_t s = 2; s < 6; s++) {
        fake_palette[s] = 0;
    }
    palette = PAL_TWO;
}

static void palette_one(void) {
    fake_have = true;
    fake_palette[0] = 0x00FF00;
    for (size_t s = 1; s < 6; s++) {
        fake_palette[s] = 0;
    }
    palette = PAL_ONE;
}

static void palette_none(void) {
    fake_have = false;
    palette = PAL_NONE;
}

static double worst, worst_heat;

/* The pixel the design gives heat f under the palette in use: the brightness
 * ramp times the blend, brought back to full scale (frame.h's span()). */
static void expected(double f, double *r, double *g, double *b) {
    double bright = fmin(1.0, f / M_FULL);
    *r = *g = *b = 0;
    if (f <= 0) {
        return;
    }
    switch (palette) {
    case PAL_TWO: {
        double rr = f, bb = 1 - f, m = fmax(rr, bb);
        *r = bright * rr / m;
        *b = bright * bb / m;
        break;
    }
    case PAL_ONE:
    case PAL_PICK:
        *g = bright;
        break;
    case PAL_NONE:
        *r = *g = *b = bright;
        break;
    }
}

/* Every LED against the model at t, where the frame's sum is under the cap
 * (over it the engine scales alike). */
static void check_frame(uint32_t t, const char *what) {
    uint32_t sum;
    frame(t, NULL, &sum);
    CHECK(sum <= CAP, "%s at %u: sum %u > CAP %u", what, t, sum, (unsigned)CAP);
    model_to(t);
    /* the heat itself, whatever the cap does to the frame */
    for (size_t i = 0; i < LED_COUNT; i++) {
        double h = heat[i] / 65536.0;
        CHECK(fabs(h - mh[i]) <= 0.005, "%s at %u: LED %zu (key %u) heat %.4f, model %.4f", what, t, i,
              positions[i], h, mh[i]);
        worst_heat = fmax(worst_heat, fabs(h - mh[i]));
    }
    /* the pixels, where the model's frame is well under the cap (held to
     * it, every channel scales alike) */
    double want_sum = 0;
    for (size_t i = 0; i < LED_COUNT; i++) {
        double r, g, b;
        expected(mh[i], &r, &g, &b);
        want_sum += (r + g + b) * 255;
    }
    bool under = want_sum < 0.9 * CAP;
    for (size_t i = 0; i < LED_COUNT; i++) {
        double r, g, b;
        expected(mh[i], &r, &g, &b);
        if (mh[i] <= 0) {
            CHECK(black(strip_out[i]), "%s at %u: LED %zu (key %u) lit %u,%u,%u, cold", what, t, i,
                  positions[i], strip_out[i].r, strip_out[i].g, strip_out[i].b);
            continue;
        }
        if (under) {
            double dr = fabs(strip_out[i].r / 255.0 - r), dg = fabs(strip_out[i].g / 255.0 - g),
                   db = fabs(strip_out[i].b / 255.0 - b);
            CHECK(dr <= 0.02 && dg <= 0.02 && db <= 0.02,
                  "%s at %u: LED %zu (key %u) %u,%u,%u, model %.0f,%.0f,%.0f (heat %.3f)", what, t, i,
                  positions[i], strip_out[i].r, strip_out[i].g, strip_out[i].b, r * 255, g * 255, b * 255,
                  mh[i]);
            worst = fmax(worst, fmax(dr, fmax(dg, db)));
        }
    }
}

int main(void) {
#ifdef HALF_RIGHT
    const uint32_t mid = 20, thumb = 50, other = 17, other_thumb = 48;
#else
    const uint32_t mid = 14, thumb = 47, other = 18, other_thumb = 49;
#endif
    uint32_t base = 5000;

    palette_two();

    CHECK((EFFECTS_BUILT & LANTERN_EFFECT_HEATMAP) != 0, "built %08x lacks the heatmap",
          (unsigned)EFFECTS_BUILT);
    CHECK(LANTERN_EFFECT_COUNT == 9, "count %d", (int)LANTERN_EFFECT_COUNT);
    CHECK(LANTERN_EFFECT_HEATMAP == BIT(10), "bit %08x", (unsigned)LANTERN_EFFECT_HEATMAP);
    CHECK((LANTERN_EFFECT_ALL & (BIT(7) | BIT(9))) == 0, "bit 7 or 9 taken");
    CHECK(EFFECT_DEFAULT == LANTERN_EFFECT_CROSS_RING, "default %08x", (unsigned)EFFECT_DEFAULT);
    CHECK(HEATMAP_MS == 7000, "HEATMAP_MS %u", (unsigned)HEATMAP_MS);
    CHECK(lantern_effect_heatmap.life() == HEATMAP_MS, "life %u", lantern_effect_heatmap.life());
    CHECK(lantern_effect_heatmap.picture == NULL && lantern_effect_heatmap.serves == 0, "entry");
    CHECK(led_of(mid) >= 0 && led_of(thumb) >= 0 && led_of(other) < 0 && led_of(other_thumb) < 0,
          "the harness's keys are on the wrong half");
    CHECK(dist(mid, thumb) > M_REACH, "mid and thumb within reach: %.2f", dist(mid, thumb));
    CHECK(HEAT_PRESS == 8192 && HEAT_NEAR == 3932 && HEAT_REACH == 290 && HEAT_COOL == 9830 &&
              HEAT_FULL == 21845,
          "constants %u %u %u %u %u", HEAT_PRESS, HEAT_NEAR, HEAT_REACH, HEAT_COOL, HEAT_FULL);

    lantern_keys_light();
    frame(base, NULL, NULL);
    base += 100;
    pick(true, LANTERN_EFFECT_HEATMAP);
    presses = 0;

    /* one press: the key and its neighbours by distance, cooling frame by
     * frame, the key gone 833 ms on, the farthest neighbour sooner; moving
     * to the life and not at it, the hit gone with it */
    {
        bool m;
        model_reset(base);
        fake_now = base;
        press(mid);
        int near = 0; /* the neighbours whose warmth shows: a channel at least 1/255 */
        for (size_t i = 0; i < LED_COUNT; i++) {
            double d = dist(positions[i], mid), r, g, b;
            expected(d < M_REACH ? M_NEAR * (1 - d / M_REACH) : 0, &r, &g, &b);
            near += positions[i] != mid && d < M_REACH && fmax(r, fmax(g, b)) * 255 >= 1.5;
        }
        CHECK(near >= 8, "one: %d neighbours within reach", near);
        for (uint32_t t = 0; t <= HEATMAP_MS + 200; t += 10) {
            check_frame(base + t, "one");
            size_t lit_n = frame(base + t, &m, NULL);
            if (t == 0) {
                CHECK(lit_n == (size_t)near + 1, "one: %zu lit at the press, expected %d", lit_n, near + 1);
                double r, g, b;
                expected(M_PRESS, &r, &g, &b);
                CHECK(b > r && b < 0.4 && fabs(b - 0.375) < 0.01, "one: the key's blue %.3f", b);
            }
            if (t >= 840) {
                CHECK(lit_n == 0, "one at %u: %zu lit, all cold", t, lit_n);
            }
            if (t < HEATMAP_MS) {
                CHECK(m, "one: not moving at %u", t);
            } else {
                CHECK(!m, "one: moving at %u", t);
            }
        }
        CHECK(live_hits() == 0, "one: %d hits after the life", live_hits());
        base += 10000;
    }

    /* the other half's press, relayed: nothing here, the frames moving for
     * the hit's life all the same */
    {
        bool m;
        model_reset(base);
        fake_now = base;
        relayed(other);
        relayed(other_thumb);
        for (uint32_t t = 0; t < HEATMAP_MS; t += 10) {
            size_t lit_n = frame(base + t, &m, NULL);
            CHECK(lit_n == 0 && m, "relayed at %u: %zu lit, moving %d", t, lit_n, m);
        }
        frame(base + HEATMAP_MS, &m, NULL);
        CHECK(!m && live_hits() == 0, "relayed: moving %d, %d hits at the life", m, live_hits());
        base += 10000;
    }

    /* eight presses of one key 100 ms apart: the key heats toward the top,
     * full bright from the third press and red by the end, then cools to
     * nothing before the last hit's life ends */
    {
        model_reset(base);
        for (uint32_t t = 0; t <= 700 + HEATMAP_MS; t += 10) {
            if (t <= 700 && t % 100 == 0) {
                fake_now = base + t;
                press(mid);
            }
            check_frame(base + t, "eight");
            struct led_rgb px = strip_out[led_of(mid)];
            if (t == 200) {
                CHECK(px.b == 255 || px.r == 255, "eight: not full bright at the third press: %u,%u,%u", px.r,
                      px.g, px.b);
            }
            if (t == 700) {
                CHECK(px.r == 255 && px.b < 40, "eight: not red at the top: %u,%u,%u", px.r, px.g, px.b);
                model_to(base + t);
                CHECK(mh[led_of(mid)] > 0.85, "eight: model heat %.3f", mh[led_of(mid)]);
            }
            if (t >= 700 + 6700) {
                CHECK(black(px), "eight at %u: the key still %u,%u,%u", t, px.r, px.g, px.b);
            }
        }
        bool m;
        frame(base + 700 + HEATMAP_MS, &m, NULL);
        CHECK(!m && live_hits() == 0, "eight: moving %d, %d hits at the life", m, live_hits());
        base += 20000;
    }

    /* two presses of one key before a frame runs: both counted */
    {
        model_reset(base);
        fake_now = base;
        press(mid);
        press(mid);
        check_frame(base + 10, "two at once");
        model_to(base + 10);
        CHECK(fabs(mh[led_of(mid)] - 0.2485) < 0.001, "two at once: model %.4f", mh[led_of(mid)]);
        CHECK(fabs(heat[led_of(mid)] / 65536.0 - 0.2485) < 0.002, "two at once: heat %.4f",
              heat[led_of(mid)] / 65536.0);
        frame(base + HEATMAP_MS + 10, NULL, NULL);
        base += 10000;
    }

    /* a burst past the ring: 24 keys 50 ms apart (twenty a second, the ring
     * holds sixteen), the first eight presses leave the ring and their heat
     * cools on with the rest, every LED on the model at every frame */
    {
        model_reset(base);
        CHECK(LED_COUNT >= 24, "fewer than 24 LEDs");
        uint32_t t = 0;
        for (size_t k = 0; k < 24; k++) {
            while (t < 50 * k) {
                check_frame(base + t, "burst");
                t += 10;
            }
            fake_now = base + t;
            press(positions[k]);
        }
        CHECK(live_hits() == HITS, "burst: %d hits live, expected the ring full", live_hits());
        for (; t <= 50 * 23 + HEATMAP_MS + 100; t += 10) {
            check_frame(base + t, "burst on");
        }
        CHECK(live_hits() == 0, "burst: %d hits after", live_hits());
        bool m;
        frame(base + t, &m, NULL);
        CHECK(!m, "burst: moving after");
        base += 20000;
    }

    /* the press count's wrap: 70 presses 60 ms apart on two keys, the count
     * (mod 60) coming round, every one counted */
    {
        model_reset(base);
        uint32_t t = 0;
        for (size_t k = 0; k < 70; k++) {
            while (t < 60 * k) {
                check_frame(base + t, "wrap");
                t += 30;
            }
            fake_now = base + t;
            press(k % 2 ? thumb : mid);
        }
        for (; t <= 60 * 69 + HEATMAP_MS + 100; t += 30) {
            check_frame(base + t, "wrap on");
        }
        CHECK(live_hits() == 0, "wrap: %d hits after", live_hits());
        base += 20000;
    }

    /* a stage move clears the heat: the live hits warm the keys again, each
     * less its age's cooling; one the ring had dropped stays cold */
    {
        model_reset(base);
        fake_now = base;
        press(mid);
        frame(base + 300, NULL, NULL);
        fake_now = base + 500;
        CHECK(compose(2) && !black(pixels[led_of(mid)]), "stage: dark after the move");
        led_strip_update_rgb(strip, pixels, LED_COUNT);
        model_to(base + 500);
        double r, g, b;
        expected(mh[led_of(mid)], &r, &g, &b);
        CHECK(fabs(strip_out[led_of(mid)].b / 255.0 - b) <= 0.02 && fabs(strip_out[led_of(mid)].r / 255.0 - r) <= 0.02,
              "stage: %u,%u,%u, model %.0f,%.0f,%.0f", strip_out[led_of(mid)].r, strip_out[led_of(mid)].g,
              strip_out[led_of(mid)].b, r * 255, g * 255, b * 255);
        fake_now = base + 600;
        CHECK(compose(0), "stage: not moving back");
        check_frame(base + 700, "stage back");
        frame(base + HEATMAP_MS + 100, NULL, NULL);
        /* dropped: four presses of the thumb, then sixteen of mid 10 ms
         * apart, the thumb's out of the ring, then a stage move */
        model_reset(base + 10000);
        fake_now = base + 10000;
        for (size_t k = 0; k < 4; k++) {
            press(thumb);
        }
        frame(base + 10000, NULL, NULL);
        for (size_t k = 0; k < 16; k++) {
            fake_now = base + 10010 + 10 * (uint32_t)k;
            press(mid);
            frame(fake_now, NULL, NULL);
        }
        CHECK(!black(strip_out[led_of(thumb)]) && !black(strip_out[led_of(mid)]), "dropped: before the move");
        fake_now = base + 10300;
        compose(4); /* 4 mod 2 shades: the picture stays black */
        led_strip_update_rgb(strip, pixels, LED_COUNT);
        CHECK(black(strip_out[led_of(thumb)]) && !black(strip_out[led_of(mid)]),
              "dropped: after the move the thumb is %s and mid %s", black(strip_out[led_of(thumb)]) ? "dark" : "lit",
              black(strip_out[led_of(mid)]) ? "dark" : "lit");
        fake_now = base + 10310;
        compose(0);
        frame(base + 10300 + HEATMAP_MS + 300, NULL, NULL);
        CHECK(live_hits() == 0, "dropped: %d hits after", live_hits());
        base += 30000;
    }

    /* the cap: sixteen keys (the ring's HITS) pressed at once without a
     * palette, white, each warming its neighbours too, over CAP: held to it;
     * and HITS + 1 at once count HITS, the ring's drop (the first press's
     * key keeps only its neighbours' warmth) */
    {
        palette_none();
        model_reset(base);
        fake_now = base;
        for (size_t i = 0; i < HITS; i++) {
            press(positions[i]);
        }
        uint32_t s;
        size_t lit_n = frame(base, NULL, &s);
        CHECK(lit_n >= HITS, "cap: %zu lit", lit_n);
        CHECK(s <= CAP && s > CAP - 3 * (uint32_t)lit_n, "cap: sum %u, CAP %u", s, (unsigned)CAP);
        frame(base + HEATMAP_MS + 10, NULL, NULL);
        model_reset(base + 10000);
        fake_now = base + 10000;
        for (size_t i = 0; i < HITS + 1; i++) {
            press(positions[i]);
        }
        /* the model without the first press, which the ring dropped */
        for (size_t k = 1; k < mp_n; k++) {
            mp[k - 1] = mp[k];
        }
        mp_n--;
        check_frame(base + 10000, "ring drop");
        CHECK(heat[0] < heat[1], "ring: LED 0 holds %u, LED 1 %u", heat[0], heat[1]);
        frame(base + 10000 + HEATMAP_MS + 10, NULL, NULL);
        palette_two();
        pick(true, LANTERN_EFFECT_HEATMAP);
        base += 20000;
    }

    /* with the crossing ring: the ring spreads from the key, the key's heat
     * over it; moving past the ring's life */
    {
        pick(true, LANTERN_EFFECT_HEATMAP | LANTERN_EFFECT_CROSS_RING);
        model_reset(base);
        fake_now = base;
        press(mid);
        bool m;
        size_t both = frame(base + 100, &m, NULL);
        CHECK(both > 1 && m, "both: %zu lit at 100 ms, moving %d", both, m);
        frame(base + 1900, &m, NULL);
        CHECK(m, "both: not moving at 1900 ms");
        frame(base + HEATMAP_MS, &m, NULL);
        CHECK(!m, "both: moving at the life");
        pick(true, LANTERN_EFFECT_HEATMAP);
        base += 10000;
    }

    /* the pick changes mid-cooling: the hits go, the heat with them */
    {
        model_reset(base);
        fake_now = base;
        press(mid);
        size_t before = frame(base + 100, NULL, NULL);
        CHECK(before > 1, "repick: %zu lit before", before);
        pick(true, LANTERN_EFFECT_DISC);
        CHECK(live_hits() == 0, "repick: %d hits left", live_hits());
        bool mv;
        size_t after = frame(base + 130, &mv, NULL);
        CHECK(after == 0 && !mv, "repick: lit %zu moving %d", after, mv);
        pick(true, LANTERN_EFFECT_HEATMAP);
        after = frame(base + 160, &mv, NULL);
        CHECK(after == 0 && !mv, "repick back: lit %zu moving %d", after, mv);
        base += 10000;
    }

    /* the blank clears the hits and the heat */
    {
        model_reset(base);
        fake_now = base;
        press(mid);
        frame(base + 100, NULL, NULL);
        lantern_keys_blank();
        paint(NULL);
        CHECK(live_hits() == 0, "blank: %d hits", live_hits());
        size_t dark = 0;
        for (size_t i = 0; i < LED_COUNT; i++) {
            dark += black(strip_out[i]);
        }
        CHECK(dark == LED_COUNT, "blank: %zu dark", dark);
        lantern_keys_light();
        bool mv;
        size_t lit_n = frame(base + 200, &mv, NULL);
        CHECK(lit_n == 0 && !mv, "relit: %zu lit, moving %d", lit_n, mv);
        base += 10000;
    }

    /* the engine's 18-bit time: a press live at 1000 ms, spent at the frame
     * at its life, read again 262 s on draws nothing and moves nothing */
    {
        model_reset(base);
        fake_now = base;
        press(mid);
        size_t lit_n = frame(base + 500, NULL, NULL);
        CHECK(lit_n >= 1 && live_hits() == 1, "wrap18: %zu lit, %d hits at 500 ms", lit_n, live_hits());
        bool mv;
        lit_n = frame(base + HEATMAP_MS, &mv, NULL);
        CHECK(lit_n == 0 && live_hits() == 0 && !mv, "wrap18: %zu lit, %d hits, moving %d at the life", lit_n,
              live_hits(), mv);
        size_t phantom = frame(base + HIT_TIME + 1 + 300, &mv, NULL);
        CHECK(phantom == 0 && live_hits() == 0 && !mv, "wrap18: %zu lit, %d hits, moving %d after HIT_TIME",
              phantom, live_hits(), mv);
        base += 10000;
    }

    /* the delay between the engine's clock and the heatmap's (a preempted
     * frame's thread), 40 ms at every frame: the heat follows the model all
     * the same (the press's heat less the engine's age, the cooling by the
     * heatmap's own clock, which runs level), and the frame at the life
     * finds no hit: black and still; then every frame late but the last */
    {
        model_reset(base);
        fake_now = base;
        press(mid);
        for (uint32_t t = 10; t < HEATMAP_MS; t += 10) {
            uint32_t sum;
            frame_skew(base + t, 40, NULL, &sum);
            model_to(base + t);
            double r, g, b;
            expected(mh[led_of(mid)], &r, &g, &b);
            struct led_rgb px = strip_out[led_of(mid)];
            CHECK(fabs(px.b / 255.0 - b) <= 0.02 && fabs(px.r / 255.0 - r) <= 0.02,
                  "skew at %u: %u,%u,%u, model %.0f,%.0f,%.0f", t, px.r, px.g, px.b, r * 255, g * 255, b * 255);
        }
        bool m;
        size_t lit_n = frame_skew(base + HEATMAP_MS, 40, &m, NULL);
        CHECK(lit_n == 0 && !m && live_hits() == 0, "skew end: %zu lit, moving %d, %d hits", lit_n, m,
              live_hits());
        base += 10000;
        model_reset(base);
        fake_now = base;
        press(mid);
        for (uint32_t t = 10; t < HEATMAP_MS; t += 10) {
            frame_skew(base + t, 40, NULL, NULL);
        }
        lit_n = frame(base + HEATMAP_MS, &m, NULL);
        CHECK(lit_n == 0 && !m, "skew all: %zu lit, moving %d at the life", lit_n, m);
        base += 10000;
    }

    /* the pick changed and back before a frame: the hits went, the entries'
     * state did not, and the frame finds no hit: black and still */
    {
        model_reset(base);
        fake_now = base;
        press(mid);
        frame(base + 100, NULL, NULL);
        pick(true, LANTERN_EFFECT_DISC);
        pick(true, LANTERN_EFFECT_HEATMAP);
        bool m;
        size_t lit_n = frame(base + 130, &m, NULL);
        CHECK(lit_n == 0 && !m, "repick and back: %zu lit, moving %d", lit_n, m);
        base += 10000;
    }

    /* a blank and a light coalesced into one frame: the hits went, and the
     * frame finds no hit */
    {
        model_reset(base);
        fake_now = base;
        press(mid);
        frame(base + 100, NULL, NULL);
        lantern_keys_blank();
        lantern_keys_light();
        bool m;
        size_t lit_n = frame(base + 130, &m, NULL);
        CHECK(lit_n == 0 && !m, "blank+light: %zu lit, moving %d", lit_n, m);
        base += 10000;
    }

    /* the pick's colour: every warm key in it, at its heat's brightness */
    {
        fake_colour = 0x00FF00;
        pick(true, LANTERN_EFFECT_HEATMAP);
        palette = PAL_PICK;
        model_reset(base);
        fake_now = base;
        press(mid);
        fake_now = base + 100;
        press(thumb);
        for (uint32_t t = 200; t <= 1000; t += 100) {
            check_frame(base + t, "colour");
        }
        for (size_t i = 0; i < LED_COUNT; i++) {
            struct led_rgb px = strip_out[i];
            CHECK(px.r == 0 && px.b == 0, "colour: LED %zu is %u,%u,%u", i, px.r, px.g, px.b);
        }
        frame(base + HEATMAP_MS + 200, NULL, NULL);
        fake_colour = 0;
        palette_two();
        pick(true, LANTERN_EFFECT_HEATMAP);
        base += 10000;
    }

    /* one shade: the heat's brightness in it */
    {
        palette_one();
        model_reset(base);
        fake_now = base;
        press(mid);
        fake_now = base + 100;
        press(mid);
        for (uint32_t t = 110; t <= 2000; t += 50) {
            check_frame(base + t, "one shade");
        }
        frame(base + HEATMAP_MS + 200, NULL, NULL);
        palette_two();
        base += 10000;
    }

#if IS_ENABLED(CONFIG_LANTERN_LAYER_KEYS)
    /* the layer keys draw over the heatmap (the table's last line): a layer
     * lighting a hot key in slot 1 shows slot 1 over its red */
    {
        CHECK(LANTERN_EFFECT_LAYER_KEYS == BIT(8) && (EFFECTS_BUILT & BIT(8)) != 0, "layer keys built");
        pick(true, LANTERN_EFFECT_HEATMAP | LANTERN_EFFECT_LAYER_KEYS);
        model_reset(base);
        for (size_t k = 0; k < 8; k++) {
            fake_now = base + 50 * (uint32_t)k;
            press(mid);
        }
        frame(base + 400, NULL, NULL);
        struct led_rgb px = strip_out[led_of(mid)];
        CHECK(px.r == 255 && px.b < 40, "layer: the key is %u,%u,%u before the layer, expected red", px.r, px.g,
              px.b);
        uint32_t p1 = mid < 32 ? 1u << mid : 0, p2 = mid < 32 ? 0 : 1u << (mid - 32);
        p2 |= 1u << LANTERN_LAYER_SLOT_SHIFT;
        lantern_layer_keys_set(0, true, p1, p2);
        frame(base + 410, NULL, NULL);
        px = strip_out[led_of(mid)];
        CHECK(px.r == 0 && px.b == 255, "layer over heatmap: the key is %u,%u,%u, expected blue", px.r, px.g,
              px.b);
        lantern_layer_keys_clear();
        frame(base + 420, NULL, NULL);
        px = strip_out[led_of(mid)];
        CHECK(px.b < 40 && px.r == 255, "layer gone: the key is %u,%u,%u, expected red", px.r, px.g, px.b);
        frame(base + 400 + HEATMAP_MS + 10, NULL, NULL);
        pick(true, LANTERN_EFFECT_HEATMAP);
        base += 10000;
    }
#endif

    /* two presses noted while the frame's thread is inside hits_read(),
     * after it took slot 0's value (the oldest, hit_next 0) and before slot
     * 1's: the newer lands in slot 1 and is seen this frame, the older in
     * slot 0 and only next frame, out of number order; both count (Opus
     * review 2026-11-02, P1: the first version's watermark lost the older) */
    {
        model_reset(base);
        while ((size_t)atomic_get(&hit_next) != 0) {
            fake_now = base;
            relayed(other);
            frame(base, NULL, NULL);
        }
        for (size_t k = 0; k < HITS; k++) {
            fake_now = base + 10 * (uint32_t)k;
            press(mid);
            frame(fake_now, NULL, NULL);
        }
        CHECK((size_t)atomic_get(&hit_next) == 0, "race: hit_next %ld", (long)atomic_get(&hit_next));
        uint32_t T = base + 200;
        race_v = thumb;
        race_w = mid;
        fake_now = T;
        clock_hook = race_hook;
        clock_hook_at = 1;
        frame(T, NULL, NULL);
        CHECK(clock_hook == NULL, "race: the hook did not fire");
        for (uint32_t t = 30; t <= 600; t += 30) {
            check_frame(T + t, "race");
        }
        model_to(T + 600);
        CHECK(mh[led_of(thumb)] > 0.03, "race: the model has no heat on the thumb (%.3f)", mh[led_of(thumb)]);
        frame(T + HEATMAP_MS + 300, NULL, NULL);
        CHECK(live_hits() == 0, "race: %d hits after", live_hits());
        base += 20000;
    }

    /* the 32-bit clock wraps under the heat: the cooling runs on */
    {
        uint32_t b = 0xFFFFFF00u;
        model_reset(b);
        fake_now = b;
        press(mid);
        fake_now = b + 100;
        press(mid);
        check_frame(b + 240, "clock wrap before");
        check_frame(b + 512, "clock wrap after");
        check_frame(b + 1500, "clock wrap late");
        frame(b + 100 + HEATMAP_MS + 10, NULL, NULL);
        CHECK(live_hits() == 0, "clock wrap: %d hits after", live_hits());
    }

    printf("heatmap: built %08x, %d submits, worst channel %.4f, worst heat %.5f, fails %d\n",
           (unsigned)EFFECTS_BUILT, submits, worst, worst_heat, fails);
    return fails != 0;
}
