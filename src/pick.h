/*
 * Copyright (c) 2026 akira-toriyama
 *
 * SPDX-License-Identifier: MIT
 *
 * A pick (src/pick.c): a value the split central's host chooses, kept by the
 * central and every half in their settings across boots and resent by the
 * central to a half that is back. Two instances: the pick of the effects
 * (src/effect.c) and the pick of the intro (src/intro.c), each with a behavior
 * node, a settings key and a word on the port (src/port.c) of its own; the
 * mechanism is this file's and src/pick.c's, and only the two instances
 * include it. Layer contract: an instance owns the Zephyr and ZMK registrations
 * (the behavior, the settings handler, the battery listener, the init), each a
 * wrapper handing its struct lantern_pick here, and says in the struct what is
 * its own; src/pick.c stores, saves, sends and resends, and never knows what
 * the bits mean; the port parses and replies and never stores.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
#include <zmk/event_manager.h>
#include <zmk/split/central.h>
#endif

/* An instance. The first fields are the instance's, set in its initializer;
 * the rest are src/pick.c's. */
struct lantern_pick {
    const char *name;      /* the port's word, and the log's */
    const char *key;       /* the settings key, "lantern/<word>/mask" */
    const char *behavior;  /* the node's device name (DEVICE_DT_NAME), for the central's sends */
    uint32_t all;          /* every bit the instance's table knows */
    bool one;              /* a pick is exactly one bit (the intro), else one or more (the effects) */
    void (*changed)(void); /* after a stored pick changed, or the first arrived; NULL for none */

    atomic_t mask;   /* the host's bits as sent */
    atomic_t have;   /* a pick is stored */
    atomic_t loaded; /* the settings have loaded: a send is refused before */
    struct k_work save_work;
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    struct lantern_pick_resend {
        struct k_work_delayable work;
        struct lantern_pick *pick;
        uint8_t source;
    } resends[ZMK_SPLIT_CENTRAL_PERIPHERAL_COUNT];
#endif
};

/* The work items; from the instance's SYS_INIT at APPLICATION, before the
 * settings load and before any behavior runs. */
void lantern_pick_init(struct lantern_pick *p);

/* The pick stored on this device, the host's bits as sent (built here or
 * not). False while none has been stored, and *mask is then left alone. Any
 * thread. */
bool lantern_pick_get(struct lantern_pick *p, uint32_t *mask);

/* The behavior's press, where ZMK runs behaviors: store mask (saved when it
 * changed or is the first, then the instance's changed hook) and return 0; an
 * empty mask is refused with -EINVAL. The bits are stored as sent, those this
 * image's table lacks included (src/pick.c says why). */
int lantern_pick_pressed(struct lantern_pick *p, uint32_t mask);

/* The settings handler's set and commit, on the main thread inside
 * settings_load(): the stored value under the instance's subtree (the key
 * "mask"; a value of another size, or an empty one, is ignored), and the load
 * done. */
int lantern_pick_set(struct lantern_pick *p, const char *name, size_t len, settings_read_cb read_cb,
                     void *cb_arg);
void lantern_pick_commit(struct lantern_pick *p);

#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)

/* Split central only, the system work queue (the port): store mask here and
 * send it to every half, one invocation of the instance's behavior. mask is
 * one or more bits of all, else -EINVAL; one bit where the instance takes one,
 * else -E2BIG; -EAGAIN until the settings have loaded at boot. 0 means this
 * device stored the pick and handed the halves' invocations to ZMK's split
 * transport, which confirms nothing: a half that is away gets the pick when it
 * is back (the resend). */
int lantern_pick_send(struct lantern_pick *p, uint32_t mask);

/* The instance's battery listener's body: a half's battery report, the one
 * signal ZMK's central raises for a half that is back, schedules a resend of
 * the stored pick to that half (src/pick.c says when). */
void lantern_pick_resend_after(struct lantern_pick *p, const zmk_event_t *eh);

#endif /* IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL) */
