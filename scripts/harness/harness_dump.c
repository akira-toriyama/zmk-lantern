/* Dumps every frame of a fixed script as CSV, for comparing two trees of the
 * key LEDs frame for frame (t-pmfn: the one-file keys.c against the split):
 * the intro (three seeds, 1 ms frames, two presses meanwhile), then each
 * pick of the effects over a palette of five colours, one colour and none,
 * with this half's presses, the other half's relayed in, a stage move, a
 * pick change mid-ring and a blank. HARNESS_SRC names the tree. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include HARNESS_SRC

static bool fake_pick_have;
static uint32_t fake_pick;
#ifdef LANTERN_EFFECT_COLOUR_MAX
bool lantern_effect_get(uint32_t *mask, uint32_t *colour) {
    if (fake_pick_have) {
        *mask = fake_pick;
        if (colour != NULL) {
            *colour = fake_colour;
        }
    }
    return fake_pick_have;
}
#else
bool lantern_effect_get(uint32_t *mask) {
    if (fake_pick_have) {
        *mask = fake_pick;
    }
    return fake_pick_have;
}
#endif

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

struct run {
    const char *tag;
    bool have;
    uint32_t mask;
};

int main(int argc, char **argv) {
    out = argc > 1 ? fopen(argv[1], "w") : stdout;

    /* the intro, three seeds */
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

    /* the effects */
    static const struct run runs[] = {
        {"ring", true, LANTERN_EFFECT_THIN_RING},
        {"disc", true, LANTERN_EFFECT_DISC},
        {"stain", true, LANTERN_EFFECT_STAIN},
        {"cross", true, LANTERN_EFFECT_CROSS_RING},
        {"beams", true, LANTERN_EFFECT_BEAMS},
        {"water", true, LANTERN_EFFECT_WATER},
        {"dart", true, LANTERN_EFFECT_DART},
        {"ring+stain", true, LANTERN_EFFECT_THIN_RING | LANTERN_EFFECT_STAIN},
        {"ring+disc", true, LANTERN_EFFECT_THIN_RING | LANTERN_EFFECT_DISC},
        {"stain+disc", true, LANTERN_EFFECT_STAIN | LANTERN_EFFECT_DISC},
        {"all", true, 0x7f},
        {"nopick", false, 0},
        {"unbuilt", true, BIT(7)},
        {"cross+dart+water", true, LANTERN_EFFECT_CROSS_RING | LANTERN_EFFECT_DART | LANTERN_EFFECT_WATER},
    };
    uint32_t base = 80000;
    fake_have = true;
    fake_palette[0] = 0xFF0000;
    fake_palette[1] = 0x00FF00;
    fake_palette[2] = 0x0000FF;
    fake_palette[3] = 0xFFFF00;
    fake_palette[4] = 0x00FFFF;
    fake_palette[5] = 0;
    fake_now = base;
    lantern_keys_light();
    paint(NULL);
    for (size_t r = 0; r < sizeof(runs) / sizeof(runs[0]); r++) {
        char tag[48];
        snprintf(tag, sizeof(tag), "p5-%s", runs[r].tag);
        base += 5000;
        script(tag, base, runs[r].have, runs[r].mask);
    }
    /* one colour */
    fake_palette[1] = fake_palette[2] = fake_palette[3] = fake_palette[4] = 0;
    for (size_t r = 0; r < 11; r++) {
        char tag[48];
        snprintf(tag, sizeof(tag), "p1-%s", runs[r].tag);
        base += 5000;
        script(tag, base, runs[r].have, runs[r].mask);
    }
    /* no palette: white */
    fake_have = false;
    for (size_t r = 0; r < 11; r++) {
        char tag[48];
        snprintf(tag, sizeof(tag), "p0-%s", runs[r].tag);
        base += 5000;
        script(tag, base, runs[r].have, runs[r].mask);
    }
    fprintf(stderr, "%lu frames\n", frames);
    if (out != stdout) {
        fclose(out);
    }
    return 0;
}
