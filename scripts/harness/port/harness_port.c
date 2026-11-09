/* The palette, the picks and the port over their real sources (src/palette.c,
 * src/pick.c, src/effect.c, src/intro.c, src/tempo.c, src/effect_next.c,
 * src/port.c), each its own translation unit, linked with fake.c (canon
 * t-5tyw). Three builds (run.sh port), as the Kconfig gates leave them:
 * - a keyboard that is not a split, with key LEDs and the port: a line on the
 *   port is stored here and applied to its own LEDs (the palette's repaint,
 *   the pick's clearing of the effects), saved, asked back, refused before
 *   the settings load and for two intros or both tempo states; a key's
 *   &effect and &effect_next pick the same way, the next over what it builds;
 * - a split central without key LEDs (a dongle) and two halves: a line goes
 *   to both halves (the palette's three invocations, a pick's one) and is
 *   kept here, a half's battery report resends the palette and the stored
 *   picks to it a second later, a report of 0 none, and &effect_next cycles
 *   the whole table;
 * - a split half with key LEDs, no port: the central's invocations stored
 *   and applied, a stored value of another size ignored at the load, a mask
 *   alone read with colour 0, an empty pick refused.
 * "fails 0" is a pass. */
#include "effect.h"
#include "fake.h"
#include "intro.h"
#include "palette.h"
#include "tempo.h"

static int checks, fails;
#define CHECK(cond, ...)                                                                           \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(cond)) {                                                                             \
            fails++;                                                                               \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)
#define REPLY(line, want)                                                                          \
    do {                                                                                           \
        fake_line(line);                                                                           \
        CHECK(strcmp(fake_reply(), want) == 0, "%s: replied \"%s\", not \"%s\"", line,             \
              fake_reply(), want);                                                                 \
    } while (0)

/* The value saved under key: len bytes equal to want. */
static bool saved(const char *key, const void *want, size_t len) {
    size_t n = 0;
    const void *v = fake_stored(key, &n);
    return v != NULL && n == len && memcmp(v, want, len) == 0;
}

static bool palette_is(uint32_t a, uint32_t b, uint32_t c) {
    uint32_t p[LANTERN_PALETTE_SLOTS];
    return lantern_palette_get(p) && p[0] == a && p[1] == b && p[2] == c && p[3] == 0 &&
           p[4] == 0 && p[5] == 0;
}

static bool effect_is(uint32_t mask, uint32_t colour) {
    uint32_t m = 0, c = 0;
    return lantern_effect_get(&m, &c) && m == mask && c == colour;
}

#if !IS_ENABLED(CONFIG_ZMK_SPLIT)

#define BUILD "a keyboard that is not a split"

