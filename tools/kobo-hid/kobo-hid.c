/*
 * kobo-hid: makes the Kobo Sage a USB pen tablet + trackpad for a computer.
 * Nothing is installed on the computer: it sees a standard USB mouse.
 *
 *   pen                     -> moves the pointer to the same spot of the
 *                              screen (absolute, like a drawing tablet);
 *                              pen touching = left button held,
 *                              pen side button = right button
 *   one finger              -> trackpad: moves the pointer (relative)
 *   one-finger tap          -> click
 *   two-finger tap          -> right click
 *   two fingers moving      -> scroll
 *   finger held in a screen corner for 2 s -> stop
 *
 * Needs the USB HID gadget set up first (kobo-hid.sh does it): one HID
 * function, /dev/hidg0, report ID 1 = absolute pointer (6 bytes),
 * report ID 2 = relative mouse with wheel and horizontal pan (6 bytes).
 *
 * "kobo-hid <log> calib": ask for two pen taps (top-left, top-right corner as
 * the Kobo is held), work out the pen direction, save it and exit. This also
 * runs by itself the first time (config has no ORIENT_SET=1).
 *
 * The program takes the touch screen away from the Kobo's own software
 * (EVIOCGRAB) while it runs and gives it back when it stops.
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

static const char *cfg_path = "/mnt/onboard/.adds/kobohid/config";
static const char *fbink_path = "/mnt/onboard/.adds/koreader/fbink";
static FILE *logf;
static volatile sig_atomic_t stop_flag;

/* ---- settings (config file, KEY=value) ---- */
static int orient = 1;      /* 0..7: bit0 swap x/y, bit1 mirror x, bit2 mirror y */
static int orient_set = 0;  /* 1 = pen direction was set by the two-tap check */
static int aspect_w = 16, aspect_h = 10; /* computer screen shape; 0 = use whole Kobo */
static int speed = 150;     /* trackpad speed, percent */
static int finger_ok = 1;   /* 0 = no trackpad (pen only) */
static int scroll_dir = 1;  /* -1 flips scrolling */

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

static void screen_msg(const char *text)
{
    char cmd[512];
    if (access(fbink_path, X_OK) != 0) return;
    snprintf(cmd, sizeof cmd, "%s -q -m -M -c \"%s\" >/dev/null 2>&1", fbink_path, text);
    if (system(cmd) != 0) logm("fbink failed");
}

