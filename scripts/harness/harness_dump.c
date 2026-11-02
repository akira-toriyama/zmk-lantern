/* Dumps every frame of a fixed script as CSV, for comparing two trees of the
 * key LEDs frame for frame (run.sh equiv). run.sh builds it with canon's
 * halves' flags (every effect, both intros, the tempo, the no picture), so a
 * tree it compares has all of them: aeec40c (the tempo) or later. The script:
 * - the intro: the star waves (the default) at three seeds, then the bands
 *   picked, 1 ms frames, presses meanwhile;
 * - each pick of the effects, the heatmap and the layer keys among them (over
 *   one layer state set before the picks), over a palette of five colours,
 *   one colour and none, with this half's presses, the other half's relayed
 *   in, a stage move, a pick change mid-ring and a blank;
 * - some of the picks again with the pick's colour;
 * - a burst of presses at eight a second under the crossing ring, with the
 *   tempo not picked (the Kconfig default), picked off and picked on.
 * HARNESS_SRC names the tree. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include HARNESS_SRC

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

static FILE *out;
static unsigned long frames;

static void press(uint32_t position) {
    static struct zmk_position_state_changed ev;
    ev = (struct zmk_position_state_changed){
        .source = ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL, .position = position, .state = true};
    fake_event = &ev;
    zmk_event_t e = {0};
    key_listener(&e);
    fake_event = NULL;
}

static void frame_at(const char *tag, uint32_t t, size_t at) {
    fake_now = t;
    bool m = compose(at);
    led_strip_update_rgb(strip, pixels, LED_COUNT);
    for (size_t i = 0; i < LED_COUNT; i++) {
        fprintf(out, "%s,%u,%zu,%u,%u,%u,%d\n", tag, t, i, strip_out[i].r, strip_out[i].g,
                strip_out[i].b, m);
    }
    frames++;
}

static void frame(const char *tag, uint32_t t) { frame_at(tag, t, 0); }

static void pick(bool have, uint32_t mask) {
    fake_pick_have = have;
    fake_pick = mask;
    lantern_keys_effect_changed();
}

#ifdef HALF_LEFT
static const uint32_t own_a = 17, own_b = 5, own_thumb = 46, other = 18, other_thumb = 51;
#else
static const uint32_t own_a = 18, own_b = 6, own_thumb = 51, other = 17, other_thumb = 46;
#endif

/* One pick's script from base: presses and relayed hits, frames every 5 ms
 * to 2600, a stage move and back, a press then a change to the disc
 * mid-ring, and a blank. */
static void script(const char *tag, uint32_t base, bool have, uint32_t mask) {
    char t2[64];
    pick(have, mask);
    for (uint32_t t = 0; t <= 2600; t += 5) {
        uint32_t now = base + t;
        fake_now = now;
        if (t == 0) press(own_a);
        if (t == 100) lantern_keys_hit(other);
        if (t == 150) press(own_b);
        if (t == 400) press(own_thumb);
        if (t == 700) lantern_keys_hit(other_thumb);
        if (t == 1300) lantern_keys_hit(other);
        frame(tag, now);
    }
    snprintf(t2, sizeof(t2), "%s-stage", tag);
    frame_at(t2, base + 2605, 1);
    frame_at(t2, base + 2610, 1);
    frame_at(t2, base + 2615, 2);
    frame_at(t2, base + 2620, 0);
    snprintf(t2, sizeof(t2), "%s-repick", tag);
    fake_now = base + 2700;
    press(own_b);
    for (uint32_t t = 2700; t <= 2900; t += 5) {
        frame(t2, base + t);
    }
    pick(true, LANTERN_EFFECT_DISC);
    for (uint32_t t = 2905; t <= 3100; t += 5) {
        frame(t2, base + t);
    }
    snprintf(t2, sizeof(t2), "%s-blank", tag);
    fake_now = base + 3105;
    lantern_keys_blank();
    paint(NULL);
    for (size_t i = 0; i < LED_COUNT; i++) {
        fprintf(out, "%s,%u,%zu,%u,%u,%u,-\n", t2, (unsigned)fake_now, i, strip_out[i].r,
                strip_out[i].g, strip_out[i].b);
    }
    fake_now = base + 3110;
    lantern_keys_light();
    paint(NULL);
}

/* The tempo's script from base: the crossing ring, presses at eight a second
 * for two seconds, this half's and the other's in turn, then frames every
 * 5 ms to 4000 while the tempo eases back. */
static void burst(const char *tag, uint32_t base) {
    pick(true, LANTERN_EFFECT_CROSS_RING);
    for (uint32_t t = 0; t <= 4000; t += 5) {
        uint32_t now = base + t;
        fake_now = now;
        if (t <= 2000 && t % 125 == 0) {
            if ((t / 125) % 2 == 0) {
                press((t / 125) % 4 == 0 ? own_a : own_b);
            } else {
                lantern_keys_hit((t / 125) % 4 == 1 ? other : other_thumb);
            }
        }
        frame(tag, now);
    }
    fake_now = base + 4005;
    lantern_keys_blank();
    paint(NULL);
    fake_now = base + 4010;
    lantern_keys_light();
    paint(NULL);
}

struct run {
    const char *tag;
    bool have;
    uint32_t mask;
};

