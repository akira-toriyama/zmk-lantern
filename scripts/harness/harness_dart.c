/* Runs the real src/keys.c (included below) through the dart on the host
 * (canon t-6ttk): the board's ends, one press of each half against the
 * design's formula in floating point (a two-colour palette, from which each
 * LED's strength and the colour's place along the shades decode exactly),
 * the direction, the rows, a thumb, a press of the other half relayed in,
 * two presses overlapping, one shade, the cap, the dart with the crossing
 * ring, the pick's change and the blank. */
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

/* Red to blue: span(p) is then r 255 (1 - p) : b 255 p brought to full
 * scale, and a pixel's b / (r + b) is p whatever its strength. */
static void palette_two(void) {
    fake_have = true;
    fake_palette[0] = 0xFF0000;
    fake_palette[1] = 0x0000FF;
    for (size_t s = 2; s < 6; s++) {
        fake_palette[s] = 0;
    }
}

static void palette_one(void) {
    fake_have = true;
    fake_palette[0] = 0x00FF00;
    for (size_t s = 1; s < 6; s++) {
        fake_palette[s] = 0;
    }
}

/* canon's board: the keys' x from 0 to 1650. */
#define BOARD_LEFT 0.0
#define BOARD_RIGHT 1650.0

/* The design's formula (canon t-6ttk's research, 2.2, "light that runs at a
 * press") with the code's numbers, for LED i under one hit at key k aged t
 * ms: the head x_k +- v t (right from a key left of the middle), I = tri((x_i
 * - head) / width) x (1 on the row, 0.4 off it) x (1 - t / T), and the
 * colour's place along the shades min(1, run / the board's width). */
static void model(uint32_t k, size_t i, uint32_t t, double *strength, double *place) {
    double run = (double)DART_SPEED * t / 10.0; /* hundredths */
    double xk = coords[2 * k], yk = coords[2 * k + 1];
    double xi = coords[2 * positions[i]], yi = coords[2 * positions[i] + 1];
    bool rightward = 2 * xk < BOARD_LEFT + BOARD_RIGHT;
    double head = rightward ? xk + run : xk - run;
    double off = fabs(xi - head);
    double s = off < DART_WIDTH ? (DART_WIDTH - off) / DART_WIDTH : 0.0;

    if (fabs(yi - yk) >= DART_LINE) {
        s *= 0.4;
    }
    s *= t < DART_MS ? 1.0 - (double)t / DART_MS : 0.0;
    *strength = s;
    *place = fmin(1.0, run / (BOARD_RIGHT - BOARD_LEFT));
}

/* A pixel of the two-colour palette: its strength (the brightest channel of
 * 255) and the colour's place, b / (r + b). */
static void decode(struct led_rgb px, double *strength, double *place) {
    *strength = MAX(px.r, px.b) / 255.0;
    *place = px.r + px.b > 0 ? (double)px.b / (px.r + px.b) : 0.0;
}

/* One hit at key k (pressed here, or relayed from the other half) noted at
 * base, frames every 10 ms through the dart's life and past it, each LED
 * against the model; records each LED's first lit frame and its peak. */
static double worst_s, worst_p;
static void one_press(uint32_t k, uint32_t base, bool relayed, uint32_t first[LED_COUNT],
                      double peak[LED_COUNT]) {
    bool moving = true;

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
    for (uint32_t t = 0; t <= DART_MS + 200; t += 10) {
        uint32_t sum;
        bool m;
        frame(base + t, &m, &sum);
        CHECK(sum <= CAP, "key %u at %u ms: sum %u > CAP %u", k, t, sum, (unsigned)CAP);
        for (size_t i = 0; i < LED_COUNT; i++) {
            double ms, mp, s, p;
            model(k, i, t, &ms, &mp);
            decode(strip_out[i], &s, &p);
            CHECK(strip_out[i].g == 0, "key %u at %u ms: LED %zu has green %u", k, t, i,
                  strip_out[i].g);
            if (t >= DART_MS) {
                CHECK(s == 0, "key %u: LED %zu lit at %u ms, past the dart's life", k, i, t);
            }
            CHECK(fabs(s - ms) <= 0.03, "key %u at %u ms: LED %zu (key %u) strength %.3f, model %.3f", k,
                  t, i, positions[i], s, ms);
            worst_s = fmax(worst_s, fabs(s - ms));
            if (ms > 0.2) {
                CHECK(fabs(p - mp) <= 0.03, "key %u at %u ms: LED %zu place %.3f, model %.3f", k, t,
                      i, p, mp);
                worst_p = fmax(worst_p, fabs(p - mp));
            } else if (ms > 0.08) {
                CHECK(fabs(p - mp) <= 0.08, "key %u at %u ms: LED %zu place %.3f, model %.3f (faint)",
                      k, t, i, p, mp);
            }
            if (s > 0) {
                if (first[i] == UINT32_MAX) {
                    first[i] = t;
                }
                peak[i] = fmax(peak[i], s);
            }
        }
        if (t == DART_MS) {
            CHECK(!m, "key %u: still moving at the dart's life", k);
        } else if (t == DART_MS - 10) {
            CHECK(m, "key %u: not moving at %u ms", k, t);
        }
        moving = m;
    }
    CHECK(!moving, "key %u: moving after the life", k);
    CHECK(dyed() == 0, "key %u: %d dyed", k, dyed());
}