static void load_cfg(void)
{
    char line[128], key[64], val[64];
    FILE *f = fopen(cfg_path, "r");
    if (!f) return;
    while (fgets(line, sizeof line, f)) {
        int v;
        if (line[0] == '#') continue;
        if (sscanf(line, "%63[^=]=%63s", key, val) != 2) continue;
        v = (int)strtol(val, NULL, 0);
        if (!strcmp(key, "ORIENT")) orient = v & 7;
        else if (!strcmp(key, "ORIENT_SET")) orient_set = v;
        else if (!strcmp(key, "SCREEN")) {
            int w = 0, h = 0;
            if (sscanf(val, "%d:%d", &w, &h) == 2 && w > 0 && h > 0) { aspect_w = w; aspect_h = h; }
            else { aspect_w = 0; aspect_h = 0; }
        }
        else if (!strcmp(key, "SPEED")) speed = v > 10 ? v : 10;
        else if (!strcmp(key, "FINGER")) finger_ok = v;
        else if (!strcmp(key, "SCROLL")) scroll_dir = v < 0 ? -1 : 1;
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
    fprintf(f, "# kobo-hid settings\n");
    fprintf(f, "# ORIENT/ORIENT_SET: pen direction, set by \"Mac tablet - pen direction\"\n");
    fprintf(f, "ORIENT=%d\nORIENT_SET=%d\n", orient, orient_set);
    fprintf(f, "# SCREEN: shape of the computer screen (16:10 most Macs, 16:9 most monitors, 0 = whole Kobo)\n");
    if (aspect_w) fprintf(f, "SCREEN=%d:%d\n", aspect_w, aspect_h); else fprintf(f, "SCREEN=0\n");
    fprintf(f, "# SPEED: trackpad speed in percent; FINGER=0 turns the trackpad off; SCROLL=-1 flips scrolling\n");
    fprintf(f, "SPEED=%d\nFINGER=%d\nSCROLL=%d\n", speed, finger_ok, scroll_dir);
    fclose(f);
    rename(tmp, cfg_path);
    sync();
}

/* ---- HID output ---- */
static int hid_fd = -1;
static long hid_errors, n_reports;

static void hid_write(const unsigned char *buf, int n, int must)
{
    /* f_hid takes one report at a time; with O_NONBLOCK it answers EAGAIN
       while the previous one still waits for the computer. Plain moves may
       be dropped; button changes are retried for up to 200 ms. */
    long t0 = now_ms();
    if (hid_fd < 0) return;
    for (;;) {
        ssize_t r = write(hid_fd, buf, n);
        if (r == n) { n_reports++; return; }
        if (r < 0 && errno != EAGAIN && errno != EINTR) { hid_errors++; return; }
        if (!must || now_ms() - t0 > 200) { hid_errors++; return; }
        usleep(1000);
    }
}

static int abs_btn = 0, abs_x = -1, abs_y = -1;

static void send_abs(int buttons, int x, int y)
{
    unsigned char r[6];
    int must = buttons != abs_btn;
    if (!must && x == abs_x && y == abs_y) return;
    r[0] = 1;
    r[1] = buttons & 7;
    r[2] = x & 0xff; r[3] = (x >> 8) & 0xff;
    r[4] = y & 0xff; r[5] = (y >> 8) & 0xff;
    hid_write(r, 6, must);
    abs_btn = buttons; abs_x = x; abs_y = y;
}

static int rel_btn = 0;

static int clamp127(int v) { return v > 127 ? 127 : v < -127 ? -127 : v; }

static void send_rel(int buttons, int dx, int dy, int wheel, int pan)
{
    /* larger moves are split into several reports */
    int must = buttons != rel_btn;
    do {
        unsigned char r[6];
        int sx = clamp127(dx), sy = clamp127(dy), sw = clamp127(wheel), sp = clamp127(pan);
        r[0] = 2;
        r[1] = buttons & 7;
        r[2] = (unsigned char)(signed char)sx;
        r[3] = (unsigned char)(signed char)sy;
        r[4] = (unsigned char)(signed char)sw;
        r[5] = (unsigned char)(signed char)sp;
        hid_write(r, 6, must || sw || sp);
        dx -= sx; dy -= sy; wheel -= sw; pan -= sp;
        must = 0;
    } while (dx || dy || wheel || pan);
    rel_btn = buttons;
}

static void click(int button)
{
    send_rel(button, 0, 0, 0, 0);
    usleep(15000);
    send_rel(0, 0, 0, 0, 0);
}

/* ---- input devices ---- */
#define MAXDEV 12
static int in_fd[MAXDEV];
static int n_in;
static int touch_idx = -1;
static struct input_absinfo ax, ay, ap;
static int has_tooltype, has_pressure;

static void sig_stop(int s) { (void)s; stop_flag = 1; }

static void scan_inputs(void)
{
    char path[32], name[96];
    int i;
    for (i = 0; i < 16 && n_in < MAXDEV; i++) {
        unsigned long evb[NLONGS(EV_MAX + 1)], keyb[NLONGS(KEY_MAX + 1)], absb[NLONGS(ABS_MAX + 1)];
        int fd, is_touch, is_power;
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
        is_power = TEST_BIT(EV_KEY, evb) && TEST_BIT(KEY_POWER, keyb);
        logm("input %s \"%s\" touch=%d power=%d", path, name, is_touch, is_power);
        if (!(is_touch && touch_idx < 0) && !is_power) { close(fd); continue; }
        if (is_touch && touch_idx < 0) {
            /* only the touch screen is taken away from the Kobo's software;
               the power button is just listened to */
            if (ioctl(fd, EVIOCGRAB, 1) < 0) logm("  grab failed: %s", strerror(errno));
            touch_idx = n_in;
            has_tooltype = TEST_BIT(ABS_MT_TOOL_TYPE, absb);
            has_pressure = TEST_BIT(ABS_MT_PRESSURE, absb);
            ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X), &ax);
            ioctl(fd, EVIOCGABS(ABS_MT_POSITION_Y), &ay);
            if (has_pressure) ioctl(fd, EVIOCGABS(ABS_MT_PRESSURE), &ap);
            logm("  touch x %d..%d  y %d..%d  pressure %s %d..%d  pen/finger told apart: %s",
                 ax.minimum, ax.maximum, ay.minimum, ay.maximum, has_pressure ? "yes" : "no",
                 ap.minimum, ap.maximum, has_tooltype ? "yes" : "no");
        }
        in_fd[n_in++] = fd;
    }
}

