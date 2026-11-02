/* Runs the real src/keys.c (included below) through the water on the host
 * (canon t-6ttk): the sine table against libm, span() at its ends, one press
 * against the design's formula in floating point (a two-colour palette, from
 * which the surface's height and the colour's place decode exactly), the
 * front, the crests' and troughs' colours, two presses adding and
 * cancelling, a press of the other half relayed in, one shade, the cap, the
 * water with the crossing ring, the pick's change and the blank. */
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
static unsigned int maxch(struct led_rgb p) { return MAX(p.r, MAX(p.g, p.b)); }

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

static void palette_two(void) {
    fake_have = true;
    fake_palette[0] = 0xFF0000;
    fake_palette[1] = 0x0000FF;
    for (size_t s = 2; s < 6; s++) {
        fake_palette[s] = 0;
    }
}

static void palette_five(void) {
    fake_have = true;
    fake_palette[0] = 0xFF0000;
    fake_palette[1] = 0x00FF00;
    fake_palette[2] = 0x0000FF;
    fake_palette[3] = 0xFFFF00;
    fake_palette[4] = 0x00FFFF;
    fake_palette[5] = 0;
}

/* The design's formula (canon t-6ttk's research, 2.1) with the code's
 * numbers, for LED i under hits at keys[] pressed ages[] ms ago: w = sum
 * over the hits whose front has passed of sin(2 pi (v t - d) / lambda)
 * (1 - t / T) / (1 + f d), t in s, d in key units, v WATER_SPEED, lambda
 * WATER_WAVE / 100 (the design's 2.0 was a standing wave on canon's grid:
 * the code says), T WATER_MS / 1000, f WATER_FALL / 10; *reached says
 * whether any has. */
static double model(size_t i, const uint32_t keys[], const uint32_t ages[], size_t n,
                    bool *reached) {
    double w = 0;
    *reached = false;
    for (size_t h = 0; h < n; h++) {
        double t = ages[h] / 1000.0;
        if (t >= WATER_MS / 1000.0) {
            continue;
        }
        double dx = ((int)coords[2 * positions[i]] - (int)coords[2 * keys[h]]) / 100.0;
        double dy = ((int)coords[2 * positions[i] + 1] - (int)coords[2 * keys[h] + 1]) / 100.0;
        double d = sqrt(dx * dx + dy * dy);
        double r = WATER_SPEED * t;
        if (d > r) {
            continue;
        }
        *reached = true;
        w += sin(2 * M_PI * (r - d) / (WATER_WAVE / 100.0)) * (1 - t / (WATER_MS / 1000.0)) /
             (1 + WATER_FALL / 10.0 * d);
    }
    return w;
}

/* The height an LED shows under the two-colour palette (red at shade 0, blue
 * at the last): its size from the brightest channel (the colour is at full
 * scale before the pull), its sign from the blue's share of the colour (a
 * crest pulls toward red, a trough toward blue). */
static double decode(struct led_rgb px) {
    if (black(px)) {
        return 0;
    }
    double size = maxch(px) / 255.0;
    double place = (double)px.b / ((double)px.r + px.b);
    return place > 0.5 ? -size : size;
}

