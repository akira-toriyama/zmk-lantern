/* src/effects/layer_keys.c alone on a 76-key keymap (two chunks; the module's
 * stock Imprint halves): staging, the commit at the marked chunk only, the
 * draw of keys in both chunks, a central whose keymap counts one chunk, the
 * clear. Built by run.sh lk76 over the shared stubs. "fails 0" is a pass. */
#undef ZMK_KEYMAP_LEN
#define ZMK_KEYMAP_LEN 76
#include "effects/layer_keys.c"

/* 29 LEDs over keys of both chunks: 0..28 is not a split's half, but every
 * key under an LED is what the draw reads */
const uint8_t lantern_positions[29] = {0,  1,  31, 32, 33, 40, 50, 59, 60, 61, 62, 63, 70, 75, 5,
                                       6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19};
const uint8_t lantern_slots[76];

static int refreshes;
void lantern_keys_refresh(void) { refreshes++; }

static int fails;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            fails++;                                                                               \
            fprintf(stderr, "FAIL: " __VA_ARGS__);                                                 \
            fputc('\n', stderr);                                                                   \
        }                                                                                          \
    } while (0)

static struct shades sh;
static struct led_rgb px[29];

static int draw_lit(void) {
    memset(px, 0, sizeof(px));
    lantern_effect_layer_keys.draw(px, &sh, NULL, 0, 0);
    int n = 0;
    for (size_t i = 0; i < 29; i++) {
        n += px[i].r != 0 || px[i].g != 0 || px[i].b != 0;
    }
    return n;
}

static bool lit(uint8_t key) {
    for (size_t i = 0; i < 29; i++) {
        if (lantern_positions[i] == key) {
            return px[i].r != 0 || px[i].g != 0 || px[i].b != 0;
        }
    }
    return false;
}

int main(void) {
    const uint32_t colours[6] = {0xFF0000, 0x00FF00, 0x0000FF, 0xFFFF00, 0xFF00FF, 0x00FFFF};
    sh.n = 6;
    for (size_t s = 0; s < 6; s++) {
        sh.rgb[s] = colours[s];
        sh.at_level[s] = scale(colours[s], PICTURE_LEVEL);
        sh.rank[s] = (uint8_t)s;
    }
    CHECK(LANTERN_LAYER_CHUNKS == 2 && LANTERN_LAYER_WORDS == 4, "chunks %u", (unsigned)LANTERN_LAYER_CHUNKS);
    CHECK(lantern_effect_layer_keys.life() == 0, "life");

    /* keys 0, 31 (chunk 0, param1), 32, 60 (chunk 0, param2), 61, 75 (chunk 1), slot 2 */
    uint32_t slot = 2u << LANTERN_LAYER_SLOT_SHIFT;
    uint32_t c0p1 = (1u << 0) | (1u << 31), c0p2 = (1u << 0) | (1u << 28) | slot;
    uint32_t c1p1 = (1u << 0) | (1u << 14), c1p2 = slot;
    lantern_layer_keys_set(0, false, c0p1, c0p2);
    CHECK(refreshes == 0 && draw_lit() == 0, "chunk 0 unmarked: drawn or repainted");
    lantern_layer_keys_set(1, true, c1p1, c1p2);
    CHECK(refreshes == 1, "the marked chunk: %d repaints", refreshes);
    CHECK(draw_lit() == 6 && lit(0) && lit(31) && lit(32) && lit(60) && lit(61) && lit(75) && !lit(1) &&
              !lit(70) && !lit(33),
          "two chunks: the wrong keys lit");
    CHECK(px[0].r == 0 && px[0].g == 0 && px[0].b == 255, "slot 2's shade: %02x%02x%02x", px[0].r, px[0].g,
          px[0].b);
    /* the same state again: no repaint */
    lantern_layer_keys_set(0, false, c0p1, c0p2);
    lantern_layer_keys_set(1, true, c1p1, c1p2);
    CHECK(refreshes == 1, "the same state: repainted");
    /* a central counting one chunk (a shorter keymap): its marked chunk 0
     * commits, chunk 1 as this half last had it */
    lantern_layer_keys_clear();
    CHECK(refreshes == 2 && draw_lit() == 0, "clear: %d repaints, lit", refreshes);
    lantern_layer_keys_set(0, true, (1u << 5), slot);
    CHECK(refreshes == 3 && draw_lit() == 1 && lit(5), "one chunk from the central: not taken whole");
    /* a chunk past this keymap (a longer central's), marked: passed over, committed */
    lantern_layer_keys_set(2, true, 0xFFFFFFFFu, slot);
    CHECK(draw_lit() == 1 && lit(5), "chunk 2: stored");
    /* a bad slot: dropped whole */
    lantern_layer_keys_set(0, true, 0xFFFFFFFFu, 6u << LANTERN_LAYER_SLOT_SHIFT);
    CHECK(draw_lit() == 1 && lit(5), "slot 6: stored");
    printf("layer keys, 76 keys: %d repaints, fails %d\n", refreshes, fails);
    return fails != 0;
}
