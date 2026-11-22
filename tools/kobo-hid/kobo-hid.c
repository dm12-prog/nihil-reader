/*
 * kobo-hid: makes the Kobo Sage act as a USB keyboard + pen tablet for a
 * computer. The Kobo's page buttons type arrow keys; the stylus moves the
 * computer's pointer (pen down = click).
 *
 * Needs the USB HID gadget set up first (kobo-hid.sh does it): one HID
 * function, /dev/hidg0, with two reports: ID 1 keyboard (8 bytes), ID 2
 * absolute pointer (5 bytes). One function uses only 2 USB endpoints; two
 * functions (4 endpoints) failed to bind on the Kobo on 22 Nov 2026.
 *
 * Controls while running:
 *   lower page button  -> Right arrow      upper page button -> Left arrow
 *   both buttons, short press  -> next pen orientation (8 of them; saved)
 *   both buttons, hold 1.5 s   -> stop
 *   power button               -> stop
 *   stylus side buttons        -> right click
 *
 * The program takes the buttons and touch screen away from the Kobo's own
 * software (EVIOCGRAB) while it runs, and gives them back when it stops.
 * Build: arm-linux-gnueabihf-gcc -O2 -static -o kobo-hid kobo-hid.c
 */
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#ifndef ABS_MT_TOOL_TYPE
#define ABS_MT_TOOL_TYPE 0x37
#endif
#ifndef MT_TOOL_PEN
#define MT_TOOL_PEN 1
#endif

#define BITS_PER_LONG (sizeof(long) * 8)
#define NLONGS(x) (((x) + BITS_PER_LONG - 1) / BITS_PER_LONG)
#define TEST_BIT(bit, arr) (((arr)[(bit) / BITS_PER_LONG] >> ((bit) % BITS_PER_LONG)) & 1)

/* Kobo page-button key codes seen across models (KOReader / Plato use these). */
#define K_KOBO_BACK 193
#define K_KOBO_FWD 194

static const char *cfg_path = "/mnt/onboard/.adds/kobohid/config";
static FILE *logf;
static volatile sig_atomic_t stop_flag;

/* settings (config file) */
static int orient = 1;      /* 0..7: bit0 swap x/y, bit1 mirror x, bit2 mirror y */
static int finger_ok = 0;   /* 1 = fingers also move the pointer */
static int key_next = 0x4f; /* HID usage: Right arrow */
static int key_prev = 0x50; /* HID usage: Left arrow */
static int fwd_code = 0, back_code = 0; /* 0 = auto */

static void logm(const char *fmt, ...)
{
    va_list ap;
    struct timeval tv;
    if (!logf) return;
    gettimeofday(&tv, NULL);
    fprintf(logf, "%ld.%03ld ", (long)tv.tv_sec, (long)(tv.tv_usec / 1000));
    va_start(ap, fmt);
    vfprintf(logf, fmt, ap);
    va_end(ap);
    fputc('\n', logf);
    fflush(logf);
    fsync(fileno(logf)); /* the 22 Nov crash lost every unsynced line */
}

static long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static void load_cfg(void)
{
    char line[128], key[64];
    int val;
    FILE *f = fopen(cfg_path, "r");
    if (!f) return;
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#') continue;
        if (sscanf(line, "%63[^=]=%i", key, &val) != 2) continue;
        if (!strcmp(key, "ORIENT")) orient = val & 7;
        else if (!strcmp(key, "FINGER")) finger_ok = val;
        else if (!strcmp(key, "KEY_NEXT")) key_next = val;
        else if (!strcmp(key, "KEY_PREV")) key_prev = val;
        else if (!strcmp(key, "FWD_CODE")) fwd_code = val;
        else if (!strcmp(key, "BACK_CODE")) back_code = val;
    }
    fclose(f);
}