int main(void) {
#ifdef HALF_RIGHT
    const uint32_t mid = 20, thumb = 50, far_in = 17, left_k = 18, right_k = 22;
#else
    const uint32_t mid = 14, thumb = 47, far_in = 18, left_k = 12, right_k = 16;
#endif
    uint32_t base = 5000;

    CHECK((EFFECTS_BUILT & LANTERN_EFFECT_WATER) != 0, "built %08x lacks the water",
          (unsigned)EFFECTS_BUILT);
    CHECK(LANTERN_EFFECT_COUNT == 9, "count %d", (int)LANTERN_EFFECT_COUNT);
    CHECK(LANTERN_EFFECT_WATER == BIT(5), "bit %08x", (unsigned)LANTERN_EFFECT_WATER);
    CHECK(EFFECT_DEFAULT == LANTERN_EFFECT_CROSS_RING, "default %08x", (unsigned)EFFECT_DEFAULT);
    CHECK(HIT_LIFE_MS == DISC_MS && HIT_LIFE_MS == 1900, "hit life %u", (unsigned)HIT_LIFE_MS);
    CHECK(WATER_MS == 1500 && WATER_SPEED == 12 && WATER_WAVE == 300 && WATER_FALL == 3,
          "the water's constants are not the chosen ones (the design's, three keys a wave)");
    CHECK(led_of(mid) >= 0 && led_of(thumb) >= 0 && led_of(far_in) < 0 && led_of(left_k) >= 0 &&
              led_of(right_k) >= 0,
          "the harness's keys are on the wrong half");

    /* the sine: the table folded, against libm, within the table's rounding */
    for (uint32_t i = 0; i < 256; i++) {
        int want = (int)lround(127 * sin(2 * M_PI * i / 256));
        CHECK(abs((int)sine(i) - want) <= 1, "sine(%u) = %d, want %d", i, (int)sine(i), want);
    }
    CHECK(sine(0) == 0 && sine(64) == 127 && sine(128) == 0 && sine(192) == -127,
          "the sine's quarters");

    /* span(): shade 0 at 0, the last at 256, every place at full scale */
    palette_five();
    {
        uint32_t pal[6];
        bool have = lantern_palette_get(pal);
        struct shades sh;
        shades(pal, have, &sh);
        CHECK(sh.n == 5, "five shades, not %zu", sh.n);
        struct led_rgb a = span(&sh, 0), z = span(&sh, 256), mid_c = span(&sh, 128);
        CHECK(a.r == 255 && a.g == 0 && a.b == 0, "span(0) %u,%u,%u", a.r, a.g, a.b);
        CHECK(z.r == 0 && z.g == 255 && z.b == 255, "span(256) %u,%u,%u", z.r, z.g, z.b);
        CHECK(mid_c.r == 0 && mid_c.g == 0 && mid_c.b == 255, "span(128) %u,%u,%u", mid_c.r,
              mid_c.g, mid_c.b);
        for (unsigned int p = 0; p <= 256; p++) {
            CHECK(maxch(span(&sh, p)) == 255, "span(%u) not at full scale", p);
        }
        palette_two();
        have = lantern_palette_get(pal);
        shades(pal, have, &sh);
        CHECK(sh.n == 2, "two shades, not %zu", sh.n);
        for (unsigned int p = 0; p <= 256; p++) {
            struct led_rgb c = span(&sh, p);
            CHECK(c.g == 0 && maxch(c) == 255, "two: span(%u) %u,%u,%u", p, c.r, c.g, c.b);
        }
    }

    lantern_keys_light();
    frame(base, NULL, NULL);
    base += 100;
    pick(true, LANTERN_EFFECT_WATER);

    /* one press, the two-colour palette: every LED every 10 ms against the
     * design's formula; nothing ahead of the front; the pressed key in the
     * first frame; moving until the life */
    {
        uint32_t keys[1] = {mid}, ages[1];
        uint32_t first[LED_COUNT];
        uint32_t sum_max = 0;
        bool moving = true;
        double worst = 0;

        for (size_t i = 0; i < LED_COUNT; i++) {
            first[i] = UINT32_MAX;
        }
        fake_now = base;
        press(mid);
        for (uint32_t t = 10; t <= WATER_MS + 200; t += 10) {
            uint32_t s;
            bool m;
            frame(base + t, &m, &s);
            sum_max = MAX(sum_max, s);
            ages[0] = t;
            for (size_t i = 0; i < LED_COUNT; i++) {
                bool reached;
                double w = model(i, keys, ages, 1, &reached);
                double got = decode(strip_out[i]);
                struct led_rgb px = strip_out[i];

                if (!black(px) && first[i] == UINT32_MAX) {
                    first[i] = t;
                }
                CHECK(px.g == 0, "LED %zu at %u ms: green %u under a red and blue palette", i, t,
                      px.g);
                if (!reached) {
                    CHECK(maxch(px) <= 2, "LED %zu at %u ms: %u ahead of the front", i, t,
                          maxch(px));
                    continue;
                }
                if (t >= WATER_MS) {
                    CHECK(black(px), "LED %zu at %u ms: lit past the water's life", i, t);
                    continue;
                }
                double want = w > 1 ? 1 : w < -1 ? -1 : w;
                double err = fabs(got - want);
                /* a height within 0.05 either way is a few of 255 a channel,
                 * where the red and the blue round alike and the sign is lost */
                if (fabs(got) <= 0.05 && fabs(want) <= 0.05) {
                    err = fabs(fabs(got) - fabs(want));
                }
                double tol = positions[i] == mid ? 0.05 : 0.06;
                worst = MAX(worst, err);
                CHECK(err <= tol, "LED %zu (key %u) at %u ms: height %.3f, the design's %.3f", i,
                      positions[i], t, got, want);
            }
            if (t == WATER_MS) {
                CHECK(!m, "still moving at the water's life");
            } else if (t == WATER_MS - 10) {
                CHECK(m, "not moving at %u ms", t);
            }
            moving = m;
        }
        CHECK(!moving, "moving after the life");
        CHECK(live_hits() == 0, "%d hits after the water's life", live_hits());
        CHECK(first[led_of(mid)] == 10, "the pressed key first lit at %u ms", first[led_of(mid)]);
        for (size_t i = 0; i < LED_COUNT; i++) {
            int dx = (int)coords[2 * positions[i]] - (int)coords[2 * mid];
            int dy = (int)coords[2 * positions[i] + 1] - (int)coords[2 * mid + 1];
            uint32_t arrive =
                (uint32_t)(sqrt((double)dx * dx + (double)dy * dy) * 10 / WATER_SPEED);
            CHECK(first[i] != UINT32_MAX, "LED %zu (key %u) never lit", i, positions[i]);
            CHECK(first[i] + 10 >= arrive, "LED %zu (key %u) lit at %u ms, the front at %u", i,
                  positions[i], first[i], arrive);
        }
        CHECK(sum_max <= CAP, "sum %u > CAP %u", sum_max, (unsigned)CAP);
        CHECK(dyed() == 0, "%d dyed", dyed());
        printf("one press: worst height error %.3f\n", worst);
        base += 10000;
    }

    /* the crests' and troughs' colours, the five-colour palette (red, green,
     * blue, yellow, cyan): at the pressed key the first crest (a quarter
     * wavelength behind the front, 62 ms, the fade at 0.96) is red with a
     * touch of the next shade, the first trough (187 ms, 0.88) cyan toward
     * yellow, between them (125 ms) the surface is level and dark, and the
     * second crest (312 ms, 0.79) has slid toward green: the design's G of
     * the height */
    {
        palette_five();
        fake_now = base;
        press(mid);
        int i = led_of(mid);
        frame(base + 60, NULL, NULL);
        struct led_rgb c = strip_out[i];
        CHECK(c.r >= 240 && c.g >= 5 && c.g <= 40 && c.b == 0, "crest at 60 ms: %u,%u,%u, not red",
              c.r, c.g, c.b);
        frame(base + 125, NULL, NULL);
        c = strip_out[i];
        CHECK(maxch(c) <= 12, "level at 125 ms: %u,%u,%u, not dark", c.r, c.g, c.b);
        frame(base + 190, NULL, NULL);
        c = strip_out[i];
        CHECK(c.r >= 40 && c.r <= 80 && c.g >= 200 && c.b >= 140 && c.b < c.g,
              "trough at 190 ms: %u,%u,%u, not cyan toward yellow", c.r, c.g, c.b);
        /* the second crest, a wavelength on, is lower: the fade */
        frame(base + 310, NULL, NULL);
        struct led_rgb c2 = strip_out[i];
        CHECK(c2.r >= 180 && c2.g >= 100 && c2.g < c2.r && c2.b == 0,
              "second crest at 310 ms: %u,%u,%u, not a fainter red toward green", c2.r, c2.g,
              c2.b);
        pick(true, LANTERN_EFFECT_WATER);
        base += 10000;
    }

    /* a full trough with six shades: two presses of one key at once sum past
     * a crest and are clamped, and span(256) is the sixth shade itself */
    {
        fake_have = true;
        fake_palette[0] = 0xFF0000;
        fake_palette[1] = 0x00FF00;
        fake_palette[2] = 0x0000FF;
        fake_palette[3] = 0xFFFF00;
        fake_palette[4] = 0x00FFFF;
        fake_palette[5] = 0xFF00FF;
        fake_now = base;
        press(mid);
        press(mid);
        int i = led_of(mid);
        frame(base + 190, NULL, NULL);
        struct led_rgb c = strip_out[i];
        CHECK(c.r == 255 && c.g == 0 && c.b == 255, "full trough, six shades: %u,%u,%u, not the "
                                                    "sixth at full scale",
              c.r, c.g, c.b);
        frame(base + 60, NULL, NULL);
        c = strip_out[i];
        CHECK(c.r == 255 && c.g == 0 && c.b == 0, "full crest, six shades: %u,%u,%u, not the "
                                                  "first at full scale",
              c.r, c.g, c.b);
        pick(true, LANTERN_EFFECT_WATER);
        base += 10000;
    }

    /* two presses two units apart at once: the key between them, a unit from
     * each, rises twice as high as under one press, up to a full crest */
    {
        palette_two();
        int between = led_of(mid);
        unsigned int one[60], both[60];

        fake_now = base;
        press(left_k);
        for (uint32_t t = 0; t < 60; t++) {
            frame(base + 10 * t, NULL, NULL);
            one[t] = maxch(strip_out[between]);
        }
        pick(true, LANTERN_EFFECT_WATER);
        base += 10000;
        fake_now = base;
        press(left_k);
        press(right_k);
        for (uint32_t t = 0; t < 60; t++) {
            frame(base + 10 * t, NULL, NULL);
            both[t] = maxch(strip_out[between]);
        }
        size_t doubled = 0;
        for (uint32_t t = 0; t < 60; t++) {
            unsigned int want = MIN(2 * one[t], 255u);
            CHECK(both[t] + 6 >= want && both[t] <= want + 6,
                  "two presses at %u ms: %u between them, one press %u, expected about %u",
                  10 * t, both[t], one[t], want);
            doubled += one[t] > 20;
        }
        CHECK(doubled > 20, "the key between lit in only %zu of 60 frames under one press",
              doubled);
        pick(true, LANTERN_EFFECT_WATER);
        base += 10000;
    }

    /* the second press half a period (125 ms) after the first: a crest meets
     * a trough on the key between and it stays nearly still */
    {
        int between = led_of(mid);
        unsigned int most = 0;

        fake_now = base;
        press(left_k);
        fake_now = base + 125;
        press(right_k);
        for (uint32_t t = 300; t <= 1000; t += 10) {
            frame(base + t, NULL, NULL);
            most = MAX(most, maxch(strip_out[between]));
        }
        CHECK(most <= 20, "a crest meeting a trough: %u of 255 between, expected nearly dark",
              most);
        pick(true, LANTERN_EFFECT_WATER);
        base += 10000;
    }

    /* the other half's inner column, relayed: its waves reach this half's
     * inner column 6.5 units on, at 542 ms, the first crest 62 ms later at
     * about a fifth of a crest */
    {
        int inner = -1;
        uint32_t nearest = UINT32_MAX;
        for (size_t i = 0; i < LED_COUNT; i++) {
            /* on the pressed key's row (the thumbs sit nearer, off it) */
            if (coords[2 * positions[i] + 1] != coords[2 * far_in + 1]) {
                continue;
            }
            uint32_t d = distance(positions[i], (uint8_t)far_in);
            if (d < nearest) {
                nearest = d;
                inner = (int)i;
            }
        }
        CHECK(nearest == 650, "the inner columns are %u apart, not 650", nearest);
        fake_now = base;
        lantern_keys_hit((uint8_t)far_in);
        unsigned int peak = 0, early = 0;
        uint32_t at = 0;
        for (uint32_t t = 0; t <= WATER_MS; t += 10) {
            frame(base + t, NULL, NULL);
            unsigned int ch = maxch(strip_out[inner]);
            if (t < 540) {
                early = MAX(early, ch);
            }
            if (ch > peak) {
                peak = ch;
                at = t;
            }
        }
        CHECK(early == 0, "relayed: %u on the inner column before the front", early);
        CHECK(peak >= 40 && peak <= 65, "relayed inner column: peak %u of 255 at %u ms, expected "
                                        "about 53",
              peak, at);
        CHECK(at >= 590 && at <= 620, "relayed: the first crest at %u ms, expected about 604", at);
        pick(true, LANTERN_EFFECT_WATER);
        base += 10000;
    }

    /* one shade: crest and trough alike, every lit LED that hue */
    {
        fake_have = true;
        fake_palette[0] = 0x00FF00;
        for (size_t s = 1; s < 6; s++) {
            fake_palette[s] = 0;
        }
        fake_now = base;
        press(mid);
        size_t lit_n = 0;
        for (uint32_t t = 0; t <= 400; t += 10) {
            lit_n += frame(base + t, NULL, NULL);
            for (size_t i = 0; i < LED_COUNT; i++) {
                CHECK(strip_out[i].r == 0 && strip_out[i].b == 0,
                      "one shade at %u ms: LED %zu %u,%u,%u", t, i, strip_out[i].r,
                      strip_out[i].g, strip_out[i].b);
            }
        }
        CHECK(lit_n > 100, "one shade: %zu LED-frames lit", lit_n);
        pick(true, LANTERN_EFFECT_WATER);
        base += 10000;
    }

    /* sixteen presses 40 ms apart, the ring full: every hit live, every LED's
     * height against the design with all sixteen (water() called on black
     * pixels, under the cap, so a draw that read fewer hits would show), and
     * the cap holds through the life */
    {
        palette_two();
        for (size_t i = 0; i < 16; i++) {
            fake_now = base + 40 * i;
            press(positions[i]);
        }
        CHECK(live_hits() == HITS && HITS == 16, "%d hits noted of 16", live_hits());
        for (uint32_t t = 700; t <= 1000; t += 50) {
            struct hit live[HITS];
            uint32_t keys[HITS], ages[HITS], pal[6];
            fake_now = base + t;
            size_t n = hits_read(live, WATER_MS);
            CHECK(n == 16, "sixteen at %u ms: %zu live", t, n);
            for (size_t h = 0; h < n; h++) {
                keys[h] = live[h].key;
                ages[h] = live[h].age;
            }
            shades(pal, lantern_palette_get(pal), &frame_shades);
            memset(pixels, 0, sizeof(pixels));
            water_draw(pixels, &frame_shades, live, n, 0);
            for (size_t i = 0; i < LED_COUNT; i++) {
                bool reached;
                double w = model(i, keys, ages, n, &reached);
                double want = w > 1 ? 1 : w < -1 ? -1 : w;
                double got = decode(pixels[i]);
                double err = fabs(got - want);
                if (fabs(got) <= 0.05 && fabs(want) <= 0.05) {
                    err = fabs(fabs(got) - fabs(want));
                }
                CHECK(err <= 0.13, "sixteen at %u ms: LED %zu height %.3f, the design's %.3f", t,
                      i, got, want);
            }
        }
        uint32_t s, sum_max = 0;
        for (uint32_t t = 0; t <= WATER_MS + 600; t += 10) {
            frame(base + t, NULL, &s);
            sum_max = MAX(sum_max, s);
        }
        CHECK(sum_max <= CAP, "sixteen: sum %u, CAP %u", sum_max, (unsigned)CAP);
        CHECK(live_hits() == 0, "sixteen: %d hits after the life", live_hits());
        pick(true, LANTERN_EFFECT_WATER);
        base += 10000;
    }

    /* the cap: eight keys pressed at once without a palette, white at full
     * scale at their first crest, more than CAP allows */
    {
        fake_have = false;
        fake_now = base;
        for (size_t i = 0; i < 8; i++) {
            press(positions[i]);
        }
        uint32_t s;
        size_t lit_n = frame(base + 60, NULL, &s);
        CHECK(lit_n >= 8, "cap: %zu lit, expected 8 or more", lit_n);
        CHECK(s <= CAP, "cap: sum %u, CAP %u", s, (unsigned)CAP);
        uint32_t sum_max = 0;
        for (uint32_t t = 0; t <= WATER_MS; t += 10) {
            frame(base + t, NULL, &s);
            sum_max = MAX(sum_max, s);
        }
        CHECK(sum_max <= CAP, "cap: sum %u over the life, CAP %u", sum_max, (unsigned)CAP);
        palette_two();
        pick(true, LANTERN_EFFECT_WATER);
        base += 10000;
    }

    /* the water with the crossing ring: the frame moving until CROSS_MS, past
     * the water's life */
    {
        pick(true, LANTERN_EFFECT_WATER | LANTERN_EFFECT_CROSS_RING);
        fake_now = base;
        press(mid);
        bool m;
        size_t both = frame(base + 100, &m, NULL);
        CHECK(both > 3 && m, "both: %zu lit at 100 ms, moving %d", both, m);
        frame(base + CROSS_MS - 50, &m, NULL);
        CHECK(m, "both: not moving at %u ms", (unsigned)CROSS_MS - 50);
        frame(base + CROSS_MS, &m, NULL);
        CHECK(!m, "both: moving at CROSS_MS");
        pick(true, LANTERN_EFFECT_WATER);
        base += 10000;
    }

    /* the pick changes mid-wave: the hits go now */
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

    /* the 18-bit time's wrap: a hit gone at the life draws nothing 262 s on */
    pick(true, LANTERN_EFFECT_WATER);
    fake_now = base;
    press(mid);
    frame(base + 1490, NULL, NULL);
    CHECK(live_hits() == 1, "%d hits at 1490 ms", live_hits());
    frame(base + WATER_MS, NULL, NULL);
    CHECK(live_hits() == 0, "%d hits at the life", live_hits());
    fake_now = base;
    press(mid);
    frame(base + 1520, NULL, NULL);
    size_t phantom = frame(base + 1520 + HIT_TIME + 1 + 300, NULL, NULL);
    CHECK(phantom == 0 && live_hits() == 0, "wrap: %zu lit, %d hits", phantom, live_hits());
    base += 10000;

    /* the blank clears the hits */
    fake_now = base;
    press(mid);
    frame(base + 100, NULL, NULL);
    lantern_keys_blank();
    paint(NULL);
    CHECK(live_hits() == 0, "blank: %d hits", live_hits());
    size_t dark = frame(base + 200, NULL, NULL);
    CHECK(dark == 0 || !atomic_get(&lit), "blank: %zu lit", dark);

    printf("water: built %08x, %d submits, fails %d\n", (unsigned)EFFECTS_BUILT, submits, fails);
    return fails != 0;
}
