/* What the stubs of pstub_port.h share, one copy for every translation unit,
 * and the harness's handles on it (fake.h). zmk_behavior_invoke_binding()
 * follows ZMK's app/src/behavior.c (4dd95d78) and settings_name_steq()
 * Zephyr's subsys/settings/src/settings.c; the rest is the least the sources
 * need. */
#include <stdlib.h>

#include "fake.h"

#define REG_MAX 16

int fake_warnings;
int fake_errors;
int fake_saves;
int fake_refreshes;
int fake_effect_changes;
struct fake_sent fake_sent[FAKE_SENT_MAX];
int fake_nsent;

const struct device fake_cdc = {.name = "CDC_ACM_0"};

/* the key LEDs' hooks (src/keys.h) */
void lantern_keys_refresh(void) { fake_refreshes++; }
void lantern_keys_effect_changed(void) { fake_effect_changes++; }

/* init */
static int (*inits[REG_MAX])(void);
static int ninits;
void fake_add_init(int (*fn)(void)) { inits[ninits++] = fn; }
void fake_boot(void) {
    for (int i = 0; i < ninits; i++) {
        inits[i]();
    }
}

/* the clock and the system work queue */
static int64_t now;
int64_t k_uptime_get(void) { return now; }
static struct k_work *queue[64];
static int queued;
static struct k_work_delayable *delayables[REG_MAX];
static int ndelayables;
void k_work_init(struct k_work *work, void (*handler)(struct k_work *)) {
    *work = (struct k_work){.handler = handler};
}
int k_work_submit(struct k_work *work) {
    if (work->queued) {
        return 0;
    }
    if (queued == (int)ARRAY_SIZE(queue)) {
        fprintf(stderr, "fake: the work queue is full\n");
        exit(2);
    }
    work->queued = true;
    queue[queued++] = work;
    return 1;
}
void k_work_init_delayable(struct k_work_delayable *dwork, void (*handler)(struct k_work *)) {
    *dwork = (struct k_work_delayable){.work = {.handler = handler}};
    delayables[ndelayables++] = dwork;
}
int k_work_reschedule(struct k_work_delayable *dwork, k_timeout_t delay) {
    dwork->due = now + delay;
    dwork->armed = true;
    return 1;
}
void fake_run(void) {
    for (;;) {
        for (int i = 0; i < ndelayables; i++) {
            if (delayables[i]->armed && delayables[i]->due <= now) {
                delayables[i]->armed = false;
                k_work_submit(&delayables[i]->work);
            }
        }
        if (queued == 0) {
            return;
        }
        struct k_work *w = queue[0];
        memmove(queue, queue + 1, (size_t)(queued - 1) * sizeof(queue[0]));
        queued--;
        w->queued = false;
        w->handler(w);
    }
}
void fake_advance(int64_t ms) {
    now += ms;
    fake_run();
}

/* message queues */
int k_msgq_put(struct k_msgq *q, const void *data, k_timeout_t timeout) {
    ARG_UNUSED(timeout);
    if (q->used == q->max) {
        return -ENOMSG;
    }
    memcpy(q->buf + ((q->head + q->used) % q->max) * q->size, data, q->size);
    q->used++;
    return 0;
}
int k_msgq_get(struct k_msgq *q, void *data, k_timeout_t timeout) {
    ARG_UNUSED(timeout);
    if (q->used == 0) {
        return -ENOMSG;
    }
    memcpy(data, q->buf + q->head * q->size, q->size);
    q->head = (q->head + 1) % q->max;
    q->used--;
    return 0;
}

