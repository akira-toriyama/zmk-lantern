/* Runs the real src/keys.c and src/effects/layer_keys.c (unity.c) through
 * the layer keys on the host (canon t-6ttk, the layer keys' first trial): canon's
 * layers' states as src/layer.c would compute them on the dongle, each
 * half's LEDs against the keys of the state (a key of the state lights in
 * the slot's shade at full scale within the cap, every other LED keeps what
 * the frame had), the format's high word and slot, a refused slot, the
 * pick's colour, no change without a change, the effect not picked, the
 * dart under it, a press with the layer keys alone, the intro, the blank
 * and the light again, and the cap at its worst (every LED white). */
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
static bool same_px(struct led_rgb a, struct led_rgb b) { return a.r == b.r && a.g == b.g && a.b == b.b; }

/* A frame at t: the strip after it; the channel sum. */
static uint32_t frame(uint32_t t, bool *moving) {
    fake_now = t;
    bool m = compose(0);
    led_strip_update_rgb(strip, pixels, LED_COUNT);
    uint32_t s = 0;
    for (size_t i = 0; i < LED_COUNT; i++) {
        s += strip_out[i].r + strip_out[i].g + strip_out[i].b;
    }
    if (moving) {
        *moving = m;
    }
    return s;
}

static void pick(bool have, uint32_t mask) {
    fake_pick_have = have;
    fake_pick = mask;
    lantern_keys_effect_changed();
}

/* Five colours, slot 5 cleared (canon's sender fills five): each distinct. */
static const uint32_t five[6] = {0xFF2000, 0x00C040, 0x2040FF, 0xFFD000, 0xC000FF, 0};
static void palette_five(void) {
    fake_have = true;
    for (size_t s = 0; s < 6; s++) {
        fake_palette[s] = five[s];
    }
}

/* A state as src/layer.c packs it (src/layer.h): keys, a list ending in -1,
 * and the slot. */
struct st {
    uint32_t p1, p2;
};
static struct st state_of(const int *keys, uint32_t slot) {
    struct st s = {0, 0};
    for (; *keys >= 0; keys++) {
        if (*keys < 32) {
            s.p1 |= 1u << *keys;
        } else {
            s.p2 |= 1u << (*keys - 32);
        }
    }
    if (s.p1 != 0 || s.p2 != 0) {
        s.p2 |= slot << LANTERN_LAYER_SLOT_SHIFT;
    }
    return s;
}

static bool in_list(const int *keys, uint32_t k) {
    for (; *keys >= 0; keys++) {
        if ((uint32_t)*keys == k) {
            return true;
        }
    }
    return false;
}

/* canon's layers' keys (canon t-6ttk's research, 2.4, from imprint.keymap). */
static const int num_keys[] = {7, 8, 9, 19, 20, 21, 30, 31, 32, 33, -1};
static const int sym_keys[] = {6, 7, 8, 9, 10, 18, 19, 20, 21, 22, 23, 30, 31, 32, 33, 34, -1};
static const int fn_keys[] = {7, 8, 9, 10, 19, 20, 21, 22, 31, 32, 33, 34, -1};
static const int vkey_keys[] = {1,  2,  3,  4,  5,  13, 14, 15, 16, 17, 25, 26, 27, 28, 29,
                                6,  7,  8,  9,  10, 18, 19, 20, 21, 22, 30, 31, 32, 33, 34, -1};
static const int high_keys[] = {33, 44, 54, 57, -1};
static const int none_keys[] = {-1};

/* The frame against a state: the LEDs of its keys all one colour, the slot's
 * shade at full scale or that scaled down alike by the cap, never brighter in
 * any channel; every other LED as it was in before[] (black under the no
 * picture without other effects). Returns how many LEDs it lit. */
