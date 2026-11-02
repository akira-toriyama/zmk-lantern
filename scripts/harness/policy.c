/* Runs the real src/on_usb_power.c (run.sh writes it as policy_src.c,
 * included below) on the host with a fake clock and its delayable items, the
 * step's and, with CONFIG_LANTERN_WAKE_ON_HIT, the wake's: the rail rule's 11
 * scenes, and the wake's 10 in a build with it. */
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IS_ENABLED(config_macro) Z_IS_ENABLED1(config_macro)
#define Z_IS_ENABLED1(config_macro) Z_IS_ENABLED2(_XXXX##config_macro)
#define _XXXX1 _YYYY,
#define Z_IS_ENABLED2(one_or_two_args) Z_IS_ENABLED3(one_or_two_args 1, 0)
#define Z_IS_ENABLED3(ignore_this, val, ...) val
#define BUILD_ASSERT(cond, ...) _Static_assert(cond, "" __VA_ARGS__)
#define ARG_UNUSED(x) (void)(x)
#define DT_NUM_INST_STATUS_OKAY(c) 1
#define DT_INST_PROP(i, p) 100 /* settle-ms */
/* t-41g7: the rail is CONFIG_LANTERN_RAIL's; without it rail_level stays 0 and the
 * checks on it read as the rule alone */
#if IS_ENABLED(CONFIG_LANTERN_RAIL)
#define RAIL_ON (rail_level != 0)
#define RAIL_OFF (rail_level == 0)
#else
#define RAIL_ON 1
#define RAIL_OFF 1
#endif
/* the settle between the rail and the first frame: the node's 100 ms, 0 without a rail */
#define SETTLE (IS_ENABLED(CONFIG_LANTERN_RAIL) ? 100 : 0)

/* gpio */
struct gpio_dt_spec {
    int pin;
};
#define GPIO_DT_SPEC_INST_GET(i, p) {2}
#define GPIO_OUTPUT_INACTIVE 0
static int rail_level;
static int gpio_pin_set_dt(const struct gpio_dt_spec *s, int v) {
    (void)s;
    rail_level = v;
    return 0;
}
static bool gpio_is_ready_dt(const struct gpio_dt_spec *s) { return s != NULL; }
static int gpio_pin_configure_dt(const struct gpio_dt_spec *s, int f) {
    (void)s, (void)f;
    return 0;
}

/* clock and the delayable items */
static uint32_t now;
struct k_work {
    int unused;
};
struct k_work_delayable {
    void (*fn)(struct k_work *);
    bool pending;
    uint32_t due;
    uint32_t seq; /* FIFO among items due at once */
};
typedef uint32_t k_timeout_t;
#define K_MSEC(ms) (ms)
#define K_NO_WAIT 0
#define K_WORK_DELAYABLE_DEFINE(name, f) struct k_work_delayable name = {.fn = f}
static int arms;
static uint32_t seqs;
/* Zephyr: K_NO_WAIT on an item already queued (due now) leaves it queued in
 * its place; anything else (re)arms it. */
static int k_work_reschedule(struct k_work_delayable *w, k_timeout_t d) {
    if (d == 0 && w->pending && w->due <= now) {
        return 0;
    }
    w->pending = true;
    w->due = now + d;
    w->seq = ++seqs;
    arms++;
    return 1;
}

/* atomics and the uptime, for the wake */
typedef long atomic_t;
typedef long atomic_val_t;
static atomic_val_t atomic_get(const atomic_t *p) { return *p; }
static atomic_val_t atomic_set(atomic_t *p, atomic_val_t v) {
    atomic_val_t o = *p;
    *p = v;
    return o;
}
static uint32_t k_uptime_get_32(void) { return now; }

