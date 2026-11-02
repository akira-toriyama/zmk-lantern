/* Runs the real src/layer.c on the host (canon t-6ttk, the layer keys' first trial):
 * the central over canon's keymap (generated from the Imprint Dongle's
 * zephyr.dts) or a synthetic 76-key keymap (-DSYNTH76: two chunks, a &trans,
 * a reordered keymap, the default layer named with -DLIST_DEFAULT), and the
 * half's handler. Every send is recorded; the work item follows Zephyr's
 * kernel/work.c flags (cstub_central.h), run when the harness says the relay's
 * thread runs. "fails 0" is a pass. */
#include "layer.c"

#ifndef SYNTH76
#include "canon_keymap.h"
/* a test's hook, run as key walk_hook_at's binding is read: a thread that
 * preempts the relay's run in its walk of the keymap */
static int walk_hook_at;
static void (*walk_hook)(void);
static const struct zmk_behavior_binding *fake_binding(zmk_keymap_layer_id_t l, uint16_t k) {
    if (walk_hook != NULL && k == walk_hook_at) {
        void (*hook)(void) = walk_hook;
        walk_hook = NULL;
        hook();
    }
    return &canon_keymap[l][k];
}
#else
/* 76 keys, 4 layers: layer 0 the default, every key a key press of its own
 * code; layer 1 &none everywhere but keys 0, 31, 32, 60, 61 and 75, and key
 * 5 the default's binding again; layer 2 &trans everywhere but key 70; layer
 * 3 the default's bindings everywhere (nothing of its own, not listed) */
static struct zmk_behavior_binding synth[4][76];
static const struct zmk_behavior_binding *fake_binding(zmk_keymap_layer_id_t l, uint16_t k) {
    return &synth[l][k];
}
static void synth_init(void) {
    for (int k = 0; k < 76; k++) {
        synth[0][k] = (struct zmk_behavior_binding){"key_press", 0x70004 + k, 0};
        synth[1][k] = (struct zmk_behavior_binding){"none", 0, 0};
        synth[2][k] = (struct zmk_behavior_binding){"transparent", 0, 0};
        synth[3][k] = synth[0][k];
    }
    const int own[] = {0, 31, 32, 60, 61, 75};
    for (size_t i = 0; i < ARRAY_SIZE(own); i++) {
        synth[1][own[i]] = (struct zmk_behavior_binding){"key_press", 0x7003a + own[i], 0};
    }
    synth[1][5] = synth[0][5];
    synth[2][70] = (struct zmk_behavior_binding){"momentary_layer", 3, 0};
}
#endif

/* the relay, faked */
static uint32_t fake_lit = 0x3;
static struct k_work_q fake_relay_q;
struct k_work_q *lantern_relay_queue(void) { return &fake_relay_q; }
/* a test's hook, run as the lit halves are read: right after the run's look
 * for a pending wait in round 1's order, after its read of the state in
 * round 2's */
static void (*fake_before_lit)(void);
uint32_t lantern_relay_lit(void) {
    if (fake_before_lit != NULL) {
        void (*hook)(void) = fake_before_lit;
        fake_before_lit = NULL;
        hook();
    }
    return fake_lit;
}
int lantern_relay_slot(struct bt_conn *conn) { return conn->slot >= 0 ? conn->slot : -EINVAL; }

#if IS_ENABLED(CONFIG_LANTERN_LAYER_KEYS)
/* the layer keys, faked as src/effects/layer_keys.c keeps them: what this
 * device was handed */
static uint32_t local_staged[LANTERN_LAYER_WORDS], local[LANTERN_LAYER_WORDS];
static int local_sets, local_commits, local_clears;
void lantern_layer_keys_set(uint32_t chunk, bool last, uint32_t param1, uint32_t param2) {
    local_sets++;
    if (chunk < LANTERN_LAYER_CHUNKS) {
        local_staged[2 * chunk] = param1;
        local_staged[2 * chunk + 1] = param2;
    }
    if (last) {
        memcpy(local, local_staged, sizeof(local));
        local_commits++;
    }
}
void lantern_layer_keys_clear(void) {
    local_clears++;
    memset(local_staged, 0, sizeof(local_staged));
    memset(local, 0, sizeof(local));
}
#endif

static int fails;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            fails++;                                                                               \
            fprintf(stderr, "FAIL: " __VA_ARGS__);                                                 \
            fputc('\n', stderr);                                                                   \
        }                                                                                          \
    } while (0)

