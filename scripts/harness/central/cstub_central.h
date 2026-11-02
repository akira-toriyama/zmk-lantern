/* Host stubs for zmk-lantern's src/layer.c (force-included; cstub/ holds empty
 * stand-ins for the headers it names): Zephyr's macros it uses, the work item
 * run by the harness, ZMK's keymap over a table (canon's, generated from the
 * dongle's zephyr.dts, or a synthetic one with -DSYNTH76), the relay and the
 * split transport recorded. Built by run.sh central as the central
 * (CONFIG_ZMK_SPLIT_ROLE_CENTRAL), the central with key LEDs
 * (+ CONFIG_LANTERN_LAYER_KEYS) and the half (only CONFIG_LANTERN_LAYER_KEYS),
 * over canon's keymap or the synthetic one. */
#pragma once
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define IS_ENABLED(config_macro) Z_IS_ENABLED1(config_macro)
#define Z_IS_ENABLED1(config_macro) Z_IS_ENABLED2(_XXXX##config_macro)
#define _XXXX1 _YYYY,
#define Z_IS_ENABLED2(one_or_two_args) Z_IS_ENABLED3(one_or_two_args 1, 0)
#define Z_IS_ENABLED3(ignore_this, val, ...) val

#define BUILD_ASSERT(cond, ...) _Static_assert(cond, "" __VA_ARGS__)
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define ARG_UNUSED(x) (void)(x)
#define BIT(n) (1UL << (n))
#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))
#define UINT8_MAX_ 255

/* devicetree: the one node (NODE = DT_DRV_INST(0)), its name and layers */
#define DT_DRV_INST(n) NODE_layer
#define DT_NUM_INST_STATUS_OKAY(compat) 1
#define DEVICE_DT_NAME(node) DEVICE_DT_NAME_##node
#define DEVICE_DT_NAME_NODE "layer"
#define DEVICE_DT_NAME_NODE_none "none"
#define DEVICE_DT_NAME_NODE_trans "transparent"
#define DT_FOREACH_PROP_ELEM(node, prop, fn)
#define DT_NODE_HAS_PROP(node, prop) 1
#define DT_PROP_LEN_OR(node, prop, d) DT_PROP_LEN(node, prop)
#define COND_CODE_1(_flag, _if_1_code, _else_code) Z_COND_CODE_1(_flag, _if_1_code, _else_code)
#define Z_COND_CODE_1(_flag, _if_1_code, _else_code) __COND_CODE(_XXXX##_flag, _if_1_code, _else_code)
#define __COND_CODE(one_or_two_args, _if_code, _else_code) __GET_ARG2_DEBRACKET(one_or_two_args _if_code, _else_code)
#define __GET_ARG2_DEBRACKET(ignore_this, val, ...) __DEBRACKET val
#define __DEBRACKET(...) __VA_ARGS__
#define DT_FOREACH_STATUS_OKAY(compat, fn) DT_FOREACH_OKAY_##compat(fn)
#define DT_FOREACH_OKAY_zmk_behavior_none(fn) fn(NODE_none)
#ifdef SYNTH76
#define DT_FOREACH_OKAY_zmk_behavior_transparent(fn) fn(NODE_trans)
#else
#define DT_FOREACH_OKAY_zmk_behavior_transparent(fn)
#endif
#define DT_PROP_LEN(node, prop) (ARRAY_SIZE((int[])FAKE_LAYERS))
#define DT_PROP(node, prop) FAKE_LAYERS
#ifdef SYNTH76
/* layers 1 and 2 shown, slots 2 and 5; layer 3 listed too (the default
 * layer 0 is listed only with -DLIST_DEFAULT) */
#ifdef LIST_DEFAULT
#define FAKE_LAYERS {1, 2, 2, 5, 0, 4}
#else
#define FAKE_LAYERS {1, 2, 2, 5}
#endif
#else
/* canon's (config/imprint_behaviors.dtsi): Num 0, Sym1 1, Sym2 3, Fn 4, LL LM 1, RM RR 3 */
#define FAKE_LAYERS {1, 0, 2, 1, 3, 3, 4, 4, 9, 1, 10, 1, 11, 3, 12, 3}
#endif

/* behaviors */
struct zmk_behavior_binding {
    const char *behavior_dev;
    uint32_t param1;
    uint32_t param2;
};
struct zmk_behavior_binding_event {
    int layer;
    uint32_t position;
    int64_t timestamp;
    uint8_t source;
};
#define ZMK_BEHAVIOR_OPAQUE 0
#define BEHAVIOR_LOCALITY_GLOBAL 2
struct behavior_driver_api {
    int locality;
    int (*binding_pressed)(struct zmk_behavior_binding *, struct zmk_behavior_binding_event);
    int (*binding_released)(struct zmk_behavior_binding *, struct zmk_behavior_binding_event);
};
#define BEHAVIOR_DT_INST_DEFINE(...)
#define POST_KERNEL 0
#define CONFIG_KERNEL_INIT_PRIORITY_DEFAULT 0

