/*
 * Copyright (c) 2026 akira-toriyama
 *
 * SPDX-License-Identifier: MIT
 *
 * Light the key LEDs while USB powers this half and the half is in use, and
 * keep them dark and unpowered otherwise: on battery, and once ZMK's activity
 * state has left ACTIVE (CONFIG_LANTERN_OFF_WHEN_IDLE). Lit = the LED rail
 * (the lantern,rail node's gpios) on, then the palette's picture
 * (lantern_keys_light(), src/keys.c); dark = a black frame
 * (lantern_keys_blank()), then the rail off BLANK_MS later. At boot the LEDs
 * light for CONFIG_LANTERN_BOOT_LIGHT_MS whatever the power, then the rule
 * applies; lantern_keys_boot_light() tells the picture the boot light's state
 * before every light and again at its end with the LEDs lit, since the no
 * picture, which paints every key black, shows the bands for it, and the
 * intro plays in it (src/keys.c). With the intro (CONFIG_LANTERN_INTRO) the
 * boot light lasts LANTERN_INTRO_MS (keys.h) and opens again whenever USB
 * power arrives, unless it is already open: a cable plugged in, into an idle
 * half too, plays the intro, then the rule applies as after a boot (the
 * user's wish, canon t-5v1f: the light at power-on, by the switch or by the
 * cable). The rule stays true while it is open, so only its deadline is
 * pending, as at boot. The LIT step cannot tell that deadline from a rule
 * change, so an arrival processed while a dark step (an unplug's or an
 * idle's) is still queued would end its intro at once; the USB events cannot
 * overtake that step:
 * ZMK raises them from one work item that reads the live state, and the USB
 * driver's queue is cooperative at the system queue's priority (Opus review,
 * 2026-10-21). Raise the event or open the boot light from elsewhere and
 * that breaks.
 *
 * - The rail is driven here, never by ZMK's zmk,ext-power-generic driver:
 *   its init turns the rail on, its settings handler restores a stored
 *   "ext_power/state/<node>" (a stored off, from a stock keymap's &ext_power
 *   EP_OFF, kept the left half of canon's Cyboard Imprint dark, 2026-10-19),
 *   and with CONFIG_ZMK_SLEEP=n nothing turns the rail off again. The
 *   consumer keeps its EXT_POWER node disabled; a gpio hog may hold the pin
 *   low from POST_KERNEL (before it the pin is in its reset state, an input)
 *   until lantern_init() configures it at APPLICATION; nothing reserves a
 *   hogged pin.
 * - Every state change runs on the system work queue: the listener (ZMK
 *   calls listeners in the raising thread, and raises both events from work
 *   items on that queue: zmk_usb_conn_state_changed from app/src/usb.c, and
 *   zmk_activity_state_changed from wherever activity is noted, the key
 *   scan's message processor in app/src/physical_layouts.c, the sensor work
 *   in app/src/sensors.c, the pointing input's note_activity_work, and from
 *   the one-second idle check, activity_work, both in app/src/activity.c),
 *   the delayed steps and the first evaluation, so the state below needs no
 *   lock. The settings commit handler runs on the main thread and only
 *   submits the work.
 * - The first evaluation starts from a settings commit handler at commit
 *   priority 1, not from SYS_INIT: settings_load() (ZMK app/src/main.c) runs
 *   after every init level, and the palette (src/palette.c) is restored in
 *   its set phase, so a boot light that starts from the commit phase shows
 *   the palette's picture, not a white frame that the palette's own commit
 *   then repaints. Nothing else writes the strip (src/keys.c asserts ZMK's
 *   underglow off), so the first frame is this machine's.
 * - USB: zmk_usb_is_powered() is false for USB_DC_UNKNOWN and DISCONNECTED
 *   and true for the rest (CONNECTED, RESET, CONFIGURED, SUSPEND, RESUME,
 *   ...), so a host asleep and a charger both count as powered, and only a
 *   change of that boolean acts. A cable present at boot shows as CONNECTED
 *   once zmk_usb_init() (APPLICATION 96) has enabled the device: the nRF USB
 *   driver re-raises the latched power-ready event through its own work
 *   queue (zephyr drivers/usb/device/usb_dc_nrfx.c), so the first evaluation
 *   may run before or after that status, and the listener covers the first
 *   case.
 * - Idle: ZMK's activity state (app/src/activity.c), not a timer of this
 *   module's. ZMK already counts what the half does (key positions, sensor
 *   events, pointing input) and turns IDLE after CONFIG_ZMK_IDLE_TIMEOUT ms
 *   without any, per device: a split half knows nothing of the other half's
 *   keys, so each goes dark on its own. The timeout is the consumer's line
 *   (canon's halves: 300000). SLEEP counts as idle (unreachable with
 *   CONFIG_ZMK_SLEEP=n). A cable plugged into an idle half lights nothing
 *   until its next input: USB is no activity to ZMK, and lighting on it would
 *   leave the LEDs on with no idle event to follow; with the intro it plays
 *   the intro and goes dark at its end, a timed light that needs no idle
 *   event. On battery the idle input
 *   changes nothing (the rule is already false) and is only kept current.
 *   In canon's halves the only other subscriber ZMK links is battery.c, which
 *   pauses its sampling while idle (zmk.map, 2026-10-19). The countdown
 *   (src/keys.c) cuts the same timeout into stages for the picture, from its
 *   own clock of the half's keys; the dark stays this rule's.
 * - The frames: lantern_keys_light(), lantern_keys_blank() and
 *   lantern_keys_boot_light() take no lock and block on nothing; each queues
 *   at most one frame on ZMK's low priority work queue (src/keys.c).
 *   settle-ms (the node) covers the LEDs' power-up before
 *   the first frame, as the ext-power node's init-delay-ms did; BLANK_MS
 *   covers the black frame before the rail drops (a 29-LED frame at 4 MHz
 *   takes 1.4 ms, 7 ms at the Cyboard overlay's chain-length of 150). The
 *   only way to DARK is through BLANKING, so no frame the machine knows of
 *   meets an unpowered strip, and a palette that arrives while dark is kept
 *   for the next light (src/keys.c repaints only while lit).
 */

