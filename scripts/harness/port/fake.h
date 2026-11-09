/* The harness's side of fake.c (harness_port.c): the boot, the settings'
 * store and load, the system work queue and the clock, the port's bytes and
 * its replies, a key of the keymap, a half's invocation from the central, a
 * half's battery report, and what was sent and repainted. */
#pragma once

/* Every SYS_INIT, as the kernel runs them before the settings load. */
void fake_boot(void);
/* A value stored before the load, as a device flashed earlier holds it. */
void fake_store(const char *key, const void *value, size_t len);
/* settings_load(): each stored value to its subtree's handler, then every
 * handler's commit. */
void fake_load(void);
/* The value last saved under key (NULL for none), its length in *len. */
const void *fake_stored(const char *key, size_t *len);
extern int fake_saves;

/* The system work queue: every queued item, FIFO, and every delayed item
 * due, until none is left. */
void fake_run(void);
/* The clock ms on, then fake_run(). */
void fake_advance(int64_t ms);

/* A line on the port, its newline added, then fake_run(): the reply is
 * fake_reply(), "" when none came. */
void fake_line(const char *text);
const char *fake_reply(void);

/* A key of the keymap bound to behavior, pressed and released (ZMK's
 * keymap invokes it so), then fake_run(): the press's result. */
int fake_press(const char *behavior, uint32_t param1, uint32_t param2);
/* A half: the central's invocation of behavior arrives (press only, as the
 * central sends it), then fake_run(): the handler's result. */
int fake_receive(const char *behavior, uint32_t param1, uint32_t param2);
/* A split central: a half's battery report (0: its disconnection). */
void fake_battery(uint8_t source, uint8_t level);

/* What the split central's transport was handed, in order. */
struct fake_sent {
    uint8_t source;
    const char *behavior;
    uint32_t param1;
    uint32_t param2;
};
#define FAKE_SENT_MAX 64
extern struct fake_sent fake_sent[FAKE_SENT_MAX];
extern int fake_nsent;

/* The key LEDs' hooks (src/keys.h): a repaint for the palette, the effects'
 * state cleared for a changed pick. */
extern int fake_refreshes;
extern int fake_effect_changes;