/* logging */
static int warnings;
#define LOG_MODULE_REGISTER(...)
#define LOG_LEVEL_INF 3
#define LOG_WRN(...) (warnings++, fprintf(stderr, "WRN: " __VA_ARGS__), fputc('\n', stderr))

/* atomics */
typedef long atomic_t;
typedef long atomic_val_t;
static inline atomic_val_t atomic_get(const atomic_t *p) { return *p; }
static inline atomic_val_t atomic_set(atomic_t *p, atomic_val_t v) {
    atomic_val_t o = *p;
    *p = v;
    return o;
}
static inline atomic_val_t atomic_clear(atomic_t *p) { return atomic_set(p, 0); }
static inline atomic_val_t atomic_or(atomic_t *p, atomic_val_t v) {
    atomic_val_t o = *p;
    *p |= v;
    return o;
}

/* time and work: a model of Zephyr's kernel/work.c (after the Opus review's
 * probe of 2026-10-28): the QUEUED, DELAYED and RUNNING flags of
 * submit_to_queue_locked(), schedule_for_queue_locked(), unschedule_locked(),
 * work_timeout() and work_queue_main(), one queue (the relay's), FIFO. A
 * reschedule leaves a queued item queued; a schedule leaves a queued or
 * delayed item alone. */
static int64_t fake_now;
static inline int64_t k_uptime_get(void) { return fake_now; }
typedef int k_timeout_t;
#define K_MSEC(ms) (ms)
#define K_NO_WAIT 0
#define K_WORK_RUNNING (1u << 0)
#define K_WORK_CANCELING (1u << 1)
#define K_WORK_QUEUED (1u << 2)
#define K_WORK_DELAYED (1u << 3)
struct k_work {
    void (*handler)(struct k_work *);
    unsigned flags;
};
struct k_work_delayable {
    struct k_work work;
    int64_t due;
    int scheduled; /* how many times (re)scheduled with a delay or at once */
    int last_delay;
};
struct k_work_q {
    struct k_work *pending[16];
    int n;
};
#define K_WORK_DELAYABLE_DEFINE(name, fn) struct k_work_delayable name = {.work = {.handler = fn}}
static struct k_work_q *fake_q_used;
static struct k_work_delayable *fake_dwork;
static struct k_work_q *fake_dwork_q;
static int submit_locked(struct k_work_q *q, struct k_work *w) {
    if (w->flags & K_WORK_CANCELING) {
        return -16;
    }
    if (!(w->flags & K_WORK_QUEUED)) {
        int ret = (w->flags & K_WORK_RUNNING) ? 2 : 1;
        q->pending[q->n++] = w;
        w->flags |= K_WORK_QUEUED;
        return ret;
    }
    return 0;
}
static inline int k_work_reschedule_for_queue(struct k_work_q *q, struct k_work_delayable *dw,
                                              k_timeout_t d) {
    fake_q_used = q;
    fake_dwork = dw;
    fake_dwork_q = q;
    dw->scheduled++;
    dw->last_delay = d;
    dw->work.flags &= ~K_WORK_DELAYED;
    if (d == 0) {
        return submit_locked(q, &dw->work);
    }
    dw->work.flags |= K_WORK_DELAYED;
    dw->due = fake_now + d;
    return 1;
}
static inline int k_work_schedule_for_queue(struct k_work_q *q, struct k_work_delayable *dw,
                                            k_timeout_t d) {
    fake_q_used = q;
    fake_dwork = dw;
    fake_dwork_q = q;
    if ((dw->work.flags & ~K_WORK_RUNNING) != 0) {
        return 0;
    }
    dw->scheduled++;
    dw->last_delay = d;
    if (d == 0) {
        return submit_locked(q, &dw->work);
    }
    dw->work.flags |= K_WORK_DELAYED;
    dw->due = fake_now + d;
    return 1;
}
/* a test's hook, run as the flags are read: a thread that preempts the
 * relay's there (round 2 of the Opus review, 2026-10-28) */
static void (*fake_before_busy_get)(void);
static inline int k_work_delayable_busy_get(const struct k_work_delayable *dw) {
    if (fake_before_busy_get != NULL) {
        void (*hook)(void) = fake_before_busy_get;
        fake_before_busy_get = NULL;
        hook();
    }
    return (int)dw->work.flags;
}
static void fake_timers(int64_t now) {
    fake_now = now;
    if (fake_dwork != NULL && (fake_dwork->work.flags & K_WORK_DELAYED) && fake_dwork->due <= now) {
        fake_dwork->work.flags &= ~K_WORK_DELAYED;
        submit_locked(fake_dwork_q, &fake_dwork->work);
    }
}
/* the relay's thread at now: the due timer, then every queued item, FIFO;
 * how many runs */
