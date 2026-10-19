/*
 * Copyright (c) 2026 akira-toriyama
 *
 * SPDX-License-Identifier: MIT
 *
 * Light the key LEDs while USB powers this half and the half is in use, and
 * keep them dark and unpowered otherwise: on battery, and once ZMK's activity
 * state has left ACTIVE (CONFIG_LANTERN_OFF_WHEN_IDLE). Lit = the LED rail
 * (the lantern,rail node's gpios) on, then ZMK's underglow on; dark = the
 * underglow off (a black frame), then the rail off BLANK_MS later. At boot
 * the LEDs light for CONFIG_LANTERN_BOOT_LIGHT_MS whatever the power, then
 * the rule applies.
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
 *   after every init level, and its set phase restores ZMK's saved
 *   "rgb/underglow/state", starting the underglow tick when that state was
 *   on; the commit phase follows, priority 0 handlers (ZMK's own) first, so
 *   this one has the last word. The saved state is ZMK's: every
 *   zmk_rgb_underglow_on()/off() saves the whole state 60 s later
 *   (CONFIG_ZMK_SETTINGS_SAVE_DEBOUNCE), so a half that boots on battery
 *   after a plugged minute restores on, and its first frames are already
 *   queued on ZMK's low priority work queue when start() runs (main, at
 *   priority 0, has not let that queue's thread, at 10, run). Nothing cancels
 *   that static work item, so start() turns the rail on first: the frames
 *   meet LEDs that are powering up, and the machine then lights or blanks
 *   them as the rule says. On battery that is a flash of the saved colour
 *   for about BLANK_MS, boot light or not.
 * - USB: zmk_usb_is_powered() is false for USB_DC_UNKNOWN and DISCONNECTED
 *   and true for the rest (CONNECTED, RESET, CONFIGURED, SUSPEND, RESUME,
 *   ...), so a host asleep and a charger both count as powered, and only a
 *   change of that boolean acts. A cable present at boot shows as CONNECTED
 *   once zmk_usb_init() (APPLICATION 96) has enabled the device: the nRF USB
 *   driver re-raises the latched power-ready event through its own work
 *   queue (zephyr drivers/usb/device/usb_dc_nrfx.c), so the first evaluation
 *   may run before or after that status, and the listener covers the first
 *   case. ZMK's own CONFIG_ZMK_RGB_UNDERGLOW_AUTO_OFF_USB reads the USB state
 *   at init (APPLICATION 90, before USB is up) and its listener starts from
 *   "awake", so it cannot light a half that boots on a cable; it is asserted
 *   off below, as is AUTO_OFF_IDLE, whose on()/off() on activity would run
 *   the strip against an unpowered rail on battery.
 * - Idle: ZMK's activity state (app/src/activity.c), not a timer of this
 *   module's. ZMK already counts what the half does (key positions, sensor
 *   events, pointing input) and turns IDLE after CONFIG_ZMK_IDLE_TIMEOUT ms
 *   without any, per device: a split half knows nothing of the other half's
 *   keys, so each goes dark on its own. The timeout is the consumer's line
 *   (canon's halves: 300000). SLEEP counts as idle (unreachable with
 *   CONFIG_ZMK_SLEEP=n). A cable plugged into an idle half lights nothing
 *   until its next input: USB is no activity to ZMK, and lighting on it would
 *   leave the LEDs on with no idle event to follow. On battery the idle input
 *   changes nothing (the rule is already false) and is only kept current.
 *   In canon's halves the only other subscriber ZMK links is battery.c, which
 *   pauses its sampling while idle (zmk.map, 2026-10-19).
 * - ZMK's underglow calls take no lock and block on nothing: on() starts the
 *   tick timer (a frame every 50 ms on ZMK's low priority work queue), off()
 *   queues a black frame there and stops the timer. settle-ms (the node)
 *   covers the LEDs' power-up before the first frame, as the ext-power node's
 *   init-delay-ms did; BLANK_MS covers the black frame before the rail drops
 *   (a 29-LED frame at 4 MHz takes 1.4 ms, 7 ms at the Cyboard overlay's
 *   chain-length of 150). The only way to DARK is through BLANKING, so no
 *   frame the machine knows of meets an unpowered strip. Two ZMK details stay:
 *   an on() right after an off() may see that black frame land after its
 *   first frame, and the next tick repaints; off() queues the black frame
 *   before it stops the timer, so a tick in between puts a coloured frame
 *   behind it, which the rail's drop ends.
 * - Not reconciled: an underglow change the module did not make. A &rgb_ug
 *   key pressed on battery, or ZMK's idle auto-off, runs or stops the strip
 *   behind the module's back; a consumer keeps such keys off a half that
 *   runs on battery (canon's reach the halves through no dongle anyway).
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
#include <zmk/rgb_underglow.h>
#include <zmk/usb.h>

LOG_MODULE_REGISTER(lantern, LOG_LEVEL_INF);

BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) == 1,
             "CONFIG_LANTERN_ON_USB_POWER needs exactly one lantern,rail node");
BUILD_ASSERT(IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW),
             "CONFIG_LANTERN_ON_USB_POWER needs CONFIG_ZMK_RGB_UNDERGLOW=y: the LEDs are lit "
             "through ZMK's underglow");
BUILD_ASSERT(IS_ENABLED(CONFIG_USB_DEVICE_STACK),
             "CONFIG_LANTERN_ON_USB_POWER needs CONFIG_USB_DEVICE_STACK: the USB state comes "
             "from ZMK's usb.c");
BUILD_ASSERT(IS_ENABLED(CONFIG_SETTINGS),
             "CONFIG_LANTERN_ON_USB_POWER needs CONFIG_SETTINGS: the first evaluation runs "
             "from settings_load()");
BUILD_ASSERT(!IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW_EXT_POWER),
             "CONFIG_ZMK_RGB_UNDERGLOW_EXT_POWER must be n with CONFIG_LANTERN_ON_USB_POWER: "
             "the module drives the rail, and the ext_power device is disabled");
BUILD_ASSERT(!IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW_AUTO_OFF_USB),
             "CONFIG_ZMK_RGB_UNDERGLOW_AUTO_OFF_USB must be n with CONFIG_LANTERN_ON_USB_POWER: "
             "it calls the same on/off, and whichever listener runs last would win");
BUILD_ASSERT(!IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW_AUTO_OFF_IDLE),
             "CONFIG_ZMK_RGB_UNDERGLOW_AUTO_OFF_IDLE must be n with CONFIG_LANTERN_ON_USB_POWER: "
             "its on()/off() on activity would run the strip against an unpowered rail on battery; "
             "CONFIG_LANTERN_OFF_WHEN_IDLE darkens an idle half");
BUILD_ASSERT(!IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW_ON_START),
             "CONFIG_ZMK_RGB_UNDERGLOW_ON_START must be n with CONFIG_LANTERN_ON_USB_POWER: it "
             "would drive the strip at init with the rail off; CONFIG_LANTERN_BOOT_LIGHT_MS "
             "lights at boot");

/* The black frame zmk_rgb_underglow_off() queues on ZMK's low priority work
 * queue must reach the LEDs before the rail drops (the header). */