static void release_inputs(void)
{
    int i;
    for (i = 0; i < n_in; i++) { if (i == touch_idx) ioctl(in_fd[i], EVIOCGRAB, 0); close(in_fd[i]); }
    n_in = 0;
    touch_idx = -1;
}

/* raw touch position -> 0..1 across / down the Kobo as it is held */
static void held_uv(int rx, int ry, double *u, double *v)
{
    *u = ax.maximum > ax.minimum ? (double)(rx - ax.minimum) / (ax.maximum - ax.minimum) : 0;
    *v = ay.maximum > ay.minimum ? (double)(ry - ay.minimum) / (ay.maximum - ay.minimum) : 0;
    if (orient & 1) { double t = *u; *u = *v; *v = t; }
    if (orient & 2) *u = 1 - *u;
    if (orient & 4) *v = 1 - *v;
}

/* raw movement -> movement as the Kobo is held */
static void held_d(int dx, int dy, int *ox, int *oy)
{
    if (orient & 1) { int t = dx; dx = dy; dy = t; }
    if (orient & 2) dx = -dx;
    if (orient & 4) dy = -dy;
    *ox = dx; *oy = dy;
}

/* pen position -> 0..32767 on the computer screen, keeping its shape:
   the pen area is the biggest rectangle of the screen's shape centred on
   the Kobo; outside it the pointer stays on the screen edge */
static void map_abs(int rx, int ry, int *ox, int *oy)
{
    double u, v, kw, kh, aw = 1, ah = 1;
    held_uv(rx, ry, &u, &v);
    kw = (orient & 1) ? ay.maximum - ay.minimum : ax.maximum - ax.minimum; /* Kobo as held */
    kh = (orient & 1) ? ax.maximum - ax.minimum : ay.maximum - ay.minimum;
    if (aspect_w && aspect_h && kw > 0 && kh > 0) {
        double want = (double)aspect_w / aspect_h, have = kw / kh;
        if (have > want) aw = want / have; else ah = have / want;
    }
    u = (u - (1 - aw) / 2) / aw;
    v = (v - (1 - ah) / 2) / ah;
    if (u < 0) u = 0;
    if (u > 1) u = 1;
    if (v < 0) v = 0;
    if (v > 1) v = 1;
    *ox = (int)(u * 32767 + 0.5);
    *oy = (int)(v * 32767 + 0.5);
}

/* ---- touch frames ---- */
#define NSLOT 10
struct contact { int tid, tool, x, y, p; };
static struct contact sl[NSLOT];
static int cur_slot, btn_touch, have_btn_touch, side_btn;

static void touch_init(void)
{
    int i;
    for (i = 0; i < NSLOT; i++) { sl[i].tid = -1; sl[i].tool = 0; sl[i].p = 0; }
    cur_slot = 0; btn_touch = 0; have_btn_touch = 0; side_btn = 0;
}