static void save_cfg(void)
{
    char tmp[128];
    FILE *f;
    snprintf(tmp, sizeof tmp, "%s.tmp", cfg_path);
    f = fopen(tmp, "w");
    if (!f) { logm("config save failed: %s", strerror(errno)); return; }
    fprintf(f, "# kobo-hid settings (both buttons short press changes ORIENT)\n");
    fprintf(f, "ORIENT=%d\nFINGER=%d\nKEY_NEXT=0x%02x\nKEY_PREV=0x%02x\n",
            orient, finger_ok, key_next, key_prev);
    if (fwd_code) fprintf(f, "FWD_CODE=%d\n", fwd_code);
    if (back_code) fprintf(f, "BACK_CODE=%d\n", back_code);
    fclose(f);
    rename(tmp, cfg_path);
    sync();
}

/* ---- HID output ---- */
static int hid_fd = -1;
static int watch_only; /* "kobo-hid <log> watch": only hold the buttons and touch
                          screen away from the Kobo's software until stop */
static long hid_errors;

static void hid_write(int fd, const unsigned char *buf, int n, int must)
{
    if (watch_only) return;
    /* f_hid accepts one report at a time; with O_NONBLOCK it answers EAGAIN
       while the previous report is still waiting for the computer. Pointer
       moves can be dropped; key presses and button changes are retried. */
    long t0 = now_ms();
    for (;;) {
        ssize_t r = write(fd, buf, n);
        if (r == n) return;
        if (r < 0 && errno != EAGAIN && errno != EINTR) { hid_errors++; return; }
        if (!must || now_ms() - t0 > 200) { hid_errors++; return; }
        usleep(1000);
    }
}

static void send_key(int usage)
{
    unsigned char down[9] = {1, 0, 0, (unsigned char)usage, 0, 0, 0, 0, 0};
    unsigned char up[9] = {1, 0};
    hid_write(hid_fd, down, 9, 1);
    hid_write(hid_fd, up, 9, 1);
}

static int last_btn = -1, last_x = -1, last_y = -1;

static void send_ptr(int buttons, int x, int y, int must)
{
    unsigned char r[6];
    if (buttons == last_btn && x == last_x && y == last_y) return;
    r[0] = 2;
    r[1] = buttons & 7;
    r[2] = x & 0xff; r[3] = (x >> 8) & 0xff;
    r[4] = y & 0xff; r[5] = (y >> 8) & 0xff;
    hid_write(hid_fd, r, 6, must || buttons != last_btn);
    last_btn = buttons; last_x = x; last_y = y;
}

/* ---- input devices ---- */
#define MAXDEV 12
static int in_fd[MAXDEV];
static int n_in;
static int touch_idx = -1;
static struct input_absinfo ax, ay;
static int has_tooltype; /* panel tells pen and finger apart */

static void sig_stop(int s) { (void)s; stop_flag = 1; }

static void scan_inputs(void)
{
    char path[32], name[96];
    int i;
    for (i = 0; i < 16 && n_in < MAXDEV; i++) {
        unsigned long evb[NLONGS(EV_MAX + 1)], keyb[NLONGS(KEY_MAX + 1)], absb[NLONGS(ABS_MAX + 1)];
        int fd, is_touch, is_keys;
        snprintf(path, sizeof path, "/dev/input/event%d", i);
        fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;
        memset(evb, 0, sizeof evb); memset(keyb, 0, sizeof keyb); memset(absb, 0, sizeof absb);
        name[0] = 0;
        ioctl(fd, EVIOCGNAME(sizeof name), name);
        ioctl(fd, EVIOCGBIT(0, sizeof evb), evb);
        ioctl(fd, EVIOCGBIT(EV_KEY, sizeof keyb), keyb);
        ioctl(fd, EVIOCGBIT(EV_ABS, sizeof absb), absb);
        is_touch = TEST_BIT(EV_ABS, evb) && TEST_BIT(ABS_MT_POSITION_X, absb);
        is_keys = TEST_BIT(EV_KEY, evb) &&
                  (TEST_BIT(K_KOBO_BACK, keyb) || TEST_BIT(K_KOBO_FWD, keyb) ||
                   TEST_BIT(KEY_PAGEUP, keyb) || TEST_BIT(KEY_PAGEDOWN, keyb) ||
                   TEST_BIT(KEY_POWER, keyb));
        logm("input %s \"%s\" touch=%d keys=%d", path, name, is_touch, is_keys);
        if (!is_touch && !is_keys) { close(fd); continue; }
        if (ioctl(fd, EVIOCGRAB, 1) < 0) logm("  grab failed: %s", strerror(errno));
        if (is_touch && touch_idx < 0) {
            touch_idx = n_in;
            has_tooltype = TEST_BIT(ABS_MT_TOOL_TYPE, absb);
            logm("  pen/finger told apart: %s", has_tooltype ? "yes" : "no (all contacts count as pen)");
            ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X), &ax);
            ioctl(fd, EVIOCGABS(ABS_MT_POSITION_Y), &ay);
            logm("  touch range x %d..%d  y %d..%d", ax.minimum, ax.maximum, ay.minimum, ay.maximum);
        }
        in_fd[n_in++] = fd;
    }
}