int main(void) {
#ifdef HALF_RIGHT
    /* the right half: key 20 is x 1350 on the middle row, its thumbs 49..51
     * and 55..57, the left's inner column key 17 (x 500) and thumb 48 (x 750) */
    const uint32_t mid = 20, thumb = 50, far_in = 17, far_thumb = 48, far_out = 0;
    const bool rightward = false;
#else
    const uint32_t mid = 14, thumb = 47, far_in = 18, far_thumb = 49, far_out = 23;
    const bool rightward = true;
#endif
    uint32_t base = 5000;
    uint32_t first[LED_COUNT];
    double peak[LED_COUNT];

    palette_two();

    CHECK((EFFECTS_BUILT & LANTERN_EFFECT_DART) != 0, "built %08x lacks the dart", (unsigned)EFFECTS_BUILT);
    CHECK(LANTERN_EFFECT_COUNT == 9, "count %d", (int)LANTERN_EFFECT_COUNT);
    CHECK(LANTERN_EFFECT_DART == BIT(6), "bit %08x", (unsigned)LANTERN_EFFECT_DART);
    CHECK(EFFECT_DEFAULT == LANTERN_EFFECT_CROSS_RING, "default %08x", (unsigned)EFFECT_DEFAULT);
    CHECK(HIT_LIFE_MS == DISC_MS && HIT_LIFE_MS == 1900, "hit life %u", (unsigned)HIT_LIFE_MS);
    CHECK(DART_MS == 1200 && DART_SPEED == 16 && DART_WIDTH == 80 && DART_ROWS == 102, "constants");
    CHECK(led_of(mid) >= 0 && led_of(thumb) >= 0 && led_of(far_in) < 0 && led_of(far_thumb) < 0 &&
              led_of(far_out) < 0,
          "the harness's keys are on the wrong half");

    lantern_keys_light();
    frame(base, NULL, NULL);
    base += 100;
    pick(true, LANTERN_EFFECT_DART);

    /* a key of the main block: the model frame by frame; the board's ends
     * measured; nothing behind the key lights (the innermost thumbs, half a
     * unit outside the inner column, glance it for one frame: the design's
     * window is symmetric); the row first and fully, the other rows at two
     * fifths */
    one_press(mid, base, false, first, peak);
    CHECK((lantern_board() != NULL) && lantern_board()->left == 0 && lantern_board()->right == 1650, "ends %u..%u", lantern_board()->left, lantern_board()->right);
    CHECK(live_hits() == 0, "mid: %d hits after the dart's life", live_hits());
    {
        size_t lit_row = 0, lit_off = 0, behind = 0;
        for (size_t i = 0; i < LED_COUNT; i++) {
            int32_t dx = (int32_t)coords[2 * positions[i]] - (int32_t)coords[2 * mid];
            bool on_row = abs((int)coords[2 * positions[i] + 1] - (int)coords[2 * mid + 1]) < 50;
            if (first[i] == UINT32_MAX) {
                continue;
            }
            if (rightward ? dx < 0 : dx > 0) {
                behind++;
            }
            if (on_row) {
                lit_row++;
                /* the head reaches it at |dx| x 10 / 16 ms; frames 10 ms apart
                 * sample within 8 hundredths of it, 9/10 of its strength */
                double want = 1.0 - (double)abs(dx) * 10 / DART_SPEED / DART_MS;
                CHECK(peak[i] >= 0.9 * want - 0.03 && peak[i] <= want + 0.02,
                      "mid: row LED %zu (dx %d) peaked at %.2f, expected about %.2f", i, dx, peak[i],
                      want);
            } else {
                lit_off++;
                CHECK(peak[i] <= 0.42, "mid: off-row LED %zu peaked at %.2f, two fifths at most", i,
                      peak[i]);
            }
        }
        CHECK(behind == 0, "mid: %zu LEDs lit behind the key", behind);
        /* the row ahead of the key, the key itself included: the left's row
         * from x 200 is four keys, the right's from 1350 is three */
        CHECK(lit_row == (rightward ? 4 : 3), "mid: %zu row LEDs lit", lit_row);
        /* every other key ahead of it, within the width: the columns from the
         * key's x on, not the one behind */
        CHECK(lit_off >= 10, "mid: %zu off-row LEDs lit", lit_off);
    }
    base += 10000;

    /* a thumb: its cluster's row is the row, the lower thumbs and the main
     * block two fifths; the bar runs toward the other half */
    one_press(thumb, base, false, first, peak);
    {
        int self = led_of(thumb);
        CHECK(peak[self] >= 0.99, "thumb: itself peaked at %.2f", peak[self]);
    }
    base += 10000;

    /* the hit goes at the picked effects' life, not at HIT_LIFE_MS (the
     * disc's 1900): live at 1190, gone at 1200, and a frame 262 s later
     * draws nothing of it */
    fake_now = base;
    press(mid);
    frame(base + 1190, NULL, NULL);
    CHECK(live_hits() == 1, "mid: %d hits at 1190 ms", live_hits());
    frame(base + DART_MS, NULL, NULL);
    CHECK(live_hits() == 0, "mid: %d hits at the dart's life", live_hits());
    fake_now = base;
    press(mid);
    frame(base + 1220, NULL, NULL);
    size_t phantom = frame(base + 1220 + HIT_TIME + 1 + 300, NULL, NULL);
    CHECK(phantom == 0 && live_hits() == 0, "wrap: %zu lit, %d hits after HIT_TIME", phantom, live_hits());
    base += 10000;

    /* the other half's inner column, relayed: the bar comes in at this
     * half's inner column at about two thirds (406 ms of 1200) and runs on
     * to the outer column (719 ms, two fifths), its row full */
    one_press(far_in, base, true, first, peak);
    {
        int inner = -1, outer = -1;
        for (size_t i = 0; i < LED_COUNT; i++) {
            if (coords[2 * positions[i] + 1] != coords[2 * far_in + 1]) {
                continue;
            }
            if (coords[2 * positions[i]] == (rightward ? 500 : 1150)) {
                inner = (int)i;
            }
            if (coords[2 * positions[i]] == (rightward ? 0 : 1650)) {
                outer = (int)i;
            }
        }
        CHECK(inner >= 0 && outer >= 0, "relayed: no inner or outer LED on the row");
        if (inner < 0 || outer < 0) {
            return 1;
        }
        CHECK(first[inner] >= 350 && first[inner] <= 370, "relayed: inner column first lit at %u ms",
              first[inner]);
        CHECK(peak[inner] >= 0.55 && peak[inner] <= 0.67, "relayed: inner column peaked at %.2f",
              peak[inner]);
        CHECK(first[outer] > first[inner], "relayed: outer column lit at %u, inner at %u",
              first[outer], first[inner]);
        CHECK(peak[outer] >= 0.33 && peak[outer] <= 0.41, "relayed: outer column peaked at %.2f",
              peak[outer]);
    }
    base += 10000;

    /* the other half's thumb, relayed: nearly full at this half's thumbs */
    one_press(far_thumb, base, true, first, peak);
    {
        double most = 0;
        for (size_t i = 0; i < LED_COUNT; i++) {
            most = fmax(most, peak[i]);
        }
        CHECK(most >= 0.80, "relayed thumb: the strongest here %.2f", most);
    }
    base += 10000;

    /* the other half's outer column, relayed: in across the split at about
     * a third, out at the far edge at a seventh */
    one_press(far_out, base, true, first, peak);
    {
        double most = 0;
        for (size_t i = 0; i < LED_COUNT; i++) {
            most = fmax(most, peak[i]);
        }
        CHECK(most >= 0.33 && most <= 0.46, "relayed outer column: the strongest here %.2f", most);
    }
    base += 10000;

    /* two presses a key apart 50 ms apart on the same row, the second
     * behind the first: their heads run 20 hundredths apart, so an LED ahead
     * is under both, at their summed strength (full) and in the first
     * press's colour, the stronger */
    {
#ifdef HALF_RIGHT
        const uint32_t a = 23, b = 22, ahead = 19; /* x 1650, 1550; 1250, row 200 */
        const uint32_t ta = 0, tb = 50, when = 250;
#else
        const uint32_t a = 0, b = 1, ahead = 4; /* x 0, 100; 400, row 200 */
        const uint32_t ta = 0, tb = 50, when = 250;
#endif
        fake_now = base + ta;
        press(a);
        fake_now = base + tb;
        press(b);
        frame(base + when, NULL, NULL);
        double s, p, ms, mp;
        decode(strip_out[led_of(ahead)], &s, &p);
        model(a, (size_t)led_of(ahead), when, &ms, &mp);
        CHECK(s >= 0.99, "two: strength %.2f, expected the sum, full", s);
        CHECK(fabs(p - mp) <= 0.03, "two: place %.3f, expected the first press's %.3f", p, mp);
        pick(true, LANTERN_EFFECT_DART);
        base += 10000;
    }

    /* one shade: the bar is that colour wherever it has run */
    {
        palette_one();
        fake_now = base;
        press(mid);
        for (uint32_t t = 0; t < DART_MS; t += 10) {
            frame(base + t, NULL, NULL);
            for (size_t i = 0; i < LED_COUNT; i++) {
                struct led_rgb px = strip_out[i];
                CHECK(px.r == 0 && px.b == 0, "one shade at %u ms: LED %zu is %u,%u,%u", t, i, px.r,
                      px.g, px.b);
            }
        }
        palette_two();
        pick(true, LANTERN_EFFECT_DART);
        base += 10000;
    }

    /* the cap: eight keys pressed at once without a palette, white at full
     * scale (and the keys in their columns at two fifths: the bar stands on
     * the key at its start), more than CAP allows */
    {
        fake_have = false;
        fake_now = base;
        for (size_t i = 0; i < 8; i++) {
            press(positions[i]);
        }
        uint32_t s;
        size_t lit_n = frame(base, NULL, &s);
        CHECK(lit_n >= 8, "cap: %zu lit, expected 8 or more", lit_n);
        CHECK(s <= CAP && s > CAP - 3 * lit_n, "cap: sum %u, CAP %u", s, (unsigned)CAP);
        palette_two();
        pick(true, LANTERN_EFFECT_DART);
        base += 10000;
    }

    /* the dart with the crossing ring: more lit than the dart alone, the
     * frame moving until CROSS_MS */
    {
        pick(true, LANTERN_EFFECT_DART | LANTERN_EFFECT_CROSS_RING);
        fake_now = base;
        press(mid);
        bool m;
        size_t both = frame(base + 100, &m, NULL);
        CHECK(both > 5 && m, "both: %zu lit at 100 ms, moving %d", both, m);
        frame(base + 1400, &m, NULL);
        CHECK(m, "both: not moving at 1400 ms");
        pick(true, LANTERN_EFFECT_DART);
        base += 10000;
    }

    /* the pick changes mid-run: the hits go now */
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
    pick(true, LANTERN_EFFECT_DART);
    fake_now = base;
    press(mid);
    frame(base + 100, NULL, NULL);
    lantern_keys_blank();
    paint(NULL);
    CHECK(live_hits() == 0, "blank: %d hits", live_hits());
    size_t dark = frame(base + 200, NULL, NULL);
    CHECK(dark == 0 || !atomic_get(&lit), "blank: %zu lit", dark);

    printf("dart: built %08x, %d submits, worst strength %.3f place %.3f, fails %d\n",
           (unsigned)EFFECTS_BUILT, submits, worst_s, worst_p, fails);
    return fails != 0;
}
