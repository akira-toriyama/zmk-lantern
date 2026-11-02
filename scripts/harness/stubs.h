/* Host stubs for zmk-lantern's src/keys.c, force-included (-include) into
 * every program over the engine: just enough Zephyr/ZMK to run the real
 * file's frame code on a Mac (stubinc/ holds the empty headers the sources
 * #include). Tables are canon's halves' (its config/lantern_keys.dtsi and
 * the halves' overlays; equal to canon's main of 2026-11-02): HALF_LEFT or
 * HALF_RIGHT picks the half. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Zephyr's IS_ENABLED trick */
#define IS_ENABLED(config_macro) Z_IS_ENABLED1(config_macro)
#define Z_IS_ENABLED1(config_macro) Z_IS_ENABLED2(_XXXX##config_macro)
#define _XXXX1 _YYYY,
#define Z_IS_ENABLED2(one_or_two_args) Z_IS_ENABLED3(one_or_two_args 1, 0)
#define Z_IS_ENABLED3(ignore_this, val, ...) val

/* Zephyr's COND_CODE_1 (util_macro.h, util_internal.h): the flag may be
 * undefined, as an unset Kconfig symbol is. */
#define COND_CODE_1(_flag, _if_1_code, _else_code) Z_COND_CODE_1(_flag, _if_1_code, _else_code)
#define Z_COND_CODE_1(_flag, _if_1_code, _else_code) __COND_CODE(_XXXX##_flag, _if_1_code, _else_code)
#define __COND_CODE(one_or_two_args, _if_code, _else_code) __GET_ARG2_DEBRACKET(one_or_two_args _if_code, _else_code)
#define __GET_ARG2_DEBRACKET(ignore_this, val, ...) __DEBRACKET val
#define __DEBRACKET(...) __VA_ARGS__

#define BUILD_ASSERT(cond, ...) _Static_assert(cond, "" __VA_ARGS__)
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define CLAMP(val, low, high) (((val) <= (low)) ? (low) : MIN(val, high))
#define ARG_UNUSED(x) (void)(x)
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define __maybe_unused __attribute__((unused))
#define __noinline __attribute__((noinline))

/* devicetree */
#define DT_DRV_INST(n) 0
#define DT_INST_PHANDLE(inst, prop) 0
#define DT_NUM_INST_STATUS_OKAY(compat) 1
#define DT_INST(inst, compat) 0
#define DT_PROP_LEN(node, prop) DT_L_##prop
#define DT_INST_PROP(inst, prop) DT_P_##prop
#define DT_PROP(node, prop) DT_P_##prop
#define DT_INST_PROP_LEN(inst, prop) DT_L_##prop
#define DT_INST_NODE_HAS_PROP(inst, prop) DT_H_##prop
#define DT_FOREACH_PROP_ELEM(node, prop, fn)
#define DEVICE_DT_GET(node) (&fake_strip)

#define DT_P_chain_length 29
#define DT_L_positions 29
#define DT_L_slots 58
#define DT_L_coords 116
#define DT_H_coords 1
/* The scatter's shares, canon's trial ones (t-skdt), present only where a
 * combination builds the scatter, as a consumer writes them only then. */
#if defined(CONFIG_LANTERN_SCATTER)
#define DT_H_scatter_weights 1
#else
#define DT_H_scatter_weights 0
#endif
#define DT_L_scatter_weights 6
#define DT_P_scatter_weights {50, 20, 10, 5, 5, 0}
#define DT_INST_FOREACH_PROP_ELEM_SEP(inst, prop, fn, sep) DT_SUM_##prop
#define DT_SUM_scatter_weights (50 + 20 + 10 + 5 + 5 + 0)

#ifdef HALF_RIGHT
#define DT_P_positions                                                                             \
    {18, 30, 6, 19, 31, 41, 7, 20, 32, 42, 8, 21, 33, 43, 9, 22, 34, 44, 10, 23, 35, 45, 11,     \
     51, 50, 49, 57, 56, 55}
#else
#define DT_P_positions                                                                             \
    {17, 29, 5, 16, 28, 40, 4, 15, 27, 39, 3, 14, 26, 38, 2, 13, 25, 37, 1, 12, 24, 36, 0,        \
     46, 47, 48, 52, 53, 54}
#endif
#define DT_P_slots                                                                                 \
    {0, 0, 0, 1, 1, 1, 3, 3, 3, 4, 4, 4, 0, 0, 0, 1, 1, 1, 3, 3, 3, 4, 4, 4, 0, 0, 0, 1, 1,     \
     1, 3, 3, 3, 4, 4, 4, 0, 0, 0, 1, 1, 3, 3, 4, 4, 4, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2}
#ifndef DT_P_coords
#define DT_P_coords                                                                                \
    {0,    200, 100,  200, 200,  200, 300,  200, 400,  200, 500,  200, 1150, 200, 1250, 200,     \
     1350, 200, 1450, 200, 1550, 200, 1650, 200, 0,    300, 100,  300, 200,  300, 300,  300,     \
     400,  300, 500,  300, 1150, 300, 1250, 300, 1350, 300, 1450, 300, 1550, 300, 1650, 300,     \
     0,    400, 100,  400, 200,  400, 300,  400, 400,  400, 500,  400, 1150, 400, 1250, 400,     \
     1350, 400, 1450, 400, 1550, 400, 1650, 400, 0,    500, 100,  500, 200,  500, 300,  500,     \
     400,  500, 1250, 500, 1350, 500, 1450, 500, 1550, 500, 1650, 500, 550,  550, 650,  550,     \
     750,  550, 900,  550, 1000, 550, 1100, 550, 550,  650, 650,  650, 750,  650, 900,  650,     \
     1000, 650, 1100, 650}