#define DT_DRV_COMPAT lantern_rail

#include <stdbool.h>
#include <stddef.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/util.h>

#include <zmk/activity.h>
#include <zmk/event_manager.h>
#include <zmk/events/activity_state_changed.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/usb.h>

#include "keys.h"

LOG_MODULE_REGISTER(lantern, LOG_LEVEL_INF);

BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) == 1,
             "CONFIG_LANTERN_ON_USB_POWER needs exactly one lantern,rail node");
BUILD_ASSERT(IS_ENABLED(CONFIG_USB_DEVICE_STACK),
             "CONFIG_LANTERN_ON_USB_POWER needs CONFIG_USB_DEVICE_STACK: the USB state comes "
             "from ZMK's usb.c");
BUILD_ASSERT(IS_ENABLED(CONFIG_SETTINGS),
             "CONFIG_LANTERN_ON_USB_POWER needs CONFIG_SETTINGS: the first evaluation runs "
             "from settings_load()");
/* The black frame lantern_keys_blank() queues on ZMK's low priority work
 * queue must reach the LEDs before the rail drops (the header). */
#define BLANK_MS 100
#define SETTLE_MS DT_INST_PROP(0, settle_ms)
/* The boot light's length, and its name in the log. */
#if IS_ENABLED(CONFIG_LANTERN_INTRO)
#define BOOT_LIGHT_MS LANTERN_INTRO_MS
#define BOOT_LIGHT "intro"
#else
#define BOOT_LIGHT_MS CONFIG_LANTERN_BOOT_LIGHT_MS
#define BOOT_LIGHT "boot light"
#endif

static const struct gpio_dt_spec rail = GPIO_DT_SPEC_INST_GET(0, gpios);

enum phase {
    DARK,     /* rail off, the LEDs black */
    RAIL_UP,  /* rail on, the LEDs powering up: no frame yet */
    LIT,      /* rail on, the palette's picture */
    BLANKING, /* the black frame on its way, rail still on */
};

/* System work queue only (the header). */
static enum phase phase = DARK;
static bool started;
static bool boot_light;
static bool usb_powered;
static bool idle; /* stays false without CONFIG_LANTERN_OFF_WHEN_IDLE */
static bool want_lit;

static void rail_set(bool on) {
    int rc = gpio_pin_set_dt(&rail, on);
    if (rc < 0) {
        LOG_ERR("rail %s failed: %d", on ? "on" : "off", rc);
    }
}