#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)

/* A layer change at now: the listener, as ZMK raises it. */
static void change(int64_t now, uint8_t layer, bool on) {
    fake_now = now;
    if (on) {
        fake_layers |= 1u << layer;
    } else {
        fake_layers &= ~(1u << layer);
    }
    fake_layer_ev = (struct zmk_layer_state_changed){.layer = layer, .state = on};
    zmk_event_t e = {.kind = 1};
    layer_listener(&e);
}

/* The relay's thread gets the CPU at now: true if it ran anything. */
static bool run(int64_t now) { return fake_queue_run(&fake_relay_q, now) > 0; }

static bool pending(void) { return (state_work.work.flags & (K_WORK_QUEUED | K_WORK_DELAYED)) != 0; }

/* The keys a list names, as a set of positions. */
static bool listed(const int *keys, int k) {
    for (; *keys >= 0; keys++) {
        if (*keys == k) {
            return true;
        }
    }
    return false;
}

/* The sends from first on to slot s, decoded: the keys and the slot, all
 * chunks of one state in order, the last marked; returns how many states. */
static int decode(int first, uint8_t s, uint32_t w[LANTERN_LAYER_WORDS]) {
    int states = 0;
    uint32_t next = 0;

    memset(w, 0, sizeof(uint32_t) * LANTERN_LAYER_WORDS);
    for (int i = first; i < nsends; i++) {
        if (sends[i].slot != s) {
            continue;
        }
        uint32_t want = next | (next == LANTERN_LAYER_CHUNKS - 1 ? LANTERN_LAYER_LAST : 0);
        CHECK(strcmp(sends[i].dev, "layer") == 0 && sends[i].state, "send %d: %s state %d", i,
              sends[i].dev, sends[i].state);
        CHECK(sends[i].position == want, "send %d to %u: position %02x, %02x expected", i, s,
              sends[i].position, want);
        uint32_t c = sends[i].position & LANTERN_LAYER_CHUNK_MASK;
        if (c < LANTERN_LAYER_CHUNKS) {
            w[2 * c] = sends[i].param1;
            w[2 * c + 1] = sends[i].param2;
        }
        next = (next + 1) % LANTERN_LAYER_CHUNKS;
        if (next == 0) {
            states++;
        }
    }
    CHECK(next == 0, "slot %u: a state's chunks cut short", s);
    return states;
}

/* The state in w against keys (a list ending in -1) and slot; none for keys
 * NULL. */
static void expect(const char *name, const uint32_t w[LANTERN_LAYER_WORDS], const int *keys,
                   uint32_t slot) {
    for (int k = 0; k < ZMK_KEYMAP_LEN; k++) {
        bool on = lantern_layer_has(w, (uint32_t)k);
        bool want = keys != NULL && listed(keys, k);
        CHECK(on == want, "%s: key %d %s", name, k, on ? "lit, not in the layer's keys" : "dark");
    }
    for (uint32_t c = 0; c < LANTERN_LAYER_CHUNKS; c++) {
        uint32_t got = lantern_layer_slot(w[2 * c + 1]);
        CHECK(got == (keys != NULL ? slot : 0), "%s: chunk %u slot %u, %u expected", name, c, got,
              keys != NULL ? slot : 0);
        CHECK((w[2 * c + 1] & ~(LANTERN_LAYER_HIGH_MASK | (7u << LANTERN_LAYER_SLOT_SHIFT))) == 0,
              "%s: stray bits", name);
    }
    for (int k = ZMK_KEYMAP_LEN; k < (int)(LANTERN_LAYER_CHUNKS * LANTERN_LAYER_CHUNK_KEYS); k++) {
        CHECK(!lantern_layer_has(w, (uint32_t)k), "%s: key %d past the keymap set", name, k);
    }
    if (keys == NULL) {
        for (size_t i = 0; i < LANTERN_LAYER_WORDS; i++) {
            CHECK(w[i] == 0, "%s: none is not zeros (word %zu %08x)", name, i, w[i]);
        }
    }
}

static void latency0(int64_t now, int slot) {
    fake_now = now;
    struct bt_conn c = {.slot = slot};
    lantern_layer_links.le_param_updated(&c, 6, 0, 400);
}

/* round 2's window: the system work queue preempts the relay's run as it
 * reads the flags and raises Num */
