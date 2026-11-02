/* The engine of the tree -I names (run.sh: this repository's src/) under one
 * Kconfig combination (-D flags): the intros and the effects only where their
 * symbols are set, as CMake does. Compiled -fsyntax-only (run.sh combos). */
#include "keys.c"
#if IS_ENABLED(CONFIG_LANTERN_INTRO_BANDS)
#include "intros/bands.c"
#endif
#if IS_ENABLED(CONFIG_LANTERN_INTRO_STAR_WAVES)
#include "intros/star_waves.c"
#endif
#if IS_ENABLED(CONFIG_LANTERN_THIN_RING)
#include "effects/thin_ring.c"
#endif
#if IS_ENABLED(CONFIG_LANTERN_DISC)
#include "effects/disc.c"
#endif
#if IS_ENABLED(CONFIG_LANTERN_CROSS_RING)
#include "effects/cross_ring.c"
#endif
#if IS_ENABLED(CONFIG_LANTERN_BEAMS)
#include "effects/beams.c"
#endif
#if IS_ENABLED(CONFIG_LANTERN_WATER)
#include "effects/water.c"
#endif
#if IS_ENABLED(CONFIG_LANTERN_DART)
#include "effects/dart.c"
#endif
#if IS_ENABLED(CONFIG_LANTERN_HEATMAP)
#include "effects/heatmap.c"
#endif
#if IS_ENABLED(CONFIG_LANTERN_LAYER_KEYS)
#include "effects/layer_keys.c"
#endif
#if IS_ENABLED(CONFIG_LANTERN_EFFECT)
bool lantern_effect_get(uint32_t *mask, uint32_t *colour) { (void)mask; (void)colour; return false; }
#endif
#if IS_ENABLED(CONFIG_LANTERN_INTRO)
bool lantern_intro_get(uint32_t *mask, uint32_t *arg) { (void)mask; (void)arg; return false; }
#endif
int main(void) {
    (void)lantern_keys_boot_light(true);
    lantern_keys_light();
    paint(NULL);
    (void)lantern_keys_boot_light(false);
    paint(NULL);
    lantern_keys_blank();
    paint(NULL);
    return 0;
}