/* settings */
struct value {
    char key[48];
    unsigned char data[32];
    size_t len;
};
static struct value store[REG_MAX];
static int nstore;
struct handler {
    const char *subtree;
    int (*set)(const char *, size_t, settings_read_cb, void *);
    int (*commit)(void);
};
static struct handler handlers[REG_MAX];
static int nhandlers;
void fake_add_settings(const char *subtree,
                       int (*set)(const char *, size_t, settings_read_cb, void *),
                       int (*commit)(void)) {
    handlers[nhandlers++] = (struct handler){subtree, set, commit};
}
static struct value *find(const char *key) {
    for (int i = 0; i < nstore; i++) {
        if (strcmp(store[i].key, key) == 0) {
            return &store[i];
        }
    }
    return NULL;
}
void fake_store(const char *key, const void *value, size_t len) {
    struct value *v = find(key);
    if (v == NULL) {
        v = &store[nstore++];
    }
    snprintf(v->key, sizeof(v->key), "%s", key);
    if (len > sizeof(v->data)) {
        fprintf(stderr, "fake: a value of %zu bytes\n", len);
        exit(2);
    }
    memcpy(v->data, value, len);
    v->len = len;
}
int settings_save_one(const char *name, const void *value, size_t val_len) {
    fake_saves++;
    fake_store(name, value, val_len);
    return 0;
}
const void *fake_stored(const char *key, size_t *len) {
    struct value *v = find(key);
    if (v == NULL) {
        return NULL;
    }
    *len = v->len;
    return v->data;
}
int settings_name_steq(const char *name, const char *key, const char **next) {
    if (next != NULL) {
        *next = NULL;
    }
    if (name == NULL || key == NULL) {
        return 0;
    }
    while (*key != '\0' && *key == *name && *name != '\0' && *name != '=') {
        key++;
        name++;
    }
    if (*key != '\0') {
        return 0;
    }
    if (*name == '/') {
        if (next != NULL) {
            *next = name + 1;
        }
        return 1;
    }
    return *name == '=' || *name == '\0';
}
static ssize_t read_value(void *cb_arg, void *data, size_t len) {
    const struct value *v = cb_arg;
    size_t n = len < v->len ? len : v->len;
    memcpy(data, v->data, n);
    return (ssize_t)n;
}
void fake_load(void) {
    for (int i = 0; i < nstore; i++) {
        const struct handler *best = NULL;
        const char *rest = NULL;
        for (int h = 0; h < nhandlers; h++) {
            const char *next;
            if (settings_name_steq(store[i].key, handlers[h].subtree, &next) && next != NULL &&
                (best == NULL || strlen(handlers[h].subtree) > strlen(best->subtree))) {
                best = &handlers[h];
                rest = next;
            }
        }
        if (best != NULL) {
            best->set(rest, store[i].len, read_value, &store[i]);
        }
    }
    for (int h = 0; h < nhandlers; h++) {
        if (handlers[h].commit != NULL) {
            handlers[h].commit();
        }
    }
}

/* behaviors and the split central's transport */
struct behavior {
    const char *name;
    const struct behavior_driver_api *api;
};
static struct behavior behaviors[REG_MAX];
static int nbehaviors;
void fake_add_behavior(const char *name, const struct behavior_driver_api *api) {
    behaviors[nbehaviors++] = (struct behavior){name, api};
}
static const struct behavior_driver_api *behavior_api(const char *name) {
    for (int i = 0; i < nbehaviors; i++) {
        if (strcmp(behaviors[i].name, name) == 0) {
            return behaviors[i].api;
        }
    }
    return NULL;
}
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
int zmk_split_central_invoke_behavior(uint8_t source, struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event, bool state) {
    ARG_UNUSED(event);
    if (state) {
        if (fake_nsent == FAKE_SENT_MAX) {
            fprintf(stderr, "fake: too many sends\n");
            exit(2);
        }
        fake_sent[fake_nsent++] =
            (struct fake_sent){source, binding->behavior_dev, binding->param1, binding->param2};
    }
    return 0;
}
#endif
static int invoke_locally(const struct behavior_driver_api *api, struct zmk_behavior_binding *b,
                          struct zmk_behavior_binding_event event, bool pressed) {
    return pressed ? api->binding_pressed(b, event) : api->binding_released(b, event);
}
int zmk_behavior_invoke_binding(const struct zmk_behavior_binding *src_binding,
                                struct zmk_behavior_binding_event event, bool pressed) {
    struct zmk_behavior_binding binding = *src_binding;
    const struct behavior_driver_api *api = behavior_api(binding.behavior_dev);

    if (api == NULL) {
        return 1;
    }
    switch (api->locality) {
    case BEHAVIOR_LOCALITY_CENTRAL:
        return invoke_locally(api, &binding, event, pressed);
    case BEHAVIOR_LOCALITY_EVENT_SOURCE:
#if IS_ENABLED(CONFIG_ZMK_SPLIT) && IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
        if (event.source == ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL) {
            return invoke_locally(api, &binding, event, pressed);
        }
        return zmk_split_central_invoke_behavior(event.source, &binding, event, pressed);
#else
        return invoke_locally(api, &binding, event, pressed);
#endif
    case BEHAVIOR_LOCALITY_GLOBAL:
#if IS_ENABLED(CONFIG_ZMK_SPLIT) && IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
        for (int i = 0; i < ZMK_SPLIT_CENTRAL_PERIPHERAL_COUNT; i++) {
            zmk_split_central_invoke_behavior((uint8_t)i, &binding, event, pressed);
        }
#endif
        return invoke_locally(api, &binding, event, pressed);
    }
    return -ENOTSUP;
}
static struct zmk_behavior_binding_event key_event(void) {
    return (struct zmk_behavior_binding_event){
        .timestamp = now,
#if IS_ENABLED(CONFIG_ZMK_SPLIT)
        .source = ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL,
#endif
    };
}
int fake_press(const char *behavior, uint32_t param1, uint32_t param2) {
    struct zmk_behavior_binding b = {behavior, param1, param2};
    int rc = zmk_behavior_invoke_binding(&b, key_event(), true);
    zmk_behavior_invoke_binding(&b, key_event(), false);
    fake_run();
    return rc;
}
int fake_receive(const char *behavior, uint32_t param1, uint32_t param2) {
    struct zmk_behavior_binding b = {behavior, param1, param2};
    const struct behavior_driver_api *api = behavior_api(behavior);
    int rc = api == NULL ? -ENODEV : invoke_locally(api, &b, key_event(), true);
    fake_run();
    return rc;
}