static int fake_queue_run(struct k_work_q *q, int64_t now) {
    int ran = 0;
    fake_timers(now);
    while (q->n > 0) {
        struct k_work *w = q->pending[0];
        for (int i = 1; i < q->n; i++) {
            q->pending[i - 1] = q->pending[i];
        }
        q->n--;
        w->flags |= K_WORK_RUNNING;
        w->flags &= ~K_WORK_QUEUED;
        w->handler(w);
        w->flags &= ~K_WORK_RUNNING;
        ran++;
    }
    return ran;
}

/* init: the harness calls the one SYS_INIT itself */
#define APPLICATION 0
#define CONFIG_APPLICATION_INIT_PRIORITY 90
#define SYS_INIT(fn, level, prio) static int (*const fake_sys_init)(void) = fn

/* USB power, for the half */
static bool fake_usb_powered = true;
struct zmk_usb_conn_state_changed {
    int conn_state;
};
static struct zmk_usb_conn_state_changed fake_usb_ev;
static inline bool zmk_usb_is_powered(void) { return fake_usb_powered; }

/* Bluetooth: a link is its slot (or -1, no half's) */
struct bt_conn {
    int slot;
};
struct bt_conn_cb {
    void (*disconnected)(struct bt_conn *conn, uint8_t reason);
    void (*le_param_updated)(struct bt_conn *conn, uint16_t interval, uint16_t latency,
                             uint16_t timeout);
};
#define BT_CONN_CB_DEFINE(name) static const struct bt_conn_cb name

/* ZMK events */
typedef struct {
    int kind; /* 1: a layer change, 2: USB's */
} zmk_event_t;
#define ZMK_EV_EVENT_BUBBLE 0
#define ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL 255
struct zmk_layer_state_changed {
    uint8_t layer;
    bool state;
    bool locked;
    int64_t timestamp;
};
static struct zmk_layer_state_changed fake_layer_ev;
static inline const struct zmk_layer_state_changed *as_zmk_layer_state_changed(const zmk_event_t *eh) {
    return eh->kind == 1 ? &fake_layer_ev : NULL;
}
static inline const struct zmk_usb_conn_state_changed *
as_zmk_usb_conn_state_changed(const zmk_event_t *eh) {
    return eh->kind == 2 ? &fake_usb_ev : NULL;
}
#define ZMK_LISTENER(...)
#define ZMK_SUBSCRIPTION(...)

/* the split */
#define ZMK_SPLIT_CENTRAL_PERIPHERAL_COUNT 2
struct sent_cmd {
    uint8_t slot;
    char dev[16];
    uint32_t param1, param2, position;
    bool state;
};
static struct sent_cmd sends[256];
static int nsends;
static int fake_invoke_err;
static inline int zmk_split_central_invoke_behavior(uint8_t source, struct zmk_behavior_binding *b,
                                                    struct zmk_behavior_binding_event e, bool state) {
    if (fake_invoke_err != 0) {
        return fake_invoke_err;
    }
    struct sent_cmd *c = &sends[nsends++ % 256];
    c->slot = source;
    snprintf(c->dev, sizeof(c->dev), "%s", b->behavior_dev);
    c->param1 = b->param1;
    c->param2 = b->param2;
    c->position = e.position;
    c->state = state;
    return 0;
}

/* the keymap */
typedef uint8_t zmk_keymap_layer_id_t;
typedef uint8_t zmk_keymap_layer_index_t;
#define ZMK_KEYMAP_LAYER_ID_INVAL UINT8_MAX
#ifdef SYNTH76
#define ZMK_KEYMAP_LEN 76
#define ZMK_KEYMAP_LAYERS_LEN 4
#else
#define ZMK_KEYMAP_LEN 58
#define ZMK_KEYMAP_LAYERS_LEN 13
#endif
static uint32_t fake_layers; /* the active layers but the default, a bit a layer id */
typedef uint32_t zmk_keymap_layers_state_t;
static inline zmk_keymap_layers_state_t zmk_keymap_layer_state(void) { return fake_layers; }
static uint8_t fake_order[ZMK_KEYMAP_LAYERS_LEN]; /* index -> id, identity unless a test sets it */
static bool fake_reordered;
static inline zmk_keymap_layer_id_t zmk_keymap_layer_default(void) { return 0; }
static inline zmk_keymap_layer_id_t zmk_keymap_layer_index_to_id(zmk_keymap_layer_index_t i) {
    if (i >= ZMK_KEYMAP_LAYERS_LEN) {
        return ZMK_KEYMAP_LAYER_ID_INVAL;
    }
    return fake_reordered ? fake_order[i] : i;
}
static inline bool zmk_keymap_layer_active(zmk_keymap_layer_id_t l) {
    return l == 0 || (fake_layers & (1u << l)) != 0;
}
static const struct zmk_behavior_binding *fake_binding(zmk_keymap_layer_id_t l, uint16_t k);
static inline const struct zmk_behavior_binding *
zmk_keymap_get_layer_binding_at_idx(zmk_keymap_layer_id_t l, uint16_t k) {
    if (k >= ZMK_KEYMAP_LEN || l >= ZMK_KEYMAP_LAYERS_LEN) {
        return NULL;
    }
    return fake_binding(l, k);
}
