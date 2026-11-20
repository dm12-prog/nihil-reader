/*
 * Test 3a: a numbered "book" on the e-ink screen, pages turned with the buttons.
 * (Grew out of milestone 2, tests 2b / 2c: first drawing on the screen.)
 *
 * One program, two modes (chosen by the loader, see "boot parameters"):
 *   drive = 0 (test 2b, rehearsal): do everything EXCEPT switching the panel
 *       power on and starting the timing controller. The engine works out the
 *       whole update into RAM and we log what it produced.
 *   drive = 1 (test 2c): the same, then power the panel and play the frames.
 *
 * How a screen update works on this chip (Kobo source, kobolabs/b300-sage:
 * u-boot disp2/disp/de/disp_eink_manager.c + lowlevel_v2x/de_eink.c):
 *   1. "Index": the engine compares the old picture and the new picture and
 *      writes, per pixel, the pair (old grey, new grey).
 *   2. "Decode": for each of the N frames of a waveform, the engine turns the
 *      index into the raw 2-bit drive codes for every pixel ("wavedata").
 *   3. "EDMA + TCON": the timing controller clocks one wavedata buffer per
 *      frame out to the panel while the power chip (SY7636) supplies +-15 V.
 *
 * Safety: watchdog armed from the first instruction. All frames are decoded
 * BEFORE the panel gets power, so nothing slow happens while it is powered.
 * Every stage writes a marker to the black box (bb[6]) so a hang is located.
 */
#include "sage.h"
#include "../../core/reader.h"

/* ---------- time ---------- */
static uint64_t ticks(void)
{
    uint32_t lo, hi;
    __asm__ volatile("isb; mrrc p15, 0, %0, %1, c14" : "=r"(lo), "=r"(hi));
    return ((uint64_t)hi << 32) | lo;
}
static uint64_t t0;
static uint32_t now_us(void) { return (uint32_t)((ticks() - t0) / 24u); }   /* 24 MHz counter; wraps after 71 min, differences stay right */
static void sleep_us(uint32_t us)
{
    uint64_t end = ticks() + (uint64_t)us * 24u;
    while (ticks() < end)
        ;
}

/* ---------- watchdog ---------- */
static void wdt_arm(uint32_t interval_code)        /* 0xB = 16 s, 6 = 6 s, 0 = 0.5 s */
{
    REG32(WDT_CFG) = (REG32(WDT_CFG) & ~0x3u) | 0x1u;
    REG32(WDT_MODE) = (interval_code << 4) | 1u;
    REG32(WDT_CTRL) = (1u << 0) | (0x0A57u << 1);
}
static void wdt_feed(void) { REG32(WDT_CTRL) = (1u << 0) | (0x0A57u << 1); }

__attribute__((noreturn)) static void reboot(void)
{
    wdt_arm(0);
    for (;;) {
        sleep_us(5000);
        REG32(WDT_MODE) |= 1u;
    }
}

/* ---------- black box log: (tag, value) pairs ---------- */
static volatile uint32_t *const bb = (volatile uint32_t *)BLACKBOX_PHYS;
static uint32_t npairs;
static void lg(uint32_t tag, uint32_t v)
{
    if (npairs < BLACKBOX_MAX_PAIRS) {
        bb[BLACKBOX_EVENTS + npairs * 2] = tag;
        bb[BLACKBOX_EVENTS + npairs * 2 + 1] = v;
        npairs++;
        bb[3] = npairs;
    }
}
static void stage(uint32_t s) { bb[6] = s; lg(T_STAGE | s, now_us() / 1000u); wdt_feed(); }
__attribute__((noreturn)) static void finish(uint32_t code)
{
    bb[4] = code | (npairs & 0xFFFF);
    reboot();
}

/* ---------- GPIO ---------- */
static uint32_t port_base(int port) { return port == 11 ? R_PIO_BASE : PIO_BASE + (uint32_t)port * 0x24u; }
static void pin_mode(int port, int pin, uint32_t mode)
{
    uint32_t reg = port_base(port) + (uint32_t)(pin / 8) * 4u, sh = (uint32_t)(pin % 8) * 4u;
    REG32(reg) = (REG32(reg) & ~(0xFu << sh)) | (mode << sh);
}
static void pin_write(int port, int pin, int v)
{
    uint32_t reg = port_base(port) + 0x10;
    REG32(reg) = v ? (REG32(reg) | (1u << pin)) : (REG32(reg) & ~(1u << pin));
}
static int pin_read(int port, int pin) { return (REG32(port_base(port) + 0x10) >> pin) & 1; }