#endif

#define ZMK_KEYMAP_LEN 58

struct device {
    int unused;
};
static const struct device fake_strip;

struct led_rgb {
    uint8_t r, g, b;
};

/* atomics */
typedef long atomic_t;
typedef long atomic_val_t;
static inline atomic_val_t atomic_get(const atomic_t *p) { return *p; }
static inline atomic_val_t atomic_set(atomic_t *p, atomic_val_t v) {
    atomic_val_t o = *p;
    *p = v;
    return o;
}
static inline bool atomic_cas(atomic_t *p, atomic_val_t o, atomic_val_t n) {
    if (*p != o) {
        return false;
    }
    *p = n;
    return true;
}

/* clock */
static uint32_t fake_now, fake_cycles = 0x1234567u;
#if defined(LANTERN_STUB_SKEW)
/* stubs_skew.h's clock: skew_ms added from the skew_after-th reading on, and
 * a "preemption" at the clock_hook_at-th reading since clock_calls was reset
 * (in hits_read(), the reading after slot j's value was taken), one shot; the
 * hook's own readings (hit_note()) do not fire it again. */
static uint32_t clock_calls, skew_after = 0xFFFFFFFFu, skew_ms;
static void (*clock_hook)(void);
static uint32_t clock_hook_at;
static inline uint32_t k_uptime_get_32(void) {
    uint32_t c = ++clock_calls;
    if (clock_hook != NULL && c == clock_hook_at) {
        void (*h)(void) = clock_hook;
        clock_hook = NULL;
        h();
    }
    return fake_now + (c > skew_after ? skew_ms : 0);
}
#else
static inline uint32_t k_uptime_get_32(void) { return fake_now; }
#endif
static inline uint32_t k_cycle_get_32(void) { return fake_cycles; }
static inline void k_usleep(int us) { (void)us; }

/* random (the scatter's throw): a fixed xorshift, so a run repeats */
static inline uint32_t sys_rand32_get(void) {
    static uint32_t x = 0x1234567u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return x;
}

/* work: the harness drives paint() itself; these only count */
struct k_work {
    int unused;
};
struct k_work_delayable {
    int unused;
};
struct k_work_q {
    int unused;
};
typedef int k_timeout_t;
#define K_MSEC(ms) (ms)
#define K_NO_WAIT 0
#define K_WORK_DEFINE(name, handler) struct k_work name
#define K_WORK_DELAYABLE_DEFINE(name, handler) struct k_work_delayable name
static int submits, reschedules, cancels;
static inline struct k_work_q *zmk_workqueue_lowprio_work_q(void) { return NULL; }
static inline int k_work_submit_to_queue(struct k_work_q *q, struct k_work *w) {
    (void)q, (void)w;
    submits++;
    return 1;
}
static inline int k_work_reschedule_for_queue(struct k_work_q *q, struct k_work_delayable *w,
                                              k_timeout_t d) {
    (void)q, (void)w, (void)d;
    reschedules++;
    return 1;
}
static inline int k_work_reschedule(struct k_work_delayable *w, k_timeout_t d) {
    (void)w, (void)d;
    reschedules++;
    return 1;
}
static inline int k_work_cancel_delayable(struct k_work_delayable *w) {
    (void)w;
    cancels++;
    return 0;
}

/* logging */
#define LOG_MODULE_REGISTER(...)
#define LOG_INF(...)
#define LOG_ERR(...) fprintf(stderr, __VA_ARGS__)

/* the strip: the harness reads the last frame */
static struct led_rgb strip_out[29];
static inline int led_strip_update_rgb(const struct device *d, struct led_rgb *px, size_t n) {
    (void)d;
    memcpy(strip_out, px, n * sizeof(*px));
    return 0;
}

/* ZMK events (only built with the countdown or an effect) */
typedef struct {
    int unused;
} zmk_event_t;
#define ZMK_EV_EVENT_BUBBLE 0
#define ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL 255
struct zmk_position_state_changed {
    uint8_t source;
    uint32_t position;
    bool state;
    int64_t timestamp;
};
static const struct zmk_position_state_changed *fake_event;
static inline const struct zmk_position_state_changed *
as_zmk_position_state_changed(const zmk_event_t *eh) {
    (void)eh;
    return fake_event;
}
#define ZMK_LISTENER(...)
#define ZMK_SUBSCRIPTION(...)

/* the palette: none set unless the harness says */
static bool fake_have;
static uint32_t fake_palette[6];
static inline bool lantern_palette_get(uint32_t c[6]) {
    if (fake_have) {
        memcpy(c, fake_palette, sizeof(fake_palette));
    }
    return fake_have;
}

/* t-41g7: slots optional (present here), coords from the node (DT_H_coords 1
 * above), no physical layout in the stub tree. */
#define DT_H_slots 1
#define DT_H_positions 1
#define DT_HAS_COMPAT_STATUS_OKAY(compat) 0
#define DT_HAS_CHOSEN(prop) 0