#define LOG_MODULE_REGISTER(...)
static char logbuf[16384];
static void logf_(const char *fmt, ...) {
    char line[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    snprintf(logbuf + strlen(logbuf), sizeof(logbuf) - strlen(logbuf), "%u %s|", now, line);
}
#define LOG_INF(...) logf_(__VA_ARGS__)
#define LOG_ERR(...) logf_(__VA_ARGS__)

/* settings, init */
typedef int (*settings_read_cb)(void *, void *, size_t);
#define SETTINGS_STATIC_HANDLER_DEFINE_WITH_CPRIO(...)
#define SYS_INIT(...)

/* ZMK */
typedef struct {
    int kind;
} zmk_event_t;
#define ZMK_EV_EVENT_BUBBLE 0
#define ZMK_LISTENER(...)
#define ZMK_SUBSCRIPTION(...)
enum { EV_USB = 1, EV_ACTIVITY = 2 };
static const void *as_zmk_usb_conn_state_changed(const zmk_event_t *e) {
    return e->kind == EV_USB ? e : NULL;
}
static const void *as_zmk_activity_state_changed(const zmk_event_t *e) {
    return e->kind == EV_ACTIVITY ? e : NULL;
}
static bool usb, active = true;
static bool zmk_usb_is_powered(void) { return usb; }
enum { ZMK_ACTIVITY_ACTIVE, ZMK_ACTIVITY_IDLE };
static int zmk_activity_get_state(void) { return active ? ZMK_ACTIVITY_ACTIVE : ZMK_ACTIVITY_IDLE; }

/* the picture: record what the rule asks */
#define LANTERN_INTRO_MS 6000
static bool keys_lit, keys_boot;
static int intros; /* boot light (re)starts with the LEDs going lit */
static uint32_t intro_at;
static void lantern_keys_light(void) { keys_lit = true; }
static int blanks;
static void lantern_keys_blank(void) { keys_lit = false; blanks++; }
static uint32_t lantern_keys_boot_light(bool on) {
    if (on && !keys_boot) {
        intros++;
        intro_at = now;
    }
    keys_boot = on;
    return on ? LANTERN_INTRO_MS : 0;
}
#define KEYS_H_STUBBED

#include "policy_src.c"

static int fails;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            fails++;                                                                               \
            fprintf(stderr, "FAIL %s: ", scenario);                                                \
            fprintf(stderr, __VA_ARGS__);                                                          \
            fprintf(stderr, "\n  log: %s\n", logbuf);                                              \
        }                                                                                          \
    } while (0)
static const char *scenario;

static struct k_work_delayable *items[] = {
    &step_work,
#if IS_ENABLED(CONFIG_LANTERN_WAKE_ON_HIT)
    &wake_work,
#endif
};
static void run_until(uint32_t t) {
    for (;;) {
        struct k_work_delayable *next = NULL;
        for (size_t i = 0; i < sizeof(items) / sizeof(items[0]); i++) {
            struct k_work_delayable *w = items[i];
            if (w->pending && w->due <= t &&
                (next == NULL || w->due < next->due || (w->due == next->due && w->seq < next->seq))) {
                next = w;
            }
        }
        if (next == NULL) {
            break;
        }
        now = next->due;
        next->pending = false;
        next->fn(NULL);
    }
    now = t;
}
static void event(int kind) {
    zmk_event_t e = {kind};
    lantern_listener(&e);
}
static void reset_bare(bool usb0, bool active0) {
    phase = DARK;
    started = boot_light = usb_powered = idle = want_lit = false;
    woken = false;
    for (size_t i = 0; i < sizeof(items) / sizeof(items[0]); i++) {
        items[i]->pending = false;
    }
#if IS_ENABLED(CONFIG_LANTERN_WAKE_ON_HIT)
    hit_at = 0;
#endif
    now = 1000;
    rail_level = 0;
    keys_lit = keys_boot = false;
    intros = 0;
    usb = usb0;
    active = active0;
    logbuf[0] = 0;
}
static void reset(bool usb0, bool active0) {
    reset_bare(usb0, active0);
    on_commit(); /* the settings commit at boot */
}
static int count_log(const char *what) {
    int n = 0;
    for (const char *p = logbuf; (p = strstr(p, what)) != NULL; p += strlen(what)) {
        n++;
    }
    return n;
}

