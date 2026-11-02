/* Runs the real src/keys.c (included below) through the pick of the effects
 * on the host (canon t-5hqx): the default without a pick, each effect picked,
 * two at once, a bit this image does not build, and a change of the pick
 * mid-ring (the hits and the stain's dye go). */
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

static bool black(struct led_rgb p) { return p.r == 0 && p.g == 0 && p.b == 0; }

/* A frame at base + t: how many LEDs lit, and the frame's channel sum. */
static size_t frame(uint32_t base, uint32_t t, bool *moving, uint32_t *sum) {
    fake_now = base + t;
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

static int dyed(void) {
    int n = 0;
    for (size_t i = 0; i < LED_COUNT; i++) {
        n += stains[i] != 0;
    }
    return n;
}

static void pick(bool have, uint32_t mask) {
    fake_pick_have = have;
    fake_pick = mask;
    lantern_keys_effect_changed();
}

/* The key at the half's middle, pressed at base; frames at ages 0, 100, 500,
 * 1100, 1400, 1900, 2000: lit counts, moving flags, the hits alive after the
 * frame at 1400 (past the thin ring's life, within every hit's). A crossing
 * ring from a key of this half has left the half by 1100 (8.75 units at 12 a
 * second), so it lights nothing here afterwards while it still moves. */
struct run {
    size_t lit[7];
    bool moving[7];
    uint32_t sum_max;
    int dyed_after;
    int live_at_1400;
};
static const uint32_t ages[7] = {0, 100, 500, 1100, 1400, 1900, 2000};

static struct run run_press(uint32_t base, uint32_t key) {
    struct run r = {0};
    fake_now = base;
    press(key);
    for (size_t a = 0; a < 7; a++) {
        uint32_t s;
        r.lit[a] = frame(base, ages[a], &r.moving[a], &s);
        r.sum_max = MAX(r.sum_max, s);
        if (ages[a] == 1400) {
            r.live_at_1400 = live_hits();
        }
    }
    r.dyed_after = dyed();
    return r;
}

int main(void) {
    uint32_t base = 5000, key = positions[LED_COUNT / 2];

    fake_have = true;
    fake_palette[0] = 0xFF0000;
    fake_palette[1] = 0x00FF00;
    fake_palette[2] = 0x0000FF;
    fake_palette[3] = 0xFFFF00;
    fake_palette[4] = 0x00FFFF;
    fake_palette[5] = 0;

    CHECK(EFFECTS_BUILT == (LANTERN_EFFECT_THIN_RING | LANTERN_EFFECT_DISC | LANTERN_EFFECT_STAIN |
                            LANTERN_EFFECT_CROSS_RING | LANTERN_EFFECT_BEAMS | LANTERN_EFFECT_WATER |
                            LANTERN_EFFECT_DART | LANTERN_EFFECT_HEATMAP),
          "built %08x", (unsigned)EFFECTS_BUILT);
    CHECK(EFFECT_DEFAULT == LANTERN_EFFECT_CROSS_RING, "default %08x", (unsigned)EFFECT_DEFAULT);
    CHECK(LANTERN_EFFECT_COUNT == 9, "count %d", (int)LANTERN_EFFECT_COUNT);

    lantern_keys_light();
    frame(base, 0, NULL, NULL);
    base += 100;

    /* no pick: the default, the crossing ring: full strength past the thin
     * ring's second, moving until CROSS_MS, nothing dyed */
    struct run d = run_press(base, key);
    CHECK(d.lit[0] >= 1 && d.lit[2] >= 1 && d.lit[4] == 0, "default: lit %zu %zu %zu at 0/500/1400",
          d.lit[0], d.lit[2], d.lit[4]);
    CHECK(d.moving[4] && !d.moving[5] && !d.moving[6], "default: moving %d %d %d at 1400/1900/2000",
          d.moving[4], d.moving[5], d.moving[6]);
    CHECK(d.lit[5] == 0 && d.lit[6] == 0, "default: lit after the ring's life %zu %zu", d.lit[5], d.lit[6]);
    CHECK(d.dyed_after == 0, "default: %d dyed", d.dyed_after);
    CHECK(drawn_effects == LANTERN_EFFECT_CROSS_RING, "default: drew %08x", (unsigned)drawn_effects);
    base += 10000;

    /* a bit this image does not build: the default again */
    pick(true, BIT(7));
    struct run u = run_press(base, key);
    CHECK(u.lit[2] >= 1 && u.lit[4] == 0 && u.moving[4] && !u.moving[5], "unbuilt bit: lit %zu %zu moving %d %d",
          u.lit[2], u.lit[4], u.moving[4], u.moving[5]);
    CHECK(drawn_effects == LANTERN_EFFECT_CROSS_RING, "unbuilt bit: drew %08x", (unsigned)drawn_effects);
    base += 10000;

    /* the thin ring: fades within RING_MS, moving stops there though the hit
     * lives on (HIT_LIFE_MS), nothing dyed */
    pick(true, LANTERN_EFFECT_THIN_RING);
    struct run t = run_press(base, key);
    CHECK(t.lit[0] >= 1 && t.lit[2] >= 1, "thin ring: lit %zu %zu at 0/500", t.lit[0], t.lit[2]);
    CHECK(t.lit[3] == 0 && t.lit[4] == 0, "thin ring: lit %zu %zu at 1100/1400", t.lit[3], t.lit[4]);
    CHECK(t.moving[2] && !t.moving[3], "thin ring: moving %d %d at 500/1100", t.moving[2], t.moving[3]);
    CHECK(t.dyed_after == 0, "thin ring: %d dyed", t.dyed_after);
    CHECK(t.live_at_1400 == 0, "thin ring: the hit should be gone at 1400 ms, past its life (%d)", t.live_at_1400);
    CHECK(live_hits() == 0, "thin ring: %d hits after HIT_LIFE_MS", live_hits());
    base += 10000;

    /* the stain: the ring dyes within its reach; the dye stays lit after the
     * ring, over the black picture, and the frame stops moving */
    pick(true, LANTERN_EFFECT_STAIN);
    struct run s = run_press(base, key);
    CHECK(s.dyed_after > 0, "stain: nothing dyed");
    CHECK(s.lit[3] > 0 && s.lit[3] == (size_t)s.dyed_after, "stain: lit %zu at 1100, dyed %d", s.lit[3],
          s.dyed_after);
    CHECK(!s.moving[3], "stain: still moving at 1100");
    for (size_t i = 0; i < LED_COUNT; i++) {
        uint32_t dist = distance(positions[i], key);
        CHECK((stains[i] != 0) == (dist <= stain_reach), "stain: LED %zu at %u dyed %d", i, dist, stains[i]);
    }
    base += 10000;

    /* the pick changes: the hits go now, the dye at the next frame */
    fake_now = base;
    press(key);
    size_t before = frame(base, 300, NULL, NULL);
    CHECK(before > 0 && dyed() > 0, "stain mid-ring: lit %zu dyed %d", before, dyed());
    pick(true, LANTERN_EFFECT_DISC);
    CHECK(live_hits() == 0, "repick: %d hits left", live_hits());
    bool mv;
    size_t after = frame(base, 330, &mv, NULL);
    CHECK(after == 0 && !mv && dyed() == 0, "repick: lit %zu moving %d dyed %d", after, mv, dyed());
    base += 10000;

    /* the disc: the pressed key lights at once and fades; moving until DISC_MS */
    struct run c = run_press(base, key);
    CHECK(c.lit[0] >= 1 && c.lit[1] >= 1, "disc: lit %zu %zu at 0/100", c.lit[0], c.lit[1]);
    CHECK(c.moving[4] && !c.moving[5], "disc: moving %d %d at 1400/1900", c.moving[4], c.moving[5]);
    CHECK(c.dyed_after == 0, "disc: %d dyed", c.dyed_after);
    base += 10000;

    /* two at once: the thin ring and the disc, more lit than either */
    pick(true, LANTERN_EFFECT_THIN_RING | LANTERN_EFFECT_DISC);
    struct run b = run_press(base, key);
    CHECK(b.lit[1] >= MAX(t.lit[1], c.lit[1]), "both: lit %zu at 100, ring %zu disc %zu", b.lit[1], t.lit[1],
          c.lit[1]);
    CHECK(b.moving[4] && !b.moving[5], "both: moving %d %d at 1400/1900", b.moving[4], b.moving[5]);
    CHECK(b.sum_max <= CAP, "both: sum %u > CAP", b.sum_max);
    base += 10000;

    /* the stain with the thin ring's bit too: the dyeing ring */
    pick(true, LANTERN_EFFECT_THIN_RING | LANTERN_EFFECT_STAIN);
    struct run ts = run_press(base, key);
    CHECK(ts.dyed_after == s.dyed_after, "ring + stain: dyed %d, stain alone %d", ts.dyed_after, s.dyed_after);
    base += 10000;

    /* the pick withdrawn (no store): the default again */
    pick(false, 0);
    struct run w = run_press(base, key);
    CHECK(w.lit[2] >= 1 && w.lit[4] == 0 && w.moving[4] && w.dyed_after == 0,
          "withdrawn: lit %zu %zu at 500/1400, moving %d, dyed %d", w.lit[2], w.lit[4], w.moving[4], w.dyed_after);
    CHECK(drawn_effects == LANTERN_EFFECT_CROSS_RING, "withdrawn: drew %08x", (unsigned)drawn_effects);

    /* the blank clears the dye and the hits */
    pick(true, LANTERN_EFFECT_STAIN);
    fake_now = base + 10000;
    press(key);
    frame(base + 10000, 300, NULL, NULL);
    lantern_keys_blank();
    paint(NULL);
    CHECK(live_hits() == 0 && dyed() == 0, "blank: %d hits %d dyed", live_hits(), dyed());

    printf("effects: built %08x default %08x, %d submits, fails %d\n", (unsigned)EFFECTS_BUILT,
           (unsigned)EFFECT_DEFAULT, submits, fails);
    return fails != 0;
}