static void step(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(step_work, step);

static bool read_idle(void) {
    return IS_ENABLED(CONFIG_LANTERN_OFF_WHEN_IDLE) &&
           zmk_activity_get_state() != ZMK_ACTIVITY_ACTIVE;
}

/* The rule: the boot light, or USB power with the half in use. */
static bool rule(void) { return boot_light || (usb_powered && !idle); }

/* An input changed: let the machine catch up. The boot light keeps the rule
 * true, so only its own deadline is ever pending while it runs. */
static void apply_rule(void) {
    bool lit = rule();
    if (lit == want_lit) {
        return;
    }
    want_lit = lit;
    k_work_reschedule(&step_work, K_NO_WAIT);
}

/* Rail on and settled: the picture, and the boot light's end. */
static void light(void) {
    lantern_keys_boot_light(boot_light);
    lantern_keys_light();
    phase = LIT;
    LOG_INF("lit: %s", boot_light ? BOOT_LIGHT : "usb powered");
    if (boot_light) {
        k_work_reschedule(&step_work, K_MSEC(BOOT_LIGHT_MS));
    }
}

/* Rail on: the black frame, and the rail off once it is out. */
static void blank(void) {
    lantern_keys_blank();
    phase = BLANKING;
    k_work_reschedule(&step_work, K_MSEC(BLANK_MS));
}

/* The first run: the inputs, then the machine runs from DARK. */
static void start(void) {
    started = true;
    usb_powered = zmk_usb_is_powered();
    idle = read_idle();
    boot_light = BOOT_LIGHT_MS > 0;
    want_lit = rule();
    LOG_INF("start: usb %s, " BOOT_LIGHT " %d ms", usb_powered ? "powered" : "absent",
            BOOT_LIGHT_MS);
}

static void step(struct k_work *work) {
    ARG_UNUSED(work);

    if (!started) {
        start();
    }

    switch (phase) {
    case DARK:
        if (!want_lit) {
            break;
        }
        rail_set(true);
        phase = RAIL_UP;
        k_work_reschedule(&step_work, K_MSEC(SETTLE_MS));
        break;

    case RAIL_UP:
        if (want_lit) {
            light();
        } else {
            /* Through BLANKING even here: the first frame may be out. */
            blank();
        }
        break;

    case LIT:
        if (boot_light) {
            boot_light = false;
            lantern_keys_boot_light(false);
            want_lit = rule();
        }
        if (!want_lit) {
            blank();
        }
        break;

    case BLANKING:
        if (want_lit) {
            light();
            break;
        }
        rail_set(false);
        phase = DARK;
        LOG_INF("dark: %s", usb_powered ? "idle" : "on battery");
        break;
    }
}

static int lantern_listener(const zmk_event_t *eh) {
    /* Before start() the inputs are read there. */
    if (!started) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    if (as_zmk_usb_conn_state_changed(eh) != NULL) {
        bool was = usb_powered;

        usb_powered = zmk_usb_is_powered();
        if (IS_ENABLED(CONFIG_LANTERN_INTRO) && usb_powered && !was && !boot_light) {
            /* never while one plays: nothing restarts it (the header) */
            boot_light = true;
            LOG_INF("intro: usb powered");
        }
    } else if (as_zmk_activity_state_changed(eh) != NULL) {
        idle = read_idle();
    } else {
        return ZMK_EV_EVENT_BUBBLE;
    }
    apply_rule();

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(lantern, lantern_listener);
ZMK_SUBSCRIPTION(lantern, zmk_usb_conn_state_changed);
#if IS_ENABLED(CONFIG_LANTERN_OFF_WHEN_IDLE)
ZMK_SUBSCRIPTION(lantern, zmk_activity_state_changed);
#endif

/* Nothing is saved under "lantern"; Zephyr calls h_set unchecked should a key
 * ever appear there, so refuse it rather than leave the pointer NULL. */
static int on_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg) {
    ARG_UNUSED(name);
    ARG_UNUSED(len);
    ARG_UNUSED(read_cb);
    ARG_UNUSED(cb_arg);
    return -ENOENT;
}

static int on_commit(void) {
    k_work_reschedule(&step_work, K_NO_WAIT);
    return 0;
}

/* Commit priority 1: after ZMK's priority 0 handlers (the header). */
SETTINGS_STATIC_HANDLER_DEFINE_WITH_CPRIO(lantern, "lantern", NULL, on_set, on_commit, NULL, 1);

static int lantern_init(void) {
    if (!gpio_is_ready_dt(&rail)) {
        LOG_ERR("rail port not ready");
        return -ENODEV;
    }

    int rc = gpio_pin_configure_dt(&rail, GPIO_OUTPUT_INACTIVE);
    if (rc < 0) {
        LOG_ERR("rail pin configure failed: %d", rc);
        return rc;
    }

    return 0;
}

SYS_INIT(lantern_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
