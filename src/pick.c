/*
 * Copyright (c) 2026 akira-toriyama
 *
 * SPDX-License-Identifier: MIT
 *
 * The pick's mechanism (src/pick.h): what the pick of the effects
 * (src/effect.c, canon t-5hqx) and the pick of the intro (src/intro.c, canon
 * t-qvgv) share. A pick is a mask of bits the instance's table gives a meaning
 * (src/effect.h, src/intro.h) and, where the instance has one, an argument
 * (the effects' colour, canon t-rgp5), chosen by the host on the split
 * central's port (src/port.c) or by a key of the central's keymap bound to the
 * behavior, kept by the central and every half in their settings across boots,
 * and read where it is used (the key LEDs, src/keys.c). The image carries
 * every effect and intro the consumer's conf turns on; the pick says which
 * show, so canon changes the effect of the day, its colour and the intro
 * without flashing (its dongle.py effect and intro), and a consumer without a
 * host has the Kconfig defaults, or binds the behavior to a key.
 *
 * - One path for every change, the instance's behavior (lantern,behavior-effect
 *   named effect, lantern,behavior-intro named intro, the same node on every
 *   device of the split), invoked by the central's code with
 *   zmk_behavior_invoke_binding() as the palette's is (src/palette.c says how
 *   ZMK's split transport carries it by the node's name and why the name is at
 *   most 8 characters): param1 = the mask, absolute, param2 = the argument (0
 *   without one), press only. A key of the central's keymap bound to the
 *   behavior invokes it the same way (ZMK app/src/behavior.c: a GLOBAL
 *   behavior goes to every half and then runs here), on press and again on
 *   release, which stores the same values and changes nothing. One
 *   invocation a pick, so the central's run queue, which src/palette.c
 *   sizes for a whole palette a half, must hold one more a half for each pick
 *   built (asserted below): a resend after a half's battery report
 *   (src/palette.c: the one signal ZMK's central raises for a half that is
 *   back) queues the palette's three and each pick's one to that half at once,
 *   and both halves may report together. More in one go (lines from the port
 *   with a resend behind them, all put from the system work queue, which ZMK's
 *   drain cannot preempt) waits 100 ms a put and drops the oldest only when the
 *   drain stalls on a half that stopped acknowledging (ZMK central.c;
 *   src/palette.c says why that is accepted).
 * - A mask is the host's bits as sent, the ones this image does not build
 *   included, and the ones its table does not know either (a newer image's
 *   central may send a bit this half's table lacks), so that a device flashed
 *   with more shows the pick it already holds, an older one draws the bits it
 *   knows, and the port answers with the names this image knows; the reader
 *   masks with what is built. 0 is no pick: the port refuses it, the handler
 *   too, and a stored 0 is ignored at load; the reader then has the default.
 *   The central refuses a bit its own table lacks and, where the instance
 *   takes one bit (the intro), a mask of several; a half stores what it is
 *   sent and leaves the choice among several to the reader.
 * - Settings: the instance's key ("lantern/<word>/mask"), the mask as one
 *   32-bit value, and the argument under its own key ("lantern/effect/colour"),
 *   both saved when either changed or the first pick arrived, never for a
 *   pick a device already holds (a half is resent the pick at every battery
 *   report, as the palette); a stored value of another size is ignored at
 *   load, and a device without the argument's key has 0 (a device flashed
 *   from before the argument keeps its mask). The instance's changed hook runs
 *   after a changed or first store (the
 *   effects' clears the effects' state and repaints, src/keys.c; the intro's
 *   is none: the next boot light reads the pick). At boot the reader takes the
 *   mask itself after settings_load() restored it, so this subtree's commit
 *   handler only marks the load done. Until then a send is refused with
 *   -EAGAIN, as the palette's.
 * - Threads, as src/palette.c's: the handler runs where ZMK runs behaviors (a
 *   half: the Bluetooth receive work queue; the central: the system work
 *   queue, where the port's line and the resend run); the mask and the two
 *   flags are atomics; the save is a work item on the system work queue (a
 *   flash write stays off the Bluetooth queue).
 * - Delivery is not confirmed, as the palette's: the next battery report of a
 *   half resends the pick, RESEND_DELAY_MS after it (src/palette.c says why
 *   the wait).
 */

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
#include <zmk/behavior.h>
#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/split/central.h>
#endif

