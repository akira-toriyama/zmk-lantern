/* Host stubs for the palette, the picks and the port (src/palette.c,
 * src/pick.c, src/effect.c, src/intro.c, src/tempo.c, src/effect_next.c,
 * src/port.c), force-included in every translation unit: each source is
 * compiled on its own, as the build compiles it, and linked with fake.c,
 * which keeps what the stubs share (pstub/ holds empty stand-ins for the
 * headers the sources name). What the sources rely on, as ZMK and Zephyr do
 * it: zmk_behavior_invoke_binding() (app/src/behavior.c: a GLOBAL behavior
 * goes to every half on a split central and then runs here, elsewhere here
 * alone), the binding event's source only on a split
 * (app/include/zmk/behavior.h), the settings' static handlers and their
 * load, the system work queue (a FIFO the harness drains, delayed items due
 * at the fake clock) and the CDC ACM port's interrupt-driven UART (the
 * callback the port registers, fed the harness's bytes). Built by run.sh
 * port as a keyboard that is not a split, a split central and a split half
 * (harness_port.c). */
#pragma once
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#define IS_ENABLED(config_macro) Z_IS_ENABLED1(config_macro)
#define Z_IS_ENABLED1(config_macro) Z_IS_ENABLED2(_XXXX##config_macro)
#define _XXXX1 _YYYY,
#define Z_IS_ENABLED2(one_or_two_args) Z_IS_ENABLED3(one_or_two_args 1, 0)
#define Z_IS_ENABLED3(ignore_this, val, ...) val

#define BUILD_ASSERT(cond, ...) _Static_assert(cond, "" __VA_ARGS__)
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define ARG_UNUSED(x) (void)(x)
#define BIT(n) (1UL << (n))
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#define Z_STRINGIFY(s) #s
#define STRINGIFY(s) Z_STRINGIFY(s)
#define CONTAINER_OF(ptr, type, field) ((type *)(((char *)(ptr)) - offsetof(type, field)))
#define FAKE_CAT_(a, b) a##b
#define FAKE_CAT(a, b) FAKE_CAT_(a, b)
#define snprintk snprintf
/* what a registration macro ends in, so that the source's semicolon closes
 * a declaration */
#define FAKE_SEMICOLON extern int fake_semicolon

/* devicetree: one node a compatible, under the name the check builds give
 * it (the dtsi files of config/), and the port's one CDC ACM node */
#define FAKE_NODE_lantern_behavior_palette "palette"
#define FAKE_NODE_lantern_behavior_effect "effect"
#define FAKE_NODE_lantern_behavior_intro "intro"
#define FAKE_NODE_lantern_behavior_tempo "tempo"
#define FAKE_NODE_lantern_behavior_effect_next "effect_next"
#define FAKE_NODE__(compat) FAKE_NODE_##compat
#define FAKE_NODE_(compat) FAKE_NODE__(compat)
#define DT_DRV_INST(inst) FAKE_NODE_(DT_DRV_COMPAT)
#define DEVICE_DT_NAME(node) node
#define DT_NUM_INST_STATUS_OKAY(compat) 1
struct device {
    const char *name;
};
extern const struct device fake_cdc;
#define DT_FOREACH_STATUS_OKAY(compat, fn) fn(fake_cdc_node)
#define DEVICE_DT_GET(node) (&fake_cdc)
static inline bool device_is_ready(const struct device *dev) {
    ARG_UNUSED(dev);
    return true;
}

/* logging: a warning or an error is counted and its format printed (never
 * its arguments: the host's size_t is not the target's) */
extern int fake_warnings;
extern int fake_errors;
static inline void fake_log(int *count, const char *level, const char *fmt, ...) {
    if (count != NULL) {
        (*count)++;
        fprintf(stderr, "%s: %s\n", level, fmt);
    }
}
#define LOG_MODULE_REGISTER(...) FAKE_SEMICOLON
#define LOG_LEVEL_INF 3
#define LOG_INF(...) fake_log(NULL, "INF", __VA_ARGS__)
#define LOG_WRN(...) fake_log(&fake_warnings, "WRN", __VA_ARGS__)
#define LOG_ERR(...) fake_log(&fake_errors, "ERR", __VA_ARGS__)

/* atomics */
typedef long atomic_t;
typedef long atomic_val_t;
static inline atomic_val_t atomic_get(const atomic_t *p) { return *p; }
static inline atomic_val_t atomic_set(atomic_t *p, atomic_val_t v) {
    atomic_val_t o = *p;
    *p = v;
    return o;
}

/* time and work */
typedef int64_t k_timeout_t;
#define K_MSEC(ms) ((k_timeout_t)(ms))
#define K_NO_WAIT ((k_timeout_t)0)
struct k_work {
    void (*handler)(struct k_work *);
    bool queued;
};
struct k_work_delayable {
    struct k_work work;
    int64_t due;
    bool armed;
};
int64_t k_uptime_get(void);
void k_work_init(struct k_work *work, void (*handler)(struct k_work *));
int k_work_submit(struct k_work *work);
void k_work_init_delayable(struct k_work_delayable *dwork, void (*handler)(struct k_work *));
int k_work_reschedule(struct k_work_delayable *dwork, k_timeout_t delay);
static inline struct k_work_delayable *k_work_delayable_from_work(struct k_work *work) {
    return CONTAINER_OF(work, struct k_work_delayable, work);
}
#define K_WORK_DEFINE(name, fn) struct k_work name = {.handler = fn}
struct k_msgq {
    char *buf;
    size_t size;
    size_t max;
    size_t head;
    size_t used;
};
#define K_MSGQ_DEFINE(name, msg_size, max_msgs, align)                                             \
    static char FAKE_CAT(name, _buf)[(msg_size) * (max_msgs)];                                     \
    static struct k_msgq name = {.buf = FAKE_CAT(name, _buf), .size = (msg_size), .max = (max_msgs)}