static void hook_press_num(void) { change(fake_now, 1, true); }
/* round 3's: it releases Num */
static void hook_release_num(void) { change(fake_now, 1, false); }
/* the final review's X4: half 1 comes to latency 0 as the run reads the flags */
static void latency0(int64_t now, int slot);
static void hook_latency0_half1(void) { latency0(fake_now, 1); }

/* A change ZMK has written whose event has not reached this listener yet
 * (another subscriber ahead of it blocks): the bit alone. */
static void write_unheard(uint8_t layer, bool on) {
    if (on) {
        fake_layers |= 1u << layer;
    } else {
        fake_layers &= ~(1u << layer);
    }
}
/* the event then reaches the listener */
static void hear(int64_t now) {
    fake_now = now;
    zmk_event_t e = {.kind = 1};
    layer_listener(&e);
}

static bool any_keys_sent(int first) {
    for (int i = first; i < nsends; i++) {
        if (sends[i].param1 != 0 || (sends[i].param2 & LANTERN_LAYER_HIGH_MASK) != 0) {
            return true;
        }
    }
    return false;
}

#endif /* central */

int main(void) {
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    uint32_t w[LANTERN_LAYER_WORDS];
    int first;
    int64_t t = 10000;

    /* boot: the state once, a second on; -DINIT_WORD: ZMK's layer word
     * starting at another value than 0 (heard takes it at init) */
    fake_now = 0;
    first = nsends;
#ifdef INIT_WORD
    fake_layers = INIT_WORD;
#endif
    fake_sys_init();
    CHECK((state_work.work.flags & K_WORK_DELAYED) && state_work.due == BOOT_MS, "boot: not scheduled at %d",
          BOOT_MS);
    CHECK(!run(BOOT_MS - 1), "boot: ran early");

#ifdef SYNTH76
    synth_init();
    CHECK(run(BOOT_MS), "boot: not run");
    CHECK(LANTERN_LAYER_CHUNKS == 2 && LANTERN_LAYER_WORDS == 4, "chunks %u", (unsigned)LANTERN_LAYER_CHUNKS);
    static const int l1[] = {0, 31, 32, 60, 61, 75, -1};
    static const int l2[] = {70, -1};
#ifdef LIST_DEFAULT
    static int all[77];
    for (int k = 0; k < 76; k++) {
        all[k] = k;
    }
    all[76] = -1;
    /* the default layer named (slot 4): shown from boot, to the lit halves
     * and the central's own LEDs */
    CHECK(decode(first, 0, w) == 1, "boot, default named: slot 0 states");
    expect("boot, default named", w, all, 4);
    expect("boot, default named, own", local, all, 4);
#else
    CHECK(decode(first, 0, w) == 1 && decode(first, 1, w) == 1, "boot: no state to the lit halves");
    expect("boot", w, NULL, 0);
#endif
    /* layer 1: six keys over two chunks (key 5 repeats the default's), slot 2 */
    first = nsends;
    change(t, 1, true);
    CHECK(pending() && state_work.last_delay == SETTLE_MS, "layer 1: delay %d", state_work.last_delay);
    CHECK(!run(t + SETTLE_MS - 1), "layer 1: ran before the settle");
    CHECK(run(t + SETTLE_MS), "layer 1: not run at the settle");
    CHECK(decode(first, 0, w) == 1, "layer 1: slot 0 states");
    expect("layer 1 (slot 0)", w, l1, 2);
    CHECK(decode(first, 1, w) == 1, "layer 1: slot 1 states");
    expect("layer 1 (slot 1)", w, l1, 2);
    /* layer 2 over it: its one key of its own (the &trans give nothing), slot 5 */
    t += 1000;
    first = nsends;
    change(t, 2, true);
    run(t + SETTLE_MS);
    CHECK(decode(first, 0, w) == 1, "layer 2: states");
    expect("layer 2", w, l2, 5);
    /* layer 3 over both: not named, passed over; layer 2 still shows, nothing sent */
    t += 1000;
    first = nsends;
    change(t, 3, true);
    run(t + SETTLE_MS);
    CHECK(nsends == first, "layer 3 (not named): %d sends", nsends - first);
    /* reordered: layer 1 moved to the top index, it shows */
    t += 1000;
    first = nsends;
    fake_reordered = true;
    fake_order[0] = 0;
    fake_order[1] = 2;
    fake_order[2] = 3;
    fake_order[3] = 1;
    change(t, 3, true);
    run(t + SETTLE_MS);
    CHECK(decode(first, 0, w) == 1, "reordered: states");
    expect("reordered", w, l1, 2);
    fake_reordered = false;
    /* all off: none at once (the default layer, named, after the settle) */
    t += 1000;
    change(t, 1, false);
    change(t, 2, false);
    first = nsends;
    change(t, 3, false);
#ifdef LIST_DEFAULT
    CHECK(state_work.last_delay == SETTLE_MS, "all off, default named: delay %d", state_work.last_delay);
    run(t + SETTLE_MS);
    CHECK(decode(first, 0, w) == 1, "all off, default named: states");
    expect("all off, default named", w, all, 4);
#else
    CHECK(state_work.last_delay == 0, "all off: delay %d", state_work.last_delay);
    run(t);
    CHECK(decode(first, 0, w) == 1, "all off: states");
    expect("all off", w, NULL, 0);
#endif
#else
    /* canon's layers (config/layers.h), keys from canon t-6ttk's research 2.4 */
    static const int num[] = {7, 8, 9, 19, 20, 21, 30, 31, 32, 33, -1};
    static const int sym[] = {6, 7, 8, 9, 10, 18, 19, 20, 21, 22, 23, 30, 31, 32, 33, 34, -1};
    static const int fn[] = {7, 8, 9, 10, 19, 20, 21, 22, 31, 32, 33, 34, -1};
    static const int vkey[] = {1,  2,  3,  4,  5,  13, 14, 15, 16, 17, 25, 26, 27, 28, 29,
                               6,  7,  8,  9,  10, 18, 19, 20, 21, 22, 30, 31, 32, 33, 34, -1};
    struct {
        const char *name;
        uint8_t layer;
        const int *keys;
        uint32_t slot;
    } layers[] = {
        {"Num", 1, num, 0}, {"Sym1", 2, sym, 1}, {"Sym2", 3, sym, 3}, {"Fn", 4, fn, 4},
        {"LL", 9, vkey, 1}, {"LM", 10, vkey, 1}, {"RM", 11, vkey, 3}, {"RR", 12, vkey, 3},
    };
    CHECK(run(BOOT_MS), "boot: not run");
    CHECK(decode(first, 0, w) == 1 && decode(first, 1, w) == 1, "boot: no state to the lit halves");
    expect("boot", w, NULL, 0);
    CHECK(LANTERN_LAYER_CHUNKS == 1, "chunks %u", (unsigned)LANTERN_LAYER_CHUNKS);
    CHECK(CANON_LAYERS == 13 && CANON_KEYS == 58, "the table");

    for (size_t l = 0; l < ARRAY_SIZE(layers); l++) {
        /* on: shown SETTLE_MS later, to both lit halves */
        first = nsends;
        change(t, layers[l].layer, true);
        CHECK(state_work.last_delay == SETTLE_MS, "%s on: delay %d", layers[l].name, state_work.last_delay);
        CHECK(!run(t + SETTLE_MS - 1), "%s: early", layers[l].name);
        CHECK(run(t + SETTLE_MS), "%s: not run", layers[l].name);
        for (uint8_t s = 0; s < 2; s++) {
            CHECK(decode(first, s, w) == 1, "%s: slot %u states", layers[l].name, s);
            expect(layers[l].name, w, layers[l].keys, layers[l].slot);
        }
        /* off: none at once */
        t += 500;
        first = nsends;
        change(t, layers[l].layer, false);
        CHECK(state_work.last_delay == 0, "%s off: delay %d", layers[l].name, state_work.last_delay);
        run(t);
        for (uint8_t s = 0; s < 2; s++) {
            CHECK(decode(first, s, w) == 1, "%s off: slot %u states", layers[l].name, s);
            expect("off", w, NULL, 0);
        }
        t += 500;
    }

    /* the arrow layers: passed over; with Num under one, Num shows */
    for (uint8_t a = 5; a <= 8; a++) {
        first = nsends;
        change(t, a, true);
        run(t + SETTLE_MS);
        CHECK(nsends == first, "arrow %u alone: %d sends", a, nsends - first);
        change(t + 100, 1, true);
        run(t + 100 + SETTLE_MS);
        decode(first, 0, w);
        expect("Num under an arrow", w, num, 0);
        change(t + 200, 1, false);
        change(t + 200, a, false);
        run(t + 200);
        t += 1000;
    }

    /* a tap shorter than the settle: nothing sent */
    first = nsends;
    change(t, 9, true);
    change(t + SETTLE_MS - 10, 9, false);
    run(t + SETTLE_MS - 10);
    run(t + 2 * SETTLE_MS);
    CHECK(nsends == first, "short tap: %d sends", nsends - first);
    t += 1000;

    /* Num then Sym1 within the settle: Sym1 only, SETTLE_MS after it */
    first = nsends;
    change(t, 1, true);
    change(t + 30, 2, true);
    CHECK(!run(t + SETTLE_MS), "Num then Sym1: ran at Num's settle");
    CHECK(run(t + 30 + SETTLE_MS), "Num then Sym1: not run");
    CHECK(decode(first, 0, w) == 1, "Num then Sym1: states");
    expect("Num then Sym1", w, sym, 1);
    /* Sym1 released with Num held: back to Num (in view: SETTLE_MS) */
    first = nsends;
    change(t + 500, 2, false);
    run(t + 500 + SETTLE_MS);
    decode(first, 0, w);
    expect("back to Num", w, num, 0);
    /* a change that leaves the shown state alone (an arrow over Num): no send */
    first = nsends;
    change(t + 600, 5, true);
    run(t + 600 + SETTLE_MS);
    CHECK(nsends == first, "arrow over Num: %d sends", nsends - first);
    change(t + 700, 5, false);
    run(t + 700 + SETTLE_MS);
    CHECK(nsends == first, "arrow off over Num: %d sends", nsends - first);
    change(t + 800, 1, false);
    run(t + 800);
    t += 2000;

    /* the Opus review's cases (2026-10-28): nothing cuts a settle short.
     * B: Sym1 shown; Sym1 released and Num pressed before the relay runs (the
     * release's run queued, then Num's settle): the queued run sends nothing,
     * Num shows at its settle; a Num tapped 30 ms instead shows never, and the
     * release then clears Sym1 */
    change(t, 2, true);
    run(t + SETTLE_MS);
    first = nsends;
    change(t + 500, 2, false);
    change(t + 500, 1, true);
    CHECK((state_work.work.flags & K_WORK_QUEUED) && (state_work.work.flags & K_WORK_DELAYED),
          "B: not queued and delayed both (%x)", state_work.work.flags);
    run(t + 500);
    CHECK(nsends == first, "B: the queued run sent %d (a settle pending)", nsends - first);
    CHECK(run(t + 500 + SETTLE_MS), "B: Num's settle did not run");
    decode(first, 0, w);
    expect("B: Num at its settle", w, num, 0);
    change(t + 1000, 1, false);
    run(t + 1000);
    t += 2000;
    change(t, 2, true);
    run(t + SETTLE_MS);
    first = nsends;
    change(t + 500, 2, false);
    change(t + 500, 1, true);
    run(t + 500);
    change(t + 530, 1, false);
    run(t + 530);
    run(t + 600);
    CHECK(decode(first, 0, w) == 1, "B': states");
    expect("B': Sym1 cleared, Num never", w, NULL, 0);
    t += 2000;
    /* D: half 1 comes to latency 0 10 ms into a 40 ms tap of Num: nothing
     * sent at the latency, the release's none to half 1 (forced) */
    first = nsends;
    change(t, 1, true);
    latency0(t + 10, 1);
    CHECK(!run(t + 10), "D: the latency cut the settle short");
    change(t + 40, 1, false);
    run(t + 40);
    run(t + 200);
    CHECK(decode(first, 1, w) == 1, "D: half 1 states");
    expect("D: half 1, none", w, NULL, 0);
    CHECK(decode(first, 0, w) == 0, "D: half 0 sent");
    t += 2000;
    /* W (round 2): Sym1 shown; its release's run is preempted as it reads the
     * flags, and Num comes into view then; Num released 30 ms later. Num is
     * never sent; Sym1 goes at Num's release (the run that read the release
     * found Num's settle and left it) */
    change(t, 2, true);
    run(t + SETTLE_MS);
    first = nsends;
    change(t + 500, 2, false);
    fake_before_busy_get = hook_press_num;
    run(t + 500);
    CHECK(fake_before_busy_get == NULL, "W: the hook did not run");
    CHECK(nsends == first, "W: %d sends in the preempted run", nsends - first);
    change(t + 530, 1, false);
    run(t + 530);
    run(t + 700);
    CHECK(decode(first, 0, w) == 1, "W: states");
    expect("W: Sym1 cleared, Num never", w, NULL, 0);
    t += 2000;
    /* W2: the same, Num coming into view right after the run's look for a
     * wait (round 1's order read the state only then and sent Num at once,
     * the review's probe_window): the run sends the release's none, and Num,
     * released 30 ms later, never shows */
    change(t, 2, true);
    run(t + SETTLE_MS);
    first = nsends;
    change(t + 500, 2, false);
    fake_before_lit = hook_press_num;
    run(t + 500);
    CHECK(fake_before_lit == NULL, "W2: the hook did not run");
    change(t + 530, 1, false);
    run(t + 530);
    run(t + 700);
    {
        bool num_sent = false;
        for (int i = first; i < nsends; i++) {
            num_sent |= sends[i].param1 != 0 || (sends[i].param2 & LANTERN_LAYER_HIGH_MASK) != 0;
        }
        CHECK(!num_sent, "W2: Num sent though released inside its settle");
    }
    CHECK(decode(first, 0, w) >= 1, "W2: states");
    expect("W2: none at the end", w, NULL, 0);
    t += 2000;

    /* round 3 (Opus review 2026-10-28): a release between the run's read and
     * its look for a wait.
     * X: case B's setup (Sym1 shown; Sym1 released and Num pressed before the
     * relay runs), then Num released as the run reads the flags: Num is never
     * sent; Sym1 goes at the run after */
    change(t, 2, true);
    run(t + SETTLE_MS);
    first = nsends;
    change(t + 500, 2, false);
    change(t + 500, 1, true);
    fake_before_busy_get = hook_release_num;
    run(t + 500);
    CHECK(fake_before_busy_get == NULL, "X: the hook did not run");
    run(t + 700);
    CHECK(!any_keys_sent(first), "X: Num sent though released inside its settle");
    CHECK(decode(first, 0, w) >= 1, "X: states");
    expect("X: none at the end", w, NULL, 0);
    t += 2000;
    /* X2: the same, Num released during the run's walk of the keymap */
    change(t, 2, true);
    run(t + SETTLE_MS);
    first = nsends;
    change(t + 500, 2, false);
    change(t + 500, 1, true);
    walk_hook_at = 20;
    walk_hook = hook_release_num;
    run(t + 500);
    CHECK(walk_hook == NULL, "X2: the hook did not run");
    run(t + 700);
    CHECK(!any_keys_sent(first), "X2: Num sent though released inside its settle");
    CHECK(decode(first, 0, w) >= 1, "X2: states");
    expect("X2: none at the end", w, NULL, 0);
    t += 2000;
    /* X3: X, then Fn pressed before the run after: Num is never sent, and
     * Sym1 holds until Fn shows at its settle, or goes at once */
    change(t, 2, true);
    run(t + SETTLE_MS);
    first = nsends;
    change(t + 500, 2, false);
    change(t + 500, 1, true);
    fake_before_busy_get = hook_release_num;
    run(t + 500);
    change(t + 520, 4, true);
    run(t + 520);
    run(t + 520 + SETTLE_MS);
    {
        bool num = false;
        for (int i = first; i < nsends; i++) {
            num |= sends[i].param1 == ((1u << 7) | (1u << 8) | (1u << 9) | (1u << 19) | (1u << 20) | (1u << 21) |
                                       (1u << 30) | (1u << 31));
        }
        CHECK(!num, "X3: Num sent");
    }
    decode(first, 0, w);
    expect("X3: Fn at its settle", w, fn, 4);
    change(t + 1000, 4, false);
    run(t + 1000);
    t += 2000;
    /* X4: half 1 comes to latency 0 as the run reads the flags: exactly one
     * forced send to it */
    change(t, 9, true);
    run(t + SETTLE_MS);
    first = nsends;
    {
        int before = nsends;
        (void)before;
    }
    fake_before_busy_get = hook_latency0_half1;
    latency0(t + 500, 0); /* a run that comes for half 0; half 1 joins at its flag read */
    first = nsends;
    run(t + 500);
    CHECK(fake_before_busy_get == NULL, "X4: the hook did not run");
    run(t + 600);
    CHECK(atomic_get(&again) == 0 && !pending(), "X4: a forced bit or a run left (%lx)", (long)atomic_get(&again));
    CHECK(decode(first, 1, w) == 1, "X4: half 1 states");
    expect("X4: half 1, LL", w, vkey, 1);
    CHECK(decode(first, 0, w) == 1, "X4: half 0 states (its own forced send)");
    change(t + 700, 9, false);
    run(t + 700);
    t += 2000;
    /* Y: a change written but not yet heard (the listener behind a blocking
     * subscriber): a run meanwhile sends nothing of it; heard, it shows at
     * its settle */
    first = nsends;
    write_unheard(1, true);
    latency0(t, 0); /* a run that comes for another reason */
    run(t);
    CHECK(!any_keys_sent(first), "Y: an unheard layer sent");
    hear(t + 30);
    CHECK(!run(t + 30 + SETTLE_MS - 1), "Y: ran before the settle from the hearing");
    run(t + 30 + SETTLE_MS);
    CHECK(decode(first, 0, w) >= 1, "Y: states");
    expect("Y: Num at its settle", w, num, 0);
    write_unheard(1, false);
    run(t + 200);
    hear(t + 300);
    run(t + 300);
    decode(first, 1, w);
    expect("Y: none at the end", w, NULL, 0);
    t += 2000;

    /* one lit half: only it is sent; the other, lit later (latency 0), gets
     * the state of the moment, and the first is not sent again */
    fake_lit = 0x1;
    first = nsends;
    change(t, 10, true);
    run(t + SETTLE_MS);
    CHECK(decode(first, 0, w) == 1, "one lit: slot 0");
    expect("one lit", w, vkey, 1);
    CHECK(decode(first, 1, w) == 0, "one lit: slot 1 sent");
    first = nsends;
    fake_lit = 0x3;
    {
        struct bt_conn c30 = {.slot = 1}, host = {.slot = -1};
        int before = state_work.scheduled;
        lantern_layer_links.le_param_updated(&c30, 6, 30, 400);
        lantern_layer_links.le_param_updated(&host, 6, 0, 400);
        CHECK(state_work.scheduled == before, "latency 30 or a host link scheduled the state");
        latency0(t + 100, 1);
        CHECK((state_work.work.flags & K_WORK_QUEUED) && state_work.last_delay == 0,
              "latency 0: not queued at once");
    }
    run(t + 100);
    CHECK(decode(first, 1, w) == 1, "lit later: slot 1 states");
    expect("lit later", w, vkey, 1);
    CHECK(decode(first, 0, w) == 0, "lit later: slot 0 sent again");
    /* latency 0 again for a half already sent this state: sent again (forced) */
    first = nsends;
    latency0(t + 200, 0);
    run(t + 200);
    CHECK(decode(first, 0, w) == 1, "forced: slot 0 states");
    expect("forced", w, vkey, 1);
    CHECK(decode(first, 1, w) == 0, "forced: slot 1 sent");
    /* a failed send is tried again at the next run, the state unchanged */
    fake_invoke_err = -EAGAIN;
    latency0(t + 300, 1);
    int warn0 = warnings;
    run(t + 300);
    CHECK(warnings == warn0 + 1, "failed send: %d warnings", warnings - warn0);
    fake_invoke_err = 0;
    first = nsends;
    change(t + 400, 5, true); /* any change: the run goes over every half */
    run(t + 400 + SETTLE_MS);
    CHECK(decode(first, 1, w) == 1, "after a failure: slot 1 states");
    expect("after a failure", w, vkey, 1);
    CHECK(decode(first, 0, w) == 0, "after a failure: slot 0 sent");
    change(t + 500, 5, false);
    run(t + 500 + SETTLE_MS);
    /* a half that went dark is forgotten: lit again by a run, it is sent */
    fake_lit = 0x1;
    change(t + 600, 10, false);
    run(t + 600);
    first = nsends;
    fake_lit = 0x3;
    change(t + 700, 1, true);
    run(t + 700 + SETTLE_MS);
    CHECK(decode(first, 1, w) == 1, "dark then lit: slot 1 states");
    expect("dark then lit", w, num, 0);
    change(t + 800, 1, false);
    run(t + 800);
    CHECK(fake_q_used == &fake_relay_q, "not the relay's queue");
#endif

#if IS_ENABLED(CONFIG_LANTERN_LAYER_KEYS)
    /* the central's own key LEDs: handed every chunk of every run's state,
     * the last marked, the last state at the end */
    CHECK(local_commits > 0 && local_sets == local_commits * (int)LANTERN_LAYER_CHUNKS,
          "central's own: %d sets, %d commits", local_sets, local_commits);
#ifdef LIST_DEFAULT
    expect("central's own, default named", local, all, 4);
#else
    for (size_t i = 0; i < LANTERN_LAYER_WORDS; i++) {
        CHECK(local[i] == 0, "central's own: word %zu %08x after all off", i, local[i]);
    }
#endif
#endif
    printf("layer central%s: chunks %u, %d sends, %d warnings, fails %d\n",
           IS_ENABLED(CONFIG_LANTERN_LAYER_KEYS) ? " with keys" : "", (unsigned)LANTERN_LAYER_CHUNKS,
           nsends, warnings, fails);
#else
    /* the half: the handler (the last mark, the chunk past the keymap, the
     * slot, power), the link's end and USB power's */
    struct zmk_behavior_binding b = {.behavior_dev = "layer", .param1 = 0x380, .param2 = 3u << 29};
    struct zmk_behavior_binding_event e = {.position = 0};
    CHECK(api.binding_pressed(&b, e) == ZMK_BEHAVIOR_OPAQUE && local_sets == 1 && local_commits == 0,
          "half: an unmarked chunk committed");
    e.position = LANTERN_LAYER_LAST;
    CHECK(api.binding_pressed(&b, e) == ZMK_BEHAVIOR_OPAQUE && local_commits == 1 && local[0] == 0x380 &&
              local[1] == (3u << 29),
          "half: the marked chunk not taken");
    e.position = 1 | LANTERN_LAYER_LAST; /* a central with two chunks: chunk 1 past this keymap */
    b.param1 = 0x1;
    CHECK(api.binding_pressed(&b, e) == ZMK_BEHAVIOR_OPAQUE && local_commits == 2 && local[0] == 0x380,
          "half: a chunk past the keymap not passed over");
    e.position = 256;
    CHECK(api.binding_pressed(&b, e) == -EINVAL && local_sets == 3, "half: position 256 taken");
    e.position = LANTERN_LAYER_LAST;
    b.param2 = 6u << 29;
    CHECK(api.binding_pressed(&b, e) == -EINVAL && local_sets == 3, "half: slot 6 taken");
    b.param2 = 7u << 29;
    CHECK(api.binding_pressed(&b, e) == -EINVAL && local_sets == 3, "half: slot 7 taken");
    b.param2 = 1u << 29;
    fake_usb_powered = false;
    CHECK(api.binding_pressed(&b, e) == ZMK_BEHAVIOR_OPAQUE && local_sets == 3,
          "half: a state taken on battery");
    fake_usb_powered = true;
    CHECK(api.binding_released(&b, e) == ZMK_BEHAVIOR_OPAQUE && local_sets == 3, "half: a release");
    CHECK(api.locality == BEHAVIOR_LOCALITY_GLOBAL, "half: locality");
    {
        zmk_event_t usb = {.kind = 2}, other = {.kind = 1};
        usb_listener(&usb); /* powered: nothing */
        usb_listener(&other);
        CHECK(local_clears == 0, "half: cleared while powered");
        /* round 2: a dip shorter than ZMK's notifier (no event, the live
         * status unpowered) keeps a chunk, and the one merged event, powered,
         * clears nothing */
        int sets = local_sets;
        fake_usb_powered = false;
        b.param1 = 0x0;
        b.param2 = 0;
        CHECK(api.binding_pressed(&b, e) == ZMK_BEHAVIOR_OPAQUE && local_sets == sets + 1 && local[0] == 0,
              "half: the dip's zeros dropped");
        fake_usb_powered = true;
        usb_listener(&usb);
        CHECK(local_clears == 0, "half: the merged event cleared");
        /* the power gone, seen: cleared, and a chunk dropped though the live
         * status reads powered again before ZMK's next event */
        b.param1 = 0x380;
        b.param2 = 3u << 29;
        api.binding_pressed(&b, e);
        fake_usb_powered = false;
        usb_listener(&usb);
        CHECK(local_clears == 1 && local[0] == 0 && local[1] == 0, "half: USB power's end kept the state");
        fake_usb_powered = true;
        sets = local_sets;
        CHECK(api.binding_pressed(&b, e) == ZMK_BEHAVIOR_OPAQUE && local_sets == sets,
              "half: a chunk taken before the power's return was seen");
        usb_listener(&usb);
        CHECK(api.binding_pressed(&b, e) == ZMK_BEHAVIOR_OPAQUE && local_sets == sets + 1 && local[0] == 0x380,
              "half: a chunk dropped after the power's return was seen");
    }
    lantern_layer_link.disconnected(NULL, 0x13);
    CHECK(local_clears == 2, "half: the link's end kept the state");
    printf("layer half: %d warnings, fails %d\n", warnings, fails);
#endif
    return fails != 0;
}