#include "palette.h"
#include "pick.h"

LOG_MODULE_REGISTER(lantern_pick, LOG_LEVEL_INF);

BUILD_ASSERT(IS_ENABLED(CONFIG_SETTINGS),
             "CONFIG_LANTERN_PICK needs CONFIG_SETTINGS: a pick lives in the settings");
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
BUILD_ASSERT(IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING),
             "a pick on a split central needs CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING: "
             "a half's battery report is what tells the central the half is back, and the pick is "
             "resent to it then");
/* A resend to a half is the palette's invocations and one per pick built;
 * both halves may be resent at once (the header). */
#define RESEND_INVOCATIONS                                                                         \
    ((IS_ENABLED(CONFIG_LANTERN_PALETTE) ? LANTERN_PALETTE_PAIRS : 0) +                            \
     (IS_ENABLED(CONFIG_LANTERN_EFFECT) ? 1 : 0) + (IS_ENABLED(CONFIG_LANTERN_INTRO) ? 1 : 0))
BUILD_ASSERT(RESEND_INVOCATIONS * ZMK_SPLIT_CENTRAL_PERIPHERAL_COUNT <=
                 CONFIG_ZMK_SPLIT_BLE_CENTRAL_SPLIT_RUN_QUEUE_SIZE,
             "CONFIG_ZMK_SPLIT_BLE_CENTRAL_SPLIT_RUN_QUEUE_SIZE must hold a whole palette and every "
             "pick for every half at once (the header says why; 10 for two halves with the palette, "
             "the effects' pick and the intro's)");
#endif

#define RESEND_DELAY_MS 1000

bool lantern_pick_get(struct lantern_pick *p, uint32_t *out, uint32_t *arg) {
    if (!atomic_get(&p->have)) {
        return false;
    }
    *out = (uint32_t)atomic_get(&p->mask);
    if (arg != NULL) {
        *arg = (uint32_t)atomic_get(&p->arg);
    }
    return true;
}

/* System work queue: the mask, and the argument where the instance has one. */
static void save(struct k_work *work) {
    struct lantern_pick *p = CONTAINER_OF(work, struct lantern_pick, save_work);
    uint32_t copy = (uint32_t)atomic_get(&p->mask);
    int rc = settings_save_one(p->key, &copy, sizeof(copy));
    if (rc >= 0 && p->arg_key != NULL) {
        copy = (uint32_t)atomic_get(&p->arg);
        rc = settings_save_one(p->arg_key, &copy, sizeof(copy));
    }
    if (rc < 0) {
        LOG_ERR("%s: save failed: %d", p->name, rc);
    }
}

int lantern_pick_pressed(struct lantern_pick *p, uint32_t m, uint32_t a) {
    if (m == 0) {
        LOG_WRN("%s: an empty pick", p->name);
        return -EINVAL;
    }
    bool changed = (uint32_t)atomic_set(&p->mask, (atomic_val_t)m) != m;
    changed = ((uint32_t)atomic_set(&p->arg, (atomic_val_t)a) != a) || changed;
    bool new = atomic_set(&p->have, 1) == 0;
    if (changed || new) {
        k_work_submit(&p->save_work);
    }
    if (p->arg_name != NULL) {
        LOG_INF("%s: %08x %06x %s", p->name, m, a, changed || new ? "stored" : "unchanged");
    } else {
        LOG_INF("%s: %08x %s", p->name, m, changed || new ? "stored" : "unchanged");
    }
    if ((changed || new) && p->changed != NULL) {
        p->changed();
    }
    return 0;
}