/* feed one event; returns 1 at the end of a frame (SYN_REPORT) */
static int touch_event(const struct input_event *e)
{
    struct contact *c = &sl[cur_slot];
    if (e->type == EV_KEY) {
        if (e->code == BTN_TOUCH) { btn_touch = e->value > 0; have_btn_touch = 1; }
        else if (e->code == BTN_STYLUS || e->code == BTN_STYLUS2) side_btn = e->value > 0;
        else if (e->code == BTN_TOOL_PEN && e->value && !has_tooltype) c->tool = MT_TOOL_PEN;
        return 0;
    }
    if (e->type == EV_ABS) {
        switch (e->code) {
        case ABS_MT_SLOT: cur_slot = e->value >= 0 && e->value < NSLOT ? e->value : 0; break;
        case ABS_MT_TRACKING_ID: c->tid = e->value; if (e->value < 0) { c->tool = 0; c->p = 0; } break;
        case ABS_MT_TOOL_TYPE: c->tool = e->value; break;
        case ABS_MT_POSITION_X: c->x = e->value; break;
        case ABS_MT_POSITION_Y: c->y = e->value; break;
        case ABS_MT_PRESSURE: c->p = e->value; break;
        }
        return 0;
    }
    return e->type == EV_SYN && e->code == SYN_REPORT;
}

/* read events from all devices for up to timeout ms; calls on_frame for each
   touch frame; returns -1 on stop */
static int pump(int timeout, void (*on_frame)(void))
{
    struct pollfd p[MAXDEV];
    int i, r;
    for (i = 0; i < n_in; i++) { p[i].fd = in_fd[i]; p[i].events = POLLIN; }
    r = poll(p, n_in, timeout);
    if (r < 0 && errno != EINTR) return -1;
    for (i = 0; i < n_in && r > 0; i++) {
        struct input_event ev[64];
        ssize_t n;
        int k;
        if (!(p[i].revents & POLLIN)) continue;
        n = read(in_fd[i], ev, sizeof ev);
        if (n <= 0) continue;
        for (k = 0; k < (int)(n / sizeof ev[0]); k++) {
            if (i != touch_idx) {
                if (ev[k].type == EV_KEY && ev[k].code == KEY_POWER && ev[k].value == 1) {
                    logm("power button: stopping");
                    stop_flag = 1;
                }
                continue;
            }
            if (touch_event(&ev[k]) && on_frame) on_frame();
        }
    }
    return stop_flag ? -1 : 0;
}

/* is this contact the pen? */
static int is_pen(const struct contact *c)
{
    return has_tooltype ? c->tool == MT_TOOL_PEN : 1; /* no tool info: all contacts are the pen */
}

static int pen_down(const struct contact *c)
{
    if (has_pressure) return c->p > ap.minimum;
    return have_btn_touch ? btn_touch : 1;
}

/* ---- stop gesture: a finger held 2 s in any corner ---- */
static long corner_t;

static int corner_check(void)
{
    int i, in_corner = 0;
    int wx = (ax.maximum - ax.minimum) / 10, wy = (ay.maximum - ay.minimum) / 10;
    for (i = 0; i < NSLOT; i++) {
        struct contact *c = &sl[i];
        if (c->tid < 0 || (has_tooltype && c->tool == MT_TOOL_PEN)) continue;
        if ((c->x <= ax.minimum + wx || c->x >= ax.maximum - wx) &&
            (c->y <= ay.minimum + wy || c->y >= ay.maximum - wy)) in_corner = 1;
    }
    if (!in_corner) { corner_t = 0; return 0; }
    if (!corner_t) corner_t = now_ms();
    else if (now_ms() - corner_t >= 2000) {
        logm("finger held in a corner: stopping");
        stop_flag = 1;
        return 1;
    }
    return 0;
}

/* ---- pen direction check: two pen taps ---- */
static int cal_n, cal_x[2], cal_y[2], cal_was_down;

