/* lantern_effect_next() (src/effect.h), the cycle a key runs (canon t-rgp5):
 * over the whole table, over a few bits, from several bits, from a bit outside
 * the set, and a set of one. */
#include <stdio.h>

#include "effect.h"

static int fails;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            fails++;                                                                               \
            fprintf(stderr, "FAIL: " __VA_ARGS__);                                                 \
            fputc('\n', stderr);                                                                   \
        }                                                                                          \
    } while (0)

int main(void) {
    /* the seven press effects' set, 0x7f: 0 -> 1 -> 2 -> ... -> 64 -> 1 */
    uint32_t m = 0;
    for (unsigned int i = 0; i < 7; i++) {
        m = lantern_effect_next(m, 0x7f);
        CHECK(m == 1u << i, "step %u: %08x", i, m);
    }
    CHECK(lantern_effect_next(m, 0x7f) == 1, "no wrap after the last: %08x", lantern_effect_next(m, 0x7f));
    /* a few built bits: 1 and 3 */
    CHECK(lantern_effect_next(0, 0xA) == 2, "set 1010 from none");
    CHECK(lantern_effect_next(2, 0xA) == 8, "set 1010 from bit 1");
    CHECK(lantern_effect_next(8, 0xA) == 2, "set 1010 from bit 3 wraps");
    /* a pick of several bits: after the highest */
    CHECK(lantern_effect_next(0x5, 0x7f) == 8, "from bits 0 and 2");
    /* a stored bit this device does not build: the set's first */
    CHECK(lantern_effect_next(0x80, 0x7f) == 1, "from a bit outside the set");
    CHECK(lantern_effect_next(0x4, 0xA) == 8, "from an unbuilt bit between two built");
    /* a set of one */
    CHECK(lantern_effect_next(0x10, 0x10) == 0x10, "a set of one");
    CHECK(lantern_effect_next(0, 0x10) == 0x10, "a set of one from none");
    /* a full round over the table returns to the start in LANTERN_EFFECT_COUNT steps */
    uint32_t start = LANTERN_EFFECT_DART, cur = start;
    unsigned int steps = 0;
    do {
        cur = lantern_effect_next(cur, LANTERN_EFFECT_ALL);
        steps++;
    } while (cur != start && steps < 20);
    CHECK(steps == LANTERN_EFFECT_COUNT, "a round took %u steps, not %d", steps, LANTERN_EFFECT_COUNT);
    printf("effect_next: fails %d\n", fails);
    return fails != 0;
}
