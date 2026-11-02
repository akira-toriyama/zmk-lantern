/* Runs the real src/keys.c (unity.c) through the tempo on the host (canon
 * t-6ttk, CONFIG_LANTERN_TEMPO): one press at rest (the tempo as the count
 * stands, a sixteenth up), a burst to the top (twice the engine's clock, eased
 * there), each live hit's tempo age advancing by the tempo times the frame's
 * period and never jumping, the crossing ring drawn at the tempo's radius, the
 * tempo falling back as the beats leave the window and snapping at the next
 * rest, the heatmap untouched (its heat on the real clock), the blank dropping
 * the beats, stale beats cleared, and none coming back when the hits' 18-bit
 * clock wraps (a beat keeps the whole clock), a press noted while the beats
 * are counted never cleared as stale, and the beams, the water and the dart
 * under the tempo within the cap. */
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

static int live_beats(void) {
    int n = 0;
    for (size_t j = 0; j < TEMPO_FULL; j++) {
        n += beats[j] != 0;
    }
    return n;
}

static bool black(struct led_rgb p) { return p.r == 0 && p.g == 0 && p.b == 0; }

/* A frame at t: the strip after it; how many LEDs lit, the channel sum. */
static size_t frame(uint32_t t, bool *moving, uint32_t *sum) {
    fake_now = t;
    clock_calls = 0;
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

static void pick(uint32_t mask) {
    fake_pick_have = true;
    fake_pick = mask;
    lantern_keys_effect_changed();
}

/* The brightest LED of the last frame, or -1 when none is lit. */
static int brightest(void) {
    int best = -1;
    unsigned int w = 0;
    for (size_t i = 0; i < LED_COUNT; i++) {
        unsigned int s = strip_out[i].r + strip_out[i].g + strip_out[i].b;
        if (s > w) {
            w = s;
            best = (int)i;
        }
    }
    return best;
}

static uint32_t off(uint32_t d, uint32_t r) { return d > r ? d - r : r - d; }

/* The crossing ring of a single live hit of key was drawn at the tempo's
 * radius, not the real one: the brightest LED sits nearer the tempo's radius
 * where the two differ by more than a key (else the frame tells nothing). */
static void check_ring_at_tempo(uint32_t key, const char *what) {
    uint32_t rt = (uint32_t)tempo_ages[0] * CROSS_SPEED / 10, rr = (uint32_t)real_ages[0] * CROSS_SPEED / 10;
    int i = brightest();
    if (off(rt, rr) <= 100 || i < 0) {
        return;
    }
    uint32_t d = distance(led_key((size_t)i), (uint8_t)key);
    CHECK(off(d, rt) < off(d, rr), "%s: brightest LED %d at %u from key %u: tempo radius %u, real %u", what,
          i, d, key, rt, rr);
}

/* A home-row key of this half, and another. */
#ifdef HALF_RIGHT
#define K1 31
#define K2 33
#else
#define K1 28
#define K2 26
#endif

static int led_of(uint32_t key) {
    for (size_t i = 0; i < LED_COUNT; i++) {
        if (positions[i] == key) {
            return (int)i;
        }
    }
    return -1;
}

static uint32_t race_t;
static void race_hook(void) { press(K2); }

int main(void) {
    fake_have = true;
    fake_palette[0] = 0xFF0000;
    fake_palette[1] = 0x0000FF;
    for (size_t s = 2; s < 6; s++) {
        fake_palette[s] = 0;
    }
    lantern_keys_light();
    pick(LANTERN_EFFECT_CROSS_RING);

    /* 1. one press at rest: the tempo is the count's, 1 + 1/16, from the
     * first frame; the hit's tempo age is its real age at that tempo, within
     * a ms of the frame's rounding, and grows with the frames. */
    uint32_t t = 1000;
    fake_now = t;
    press(K1);
    bool moving;
    frame(t + 30, &moving, NULL);
    CHECK(moving, "1: a press moves the frame");
    CHECK(tempo_f == 272, "1: tempo at one beat %u, want 272", tempo_f);
    CHECK(real_ages[0] == 30, "1: real age %u", real_ages[0]);
    CHECK(tempo_ages[0] == 30 * 272 / 256, "1: tempo age %u, want %u", tempo_ages[0], 30 * 272 / 256);
    uint32_t last = tempo_ages[0];
    for (uint32_t k = 2; k <= 20; k++) {
        frame(t + 30 * k, NULL, NULL);
        CHECK(tempo_f == 272, "1: tempo held at 272, got %u at frame %u", tempo_f, k);
        double want = 30.0 * k * 272 / 256;
        CHECK(fabs((double)tempo_ages[0] - want) <= 1.0, "1: frame %u tempo age %u, want %.1f", k,
              tempo_ages[0], want);
        CHECK(tempo_ages[0] >= last, "1: tempo age fell");
        last = tempo_ages[0];
    }
    /* the hit goes at its real life, not sooner */
    uint32_t life = cross_life();
    frame(t + life - 1, &moving, NULL);
    CHECK(moving && live_hits() == 1, "1: the hit lives its real life (%u ms)", life);
    frame(t + life, &moving, NULL);
    CHECK(!moving && live_hits() == 0, "1: the hit goes at its real life");
    CHECK(!tempo_running, "1: at rest after the hit went");

    /* 2. a burst: sixteen presses 50 ms apart with frames between, the tempo
     * eased up to twice (every beat still in the window) and no hit's tempo
     * age jumping (each frame adds the tempo times the period, within a ms);
     * the ring at the tempo's radius. */
    t = 10000;
    uint32_t f_last = 0, prev_now = 0;
    uint16_t age_last[HITS] = {0};
    atomic_val_t v_last[HITS] = {0};
    for (uint32_t p = 0; p < 16; p++) {
        fake_now = t + 50 * p;
        press(p % 2 == 0 ? K1 : K2);
        for (uint32_t k = 1; k <= 1; k++) {
            uint32_t now = t + 50 * p + 30 * k;
            /* the frames fall 50 ms apart (a press between each two) */
            uint32_t dt = prev_now == 0 ? 0 : now - prev_now;
            prev_now = now;
            frame(now, &moving, NULL);
            CHECK(moving, "2: moving at %u", now);
            uint32_t want_target = 256 + 256 * MIN(p + 1, 16u) / 16;
            CHECK(tempo_f <= want_target, "2: tempo %u above the count's %u at %u", tempo_f, want_target, now);
            CHECK(tempo_f >= f_last, "2: tempo fell from %u to %u at %u while the beats grew", f_last, tempo_f,
                  now);
            f_last = tempo_f;
            /* each slot's age advanced by dt x tempo, where the slot kept its hit */
            size_t n = 0;
            for (size_t j = 0; j < HITS; j++) {
                if (hits[j] == 0) {
                    continue;
                }
                /* the live[] order is the slot order (hits_read) */
                if (v_last[j] == hits[j]) {
                    double want = age_last[j] + (double)dt * tempo_f / 256;
                    CHECK(fabs((double)tempo_ages[n] - want) <= 1.5, "2: slot %zu tempo age %u, want %.1f at %u", j,
                          tempo_ages[n], want, now);
                }
                CHECK(tempo_ages[n] >= real_ages[n], "2: tempo age %u under real %u", tempo_ages[n], real_ages[n]);
                CHECK(tempo_ages[n] <= 2 * real_ages[n] + 1, "2: tempo age %u over twice real %u", tempo_ages[n],
                      real_ages[n]);
                age_last[j] = tempo_ages[n];
                v_last[j] = hits[j];
                n++;
            }
        }
    }
    CHECK(tempo_f >= 400, "2: tempo after sixteen presses %u, not near the top", tempo_f);
    /* frames on, no press: the tempo reaches the top and holds while the
     * beats are in the window (the first beat leaves it at t + 2000) */
    for (uint32_t k = 1; k <= 38; k++) {
        frame(t + 800 + 30 * k, NULL, NULL);
    }
    CHECK(tempo_f == 512, "2: tempo at the top %u, want 512", tempo_f);
    /* a single fresh press now: its ring runs at the tempo's radius */
    fake_now = t + 2000;
    pick(LANTERN_EFFECT_CROSS_RING); /* the hits go, the beats stay */
    CHECK(live_hits() == 0 && live_beats() == 16, "2: a pick change keeps the beats (%d) and drops the hits (%d)",
          live_beats(), live_hits());
    press(K1);
    for (uint32_t k = 1; k <= 8; k++) {
        frame(t + 2000 + 30 * k, NULL, NULL);
        check_ring_at_tempo(K1, "2: ring");
    }

    /* 3. the beats leave the window: the tempo eases down while frames run,
     * and the next press after a rest takes the count's tempo at once. */
    uint32_t f_before = tempo_f;
    for (uint32_t k = 9; k <= 100; k++) {
        frame(t + 2000 + 30 * k, &moving, NULL);
        if (!moving) {
            break;
        }
    }
    CHECK(tempo_f < f_before, "3: tempo did not fall as the beats aged (%u -> %u)", f_before, tempo_f);
    CHECK(!tempo_running && live_hits() == 0, "3: at rest after the ring");
    fake_now = t + 8000;
    press(K2);
    frame(t + 8030, NULL, NULL);
    CHECK(tempo_f == 272, "3: after a rest the count's tempo at once, got %u", tempo_f);
    CHECK(live_beats() == 1, "3: the beats past the window cleared (%d left)", live_beats());
    frame(t + 8000 + cross_life(), &moving, NULL);
    CHECK(!moving, "3: quiet");

    /* 4. the heatmap keeps the real clock: eight presses of one key 100 ms
     * apart warm it to 8 x 0.125 less 0.7 s x 0.15 of cooling at the eighth,
     * then cool on; a tempo age (twice the real) would double the cooling. */
    t = 30000;
    fake_now = t;
    pick(LANTERN_EFFECT_HEATMAP);
    for (uint32_t p = 0; p < 8; p++) {
        fake_now = t + 100 * p;
        press(K1);
        frame(t + 100 * p + 30, NULL, NULL);
    }
    CHECK(tempo_f > 256, "4: the tempo rose during the burst (%u)", tempo_f);
    double want = 8 * 0.125 - 0.15 * 0.73;
    double got = heat[led_of(K1)] / 65536.0;
    CHECK(fabs(got - want) <= 0.01, "4: heat %.4f, want %.4f (real clock)", got, want);
    for (uint32_t k = 1; k <= 300; k++) {
        frame(t + 730 + 30 * k, &moving, NULL);
        if (!moving) {
            break;
        }
    }
    CHECK(!moving, "4: the heatmap cooled");

    /* 5. the blank drops the beats: a burst, a blank, a light and one press
     * read as one beat. */
    t = 50000;
    pick(LANTERN_EFFECT_CROSS_RING);
    for (uint32_t p = 0; p < 10; p++) {
        fake_now = t + 50 * p;
        press(K1);
    }
    frame(t + 500, NULL, NULL);
    CHECK(tempo_f > 272, "5: tempo up (%u)", tempo_f);
    lantern_keys_blank();
    paint(NULL);
    CHECK(live_beats() == 0 && live_hits() == 0, "5: the blank dropped the beats (%d) and the hits (%d)", live_beats(),
          live_hits());
    lantern_keys_light();
    fake_now = t + 600;
    press(K2);
    frame(t + 630, NULL, NULL);
    CHECK(tempo_f == 272, "5: after the blank one beat, tempo %u", tempo_f);
    frame(t + 600 + cross_life(), &moving, NULL);
    CHECK(!moving, "5: quiet");

    /* 6. stale beats: the frames stop at the ring's life, under the window,
     * so a burst's last beats sit unread; a press one 18-bit clock (262 s)
     * and a little later must read them as stale, not fresh (Opus review,
     * 2026-11-02: the first version kept 18 bits and gave 1.5x). */
    for (uint32_t p = 0; p < 12; p++) {
        fake_now = t + 1000 + 60 * p;
        press(K1);
        frame(t + 1000 + 60 * p + 30, NULL, NULL);
    }
    for (uint32_t k = 1; k <= 100; k++) {
        frame(t + 1720 + 30 * k, &moving, NULL);
        if (!moving) {
            break;
        }
    }
    CHECK(!moving && live_beats() > 0, "6: at rest with %d beats unread", live_beats());
    uint32_t wrapped = t + 1660 + (HIT_TIME + 1) + 100;
    fake_now = wrapped;
    press(K1);
    frame(wrapped + 30, NULL, NULL);
    CHECK(live_beats() == 1, "6: stale beats cleared across the 18-bit wrap, %d left", live_beats());
    CHECK(tempo_f == 272, "6: tempo after the wrap %u, want 272", tempo_f);
    frame(wrapped + cross_life(), &moving, NULL);
    CHECK(!moving, "6: quiet");

    /* 7. a press noted while the beats are counted (the clock read after the
     * beat the frame is on) is never cleared as stale: the beats stand at
     * two after the frame, and the frame counted at least the one it had. */
    t = 200000;
    fake_now = t;
    press(K1);
    race_t = t + 30;
    clock_hook = race_hook;
    /* the first clock reading of hits_read() is tempo_step()'s, the second
     * beats_count()'s after the one beat: the hook fires there */
    clock_hook_at = 2;
    frame(t + 30, NULL, NULL);
    CHECK(clock_hook == NULL, "7: the hook fired");
    CHECK(live_beats() == 2, "7: beats after the race %d, want 2", live_beats());
    CHECK(live_hits() == 2, "7: hits after the race %d, want 2", live_hits());
    frame(t + 60, NULL, NULL);
    CHECK(live_beats() == 2, "7: beats a frame on %d, want 2", live_beats());
    CHECK(tempo_f >= 272, "7: tempo %u", tempo_f);

    /* 8. the beams, the water and the dart under the tempo: a burst, frames
     * to the end, every frame within the cap (each skips a hit at or past
     * its life before any fade). */
    const uint32_t picks[] = {LANTERN_EFFECT_BEAMS, LANTERN_EFFECT_WATER, LANTERN_EFFECT_DART,
                              LANTERN_EFFECT_THIN_RING | LANTERN_EFFECT_STAIN, LANTERN_EFFECT_DISC};
    for (size_t q = 0; q < ARRAY_SIZE(picks); q++) {
        t = 300000 + 20000 * (uint32_t)q;
        fake_now = t;
        pick(picks[q]);
        for (uint32_t p = 0; p < 16; p++) {
            fake_now = t + 50 * p;
            press(p % 2 == 0 ? K1 : K2);
            uint32_t sum;
            frame(t + 50 * p + 30, NULL, &sum);
            CHECK(sum <= CAP, "8: pick %x sum %u > CAP at press %u", picks[q], sum, p);
        }
        CHECK(tempo_f >= 400, "8: pick %x tempo %u", picks[q], tempo_f);
        for (uint32_t k = 1; k <= 400; k++) {
            uint32_t sum;
            frame(t + 800 + 30 * k, &moving, &sum);
            CHECK(sum <= CAP, "8: pick %x sum %u > CAP at frame %u", picks[q], sum, k);
            if (!moving) {
                break;
            }
        }
        CHECK(!moving, "8: pick %x still moving", picks[q]);
    }

    /* 9. the pick: off hands every entry the real ages and rests the tempo,
     * whatever the beats; on starts from the count; a pick with neither
     * state (a bit this image's list lacks) is the Kconfig default, on. */
    t = 500000;
    fake_now = t;
    pick(LANTERN_EFFECT_CROSS_RING);
    fake_tempo_have = true;
    fake_tempo = LANTERN_TEMPO_OFF;
    for (uint32_t p = 0; p < 12; p++) {
        fake_now = t + 50 * p;
        press(p % 2 == 0 ? K1 : K2);
        frame(t + 50 * p + 30, NULL, NULL);
        size_t n = 0;
        for (size_t j = 0; j < HITS; j++) {
            if (hits[j] != 0) {
                CHECK(tempo_ages[n] == real_ages[n], "9: off: tempo age %u, real %u", tempo_ages[n], real_ages[n]);
                n++;
            }
        }
        CHECK(!tempo_running, "9: off: the tempo runs");
    }
    /* at least these twelve: beats older than the window stay until the
     * tempo next counts them (off, nothing counts) */
    CHECK(live_beats() >= 12, "9: off: the beats are still noted (%d)", live_beats());
    fake_tempo = LANTERN_TEMPO_ON;
    fake_now = t + 600;
    press(K1);
    frame(t + 630, NULL, NULL);
    CHECK(tempo_f == 256 + 256 * 13 / 16, "9: on: the count's tempo at once, got %u", tempo_f);
    CHECK(tempo_running, "9: on: the tempo runs");
    check_ring_at_tempo(K1, "9: on");
    fake_tempo = BIT(5);
    frame(t + 660, NULL, NULL);
    CHECK(tempo_running, "9: a pick with neither state: the default, on");
    fake_tempo_have = false;
    frame(t + 690, NULL, NULL);
    CHECK(tempo_running, "9: no pick: the default, on");
    for (uint32_t k = 1; k <= 100; k++) {
        frame(t + 690 + 30 * k, &moving, NULL);
        if (!moving) {
            break;
        }
    }
    CHECK(!moving, "9: quiet");

    printf("tempo fails %d\n", fails);
    return fails == 0 ? 0 : 1;
}