/* ---------- bit-banged I2C on PH2 (clock) / PH3 (data) = Kobo's "twi1" ---------- */
static int I2C_PORT = 7, I2C_SCL = 2, I2C_SDA = 3;           /* chosen by i2c_find_pmic() */
static void i2c_delay(void) { sleep_us(5); }                 /* ~100 kHz */
/* A line is "high" when we let go of it (pin = input, the board pulls it up)
 * and "low" when we drive it (pin = output with its data bit 0).
 * The data bit is cleared every time just before driving: writing any pin of
 * a port copies the current level of the port's input pins into their data
 * bits, so a released (high) line would otherwise be driven HIGH later.
 * That was the bug in the first two tries: the clock line never went low. */
static void line(int pin, int hi)
{
    if (hi) {
        pin_mode(I2C_PORT, pin, 0);
    } else {
        pin_write(I2C_PORT, pin, 0);
        pin_mode(I2C_PORT, pin, 1);
    }
    i2c_delay();
}
static void scl(int hi) { line(I2C_SCL, hi); }
static void sda(int hi) { line(I2C_SDA, hi); }
static void i2c_init(void)
{
    /* Released = input (pulled high by the board), driven = output low. */
    pin_mode(I2C_PORT, I2C_SCL, 0);
    pin_mode(I2C_PORT, I2C_SDA, 0);
    pin_write(I2C_PORT, I2C_SCL, 0);
    pin_write(I2C_PORT, I2C_SDA, 0);
    sleep_us(50);
}
static void i2c_start(void) { sda(1); scl(1); sda(0); scl(0); }
static void i2c_stop(void) { sda(0); scl(1); sda(1); }
static int i2c_tx(uint8_t b)                                  /* returns 1 if the chip answered */
{
    for (int i = 7; i >= 0; i--) {
        sda((b >> i) & 1);
        scl(1);
        scl(0);
    }
    sda(1);
    scl(1);
    int ack = !pin_read(I2C_PORT, I2C_SDA);
    scl(0);
    return ack;
}
static uint8_t i2c_rx_last(void)
{
    uint8_t b = 0;
    sda(1);
    for (int i = 0; i < 8; i++) {
        scl(1);
        b = (uint8_t)((b << 1) | pin_read(I2C_PORT, I2C_SDA));
        scl(0);
    }
    sda(1);                                                   /* NACK: last byte */
    scl(1);
    scl(0);
    return b;
}
static int pmic_read(uint8_t reg)                             /* -1 = no answer */
{
    i2c_start();
    if (!i2c_tx(SY7636_ADDR << 1) || !i2c_tx(reg)) { i2c_stop(); return -1; }
    i2c_start();
    if (!i2c_tx((SY7636_ADDR << 1) | 1)) { i2c_stop(); return -1; }
    uint8_t v = i2c_rx_last();
    i2c_stop();
    return v;
}
static int pmic_write(uint8_t reg, uint8_t val)               /* 1 = ok */
{
    i2c_start();
    int ok = i2c_tx(SY7636_ADDR << 1) && i2c_tx(reg) && i2c_tx(val);
    i2c_stop();
    return ok;
}

/* The published board file says the power chip is on twi1 (PH2/PH3), but this
 * board (D00) has surprised us before. So: look at every I2C bus the chip
 * could be on, list who answers, and use the bus where 0x62 answers.
 * A probe is only "start, address, stop": it changes nothing in any chip. */
static int i2c_find_pmic(void)
{
    static const uint8_t bus[][3] = { {7, 2, 3}, {7, 0, 1}, {4, 1, 2} };   /* port, clock pin, data pin */
    int found = -1;
    for (unsigned b = 0; b < 3; b++) {
        I2C_PORT = bus[b][0]; I2C_SCL = bus[b][1]; I2C_SDA = bus[b][2];
        uint32_t cfg = REG32(port_base(I2C_PORT));           /* remember the pin functions */
        i2c_init();
        scl(0); int c0 = pin_read(I2C_PORT, I2C_SCL); scl(1);
        sda(0); int d0 = pin_read(I2C_PORT, I2C_SDA); sda(1);
        lg(T_PMIC | 0x100u * (b + 1) | 2, (uint32_t)(c0 | d0 << 1));   /* 0 = both lines really go low */
        lg(T_PMIC | 0x100u * (b + 1), (uint32_t)(pin_read(I2C_PORT, I2C_SCL) | pin_read(I2C_PORT, I2C_SDA) << 1));
        uint32_t n = 0;
        for (uint8_t a = 0x08; a < 0x78; a++) {
            i2c_start();
            int ack = i2c_tx((uint8_t)(a << 1));
            i2c_stop();
            if (ack) {
                if (n < 12)
                    lg(T_PMIC | 0x100u * (b + 1) | (0x10 + n), a);
                n++;
                if (a == SY7636_ADDR && found < 0)
                    found = (int)b;
            }
        }
        lg(T_PMIC | 0x100u * (b + 1) | 1, n);
        if (found != (int)b)
            REG32(port_base(I2C_PORT)) = cfg;                 /* not ours: put the pins back */
        wdt_feed();
    }
    if (found >= 0) {
        I2C_PORT = bus[found][0]; I2C_SCL = bus[found][1]; I2C_SDA = bus[found][2];
    }
    lg(T_PMIC | 0x10, (uint32_t)found);
    return found;
}