static const struct run runs[] = {
    {"ring", true, LANTERN_EFFECT_THIN_RING},
    {"disc", true, LANTERN_EFFECT_DISC},
    {"stain", true, LANTERN_EFFECT_STAIN},
    {"cross", true, LANTERN_EFFECT_CROSS_RING},
    {"beams", true, LANTERN_EFFECT_BEAMS},
    {"water", true, LANTERN_EFFECT_WATER},
    {"dart", true, LANTERN_EFFECT_DART},
    {"heatmap", true, LANTERN_EFFECT_HEATMAP},
    {"layer-keys", true, LANTERN_EFFECT_LAYER_KEYS},
    {"ring+stain", true, LANTERN_EFFECT_THIN_RING | LANTERN_EFFECT_STAIN},
    {"ring+disc", true, LANTERN_EFFECT_THIN_RING | LANTERN_EFFECT_DISC},
    {"stain+disc", true, LANTERN_EFFECT_STAIN | LANTERN_EFFECT_DISC},
    {"dart+layer-keys", true, LANTERN_EFFECT_DART | LANTERN_EFFECT_LAYER_KEYS},
    {"seven", true, 0x7f},
    {"every", true, LANTERN_EFFECT_ALL},
    {"nopick", false, 0},
    {"unbuilt", true, BIT(7)},
    {"cross+dart+water", true, LANTERN_EFFECT_CROSS_RING | LANTERN_EFFECT_DART | LANTERN_EFFECT_WATER},
};

/* The picks run again with the pick's colour. */
static const struct run coloured[] = {
    {"ring+stain", true, LANTERN_EFFECT_THIN_RING | LANTERN_EFFECT_STAIN},
    {"cross", true, LANTERN_EFFECT_CROSS_RING},
    {"water", true, LANTERN_EFFECT_WATER},
    {"dart", true, LANTERN_EFFECT_DART},
    {"heatmap", true, LANTERN_EFFECT_HEATMAP},
    {"layer-keys", true, LANTERN_EFFECT_LAYER_KEYS},
    {"every", true, LANTERN_EFFECT_ALL},
};

static void palette_five(void) {
    fake_have = true;
    fake_palette[0] = 0xFF0000;
    fake_palette[1] = 0x00FF00;
    fake_palette[2] = 0x0000FF;
    fake_palette[3] = 0xFFFF00;
    fake_palette[4] = 0x00FFFF;
    fake_palette[5] = 0;
}

static uint32_t all_runs(const char *prefix, const struct run *r, size_t n, uint32_t base) {
    for (size_t i = 0; i < n; i++) {
        char tag[48];
        snprintf(tag, sizeof(tag), "%s-%s", prefix, r[i].tag);
        base += 5000;
        script(tag, base, r[i].have, r[i].mask);
    }
    return base;
}

int main(int argc, char **argv) {
    out = argc > 1 ? fopen(argv[1], "w") : stdout;

    /* the intro: the default (the star waves) at three seeds */
    const uint32_t seeds[3] = {0x1234567u, 0xa3cbd250u, 0xdeadbeefu};
    for (size_t s = 0; s < 3; s++) {
        char tag[32];
        snprintf(tag, sizeof(tag), "intro%zu", s);
        uint32_t base = 5000 + (uint32_t)s * 20000;
        fake_cycles = seeds[s];
        fake_now = base;
        lantern_keys_boot_light(true);
        lantern_keys_light();
        for (uint32_t t = 0; t <= LANTERN_INTRO_MS + 60; t += 1) {
            fake_now = base + t;
            if (t == 2000 || t == 4000) {
                press(positions[3]);
            }
            frame(tag, base + t);
        }
        lantern_keys_boot_light(false);
        frame(tag, base + LANTERN_INTRO_MS + 100);
        lantern_keys_blank();
        paint(NULL);
    }
    /* the bands, picked, over the five colours (without a palette every band
     * is white, and a band's colour would not show) */
    {
        uint32_t base = 65000;
        palette_five();
        fake_intro_have = true;
        fake_intro = LANTERN_INTRO_BANDS;
        fake_now = base;
        uint32_t ms = lantern_keys_boot_light(true);
        lantern_keys_light();
        for (uint32_t t = 0; t <= ms + 60; t += 1) {
            fake_now = base + t;
            if (t == 300) {
                press(positions[3]);
            }
            frame("intro-bands", base + t);
        }
        lantern_keys_boot_light(false);
        frame("intro-bands", base + ms + 100);
        lantern_keys_blank();
        paint(NULL);
        fake_intro_have = false;
        fake_have = false;
    }

    /* the effects: one layer state for the layer keys, keys on both halves */
    uint32_t base = 80000;
    palette_five();
    fake_now = base;
    lantern_keys_light();
    paint(NULL);
    lantern_layer_keys_set(0, true, BIT(1) | BIT(8) | BIT(14) | BIT(20) | BIT(27),
                           BIT(33 - 32) | BIT(40 - 32) | BIT(44 - 32) | BIT(57 - 32) |
                               (2u << LANTERN_LAYER_SLOT_SHIFT));
    base = all_runs("p5", runs, ARRAY_SIZE(runs), base);
    /* one colour */
    fake_palette[1] = fake_palette[2] = fake_palette[3] = fake_palette[4] = 0;
    base = all_runs("p1", runs, ARRAY_SIZE(runs), base);
    /* no palette: white */
    fake_have = false;
    base = all_runs("p0", runs, ARRAY_SIZE(runs), base);
    /* the pick's colour over the five */
    palette_five();
    fake_colour = 0xFF8000;
    base = all_runs("pc", coloured, ARRAY_SIZE(coloured), base);
    fake_colour = 0;

    /* the tempo: not picked (the default), off, on */
    base += 5000;
    burst("tempo-default", base);
    fake_tempo_have = true;
    fake_tempo = LANTERN_TEMPO_OFF;
    base += 5000;
    burst("tempo-off", base);
    fake_tempo = LANTERN_TEMPO_ON;
    base += 5000;
    burst("tempo-on", base);
    fake_tempo_have = false;

    fprintf(stderr, "%lu frames\n", frames);
    if (out != stdout) {
        fclose(out);
    }
    return 0;
}
