/* The key LEDs of the tree -I names (run.sh: this repository's src/, or a
 * base tree for run.sh equiv) as one translation unit (the engine, the
 * intros, the effects; the stain is the thin ring's), with the names the
 * harness programs knew from the one-file keys.c, and the intro pick's store
 * faked (fake_intro_have / fake_intro: none by default, so the Kconfig default
 * plays). A new effect or intro joins the includes below, and more besides
 * (run.sh's header lists it). */
#include "keys.c"
#include "intros/bands.c"
#include "intros/star_waves.c"
#include "effects/thin_ring.c"
#include "effects/disc.c"
#include "effects/cross_ring.c"
#include "effects/beams.c"
#include "effects/water.c"
#include "effects/dart.c"
#if defined(CONFIG_LANTERN_LAYER_KEYS)
#include "effects/layer_keys.c"
#endif
#if defined(CONFIG_LANTERN_HEATMAP)
#include "effects/heatmap.c"
#endif
#define positions lantern_positions
#define slots lantern_slots
#define coords lantern_coords
#define HIT_LIFE_MS MAX(RING_MS, MAX(DISC_MS, MAX(CROSS_MS, MAX(BEAMS_MS, MAX(WATER_MS, DART_MS)))))
#define LANTERN_INTRO_MS STAR_WAVES_MS
static bool fake_intro_have;
static uint32_t fake_intro;
bool lantern_intro_get(uint32_t *mask, uint32_t *arg) {
    if (fake_intro_have) {
        *mask = fake_intro;
        if (arg != NULL) {
            *arg = 0;
        }
    }
    return fake_intro_have;
}
/* the effects' pick's colour, for the programs' fake lantern_effect_get() */
static uint32_t fake_colour;

#if defined(CONFIG_LANTERN_TEMPO_PICK)
/* the tempo pick's store, faked: none by default, so the Kconfig default runs */
static bool fake_tempo_have;
static uint32_t fake_tempo;
bool lantern_tempo_get(uint32_t *mask, uint32_t *arg) {
    if (fake_tempo_have) {
        *mask = fake_tempo;
        if (arg != NULL) {
            *arg = 0;
        }
    }
    return fake_tempo_have;
}
#endif