static size_t check_state(const char *name, const int *keys, struct led_rgb want,
                          const struct led_rgb before[LED_COUNT], uint32_t sum) {
    size_t lit_n = 0;
    struct led_rgb first = {0};
    bool have_first = false;

    CHECK(sum <= CAP, "%s: sum %u > CAP %u", name, sum, (unsigned)CAP);
    for (size_t i = 0; i < LED_COUNT; i++) {
        uint32_t k = positions[i];
        if (in_list(keys, k)) {
            lit_n++;
            CHECK(!black(strip_out[i]), "%s: LED %zu (key %u) dark", name, i, k);
            CHECK(strip_out[i].r <= want.r && strip_out[i].g <= want.g && strip_out[i].b <= want.b,
                  "%s: LED %zu (key %u) %02x%02x%02x above the shade %02x%02x%02x", name, i, k,
                  strip_out[i].r, strip_out[i].g, strip_out[i].b, want.r, want.g, want.b);
            if (!have_first) {
                first = strip_out[i];
                have_first = true;
            } else {
                CHECK(same_px(first, strip_out[i]), "%s: LED %zu (key %u) differs from the others",
                      name, i, k);
            }
        } else {
            CHECK(same_px(strip_out[i], before[i]),
                  "%s: LED %zu (key %u) %02x%02x%02x, not the frame's %02x%02x%02x", name, i, k,
                  strip_out[i].r, strip_out[i].g, strip_out[i].b, before[i].r, before[i].g,
                  before[i].b);
        }
    }
    if (have_first && sum <= CAP) {
        /* within the cap, unscaled: the shade itself */
        uint32_t lit_sum = (uint32_t)lit_n * (want.r + want.g + want.b);
        if (lit_sum <= CAP) {
            CHECK(same_px(first, want), "%s: %02x%02x%02x, not the shade %02x%02x%02x though within the cap",
                  name, first.r, first.g, first.b, want.r, want.g, want.b);
        }
    }
    return lit_n;
}

static size_t on_half(const int *keys) {
    size_t n = 0;
    for (size_t i = 0; i < LED_COUNT; i++) {
        n += in_list(keys, positions[i]);
    }
    return n;
}

static struct led_rgb shade_of(uint32_t slot) {
    return shade_hi(&frame_shades, frame_shades.rank[slot]);
}