int k_msgq_put(struct k_msgq *q, const void *data, k_timeout_t timeout);
int k_msgq_get(struct k_msgq *q, void *data, k_timeout_t timeout);

/* init: every SYS_INIT, in link order, when the harness boots */
#define POST_KERNEL 0
#define APPLICATION 1
#define CONFIG_KERNEL_INIT_PRIORITY_DEFAULT 40
#define CONFIG_APPLICATION_INIT_PRIORITY 90
void fake_add_init(int (*fn)(void));
#define SYS_INIT(fn, level, prio)                                                                  \
    __attribute__((constructor)) static void FAKE_CAT(fake_init_, fn)(void) { fake_add_init(fn); } \
    FAKE_SEMICOLON

/* settings: the static handlers, a store of values by key, the load */
typedef ssize_t (*settings_read_cb)(void *cb_arg, void *data, size_t len);
int settings_save_one(const char *name, const void *value, size_t val_len);
int settings_name_steq(const char *name, const char *key, const char **next);
void fake_add_settings(const char *subtree,
                       int (*set)(const char *, size_t, settings_read_cb, void *),
                       int (*commit)(void));
#define SETTINGS_STATIC_HANDLER_DEFINE(hname, tree, get, set, commit, export)                     \
    __attribute__((constructor)) static void FAKE_CAT(fake_settings_, hname)(void) {               \
        fake_add_settings(tree, set, commit);                                                      \
    }                                                                                              \
    FAKE_SEMICOLON

/* behaviors */
enum behavior_locality {
    BEHAVIOR_LOCALITY_CENTRAL,
    BEHAVIOR_LOCALITY_EVENT_SOURCE,
    BEHAVIOR_LOCALITY_GLOBAL,
};
struct zmk_behavior_binding {
    const char *behavior_dev;
    uint32_t param1;
    uint32_t param2;
};
struct zmk_behavior_binding_event {
    int layer;
    uint32_t position;
    int64_t timestamp;
#if IS_ENABLED(CONFIG_ZMK_SPLIT)
    uint8_t source;
#endif
};
#define ZMK_BEHAVIOR_OPAQUE 0
#define ZMK_BEHAVIOR_TRANSPARENT 1
struct behavior_driver_api {
    enum behavior_locality locality;
    int (*binding_pressed)(struct zmk_behavior_binding *, struct zmk_behavior_binding_event);
    int (*binding_released)(struct zmk_behavior_binding *, struct zmk_behavior_binding_event);
};
void fake_add_behavior(const char *name, const struct behavior_driver_api *api);
#define BEHAVIOR_DT_INST_DEFINE(inst, init, pm, data, cfg, level, prio, api_ptr)                  \
    __attribute__((constructor)) static void FAKE_CAT(fake_behavior_, inst)(void) {                \
        fake_add_behavior(DEVICE_DT_NAME(DT_DRV_INST(inst)), api_ptr);                             \
    }                                                                                              \
    FAKE_SEMICOLON
int zmk_behavior_invoke_binding(const struct zmk_behavior_binding *binding,
                                struct zmk_behavior_binding_event event, bool pressed);
#define ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL UINT8_MAX

/* events: the listeners, called with the harness's battery reports */
typedef struct {
    int kind;
} zmk_event_t;
#define ZMK_EV_EVENT_BUBBLE 0
struct zmk_peripheral_battery_state_changed {
    zmk_event_t header;
    uint8_t source;
    uint8_t state_of_charge;
};
#define FAKE_EVENT_PERIPHERAL_BATTERY 1
const struct zmk_peripheral_battery_state_changed *
as_zmk_peripheral_battery_state_changed(const zmk_event_t *eh);
void fake_add_listener(int (*cb)(const zmk_event_t *));
#define ZMK_LISTENER(mod, cb)                                                                      \
    __attribute__((constructor)) static void FAKE_CAT(fake_listener_, mod)(void) {                 \
        fake_add_listener(cb);                                                                     \
    }                                                                                              \
    FAKE_SEMICOLON
#define ZMK_SUBSCRIPTION(mod, ev) FAKE_SEMICOLON

/* the split central's transport: every invocation recorded */
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
#define ZMK_SPLIT_CENTRAL_PERIPHERAL_COUNT 2
int zmk_split_central_invoke_behavior(uint8_t source, struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event, bool state);
#endif

/* the CDC ACM port's UART */
typedef void (*uart_irq_callback_user_data_t)(const struct device *dev, void *user_data);
int uart_irq_callback_user_data_set(const struct device *dev, uart_irq_callback_user_data_t cb,
                                    void *user_data);
void uart_irq_rx_enable(const struct device *dev);
int uart_irq_update(const struct device *dev);
int uart_irq_rx_ready(const struct device *dev);
int uart_fifo_read(const struct device *dev, uint8_t *rx_data, const int size);
void uart_poll_out(const struct device *dev, unsigned char out_char);