/* events */
static int (*listeners[REG_MAX])(const zmk_event_t *);
static int nlisteners;
void fake_add_listener(int (*cb)(const zmk_event_t *)) { listeners[nlisteners++] = cb; }
const struct zmk_peripheral_battery_state_changed *
as_zmk_peripheral_battery_state_changed(const zmk_event_t *eh) {
    if (eh->kind != FAKE_EVENT_PERIPHERAL_BATTERY) {
        return NULL;
    }
    size_t at = offsetof(struct zmk_peripheral_battery_state_changed, header);
    return (const struct zmk_peripheral_battery_state_changed *)((const char *)eh - at);
}
void fake_battery(uint8_t source, uint8_t level) {
    struct zmk_peripheral_battery_state_changed ev = {
        .header = {FAKE_EVENT_PERIPHERAL_BATTERY}, .source = source, .state_of_charge = level};
    for (int i = 0; i < nlisteners; i++) {
        listeners[i](&ev.header);
    }
    fake_run();
}

/* the port's UART */
static uart_irq_callback_user_data_t rx_cb;
static void *rx_user;
static char rx[512];
static size_t rx_len, rx_at;
static char tx[2048];
static size_t tx_len;
int uart_irq_callback_user_data_set(const struct device *dev, uart_irq_callback_user_data_t cb,
                                    void *user_data) {
    ARG_UNUSED(dev);
    rx_cb = cb;
    rx_user = user_data;
    return 0;
}
void uart_irq_rx_enable(const struct device *dev) { ARG_UNUSED(dev); }
int uart_irq_update(const struct device *dev) {
    ARG_UNUSED(dev);
    return 1;
}
int uart_irq_rx_ready(const struct device *dev) {
    ARG_UNUSED(dev);
    return rx_at < rx_len;
}
int uart_fifo_read(const struct device *dev, uint8_t *rx_data, const int size) {
    ARG_UNUSED(dev);
    int n = 0;
    while (n < size && rx_at < rx_len) {
        rx_data[n++] = (uint8_t)rx[rx_at++];
    }
    return n;
}
void uart_poll_out(const struct device *dev, unsigned char out_char) {
    ARG_UNUSED(dev);
    if (tx_len + 1 < sizeof(tx)) {
        tx[tx_len++] = (char)out_char;
        tx[tx_len] = '\0';
    }
}
void fake_line(const char *text) {
    if (rx_cb == NULL) {
        fprintf(stderr, "fake: no callback on the port\n");
        exit(2);
    }
    rx_len = (size_t)snprintf(rx, sizeof(rx), "%s\n", text);
    rx_at = 0;
    tx_len = 0;
    tx[0] = '\0';
    rx_cb(&fake_cdc, rx_user);
    fake_run();
}
/* The last line the port wrote since fake_line(): a reply is a newline, the
 * text and a newline (src/port.c). */
const char *fake_reply(void) {
    static char line[256];
    size_t end = tx_len;
    while (end > 0 && tx[end - 1] == '\n') {
        end--;
    }
    size_t start = end;
    while (start > 0 && tx[start - 1] != '\n') {
        start--;
    }
    snprintf(line, sizeof(line), "%.*s", (int)(end - start), tx + start);
    return line;
}