static void cal_frame(void)
{
    int i;
    corner_check();
    for (i = 0; i < NSLOT; i++) {
        struct contact *c = &sl[i];
        if (c->tid < 0 || !is_pen(c)) continue;
        if (pen_down(c)) {
            if (!cal_was_down && cal_n < 2) {
                cal_x[cal_n] = c->x; cal_y[cal_n] = c->y;
                logm("direction check tap %d at raw %d,%d", cal_n + 1, c->x, c->y);
                cal_n++;
            }
            cal_was_down = 1;
        } else cal_was_down = 0;
        return;
    }
    cal_was_down = 0;
}

/* works out ORIENT from two taps; returns -1 if they are unusable */
int orient_from_taps(int x0, int y0, int x1, int y1, double mx, double my)
{
    int dx = x1 - x0, dy = y1 - y0, o = 0;
    if (abs(dx) < 300 && abs(dy) < 300) return -1;
    if (abs(dx) >= abs(dy)) {      /* "right" runs along raw x */
        if (dx < 0) o |= 2;
        if (y0 > my) o |= 4;       /* top-left is on the high-y side */
    } else {                       /* "right" runs along raw y */
        o |= 1;
        if (dy < 0) o |= 2;
        if (x0 > mx) o |= 4;
    }
    return o;
}

static int calibrate(void)
{
    int o;
    cal_n = 0; cal_was_down = 0;
    screen_msg("Pen direction. Hold the Kobo the way you will use it, then tap the TOP-LEFT corner with the pen.");
    while (cal_n < 1) if (pump(1000, cal_frame) < 0) return -1;
    screen_msg("Good. Now tap the TOP-RIGHT corner with the pen.");
    while (cal_n < 2 || cal_was_down) if (pump(1000, cal_frame) < 0) return -1;
    o = orient_from_taps(cal_x[0], cal_y[0], cal_x[1], cal_y[1],
                         (ax.minimum + ax.maximum) / 2.0, (ay.minimum + ay.maximum) / 2.0);
    if (o < 0) {
        logm("direction taps too close together: not saved");
        screen_msg("The two taps were too close together. Pen direction not changed.");
        sleep(2);
        return 0;
    }
    orient = o;
    orient_set = 1;
    save_cfg();
    logm("pen direction saved: ORIENT=%d", orient);
    return 0;
}

/* ---- tablet + trackpad ---- */
static int pen_active;            /* pen near or on the screen: fingers ignored */
static long pen_gone_t;
static int g_active, g_max_fingers, g_moved, g_travel, g_nf;
static long g_start_t;
static int g_lastx, g_lasty;
static double acc_x, acc_y, acc_s, acc_p;
static long hover_frames, down_frames, pen_frames_logged;