static void scenario(void) {
    fake_boot();
    REPLY("palette FF0000", "palette error: not ready: the settings have not loaded yet");
    REPLY("effect dart", "effect error: not ready: the settings have not loaded yet");
    uint32_t p[LANTERN_PALETTE_SLOTS];
    CHECK(!lantern_palette_get(p) && fake_saves == 0, "nothing stored before the load");

    fake_load();
    REPLY("palette", "palette none");
    REPLY("effect?", "effect none");

    int refreshes = fake_refreshes;
    REPLY("palette FF0000 00ff00", "palette ok FF0000 00FF00 000000 000000 000000 000000");
    CHECK(palette_is(0xFF0000, 0x00FF00, 0), "the palette stored here");
    const uint32_t pal[LANTERN_PALETTE_SLOTS] = {0xFF0000, 0x00FF00};
    CHECK(saved("lantern/palette/colours", pal, sizeof(pal)), "the palette saved");
    CHECK(fake_refreshes > refreshes, "the palette repainted this keyboard's LEDs");
    int saves = fake_saves;
    refreshes = fake_refreshes;
    REPLY("palette FF0000 00FF00", "palette ok FF0000 00FF00 000000 000000 000000 000000");
    CHECK(fake_saves == saves && fake_refreshes == refreshes,
          "an unchanged palette neither saved nor repainted");
    REPLY("palette ok FF0000", "");

    int changes = fake_effect_changes;
    REPLY("effect stain 00ff00", "effect ok stain 00FF00");
    CHECK(effect_is(LANTERN_EFFECT_STAIN, 0x00FF00), "the effects' pick stored here");
    const uint32_t stain[2] = {LANTERN_EFFECT_STAIN, 0x00FF00};
    CHECK(saved("lantern/effect/mask", stain, sizeof(stain)), "the effects' pick saved");
    CHECK(fake_effect_changes > changes, "the pick cleared this keyboard's effects");
    REPLY("effect", "effect stain 00FF00");

    uint32_t m = 0;
    REPLY("intro bands", "intro ok bands");
    REPLY("intro bands star-waves", "intro error: one intro at a time");
    CHECK(lantern_intro_get(&m, NULL) && m == LANTERN_INTRO_BANDS, "the intro's pick stored here");
    REPLY("tempo off", "tempo ok off");
    REPLY("tempo on off", "tempo error: on or off, not both");
    CHECK(lantern_tempo_get(&m, NULL) && m == LANTERN_TEMPO_OFF, "the tempo's pick stored here");

    /* the keys: &effect_next over what this keyboard builds, the colour
     * kept; the last built wraps to the first; &effect <mask> <colour> */
    changes = fake_effect_changes;
    fake_press("effect_next", 0, 0);
    CHECK(effect_is(LANTERN_EFFECT_CROSS_RING, 0x00FF00),
          "&effect_next: the stain to the crossing ring");
    CHECK(fake_effect_changes > changes, "&effect_next cleared this keyboard's effects");
    fake_press("effect", LANTERN_EFFECT_HEATMAP, 0);
    fake_press("effect_next", 0, 0);
    CHECK(effect_is(LANTERN_EFFECT_THIN_RING, 0),
          "&effect_next: the heatmap round to the thin ring");
    fake_press("effect", LANTERN_EFFECT_DART, 0xFF0000);
    CHECK(effect_is(LANTERN_EFFECT_DART, 0xFF0000), "&effect: the dart in red");
    REPLY("effect", "effect dart FF0000");

    CHECK(fake_warnings == 0 && fake_errors == 0, "%d warning(s), %d error(s)", fake_warnings,
          fake_errors);
}

#elif IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)

#define BUILD "a split central"

/* The sends from index from on are n invocations of behavior, to each half
 * in turn when source is -1, else all to source. */
static bool sends(int from, int n, const char *behavior, int source) {
    if (fake_nsent - from != n) {
        return false;
    }
    for (int i = from; i < fake_nsent; i++) {
        int want = source >= 0 ? source : (i - from) % ZMK_SPLIT_CENTRAL_PERIPHERAL_COUNT;
        if (strcmp(fake_sent[i].behavior, behavior) != 0 || fake_sent[i].source != want) {
            return false;
        }
    }
    return true;
}