int main(void) {
    /* 1. battery boot: the intro, then dark */
    scenario = "battery boot";
    reset(false, true);
    run_until(1000 + SETTLE + 10);
    CHECK(keys_lit && keys_boot && RAIL_ON, "intro not playing at 110 ms");
    run_until(1000 + SETTLE + LANTERN_INTRO_MS - 1);
    CHECK(keys_lit && keys_boot, "intro cut short");
    run_until(1000 + SETTLE + LANTERN_INTRO_MS + 200);
    CHECK(!keys_lit && RAIL_OFF && intros == 1, "not dark after the intro (lit %d rail_level %d intros %d)",
          keys_lit, rail_level, intros);

    /* 2. USB boot, the USB status after the first evaluation: one intro, then lit */
    scenario = "usb boot, late status";
    reset(false, true);
    run_until(1300);
    usb = true;
    event(EV_USB);
    run_until(1000 + SETTLE + LANTERN_INTRO_MS + 200);
    CHECK(keys_lit && !keys_boot && RAIL_ON && intros == 1, "lit %d boot %d rail_level %d intros %d",
          keys_lit, keys_boot, rail_level, intros);

    /* 3. USB boot, the status before: one intro, then lit */
    scenario = "usb boot";
    reset(true, true);
    run_until(1000 + SETTLE + LANTERN_INTRO_MS + 200);
    CHECK(keys_lit && !keys_boot && intros == 1, "lit %d boot %d intros %d", keys_lit, keys_boot,
          intros);

    /* 4. a cable into an idle half on battery: the intro, then dark */
    scenario = "plug idle";
    reset(false, true);
    run_until(20000);
    active = false;
    event(EV_ACTIVITY);
    run_until(30000);
    CHECK(!keys_lit && RAIL_OFF, "dark before the plug");
    usb = true;
    event(EV_USB);
    run_until(30000 + SETTLE + 10);
    CHECK(keys_lit && keys_boot && intros == 2, "intro not playing after the plug (intros %d)",
          intros);
    run_until(30000 + SETTLE + LANTERN_INTRO_MS + 200);
    CHECK(!keys_lit && RAIL_OFF, "not dark after the intro on an idle half");

    /* 5. a cable into an active half on battery: the intro, then lit; a key meanwhile changes nothing */
    scenario = "plug active";
    reset(false, true);
    run_until(20000);
    usb = true;
    event(EV_USB);
    run_until(23000);
    event(EV_ACTIVITY); /* still active */
    run_until(20000 + SETTLE + LANTERN_INTRO_MS + 200);
    CHECK(keys_lit && !keys_boot && RAIL_ON && intros == 2, "lit %d boot %d intros %d", keys_lit,
          keys_boot, intros);

    /* 6. unplug and replug during the intro: no restart, one end */
    scenario = "flap during intro";
    reset(false, true);
    run_until(20000);
    usb = true;
    event(EV_USB);
    run_until(22000);
    usb = false;
    event(EV_USB);
    run_until(23000);
    usb = true;
    event(EV_USB);
    run_until(24000);
    CHECK(intros == 2 && intro_at == 20000 + SETTLE + (SETTLE ? 1 : 0), "restarted (intros %d at %u)", intros, intro_at);
    run_until(20000 + SETTLE + LANTERN_INTRO_MS - 5);
    CHECK(keys_boot, "cut short");
    run_until(20000 + SETTLE + LANTERN_INTRO_MS + 200);
    CHECK(keys_lit && !keys_boot, "not lit after the intro on the cable");

    /* 7. unplug during the intro and stay on battery: plays out, then dark */
    scenario = "unplug during intro";
    reset(false, true);
    run_until(20000);
    usb = true;
    event(EV_USB);
    run_until(21000);
    usb = false;
    event(EV_USB);
    run_until(20000 + SETTLE + LANTERN_INTRO_MS - 5);
    CHECK(keys_lit && keys_boot, "cut short on battery");
    run_until(20000 + SETTLE + LANTERN_INTRO_MS + 200);
    CHECK(!keys_lit && RAIL_OFF, "not dark");

    /* 8. a plug while the battery boot's intro blanks: a second intro */
    scenario = "plug while blanking";
    reset(false, true);
    run_until(1000 + SETTLE + LANTERN_INTRO_MS + 50); /* blanking: rail_level still on */
    CHECK(RAIL_ON && !keys_lit, "not blanking (rail_level %d lit %d)", rail_level, keys_lit);
    usb = true;
    event(EV_USB);
    run_until(1000 + SETTLE + LANTERN_INTRO_MS + 60);
    CHECK(keys_lit && keys_boot && intros == 2, "no intro from blanking (intros %d)", intros);
    run_until(20000);
    CHECK(keys_lit && !keys_boot, "not lit after");

    /* 9. idle while the intro plays on the cable: plays out, then dark */
    scenario = "idle during intro";
    reset(true, true);
    run_until(3000);
    active = false;
    event(EV_ACTIVITY);
    run_until(1000 + SETTLE + LANTERN_INTRO_MS - 5);
    CHECK(keys_boot, "cut short by idle");
    run_until(1000 + SETTLE + LANTERN_INTRO_MS + 200);
    CHECK(!keys_lit && RAIL_OFF, "not dark after the intro on an idle cable");

#if IS_ENABLED(CONFIG_LANTERN_WAKE_ON_HIT)
#define TIMEOUT CONFIG_ZMK_IDLE_TIMEOUT
    /* 10. a USB half dark for idle: the other half's press lights it, the
     * timeout from that press darkens it */
    scenario = "wake from the other half";
    reset(true, true);
    run_until(20000);
    CHECK(keys_lit && !keys_boot && intros == 1, "not lit after the boot");
    active = false;
    event(EV_ACTIVITY);
    run_until(20000 + BLANK_MS + 10);
    CHECK(!keys_lit && RAIL_OFF, "not dark at idle");
    run_until(30000);
    lantern_rule_hit();
    run_until(30000 + SETTLE - 1);
    CHECK(!keys_lit, "lit before the settle");
    run_until(30000 + SETTLE + 10);
    CHECK(keys_lit && RAIL_ON && !keys_boot && intros == 1, "not woken (lit %d boot %d intros %d)",
          keys_lit, keys_boot, intros);
    run_until(30000 + TIMEOUT - 1);
    CHECK(keys_lit, "dark before the timeout");
    run_until(30000 + TIMEOUT + BLANK_MS + 10);
    CHECK(!keys_lit && RAIL_OFF && count_log("other half: idle") == 1, "not dark after the timeout");

    /* 11. the other half typed on: each press restarts the timeout; one
     * wake logged */
    scenario = "presses keep it lit";
    reset(true, true);
    run_until(20000);
    active = false;
    event(EV_ACTIVITY);
    run_until(30000);
    for (int i = 0; i < 5; i++) {
        lantern_rule_hit(); /* a burst within one millisecond */
    }
    run_until(230000);
    lantern_rule_hit();
    run_until(30000 + TIMEOUT + 1000);
    CHECK(keys_lit, "dark at the first press's timeout");
    run_until(230000 + TIMEOUT - 1);
    CHECK(keys_lit, "dark before the last press's timeout");
    run_until(230000 + TIMEOUT + BLANK_MS + 10);
    CHECK(!keys_lit && RAIL_OFF, "not dark after the last press's timeout");
    CHECK(count_log("other half: in use") == 1 && count_log("other half: idle") == 1, "in use %d, idle %d",
          count_log("other half: in use"), count_log("other half: idle"));

    /* 12. its own input after the wake: lit until ZMK's idle, past the wake */
    scenario = "own input after the wake";
    reset(true, true);
    run_until(20000);
    active = false;
    event(EV_ACTIVITY);
    run_until(30000);
    lantern_rule_hit();
    run_until(31000);
    active = true;
    event(EV_ACTIVITY);
    run_until(30000 + TIMEOUT + 1000);
    CHECK(keys_lit, "dark at the wake's end while ZMK is active");
    active = false;
    event(EV_ACTIVITY);
    run_until(now + BLANK_MS + 10);
    CHECK(!keys_lit && RAIL_OFF, "not dark at ZMK's idle after the wake");

    /* 13. ZMK idle while woken: lit until the wake's end */
    scenario = "idle while woken";
    reset(true, true);
    run_until(20000);
    lantern_rule_hit();
    run_until(25000);
    active = false;
    event(EV_ACTIVITY);
    run_until(25000 + BLANK_MS + 10);
    CHECK(keys_lit, "dark at ZMK's idle while woken");
    run_until(20000 + TIMEOUT - 1);
    CHECK(keys_lit, "dark before the wake's end");
    run_until(20000 + TIMEOUT + BLANK_MS + 10);
    CHECK(!keys_lit && RAIL_OFF, "not dark at the wake's end");

    /* 14. a press on battery lights nothing; a cable within the timeout
     * plays the intro and then stays lit until the wake's end */
    scenario = "wake on battery";
    reset(false, true);
    run_until(20000);
    CHECK(!keys_lit && RAIL_OFF, "not dark on battery");
    active = false;
    event(EV_ACTIVITY);
    lantern_rule_hit();
    run_until(20000 + SETTLE + 10);
    CHECK(!keys_lit && RAIL_OFF, "lit on battery");
    run_until(30000);
    usb = true;
    event(EV_USB);
    run_until(30000 + SETTLE + 10);
    CHECK(keys_lit && keys_boot && intros == 2, "no intro at the plug (intros %d)", intros);
    run_until(30000 + SETTLE + LANTERN_INTRO_MS + 200);
    CHECK(keys_lit && !keys_boot, "dark after the intro within the wake");
    run_until(20000 + TIMEOUT + BLANK_MS + 10);
    CHECK(!keys_lit && RAIL_OFF, "not dark at the wake's end");

    /* 15. a press while the intro plays on an idle half: the intro plays
     * out, then lit until the wake's end */
    scenario = "wake during the intro";
    reset(false, true);
    run_until(20000);
    active = false;
    event(EV_ACTIVITY);
    run_until(30000);
    usb = true;
    event(EV_USB);
    run_until(33000);
    lantern_rule_hit();
    run_until(30000 + SETTLE + LANTERN_INTRO_MS - 5);
    CHECK(keys_boot && keys_lit && intros == 2, "the intro cut short by the wake");
    run_until(30000 + SETTLE + LANTERN_INTRO_MS + 200);
    CHECK(keys_lit && !keys_boot, "dark after the intro though woken");
    run_until(33000 + TIMEOUT + BLANK_MS + 10);
    CHECK(!keys_lit && RAIL_OFF, "not dark at the wake's end");

    /* 16. a press while the black frame is on its way: lit again at once,
     * the rail never dropped */
    scenario = "wake while blanking";
    reset(true, true);
    run_until(20000);
    active = false;
    event(EV_ACTIVITY);
    run_until(20000 + BLANK_MS / 2);
    CHECK(phase == BLANKING && RAIL_ON && !keys_lit, "not blanking (phase %d)", (int)phase);
    lantern_rule_hit();
    run_until(20000 + BLANK_MS / 2 + 1);
    CHECK(phase == LIT && keys_lit && RAIL_ON, "not relit from blanking (phase %d)", (int)phase);

    /* 17. a press before the first evaluation: dropped, the machine not
     * started early */
    scenario = "wake before the commit";
    reset_bare(true, true);
    lantern_rule_hit();
    run_until(1005);
    CHECK(!started && phase == DARK && RAIL_OFF && !woken, "ran before the commit");
    on_commit();
    run_until(1005 + SETTLE + LANTERN_INTRO_MS + 200);
    CHECK(keys_lit && !keys_boot && intros == 1 && !woken, "boot after a dropped press");

    /* 18. unplugged while woken: dark; plugged back within the wake: the
     * intro, then lit until the wake's end */
    scenario = "unplug while woken";
    reset(true, true);
    run_until(20000);
    active = false;
    event(EV_ACTIVITY);
    run_until(30000);
    lantern_rule_hit();
    run_until(30000 + SETTLE + 10);
    CHECK(keys_lit, "not woken");
    run_until(40000);
    usb = false;
    event(EV_USB);
    run_until(40000 + BLANK_MS + 10);
    CHECK(!keys_lit && RAIL_OFF, "lit on battery while woken");
    run_until(50000);
    usb = true;
    event(EV_USB);
    run_until(50000 + SETTLE + LANTERN_INTRO_MS + 200);
    CHECK(keys_lit && !keys_boot && intros == 2, "lit %d boot %d intros %d", keys_lit, keys_boot,
          intros);
    run_until(30000 + TIMEOUT + BLANK_MS + 10);
    CHECK(!keys_lit && RAIL_OFF, "not dark at the wake's end");

    /* 19. the wake ends while an intro plays: the intro plays out, then dark */
    scenario = "wake ends during the intro";
    reset(true, true);
    run_until(20000);
    active = false;
    event(EV_ACTIVITY);
    run_until(30000);
    lantern_rule_hit();
    run_until(30000 + TIMEOUT - 3000);
    usb = false;
    event(EV_USB);
    run_until(30000 + TIMEOUT - 2000);
    usb = true;
    event(EV_USB);
    run_until(30000 + TIMEOUT + 10);
    CHECK(keys_boot && keys_lit, "the intro cut short by the wake's end");
    run_until(30000 + TIMEOUT - 2000 + SETTLE + LANTERN_INTRO_MS - 5);
    CHECK(keys_boot && keys_lit, "the intro cut short");
    run_until(30000 + TIMEOUT - 2000 + SETTLE + LANTERN_INTRO_MS + 200);
    CHECK(!keys_lit && RAIL_OFF, "lit after the intro past the wake");
#endif

    /* 20. a flap within the settle (canon t-gwsw): idle and in use again within
     * settle-ms of the rail going on; no frame before the LEDs are up */
    scenario = "flap within settle";
    reset(true, true);
    run_until(1000 + SETTLE + LANTERN_INTRO_MS + 200); /* lit on the cable */
    CHECK(keys_lit && !keys_boot && RAIL_ON, "not lit on the cable");
    active = false;
    event(EV_ACTIVITY);
    run_until(40000);
    CHECK(!keys_lit && RAIL_OFF, "not dark for idle");
    active = true;
    event(EV_ACTIVITY); /* 40000: the rail goes on */
    int blanks_before = blanks;
    run_until(40030);
    active = false;
    event(EV_ACTIVITY); /* within the settle */
    run_until(40060);
    active = true;
    event(EV_ACTIVITY);
    if (SETTLE > 60) { /* without a rail there is no settle to wait out */
        run_until(40000 + SETTLE - 1);
        CHECK(!keys_lit, "a frame before the settle (rail on at 40000, lit by %u)", now);
        CHECK(blanks == blanks_before, "a black frame before the settle (%d)", blanks - blanks_before);
    }
    run_until(40000 + (SETTLE > 60 ? SETTLE : 60) + 10);
    CHECK(keys_lit && RAIL_ON && !keys_boot, "not lit after the settle (lit %d rail %d)", keys_lit, rail_level);
    run_until(60000);
    CHECK(keys_lit && RAIL_ON, "not still lit");

    /* 21. idle for good within the settle: the black frame, the rail off */
    scenario = "idle within settle";
    reset(true, true);
    run_until(1000 + SETTLE + LANTERN_INTRO_MS + 200);
    active = false;
    event(EV_ACTIVITY);
    run_until(40000);
    active = true;
    event(EV_ACTIVITY);
    run_until(40030);
    active = false;
    event(EV_ACTIVITY);
    run_until(40000 + SETTLE + 200);
    CHECK(!keys_lit && RAIL_OFF, "not dark again (lit %d rail %d)", keys_lit, rail_level);

    printf("policy scenarios: %d failure(s); last log: %s\n", fails, logbuf);
    return fails != 0;
}