static void pad_frame(void)
{
    int i, nf = 0, fx = 0, fy = 0;
    struct contact *pen = NULL;

    if (corner_check()) return;
    for (i = 0; i < NSLOT; i++) {
        struct contact *c = &sl[i];
        if (c->tid < 0) continue;
        if (is_pen(c)) { if (!pen) pen = c; }
        else { nf++; fx += c->x; fy += c->y; }
    }

    /* pen: absolute pointer */
    if (pen) {
        int x, y, down = pen_down(pen), b;
        if (pen_frames_logged < 40) {
            pen_frames_logged++;
            logm("pen raw %d,%d p=%d touch=%d side=%d", pen->x, pen->y, pen->p, btn_touch, side_btn);
        }
        if (down) down_frames++; else hover_frames++;
        map_abs(pen->x, pen->y, &x, &y);
        b = down ? (side_btn ? 2 : 1) : 0;
        send_abs(b, x, y);
        pen_active = 1;
        g_active = 0; /* a finger gesture under the pen hand is cancelled */
        return;
    }
    if (pen_active) {
        if (abs_btn) send_abs(0, abs_x, abs_y);
        pen_active = 0;
        pen_gone_t = now_ms();
    }
    if (!finger_ok) return;
    /* palm guard: fingers count again 300 ms after the pen leaves */
    if (now_ms() - pen_gone_t < 300) { g_active = 0; return; }

    /* fingers: trackpad */
    if (nf == 0) {
        if (g_active) {
            if (!g_moved && now_ms() - g_start_t < 300) {
                if (g_max_fingers >= 2) { logm("two-finger tap: right click"); click(2); }
                else click(1);
            }
            g_active = 0;
        }
        return;
    }
    fx /= nf; fy /= nf;
    if (!g_active) {
        g_active = 1; g_max_fingers = 0; g_moved = 0; g_travel = 0; g_nf = 0;
        g_start_t = now_ms();
        acc_x = acc_y = acc_s = acc_p = 0;
    }
    if (nf > g_max_fingers) g_max_fingers = nf;
    if (nf != g_nf) {
        /* new gesture, or a finger added/lifted: new reference point */
        g_lastx = fx; g_lasty = fy; g_nf = nf;
        return;
    }
    {
        int dx, dy, rdx = fx - g_lastx, rdy = fy - g_lasty;
        held_d(rdx, rdy, &dx, &dy);
        g_lastx = fx; g_lasty = fy;
        if (!rdx && !rdy) return;
        if (!g_moved) {
            g_travel += abs(dx) + abs(dy);
            if (g_travel < 25) return; /* small wobble: still a tap */
            g_moved = 1;
        }
        if (nf == 1) {
            int ox, oy;
            acc_x += dx * speed / 100.0;
            acc_y += dy * speed / 100.0;
            ox = (int)acc_x; oy = (int)acc_y;
            acc_x -= ox; acc_y -= oy;
            if (ox || oy) send_rel(0, ox, oy, 0, 0);
        } else {
            int w, pn;
            /* one scroll step per 20 raw units (about 2 mm) of finger travel */
            acc_s += -dy * scroll_dir / 20.0;
            acc_p += dx * scroll_dir / 20.0;
            w = (int)acc_s; pn = (int)acc_p;
            acc_s -= w; acc_p -= pn;
            if (w || pn) send_rel(0, 0, 0, w, pn);
        }
    }
}

#ifndef KOBO_HID_TEST
int main(int argc, char **argv)
{
    char *logpath = argc > 1 ? argv[1] : "/tmp/kobo-hid.log";
    int calib_only = argc > 2 && !strcmp(argv[2], "calib");

    logf = fopen(logpath, "a");
    logm("kobo-hid start (%s)", calib_only ? "pen direction" : "tablet + trackpad");
    load_cfg();
    logm("config: ORIENT=%d ORIENT_SET=%d SCREEN=%d:%d SPEED=%d FINGER=%d SCROLL=%d",
         orient, orient_set, aspect_w, aspect_h, speed, finger_ok, scroll_dir);
    signal(SIGTERM, sig_stop);
    signal(SIGINT, sig_stop);

    if (!calib_only) {
        hid_fd = open("/dev/hidg0", O_RDWR | O_NONBLOCK);
        if (hid_fd < 0) {
            logm("cannot open /dev/hidg0 (%s): gadget not set up", strerror(errno));
            return 2;
        }
    }
    scan_inputs();
    if (touch_idx < 0) { logm("no touch screen found"); release_inputs(); return 3; }
    touch_init();

    if (calib_only || !orient_set) {
        if (calibrate() < 0) { release_inputs(); return 0; }
        if (calib_only) {
            screen_msg("Pen direction saved.");
            release_inputs();
            return 0;
        }
    }
    screen_msg("Mac tablet ON. Pen = pointer, pen touching = click. One finger = trackpad, tap = click, two-finger tap = right click, two fingers = scroll. Stop: hold a finger in a corner 2 s.");

    while (!stop_flag)
        if (pump(1000, pad_frame) < 0) break;

    if (abs_btn) send_abs(0, abs_x, abs_y);
    if (rel_btn) send_rel(0, 0, 0, 0, 0);
    release_inputs();
    logm("kobo-hid stop: %ld reports sent, %ld dropped/failed, pen frames %ld hovering %ld touching",
         n_reports, hid_errors, hover_frames + down_frames, hover_frames, down_frames);
    return 0;
}
#endif