int main(void) {
    static const struct led_rgb blacks[LED_COUNT];
    uint32_t base = 5000, sum;
    bool moving;
    int before_submits;

    palette_five();
    CHECK((EFFECTS_BUILT & LANTERN_EFFECT_LAYER_KEYS) != 0, "built %08x lacks the layer keys",
          (unsigned)EFFECTS_BUILT);
    CHECK(LANTERN_EFFECT_LAYER_KEYS == BIT(8), "bit %08x", (unsigned)LANTERN_EFFECT_LAYER_KEYS);
    CHECK(LANTERN_EFFECT_COUNT == 9, "count %d", (int)LANTERN_EFFECT_COUNT);
    CHECK((LANTERN_EFFECT_ALL & BIT(7)) == 0, "bit 7 taken: %08x", (unsigned)LANTERN_EFFECT_ALL);
    CHECK(LANTERN_LAYER_CHUNK_KEYS == 61 && LANTERN_LAYER_CHUNKS == 1 && LANTERN_LAYER_WORDS == 2 && LANTERN_LAYER_SLOT_SHIFT == 29, "format");

    lantern_keys_light();
    frame(base, NULL);
    base += 100;
    pick(true, LANTERN_EFFECT_LAYER_KEYS);

    /* nothing set: black */
    sum = frame(base, &moving);
    CHECK(sum == 0 && !moving, "no state: sum %u moving %d", sum, moving);

    /* the Number layer, slot 0: the right half's ten digits, the left nothing */
    {
        struct st s = state_of(num_keys, 0);
        before_submits = submits;
        lantern_layer_keys_set(0, true, s.p1, s.p2);
        CHECK(submits == before_submits + 1, "Num: %d submits for a change", submits - before_submits);
        sum = frame(base += 30, &moving);
        size_t n = check_state("Num", num_keys, shade_of(0), blacks, sum);
        CHECK(n == on_half(num_keys), "Num: %zu lit, %zu on this half", n, on_half(num_keys));
#ifdef HALF_RIGHT
        CHECK(n == 10, "Num: %zu lit on the right half", n);
#else
        CHECK(n == 0, "Num: %zu lit on the left half", n);
#endif
        CHECK(!moving, "Num: moving");
        /* the same state again: no repaint */
        before_submits = submits;
        lantern_layer_keys_set(0, true, s.p1, s.p2);
        CHECK(submits == before_submits, "Num again: %d submits", submits - before_submits);
    }

    /* Symbol 1 and 2, slots 1 and 3; Function, slot 4; a Vkey layer, slot 1,
     * fifteen keys on each half */
    {
        struct {
            const char *name;
            const int *keys;
            uint32_t slot;
            size_t right, left;
        } layers[] = {
            {"Sym1", sym_keys, 1, 16, 0},
            {"Sym2", sym_keys, 3, 16, 0},
            {"Fn", fn_keys, 4, 12, 0},
            {"Vkey LL", vkey_keys, 1, 15, 15},
            {"Vkey RR", vkey_keys, 3, 15, 15},
        };
        for (size_t l = 0; l < ARRAY_SIZE(layers); l++) {
            struct st s = state_of(layers[l].keys, layers[l].slot);
            before_submits = submits;
            lantern_layer_keys_set(0, true, s.p1, s.p2);
            CHECK(submits == before_submits + 1, "%s: %d submits", layers[l].name, submits - before_submits);
            sum = frame(base += 30, &moving);
            size_t n = check_state(layers[l].name, layers[l].keys, shade_of(layers[l].slot), blacks, sum);
#ifdef HALF_RIGHT
            CHECK(n == layers[l].right, "%s: %zu lit on the right", layers[l].name, n);
#else
            CHECK(n == layers[l].left, "%s: %zu lit on the left", layers[l].name, n);
#endif
            printf("%s slot %u: %zu lit, sum %u of CAP %u\n", layers[l].name, layers[l].slot, n, sum,
                   (unsigned)CAP);
        }
    }

    /* the high word: keys 33 (a digit), 44, 54 and 57 (thumbs), slot 3 */
    {
        struct st s = state_of(high_keys, 3);
        lantern_layer_keys_set(0, true, s.p1, s.p2);
        sum = frame(base += 30, &moving);
        size_t n = check_state("high", high_keys, shade_of(3), blacks, sum);
        CHECK(n == on_half(high_keys) && n == (positions[0] == 17 ? 1u : 3u), "high: %zu lit", n);
    }

    /* slot 5 (cleared): slot 0's colour */
    {
        struct st s = state_of(num_keys, 5);
        lantern_layer_keys_set(0, true, s.p1, s.p2);
        sum = frame(base += 30, &moving);
        CHECK(same_px(shade_of(5), shade_of(0)), "slot 5 is not slot 0's");
        check_state("slot 5", num_keys, shade_of(0), blacks, sum);
    }

    /* slots 6 and 7: no palette slot, dropped, nothing repainted */
    {
        struct st keep = state_of(fn_keys, 4);
        lantern_layer_keys_set(0, true, keep.p1, keep.p2);
        frame(base += 30, NULL);
        for (uint32_t bad = 6; bad <= 7; bad++) {
            struct st s = state_of(num_keys, bad);
            before_submits = submits;
            lantern_layer_keys_set(0, true, s.p1, s.p2);
            CHECK(submits == before_submits, "slot %u: %d submits", bad, submits - before_submits);
            sum = frame(base += 30, NULL);
            check_state("after a bad slot", fn_keys, shade_of(4), blacks, sum);
        }
        /* a chunk past the keymap's (one chunk for 58 keys): dropped */
        {
            struct st s = state_of(num_keys, 0);
            before_submits = submits;
            lantern_layer_keys_set(1, false, s.p1, s.p2);
            lantern_layer_keys_set(1, true, s.p1, s.p2);
            CHECK(submits == before_submits, "chunk 1: %d submits", submits - before_submits);
            sum = frame(base += 30, NULL);
            check_state("after a bad chunk", fn_keys, shade_of(4), blacks, sum);
        }
    }

    /* the pick's colour: every lit key in it, whatever the slot */
    {
        struct st s = state_of(sym_keys, 3);
        fake_colour = 0x00FF00;
        pick(true, LANTERN_EFFECT_LAYER_KEYS);
        lantern_layer_keys_set(0, true, s.p1, s.p2);
        sum = frame(base += 30, NULL);
        check_state("colour", sym_keys, scale(0x00FF00, HI_LEVEL), blacks, sum);
        fake_colour = 0;
        pick(true, LANTERN_EFFECT_LAYER_KEYS);
    }

    /* no layer: black, one repaint */
    {
        before_submits = submits;
        lantern_layer_keys_clear();
        CHECK(submits == before_submits + 1, "clear: %d submits", submits - before_submits);
        sum = frame(base += 30, &moving);
        check_state("clear", none_keys, (struct led_rgb){0}, blacks, sum);
        CHECK(sum == 0, "clear: sum %u", sum);
    }

    /* a press with the layer keys alone: one frame, no motion, no hit kept */
    {
        struct st s = state_of(vkey_keys, 1);
        lantern_layer_keys_set(0, true, s.p1, s.p2);
        frame(base += 30, NULL);
        fake_now = base += 30;
        before_submits = submits;
        press(positions[3]);
        CHECK(submits == before_submits + 1, "press: %d submits", submits - before_submits);
        sum = frame(base, &moving);
        CHECK(!moving, "press: the layer keys alone move");
        CHECK(live_hits() == 0, "press: %d hits kept with life 0", live_hits());
        check_state("press", vkey_keys, shade_of(1), blacks, sum);
    }

    /* not picked: the state is kept and not drawn; picked again, drawn */
    {
        pick(true, LANTERN_EFFECT_DART);
        sum = frame(base += 30, &moving);
        CHECK(sum == 0, "dart picked, no press: sum %u (the layer keys drew)", sum);
        pick(true, LANTERN_EFFECT_LAYER_KEYS);
        sum = frame(base += 30, NULL);
        check_state("picked again", vkey_keys, shade_of(1), blacks, sum);
    }

    /* the dart under the layer keys: a press, then the layer keys' LEDs in
     * the layer's shade and every other LED as the dart alone draws it */
    {
        struct led_rgb dart_alone[LED_COUNT];
        uint32_t t0 = base += 2000;

        pick(true, LANTERN_EFFECT_DART);
        fake_now = t0;
        press(positions[10]);
        frame(t0 + 90, NULL);
        memcpy(dart_alone, strip_out, sizeof(dart_alone));
        int lit_by_dart = 0;
        for (size_t i = 0; i < LED_COUNT; i++) {
            lit_by_dart += !black(dart_alone[i]);
        }
        CHECK(lit_by_dart > 0, "dart: nothing lit at 90 ms");

        pick(true, LANTERN_EFFECT_DART | LANTERN_EFFECT_LAYER_KEYS);
        fake_now = t0 = base += 2000;
        press(positions[10]);
        sum = frame(t0 + 90, &moving);
        CHECK(moving, "dart + layer keys: not moving at 90 ms");
        /* the cap may scale both together: compare the others' hue order only
         * when within the cap unscaled */
        uint32_t raw = 0;
        for (size_t i = 0; i < LED_COUNT; i++) {
            raw += in_list(vkey_keys, positions[i])
                       ? (uint32_t)(shade_of(1).r + shade_of(1).g + shade_of(1).b)
                       : (uint32_t)(dart_alone[i].r + dart_alone[i].g + dart_alone[i].b);
        }
        if (raw <= CAP) {
            check_state("dart + layer keys", vkey_keys, shade_of(1), dart_alone, sum);
        } else {
            CHECK(sum <= CAP, "dart + layer keys: sum %u > CAP", sum);
            printf("dart + layer keys: raw %u over CAP %u, compared under the cap only\n", raw,
                   (unsigned)CAP);
        }
        pick(true, LANTERN_EFFECT_LAYER_KEYS);
        frame(base += 2000, NULL);
    }

    /* the intro: its frame, not the layer keys */
    {
        CHECK(lantern_keys_boot_light(true) > 0, "boot light: no intro");
        fake_now = base += 30;
        bool m;
        uint32_t s2 = frame(base + 10, &m);
        (void)s2;
        size_t vk = 0, same_as_layer = 0;
        for (size_t i = 0; i < LED_COUNT; i++) {
            if (in_list(vkey_keys, positions[i])) {
                vk++;
                same_as_layer += same_px(strip_out[i], shade_of(1));
            }
        }
        CHECK(vk == 0 || same_as_layer < vk, "intro: the layer keys drew over the intro");
        lantern_keys_boot_light(false);
        sum = frame(base += 30, NULL);
        check_state("after the intro", vkey_keys, shade_of(1), blacks, sum);
    }

    /* the blank keeps the state (the central's), the next light shows it */
    {
        lantern_keys_blank();
        fake_now = base += 30;
        paint(NULL);
        sum = 0;
        for (size_t i = 0; i < LED_COUNT; i++) {
            sum += strip_out[i].r + strip_out[i].g + strip_out[i].b;
        }
        CHECK(sum == 0, "blank: sum %u", sum);
        lantern_keys_light();
        sum = frame(base += 30, NULL);
        check_state("light again", vkey_keys, shade_of(1), blacks, sum);
    }

    /* the cap at its worst: every key of the keymap, no palette (white) */
    {
        int all[KEY_COUNT + 1];
        for (int k = 0; k < KEY_COUNT; k++) {
            all[k] = k;
        }
        all[KEY_COUNT] = -1;
        fake_have = false;
        struct st s = state_of(all, 0);
        lantern_layer_keys_set(0, true, s.p1, s.p2);
        lantern_keys_refresh();
        sum = frame(base += 30, NULL);
        size_t n = check_state("all white", all, shade_of(0), blacks, sum);
        CHECK(n == LED_COUNT, "all white: %zu lit", n);
        printf("all keys white: sum %u of CAP %u, each %02x%02x%02x\n", sum, (unsigned)CAP,
               strip_out[0].r, strip_out[0].g, strip_out[0].b);
        palette_five();
        lantern_layer_keys_clear();
    }

    printf("layer keys: built %08x, %d submits, fails %d\n", (unsigned)EFFECTS_BUILT, submits, fails);
    return fails != 0;
}