#define BLANK_MS 100
#define SETTLE_MS DT_INST_PROP(0, settle_ms)

static const struct gpio_dt_spec rail = GPIO_DT_SPEC_INST_GET(0, gpios);

enum phase {
    DARK,     /* rail off, underglow off */
    RAIL_UP,  /* rail on, the LEDs powering up: no frame of ours yet */
    LIT,      /* rail on, underglow on */
    BLANKING, /* underglow off, its black frame on its way, rail still on */
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

/* Rail on: light, and give the boot light its end. */
static void light(void) {
    zmk_rgb_underglow_on();
    phase = LIT;
    LOG_INF("lit: %s", boot_light ? "boot light" : "usb powered");
    if (boot_light) {
        k_work_reschedule(&step_work, K_MSEC(CONFIG_LANTERN_BOOT_LIGHT_MS));
    }
}

/* Rail on: blank, and drop the rail once the black frame is out. */
static void blank(void) {
    zmk_rgb_underglow_off();
    phase = BLANKING;
    k_work_reschedule(&step_work, K_MSEC(BLANK_MS));
}

/* The first run. Returns true when it has set the machine going itself. */
static bool start(void) {
    started = true;
    usb_powered = zmk_usb_is_powered();
    idle = read_idle();
    boot_light = CONFIG_LANTERN_BOOT_LIGHT_MS > 0;
    want_lit = rule();
    LOG_INF("start: usb %s, boot light %d ms", usb_powered ? "powered" : "absent",
            CONFIG_LANTERN_BOOT_LIGHT_MS);

    bool on;
    if (zmk_rgb_underglow_get_state(&on) < 0 || !on) {
        return false;
    }

    /* A saved on restored the tick, and its frames are queued (the header):
     * power the LEDs for them, then light or blank them as the rule says. */
    rail_set(true);
    if (want_lit) {
        phase = RAIL_UP;
        k_work_reschedule(&step_work, K_MSEC(SETTLE_MS));
    } else {
        blank();
    }
    return true;
}

static void step(struct k_work *work) {
    ARG_UNUSED(work);

    if (!started && start()) {
        return;
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
            /* Through BLANKING even here: a restored tick may be running. */
            blank();
        }
        break;

    case LIT:
        if (boot_light) {
            boot_light = false;
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
        usb_powered = zmk_usb_is_powered();
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

/* Commit priority 1: after ZMK's priority 0 handlers, the underglow's among
 * them (the header). */
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