static void scenario(void) {
    fake_boot();
    fake_load();
    REPLY("palette FF0000 00FF00 0000FF", "palette ok FF0000 00FF00 0000FF 000000 000000 000000");
    CHECK(sends(0, 6, "palette", -1), "the palette: three pairs to each half, %d sent", fake_nsent);
    CHECK(fake_sent[0].param1 == 0xFF0000 && fake_sent[0].param2 == 0x00FF00 &&
              fake_sent[2].param1 == ((1u << 24) | 0x0000FF) && fake_sent[4].param1 == (2u << 24),
          "the pairs' packing");
    CHECK(palette_is(0xFF0000, 0x00FF00, 0x0000FF), "the palette kept here too");
    int at = fake_nsent;
    REPLY("effect dart", "effect ok dart");
    CHECK(sends(at, 2, "effect", -1) && fake_sent[at].param1 == LANTERN_EFFECT_DART &&
              fake_sent[at].param2 == 0,
          "the effects' pick to each half");

    /* a half that is back: the palette and the stored pick a second after
     * its report, nothing for the picks never made */
    at = fake_nsent;
    fake_battery(1, 80);
    fake_advance(999);
    CHECK(fake_nsent == at, "nothing resent before the second");
    fake_advance(1);
    bool to_one = fake_nsent - at == 4;
    int palettes = 0, effects = 0;
    for (int i = at; i < fake_nsent; i++) {
        to_one = to_one && fake_sent[i].source == 1;
        palettes += strcmp(fake_sent[i].behavior, "palette") == 0;
        effects += strcmp(fake_sent[i].behavior, "effect") == 0;
    }
    CHECK(to_one, "the resend: %d sent, all to half 1", fake_nsent - at);
    CHECK(palettes == 3 && effects == 1, "the resend: the palette's three and the effect's one");
    at = fake_nsent;
    fake_battery(0, 0);
    fake_advance(2000);
    CHECK(fake_nsent == at, "no resend after a report of 0");

    /* &effect_next on a dongle, which builds no effect: the whole table,
     * the dart to the layer keys */
    fake_press("effect_next", 0, 0);
    CHECK(effect_is(LANTERN_EFFECT_LAYER_KEYS, 0) && sends(at, 2, "effect", -1) &&
              fake_sent[at].param1 == LANTERN_EFFECT_LAYER_KEYS,
          "&effect_next: the dart to the layer keys, to each half");
    CHECK(fake_warnings == 0 && fake_errors == 0, "%d warning(s), %d error(s)", fake_warnings,
          fake_errors);
}

#else

#define BUILD "a split half"

static void scenario(void) {
    const uint32_t four[4] = {1, 2, 3, 4};
    const uint32_t disc = LANTERN_EFFECT_DISC;
    fake_store("lantern/palette/colours", four, sizeof(four));
    fake_store("lantern/effect/mask", &disc, sizeof(disc));
    fake_boot();
    fake_load();
    uint32_t p[LANTERN_PALETTE_SLOTS];
    CHECK(!lantern_palette_get(p) && fake_warnings == 1, "a palette of four slots ignored");
    CHECK(effect_is(LANTERN_EFFECT_DISC, 0), "a mask alone read with colour 0");

    int refreshes = fake_refreshes;
    fake_receive("palette", 0x112233, 0x445566);
    fake_receive("palette", (1u << 24) | 0x778899, 0);
    fake_receive("palette", 2u << 24, 0);
    CHECK(palette_is(0x112233, 0x445566, 0x778899), "the central's palette stored");
    const uint32_t pal[LANTERN_PALETTE_SLOTS] = {0x112233, 0x445566, 0x778899};
    CHECK(saved("lantern/palette/colours", pal, sizeof(pal)), "the palette saved");
    CHECK(fake_refreshes > refreshes, "the palette repainted the half's LEDs");

    int changes = fake_effect_changes;
    CHECK(fake_receive("effect", LANTERN_EFFECT_THIN_RING, 0xABCDEF) == ZMK_BEHAVIOR_OPAQUE &&
              effect_is(LANTERN_EFFECT_THIN_RING, 0xABCDEF) && fake_effect_changes > changes,
          "the central's pick stored and applied");
    int saves = fake_saves;
    CHECK(fake_receive("effect", 0, 0) < 0 && fake_saves == saves &&
              effect_is(LANTERN_EFFECT_THIN_RING, 0xABCDEF) && fake_warnings == 2,
          "an empty pick refused");
    CHECK(fake_errors == 0, "%d error(s)", fake_errors);
}

#endif

int main(void) {
    scenario();
    printf("port, %s: %d checks, fails %d\n", BUILD, checks, fails);
    return fails != 0;
}
