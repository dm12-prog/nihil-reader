/*
 * Milestone 1, test 1e: black box (all pin ports, 30 s) + a page button reboots at once.
 *
 * Test 1b proved Nihil runs (clock + watchdog reboot work) but the page buttons
 * were not seen. We can't show anything on screen yet, so this version writes
 * what it sees into a RAM area that Linux never uses (BLACKBOX_PHYS) and that
 * should survive a watchdog reboot. After the Kobo is back, the loader module
 * reads it out (readlog=1).
 *
 * What it does:
 *   - frontlight off on start
 *   - snapshot of the pin registers (ports L and H) before and after we touch them
 *   - for 20 s: every change on port L or port H is logged with a timestamp
 *   - then reboot
 */
#include "sage.h"

static uint64_t ticks(void)
{
    uint32_t lo, hi;
    __asm__ volatile("isb; mrrc p15, 0, %0, %1, c14" : "=r"(lo), "=r"(hi));
    return ((uint64_t)hi << 32) | lo;
}

static uint32_t ticks_per_second(void)
{
    uint32_t f;
    __asm__ volatile("mrc p15, 0, %0, c14, c0, 0" : "=r"(f));
    return f ? f : 24000000u;
}

static uint64_t ms_to_ticks(uint32_t ms)
{
    return (uint64_t)ticks_per_second() / 1000u * ms;
}

static void sleep_ms(uint32_t ms)
{
    uint64_t end = ticks() + ms_to_ticks(ms);
    while (ticks() < end)
        ;
}

static uint32_t port_l_reg(uint32_t offset) { return R_PIO_BASE + PORT_L * 0x24u + offset; }
static uint32_t port_h_reg(uint32_t offset) { return PIO_BASE + PORT_H * 0x24u + offset; }

static void port_l_mode(int pin, uint32_t mode)
{
    uint32_t reg = port_l_reg((uint32_t)(pin / 8) * 4u);
    uint32_t shift = (uint32_t)(pin % 8) * 4u;
    REG32(reg) = (REG32(reg) & ~(0xFu << shift)) | (mode << shift);
}

static void port_l_write(int pin, int value)
{
    if (value)
        REG32(port_l_reg(0x10)) |= (1u << pin);
    else
        REG32(port_l_reg(0x10)) &= ~(1u << pin);
}

__attribute__((noreturn)) static void reboot(void)
{
    REG32(WDT_CFG) = (REG32(WDT_CFG) & ~0x3u) | 0x1u;
    REG32(WDT_MODE) = (REG32(WDT_MODE) & ~(0xFu << 4)) | 1u;
    REG32(WDT_CTRL) = (1u << 0) | (0x0A57u << 1);
    for (;;) {
        sleep_ms(5);
        REG32(WDT_MODE) |= 1u;
    }
}

static volatile uint32_t *const bb = (volatile uint32_t *)BLACKBOX_PHYS;

/* Data registers of ports A..I (main pio, index 0..8) and L (r_pio). Reading is harmless. */
#define BB_PORTS    10
#define BB_EV_WORDS (1 + BB_PORTS)
static void read_ports(uint32_t *out)
{
    for (int i = 0; i < 9; i++)
        out[i] = REG32(PIO_BASE + (uint32_t)i * 0x24u + 0x10);
    out[9] = REG32(port_l_reg(0x10));
}

static void snapshot(int at)
{
    bb[at + 0] = REG32(port_l_reg(0x00));   /* PL0-7 functions  */
    bb[at + 1] = REG32(port_l_reg(0x04));   /* PL8-15 functions */
    bb[at + 2] = REG32(port_l_reg(0x10));   /* PL data          */
    bb[at + 3] = REG32(port_l_reg(0x1C));   /* PL0-15 pulls     */
    bb[at + 4] = REG32(port_h_reg(0x00));   /* PH0-7 functions  */
    bb[at + 5] = REG32(port_h_reg(0x04));   /* PH8-15 functions */
    bb[at + 6] = REG32(port_h_reg(0x10));   /* PH data          */
    bb[at + 7] = REG32(port_h_reg(0x1C));   /* PH0-15 pulls     */
}

__attribute__((noreturn)) int main(void)
{
    uint64_t start = ticks();
    uint32_t last[BB_PORTS], now[BB_PORTS];

    for (int i = 0; i < BLACKBOX_WORDS; i++)
        bb[i] = 0;
    bb[1] = 2;                               /* log format version 2: all ports */
    bb[2] = ticks_per_second();
    snapshot(8);

    port_l_mode(FRONTLIGHT_EN_PIN, 1);
    port_l_write(FRONTLIGHT_EN_PIN, 0);
    port_l_mode(BTN_PAGEUP_PIN, 0);
    port_l_mode(BTN_PAGEDOWN_PIN, 0);
    snapshot(16);
    bb[0] = BLACKBOX_MAGIC;

    read_ports(last);
    for (int i = 0; i < BB_PORTS; i++)
        bb[24 + i] = last[i];                /* words 24-33: starting data of A..I, L */
    uint32_t n = 0;
    int held = 0;

    while (ticks() - start < ms_to_ticks(30000)) {
        read_ports(now);
        int changed = 0;
        for (int i = 0; i < BB_PORTS; i++)
            changed |= (now[i] != last[i]);
        if (changed && n < BLACKBOX_MAX_EVENTS) {
            volatile uint32_t *e = &bb[BLACKBOX_EVENTS + n * BB_EV_WORDS];
            e[0] = (uint32_t)(ticks() - start) / (ticks_per_second() / 1000u);
            for (int i = 0; i < BB_PORTS; i++) {
                e[1 + i] = now[i];
                last[i] = now[i];
            }
            n++;
            bb[3] = n;
        }
        /* Test 1e: a page button held ~50 ms reboots right away (bb[5] says which). */
        uint32_t l = now[9];
        int up = !((l >> BTN_PAGEUP_PIN) & 1), down = !((l >> BTN_PAGEDOWN_PIN) & 1);
        held = (up || down) && (ticks() - start > ms_to_ticks(2000)) ? held + 1 : 0;
        if (held >= 25) {
            bb[5] = up ? 0x55500000u : 0xD0D00000u;   /* "UP" or "DOWN" */
            bb[4] = 0xB7700000u | (n & 0xFFFF);
            reboot();
        }
        sleep_ms(2);
    }
    bb[4] = 0x600D0000u | (n & 0xFFFF);
    reboot();
}