/* ---------- boot parameters (written by the loader module) ---------- */
struct timing { uint32_t lsl, lbl, ldl, lel, gdck_sta, lgonl, fsl, fbl, fdl, fel; };
static struct timing tm;
static uint32_t p_drive, p_mode, p_temp, p_pm1, p_pm2;        /* p_pm*: power chip settings copied from Linux */

/* ---------- waveform file (AWF), preloaded by U-Boot ---------- */
static uint32_t rd32(uint32_t a)
{
    const volatile uint8_t *p = (const volatile uint8_t *)a;
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
/* disp_waveform.c get_waveform_data(): returns address of frame 0's table, sets *frames */
static uint32_t awf_lookup(uint32_t mode_slot, uint32_t temp, uint32_t *frames, uint32_t *frame_bytes)
{
    const volatile uint8_t *tt = (const volatile uint8_t *)(WAVEFORM_PHYS + 128);
    uint32_t mode = WAVEFORM_PHYS + rd32(WAVEFORM_PHYS + 160 + mode_slot * 4);
    uint32_t i;
    for (i = 0; i < 32; i++) {
        uint32_t off = rd32(mode + i * 4);
        if ((off == 0 || tt[i] == 0) && i > 0)
            break;
        if (temp <= tt[i])
            break;
    }
    if (i > 0)
        i--;
    uint32_t toff = rd32(mode + i * 4);
    uint32_t head = rd32(mode + toff);
    *frames = head & 0xFFFF;
    *frame_bytes = head >> 16;
    lg(T_WF | 0x10 | mode_slot, mode - WAVEFORM_PHYS);
    lg(T_WF | 0x20 | mode_slot, (i << 24) | toff);
    lg(T_WF | 0x30 | mode_slot, head);
    return mode + toff + 4;
}

/* ---------- control-signal template: disp_eink_data.c init_eink_ctrl_data_16() ---------- */
static uint32_t LTL, FTL;
static void build_template(volatile uint32_t *w)
{
    for (uint32_t h = 0; h < FTL; h++) {
        for (uint32_t x = 0; x < LTL; x++) {
            uint32_t v = 0;
            int in_rows = h >= tm.fsl + tm.fbl && h < tm.fsl + tm.fbl + tm.fdl;
            if (h > tm.fsl + tm.fbl && h < tm.fsl + tm.fbl + tm.fdl && x < tm.lsl)
                v |= 1u << 2;                                             /* sdle */
            if (!(in_rows && x >= tm.lsl + tm.lbl && x < tm.lsl + tm.lbl + tm.ldl))
                v |= 1u << 3;                                             /* sdsp (active low) */
            if (h < tm.fsl + tm.fbl + tm.fdl && x >= tm.gdck_sta + tm.lsl && x < tm.gdck_sta + tm.lsl + tm.lgonl)
                v |= 1u << 4;                                             /* gdck */
            if (!((h == 0 && x >= tm.gdck_sta + (tm.lgonl >> 1)) || (h > 0 && h < tm.fsl) ||
                  (h == tm.fsl && x < LTL - tm.gdck_sta - (tm.lgonl >> 1))))
                v |= 1u << 5;                                             /* gdsp (active low) */
            if ((h == tm.fbl && x >= tm.gdck_sta) || (h > tm.fbl && h < tm.fbl + tm.fdl) ||
                (h == tm.fbl + tm.fdl && x < tm.gdck_sta))
                v |= 1u << 1;                                             /* sdoe */
            if ((h == 0 && x >= tm.gdck_sta) || (h > 0 && h < tm.fbl + tm.fdl) ||
                (h == tm.fbl + tm.fdl && x < tm.gdck_sta))
                v |= 1u << 0;                                             /* gdoe */
            *w++ = v;
        }
        if ((h & 0xFF) == 0)
            wdt_feed();
    }
}

static void copy_words(volatile uint32_t *d, const volatile uint32_t *s, uint32_t n)
{
    while (n--)
        *d++ = *s++;
}
static void fill_words(volatile uint32_t *d, uint32_t v, uint32_t n)
{
    while (n--)
        *d++ = v;
}

/* ---------- EINK engine registers (de_eink.c) ---------- */
#define EE(off) REG32(EINK_BASE + (off))
static int ee_wait(uint32_t bit, uint32_t timeout_us)
{
    uint32_t start = now_us();
    while (!(EE(0x004) & bit))
        if (now_us() - start > timeout_us)
            return 0;
    return 1;
}

static void panel_power_off(void)
{
    pin_write(4, 3, 0);                                       /* PE3: VCOM off */
    sleep_us(2000);
    pmic_write(0x00, 0x60);                                   /* rails off */
    uint32_t s = now_us();
    while (pin_read(7, 7) && now_us() - s < 300000u)
        ;
    lg(T_PWR | 0x10, now_us() - s);                           /* time until power-good dropped */
    sleep_us(20000);
    pin_write(1, 7, 0);                                       /* PB7: xon off */
}

static void tcon_off(void)
{
    REG32(TCON_BASE + 0x040) = 0;
    REG32(TCON_BASE + 0x000) = 0;
    REG32(PIO_BASE + 3 * 0x24 + 0x0) = 0x77777777;            /* port D pins back to "off" */
    REG32(PIO_BASE + 3 * 0x24 + 0x4) = 0x77777777;
    REG32(PIO_BASE + 3 * 0x24 + 0x8) = 0x77777777;
    REG32(CCU_BASE + 0xB7C) = 0;
    REG32(CCU_BASE + 0xB60) = 0;
    REG32(CCU_BASE + 0x040) &= ~(1u << 31);
}

/* ---------- one screen update: picture at img_cur replaces picture at img_last ---------- */
static uint32_t g_wf, g_frames, p_drive_;
static uint32_t img_last = IMG_LAST, img_cur = IMG_CUR;

static void show(int verbose)
{
    uint32_t frames = g_frames, ts;
    wdt_arm(0xB);

    /* index calculation (old picture, new picture -> per-pixel pair) */
    for (uint32_t p = 0; p < 16; p++)
        EE(0x040 + (p << 4)) = 0;                             /* all pipes off */
    EE(0x000) = 0x12;                                         /* 5-bit greys, 16 data lines (as Linux) */
    EE(0x004) = 1u << 4;                                      /* status flags on */
    EE(0x004) = (1u << 4) | 3u;                               /* clear old flags */
    EE(0x00C) = ((PANEL_H - 1) << 16) | (PANEL_W - 1);
    EE(0x010) = img_last;  EE(0x014) = PANEL_W;
    EE(0x018) = img_cur;   EE(0x01C) = PANEL_W;
    EE(0x024) = 0;
    EE(0x028) = ((PANEL_H - 1) << 16) | (PANEL_W - 1);
    EE(0x020) = IDX_OLD;
    EE(0x02C) = IDX_NEW;
    ts = now_us();
    EE(0x008) = (1u << 8) | 1u;                               /* flash mode GLOBAL, start */
    int idx_ok = ee_wait(2, 500000);
    if (verbose || !idx_ok) {
        lg(T_IDX, (uint32_t)idx_ok); lg(T_IDX | 1, now_us() - ts); lg(T_IDX | 2, EE(0x004));
        lg(T_IDX | 3, EE(0x024)); lg(T_IDX | 4, EE(0x028));
    }
    EE(0x004) = (EE(0x004) & 0x1E);                           /* clear the index flag */
    if (!idx_ok)
        finish(END_IDX_TIMEOUT);

    /* decode every frame into its own buffer */
    uint32_t dec_pitch = ((tm.lbl + tm.lsl + tm.lel) << 2) + (PANEL_W >> 1);
    uint32_t dec_off = (tm.fbl + tm.fsl) * dec_pitch + ((tm.lbl + tm.lsl) << 2);
    EE(0x044) = 0;
    EE(0x048) = ((PANEL_H - 1) << 16) | (PANEL_W - 1);
    uint32_t worst = 0;
    for (uint32_t f = 0; f < frames; f++) {
        EE(0x04C) = g_wf + 1024u * f;                         /* this frame's 32x32 table */
        EE(0x040) = 1;
        EE(0x00C) = ((PANEL_H - 1) << 16) | (PANEL_W - 1);
        EE(0x15C) = dec_pitch;
        EE(0x154) = IDX_NEW;
        EE(0x158) = WAV_BUF0 + f * WAV_BUF_STEP + dec_off;
        ts = now_us();
        EE(0x150) = 1;
        if (!ee_wait(1, 500000)) {
            lg(T_DEC | 0xFF, f); lg(T_DEC | 0xFE, EE(0x004));
            finish(END_DEC_TIMEOUT);
        }
        uint32_t d = now_us() - ts;
        if (d > worst)
            worst = d;
        EE(0x004) = (EE(0x004) & 0x1D);                       /* clear the decode flag */
        wdt_feed();
    }
    EE(0x040) = 0;
    if (verbose) { lg(T_DEC, frames); lg(T_DEC | 1, worst); }

    /* The control bits of every buffer must still equal the template: a
     * damaged buffer would send wrong signals to the panel. Checked on 3 frames. */
    for (uint32_t k = 0; k < 3; k++) {
        uint32_t f = k == 0 ? 0 : k == 1 ? frames / 2 : frames - 1;
        volatile uint32_t *b = (volatile uint32_t *)(WAV_BUF0 + f * WAV_BUF_STEP);
        volatile uint32_t *t = (volatile uint32_t *)WAV_TEMPLATE;
        uint32_t ctl_bad = 0, outside = 0;
        for (uint32_t i = 0; i < LTL * FTL; i += 7) {
            uint32_t v = b[i];
            if ((v & 0xFF) != t[i]) ctl_bad++;
            if ((v >> 8) && (t[i] & 8)) outside++;            /* data where no pixel is clocked */
        }
        if (verbose || ctl_bad) { lg(T_DEC | 0x30 | k, ctl_bad); lg(T_DEC | 0x50 | k, outside); }
        if (ctl_bad)
            finish(END_DEC_TIMEOUT);
        wdt_feed();
    }

    if (!p_drive_)
        finish(END_REHEARSAL_OK);

    /* DRIVE: power the panel and play the frames */
    wdt_arm(6);                                               /* from here: 6 s to finish or reboot */
    uint32_t hs = tm.lbl + tm.lsl + tm.lel, vs = tm.fbl + tm.fsl + tm.fel;
    EE(0x404) = 0x80000011;                                   /* EDMA on, 16 data lines */
    EE(0x400) = 0x80000000;
    EE(0x408) = WAV_TEMPLATE;                                 /* first: an empty frame (no drive) */
    EE(0x40C) = (PANEL_W + (hs << 3)) >> 1;
    EE(0x410) = ((PANEL_H + vs - 1) << 16) | (PANEL_W + (hs << 3) - 1);
    EE(0x414) = ((tm.fbl + tm.fsl) << 16) | ((tm.lbl + tm.lsl) << 3);
    EE(0x418) = ((PANEL_H - 1) << 16) | (PANEL_W - 1);
    EE(0x404) |= 1;
    EE(0x400) = 0x80010001;                                   /* "buffer ready" */

    /* timing controller clock + settings (values seen under Linux, test 2a) */
    REG32(CCU_BASE + 0x040) |= 1u << 31;                      /* video PLL on */
    sleep_us(20000);                                          /* same wait as the run that worked (2c-1) */
    REG32(CCU_BASE + 0xB60) = 0x80000000;
    REG32(CCU_BASE + 0xB7C) = 0x00010000;
    REG32(CCU_BASE + 0xB7C) = 0x00010001;
    sleep_us(100);
    REG32(TCON_BASE + 0x000) = 0;
    REG32(TCON_BASE + 0x004) = 0;
    REG32(TCON_BASE + 0x044) = 0xF0000006;
    REG32(TCON_BASE + 0x048) = 0x014305AA;
    REG32(TCON_BASE + 0x04C) = 0x014B0001;
    REG32(TCON_BASE + 0x050) = 0x0B760001;
    REG32(TCON_BASE + 0x054) = 0x00010001;
    REG32(TCON_BASE + 0x088) = 0x04000000;
    REG32(TCON_BASE + 0x08C) = 0;                             /* outputs enabled */

    /* panel power: xon, rails on over I2C, wait for "power good", then VCOM */
    pin_write(1, 7, 1);                                       /* PB7 xon */
    int w2 = pmic_write(0x00, 0xE0);
    ts = now_us();
    while (!pin_read(7, 7) && now_us() - ts < 200000u)
        ;
    uint32_t pg_us = now_us() - ts;
    if (verbose || !w2 || !pin_read(7, 7)) {
        lg(T_PWR, (uint32_t)(1 | (w2 << 1) | (pin_read(7, 7) << 2))); lg(T_PWR | 1, pg_us);
        lg(T_PWR | 2, (uint32_t)pmic_read(0x07));
    }
    if (!w2 || !pin_read(7, 7)) {
        panel_power_off();
        tcon_off();
        finish(END_NO_POWER_GOOD);
    }
    pin_write(4, 3, 1);                                       /* PE3: VCOM on */
    sleep_us(10000);

    /* go: pins to the panel, controller on */
    REG32(PIO_BASE + 3 * 0x24 + 0x0) = 0x22222222;
    REG32(PIO_BASE + 3 * 0x24 + 0x4) = 0x22222222;
    REG32(PIO_BASE + 3 * 0x24 + 0x8) = 0x72222222;
    REG32(TCON_BASE + 0x040) = 0x82000080;
    REG32(TCON_BASE + 0x000) = 0x80000000;

    /* Play list: 2 empty frames, the real frames, 2 empty frames. A new buffer
     * is handed over once per scan, when the line counter wraps around. */
    uint32_t total = frames + 4, shown = 0, prev = 0, wraps_bad = 0;
    ts = now_us();
    uint32_t last_wrap = ts;
    while (shown < total) {
        uint32_t line = (REG32(TCON_BASE + 0x0FC) >> 16) & 0xFFF;
        if (line < prev) {                                    /* a new scan just started */
            shown++;
            uint32_t next = shown;                            /* buffer for the scan after this one */
            uint32_t addr = (next >= 2 && next < 2 + frames) ? WAV_BUF0 + (next - 2) * WAV_BUF_STEP : WAV_TEMPLATE;
            EE(0x408) = addr;
            EE(0x400) = 0x80010001;
            uint32_t n = now_us();
            if (n - last_wrap > 16000u || n - last_wrap < 8000u)
                wraps_bad++;
            last_wrap = n;
        }
        prev = line;
        if (now_us() - last_wrap > 100000u)                   /* scanning stopped? bail out */
            break;
    }
    REG32(TCON_BASE + 0x040) = 0;                             /* stop clocking the panel */
    uint32_t play_us = now_us() - ts;
    int pg_end = pin_read(7, 7);
    sleep_us(20000);
    panel_power_off();
    EE(0x404) &= ~1u;                                         /* frame feeder off */
    tcon_off();
    if (verbose || shown != total || !pg_end || wraps_bad) {
        lg(T_PLAY, shown); lg(T_PLAY | 1, play_us); lg(T_PLAY | 2, wraps_bad); lg(T_PLAY | 4, (uint32_t)pg_end);
    }
    if (shown != total || !pg_end)
        finish(END_PLAY_ABORT);
    wdt_arm(0xB);

    uint32_t t = img_last; img_last = img_cur; img_cur = t;   /* the new picture is now "what is on screen" */
}


__attribute__((noreturn)) int main(void)
{
    t0 = ticks();
    wdt_arm(0xB);
    for (int i = 0; i < BLACKBOX_WORDS; i++)
        bb[i] = 0;
    bb[1] = 3;
    bb[2] = 24000000u;
    bb[0] = BLACKBOX_MAGIC;

    /* --- 1. boot parameters --- */
    stage(1);
    volatile uint32_t *pr = (volatile uint32_t *)PARAMS_PHYS;
    lg(T_PARAM, pr[0]);
    lg(T_PARAM | 1, pr[1]);
    if (pr[0] != PARAMS_MAGIC || pr[1] < 13)
        finish(END_BAD_PARAMS);
    tm.lsl = pr[2]; tm.lbl = pr[3]; tm.ldl = pr[4]; tm.lel = pr[5]; tm.gdck_sta = pr[6];
    tm.lgonl = pr[7]; tm.fsl = pr[8]; tm.fbl = pr[9]; tm.fdl = pr[10]; tm.fel = pr[11];
    p_drive = pr[12]; p_mode = pr[13]; p_temp = pr[14];
    p_pm1 = pr[1] >= 15 ? pr[15] : 0;                         /* VCOM_ADJ1 | VCOM_ADJ2<<8 | VLDO_ADJ<<16 */
    p_pm2 = pr[1] >= 15 ? pr[16] : 0;                         /* VEE_VP_EXT | PWRON_DLY<<8 */
    uint32_t p_rot = pr[1] >= 16 ? pr[17] & 3u : 0;           /* how the page sits on the panel */
    /* The book (plain text) sits somewhere in what was Linux's memory: move it
     * to our own corner before we start using RAM for pictures. */
    uint32_t book_src = pr[32], book_len = pr[33];
    lg(T_READ | 0x20, book_src); lg(T_READ | 0x21, book_len);
    if (book_src < 0x40000000u || book_src >= 0x5F000000u || book_len == 0 || book_len > BOOK_MAX)
        book_len = 0;
    if (book_len) {
        volatile uint8_t *d = (volatile uint8_t *)BOOK_BUF, *sr = (volatile uint8_t *)book_src;
        if (book_src + book_len <= BOOK_BUF)
            for (uint32_t i = 0; i < book_len; i++) { d[i] = sr[i]; if (!(i & 0xFFFF)) wdt_feed(); }
        else
            for (uint32_t i = book_len; i-- > 0;) { d[i] = sr[i]; if (!(i & 0xFFFF)) wdt_feed(); }
    }
    for (int i = 2; i < 18; i++)
        lg(T_PARAM | (uint32_t)i, pr[i]);
    pr[0] = 0;                                                /* one-shot */
    LTL = tm.lsl + tm.lbl + tm.ldl + tm.lel;
    FTL = tm.fsl + tm.fbl + tm.fdl + tm.fel;

    /* Make sure the engine's clock is running (Linux normally leaves it on). */
    lg(T_CHK | 8, REG32(CCU_BASE + 0x610)); lg(T_CHK | 9, REG32(CCU_BASE + 0x61C));
    if ((REG32(CCU_BASE + 0x61C) & 0x00010001u) != 0x00010001u) {
        REG32(CCU_BASE + 0x610) = 0x80000001;                 /* value seen under Linux (test 2a) */
        REG32(CCU_BASE + 0x61C) = 0x00010000;
        REG32(CCU_BASE + 0x61C) = 0x00010001;
        sleep_us(1000);
        lg(T_CHK | 10, REG32(CCU_BASE + 0x61C));
    }

    /* Cross-check the timing. Always: against the frame size Linux was seen
     * using in test 2a (324 x 1451). And, if Linux left its settings in the
     * engine (it clears them after a sleep), against those too. */
    uint32_t coor = EE(0x414), wsize = EE(0x410), pitch = EE(0x40C);
    lg(T_CHK, coor); lg(T_CHK | 1, wsize); lg(T_CHK | 2, pitch); lg(T_CHK | 3, EE(0x000));
    int ok = tm.ldl * 8 == PANEL_W && tm.fdl == PANEL_H && tm.fsl >= 1 && tm.fbl >= 1 &&
             LTL * 4 == WAV_PITCH && FTL == WAV_LINES;
    if (wsize != 0)
        ok = ok && coor == (((tm.fbl + tm.fsl) << 16) | ((tm.lbl + tm.lsl) << 3)) &&
             wsize == (((FTL - 1) << 16) | (LTL * 8 - 1)) && pitch == LTL * 4;
    lg(T_CHK | 4, (uint32_t)ok);
    if (!ok)
        finish(END_BAD_TIMING);

    /* --- 2. waveform file --- */
    stage(2);
    for (uint32_t o = 0; o < 16; o += 4)
        lg(T_WF | o, rd32(WAVEFORM_PHYS + o));
    for (uint32_t o = 0; o < 16; o += 4)
        lg(T_WF | 0x80 | o, rd32(WAVEFORM_PHYS + 128 + o));  /* temperature table */
    lg(T_PMIC | 0x20, REG32(PIO_BASE + 4 * 0x24 + 0x10));    /* port E: PE4/PE5 = chip awake? */
    lg(T_PMIC | 0x21, REG32(PIO_BASE + 7 * 0x24 + 0x10));    /* port H */
    lg(T_PMIC | 0x22, REG32(PIO_BASE + 1 * 0x24 + 0x10));    /* port B */
    int pmic_bus = i2c_find_pmic();
    int reg[9];
    for (int r = 0; r < 9; r++) {
        reg[r] = pmic_read((uint8_t)r);
        lg(T_PMIC | (uint32_t)r, (uint32_t)reg[r]);
    }
    /* Linux is stopped mid-flight, so the panel rails can still be on (seen in
     * test 2b-6). Nothing is scanning the panel now: switch them off cleanly. */
    if (pmic_bus >= 0 && reg[0] >= 0 && (reg[0] & 0x80)) {
        panel_power_off();
        lg(T_PWR | 0x20, (uint32_t)pmic_read(0x00));
    }
    uint32_t temp = p_temp ? p_temp : (reg[8] > 0 && reg[8] < 60 ? (uint32_t)reg[8] : 25u);
    lg(T_WF | 0x40, temp);
    uint32_t frames_init, frames_gc16, fb;
    awf_lookup(0, temp, &frames_init, &fb);
    uint32_t wf_gc16 = awf_lookup(1, temp, &frames_gc16, &fb);
    uint32_t frames_du, fb_du;
    uint32_t wf_du = awf_lookup(3, temp, &frames_du, &fb_du);
    uint32_t wf = p_mode == 3 ? wf_du : wf_gc16;
    uint32_t frames = p_mode == 3 ? frames_du : frames_gc16;
    if (p_mode == 3)
        fb = fb_du;
    lg(T_WF | 0x41, wf); lg(T_WF | 0x42, frames); lg(T_WF | 0x43, fb);
    if (frames == 0 || frames > MAX_FRAMES || (fb != 1024 && fb != 0))
        finish(END_BAD_WAVEFORM);

    /* --- 3. memory: switch the IOMMU to pass-through, build pictures --- */
    stage(3);
    lg(T_MMU, REG32(IOMMU_BASE + 0x20)); lg(T_MMU | 1, REG32(IOMMU_BASE + 0x30));
    REG32(IOMMU_BASE + 0x30) = 0x7F;                          /* every master: real addresses */
    lg(T_MMU | 2, REG32(IOMMU_BASE + 0x30));
    fill_words((volatile uint32_t *)IMG_LAST, 0xFFFFFFFFu, PANEL_W * PANEL_H / 4);   /* "screen is white" */
    fill_words((volatile uint32_t *)IDX_OLD, 0, PANEL_W * PANEL_H * 2 / 4);
    fill_words((volatile uint32_t *)IDX_NEW, 0, PANEL_W * PANEL_H * 2 / 4);

    /* --- 4. control-signal template + one copy per frame (done once) --- */
    stage(4);
    build_template((volatile uint32_t *)WAV_TEMPLATE);
    stage(5);
    for (uint32_t f = 0; f < frames; f++) {
        copy_words((volatile uint32_t *)(WAV_BUF0 + f * WAV_BUF_STEP), (volatile uint32_t *)WAV_TEMPLATE, LTL * FTL);
        wdt_feed();
    }

    /* --- 5. power chip: same settings as Linux, verified, before any drawing --- */
    stage(6);
    g_wf = wf; g_frames = frames; p_drive_ = p_drive;
    if (p_drive) {
        if (pmic_bus < 0)
            finish(END_NO_PMIC);
        if (!p_pm1 || !p_pm2)
            finish(END_BAD_PARAMS);                           /* never power the panel with guessed voltages */
        static const uint8_t r[6] = { 1, 2, 3, 5, 6, 4 };
        uint8_t v[6] = { (uint8_t)p_pm1, (uint8_t)(p_pm1 >> 8), (uint8_t)(p_pm1 >> 16),
                         (uint8_t)p_pm2, (uint8_t)(p_pm2 >> 8), 0 };
        uint32_t bad = 0;
        for (int i = 0; i < 6; i++) {
            if (!pmic_write(r[i], v[i]) || pmic_read(r[i]) != v[i])
                bad |= 1u << i;
            lg(T_PWR | 0x30 | (uint32_t)r[i], (uint32_t)pmic_read(r[i]));
        }
        lg(T_PWR | 0x40, bad);
        if (bad)
            finish(END_PMIC_VERIFY);
    }

    /* --- 6. test 3c: a real book, pages turned with the page buttons --- */
    stage(7);
    static const char no_book[] = "No book was handed to Nihil.\nPut a plain text file at .adds/nihil/book.txt and run the test again.\n";
    struct reader rd;
    rd.pw = PANEL_W; rd.ph = PANEL_H; rd.rot = p_rot;
    rd.text = book_len ? (const uint8_t *)BOOK_BUF : (const uint8_t *)no_book;
    rd.len = book_len ? book_len : sizeof no_book - 1;
    rd_defaults(&rd);
    volatile uint32_t *page_start = (volatile uint32_t *)PAGE_TAB;   /* text offset where each page begins */
    uint32_t page = 0, turns = 0, next_pos;
    page_start[0] = 0;

    uint32_t tr = now_us();
    rd.img = (volatile uint8_t *)img_cur;
    fill_words((volatile uint32_t *)img_cur, 0xFFFFFFFFu, PANEL_W * PANEL_H / 4);
    next_pos = rd_page(&rd, page_start[page], 1);
    rd_footer(&rd, page + 1, next_pos);
    lg(T_READ, now_us() - tr);                                /* time to paint one page */
    show(1);
    stage(8);

    uint64_t idle_from = ticks();
    for (;;) {
        wdt_feed();
        uint32_t l = REG32(R_PIO_BASE + 0x10);
        int down = !((l >> BTN_NEXT_PIN) & 1), up = !((l >> BTN_BACK_PIN) & 1);
        if ((ticks() - idle_from) / 24000000u >= 600u) {      /* 10 minutes without a press: leave */
            lg(T_READ | 2, turns);
            break;
        }
        if (!down && !up)
            continue;
        sleep_us(40000);                                      /* still pressed after 40 ms = a real press */
        l = REG32(R_PIO_BASE + 0x10);
        int down2 = !((l >> BTN_NEXT_PIN) & 1), up2 = !((l >> BTN_BACK_PIN) & 1);
        if (down2 && up2) {                                   /* both buttons: leave */
            lg(T_READ | 3, turns);
            break;
        }
        if (down != down2 || up != up2)
            continue;
        idle_from = ticks();
        if (down) {
            if (next_pos >= rd.len || page + 1 >= PAGE_TAB_MAX)
                continue;                                     /* last page: nothing to turn to */
            page++;
            page_start[page] = next_pos;
        } else {
            if (page == 0)
                continue;
            page--;
        }
        turns++;
        tr = now_us();
        rd.img = (volatile uint8_t *)img_cur;
        fill_words((volatile uint32_t *)img_cur, 0xFFFFFFFFu, PANEL_W * PANEL_H / 4);
        next_pos = rd_page(&rd, page_start[page], 1);
        rd_footer(&rd, page + 1, next_pos);
        uint32_t paint_ms = (now_us() - tr) / 1000u;
        show(0);
        lg(T_READ | 0x10 | (uint32_t)(down ? 0 : 1), (page << 20) | (paint_ms << 10) | (((now_us() - tr) / 1000u) & 0x3FF));
        /* wait for the button to be let go (or both pressed -> handled next round) */
        uint64_t w = ticks();
        while ((ticks() - w) / 24000u < 3000u) {
            l = REG32(R_PIO_BASE + 0x10);
            int d3 = !((l >> BTN_NEXT_PIN) & 1), u3 = !((l >> BTN_BACK_PIN) & 1);
            if ((!d3 && !u3) || (d3 && u3))
                break;
            wdt_feed();
        }
        idle_from = ticks();
    }
    stage(9);
    finish(END_DRAWN_OK);
}