static void release_inputs(void)
{
    int i;
    for (i = 0; i < n_in; i++) { ioctl(in_fd[i], EVIOCGRAB, 0); close(in_fd[i]); }
}

/* map raw touch coordinates to 0..32767 on the computer screen */
static void map_xy(int rx, int ry, int *ox, int *oy)
{
    double u = ax.maximum > ax.minimum ? (double)(rx - ax.minimum) / (ax.maximum - ax.minimum) : 0;
    double v = ay.maximum > ay.minimum ? (double)(ry - ay.minimum) / (ay.maximum - ay.minimum) : 0;
    if (orient & 1) { double t = u; u = v; v = t; }
    if (orient & 2) u = 1 - u;
    if (orient & 4) v = 1 - v;
    if (u < 0) u = 0;
    if (u > 1) u = 1;
    if (v < 0) v = 0;
    if (v > 1) v = 1;
    *ox = (int)(u * 32767 + 0.5);
    *oy = (int)(v * 32767 + 0.5);
}

int main(int argc, char **argv)
{
    /* touch state (slot 0 only = first contact) */
    int slot = 0, tid = -1, tool = 0, rx = 0, ry = 0, btn_touch = -1, side = 0;
    int have_btn_touch = 0;
    /* page buttons */
    int fwd_down = 0, back_down = 0, combo = 0;
    long combo_t = 0;
    long n_moves = 0;
    char *logpath = argc > 1 ? argv[1] : "/tmp/kobo-hid.log";

    logf = fopen(logpath, "a");
    logm("kobo-hid start");
    load_cfg();
    logm("config: ORIENT=%d FINGER=%d KEY_NEXT=0x%02x KEY_PREV=0x%02x", orient, finger_ok, key_next, key_prev);

    watch_only = argc > 2 && !strcmp(argv[2], "watch");
    if (watch_only) logm("watch mode: no USB output");
    else hid_fd = open("/dev/hidg0", O_RDWR | O_NONBLOCK);
    if (!watch_only && hid_fd < 0) {
        logm("cannot open /dev/hidg0 (%s): gadget not set up", strerror(errno));
        return 2;
    }
    signal(SIGTERM, sig_stop);
    signal(SIGINT, sig_stop);
    scan_inputs();
    if (n_in == 0) { logm("no input devices found"); return 3; }

    while (!stop_flag) {
        struct pollfd p[MAXDEV];
        int i, r;
        for (i = 0; i < n_in; i++) { p[i].fd = in_fd[i]; p[i].events = POLLIN; }
        r = poll(p, n_in, combo ? 50 : 1000);
        if (r < 0 && errno != EINTR) break;

        if (combo && fwd_down && back_down && now_ms() - combo_t >= 1500) {
            logm("both buttons held: stopping");
            break;
        }
        for (i = 0; i < n_in; i++) {
            struct input_event ev[64];
            ssize_t n;
            int k;
            if (!(p[i].revents & POLLIN)) continue;
            n = read(in_fd[i], ev, sizeof ev);
            if (n <= 0) continue;
            for (k = 0; k < (int)(n / sizeof ev[0]); k++) {
                struct input_event *e = &ev[k];
                if (e->type == EV_KEY) {
                    int c = e->code, is_fwd, is_back;
                    if (e->value == 2) continue; /* autorepeat */
                    if (c != BTN_TOUCH && c != BTN_TOOL_PEN && c != BTN_TOOL_FINGER)
                        logm("key code %d value %d", c, e->value);
                    if (c == KEY_POWER && e->value == 1) { logm("power button: stopping"); stop_flag = 1; break; }
                    if (c == BTN_TOUCH) { btn_touch = e->value; have_btn_touch = 1; continue; }
                    if (c == BTN_TOOL_PEN) { if (e->value) tool = MT_TOOL_PEN; continue; }
                    if (c == BTN_STYLUS || c == BTN_STYLUS2) { side = e->value ? 1 : 0; continue; }
                    is_fwd = fwd_code ? c == fwd_code : (c == K_KOBO_FWD || c == KEY_PAGEDOWN);
                    is_back = back_code ? c == back_code : (c == K_KOBO_BACK || c == KEY_PAGEUP);
                    if (!is_fwd && !is_back) continue;
                    if (e->value == 1) {
                        if (is_fwd) fwd_down = 1; else back_down = 1;
                        if (fwd_down && back_down) { combo = 1; combo_t = now_ms(); }
                    } else {
                        int was_combo = combo;
                        if (is_fwd) fwd_down = 0; else back_down = 0;
                        if (was_combo) {
                            if (!fwd_down && !back_down) {
                                combo = 0;
                                if (!watch_only) {
                                    orient = (orient + 1) & 7;
                                    logm("orientation -> %d", orient);
                                    save_cfg();
                                }
                            }
                        } else {
                            /* sent on release, so a two-button press is not
                               also typed as an arrow key */
                            send_key(is_fwd ? key_next : key_prev);
                        }
                    }
                } else if (e->type == EV_ABS && i == touch_idx) {
                    switch (e->code) {
                    case ABS_MT_SLOT: slot = e->value; break;
                    case ABS_MT_TRACKING_ID: if (slot == 0) { tid = e->value; if (tid < 0) tool = 0; } break;
                    case ABS_MT_TOOL_TYPE: if (slot == 0) tool = e->value; break;
                    case ABS_MT_POSITION_X: if (slot == 0) rx = e->value; break;
                    case ABS_MT_POSITION_Y: if (slot == 0) ry = e->value; break;
                    }
                } else if (e->type == EV_SYN && e->code == SYN_REPORT && i == touch_idx) {
                    int x, y, contact, down, buttons;
                    contact = tid >= 0;
                    if (contact && has_tooltype && tool != MT_TOOL_PEN && !finger_ok) contact = 0;
                    if (!contact) {
                        if (last_btn > 0) send_ptr(0, last_x < 0 ? 0 : last_x, last_y < 0 ? 0 : last_y, 1);
                        continue;
                    }
                    /* pen down: BTN_TOUCH if the panel reports it, else any contact */
                    down = have_btn_touch ? btn_touch > 0 : 1;
                    map_xy(rx, ry, &x, &y);
                    buttons = side ? (down ? 2 : 0) : (down ? 1 : 0);
                    send_ptr(buttons, x, y, 0);
                    if (++n_moves % 2000 == 0) logm("%ld pen reports sent, %ld dropped/failed", n_moves, hid_errors);
                }
            }
        }
    }
    /* let go of everything */
    if (last_btn > 0) send_ptr(0, last_x, last_y, 1);
    release_inputs();
    logm("kobo-hid stop: %ld pen reports, %ld dropped/failed", n_moves, hid_errors);
    return 0;
}