/* One stored 32-bit value, or -EINVAL for another size. */
static int read_value(struct lantern_pick *p, size_t len, settings_read_cb read_cb, void *cb_arg,
                      uint32_t *out) {
    if (len != sizeof(*out)) {
        LOG_WRN("%s: stored value of %u bytes ignored", p->name, (unsigned int)len);
        return -EINVAL;
    }
    ssize_t rc = read_cb(cb_arg, out, sizeof(*out));
    return rc < 0 ? (int)rc : 0;
}

int lantern_pick_set(struct lantern_pick *p, const char *name, size_t len, settings_read_cb read_cb,
                     void *cb_arg) {
    const char *next;
    uint32_t copy;
    int rc;

    if (settings_name_steq(name, "mask", &next) && !next) {
        rc = read_value(p, len, read_cb, cb_arg, &copy);
        if (rc < 0) {
            return rc;
        }
        if (copy == 0) {
            LOG_WRN("%s: stored empty pick ignored", p->name);
            return -EINVAL;
        }
        atomic_set(&p->mask, (atomic_val_t)copy);
        atomic_set(&p->have, 1);
        return 0;
    }
    if (p->arg_name != NULL && settings_name_steq(name, p->arg_name, &next) && !next) {
        rc = read_value(p, len, read_cb, cb_arg, &copy);
        if (rc < 0) {
            return rc;
        }
        atomic_set(&p->arg, (atomic_val_t)copy);
        return 0;
    }
    return -ENOENT;
}

void lantern_pick_commit(struct lantern_pick *p) { atomic_set(&p->loaded, 1); }

#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)

static struct zmk_behavior_binding_event local_event(void) {
    return (struct zmk_behavior_binding_event){
        .layer = 0,
        .position = 0,
        .timestamp = k_uptime_get(),
        .source = ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL,
    };
}

static struct zmk_behavior_binding binding_of(const struct lantern_pick *p, uint32_t m,
                                              uint32_t a) {
    return (struct zmk_behavior_binding){
        .behavior_dev = p->behavior,
        .param1 = m,
        .param2 = a,
    };
}

int lantern_pick_send(struct lantern_pick *p, uint32_t m, uint32_t a) {
    if (m == 0 || (m & ~p->all) != 0 || a > p->arg_max) {
        return -EINVAL;
    }
    if (p->one && __builtin_popcount(m) != 1) {
        return -E2BIG;
    }
    if (!atomic_get(&p->loaded)) {
        return -EAGAIN;
    }
    struct zmk_behavior_binding binding = binding_of(p, m, a);
    return zmk_behavior_invoke_binding(&binding, local_event(), true);
}

/* System work queue. */
static void resend(struct k_work *work) {
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    struct lantern_pick_resend *r = CONTAINER_OF(dwork, struct lantern_pick_resend, work);
    struct lantern_pick *p = r->pick;

    if (!atomic_get(&p->have)) {
        return;
    }
    struct zmk_behavior_binding binding =
        binding_of(p, (uint32_t)atomic_get(&p->mask), (uint32_t)atomic_get(&p->arg));
    int err = zmk_split_central_invoke_behavior(r->source, &binding, local_event(), true);
    if (err < 0) {
        LOG_WRN("%s: resend to half %u failed: %d", p->name, r->source, err);
        return;
    }
    LOG_INF("%s: resent to half %u", p->name, r->source);
}

void lantern_pick_resend_after(struct lantern_pick *p, const zmk_event_t *eh) {
    const struct zmk_peripheral_battery_state_changed *ev =
        as_zmk_peripheral_battery_state_changed(eh);
    if (ev == NULL) {
        return;
    }
    if (ev->source < ARRAY_SIZE(p->resends) && ev->state_of_charge > 0 && atomic_get(&p->have)) {
        k_work_reschedule(&p->resends[ev->source].work, K_MSEC(RESEND_DELAY_MS));
    }
}

#endif /* IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL) */

void lantern_pick_init(struct lantern_pick *p) {
    k_work_init(&p->save_work, save);
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    for (size_t i = 0; i < ARRAY_SIZE(p->resends); i++) {
        k_work_init_delayable(&p->resends[i].work, resend);
        p->resends[i].pick = p;
        p->resends[i].source = (uint8_t)i;
    }
#endif
}
