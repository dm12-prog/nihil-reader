/*
 * Milestone 1, "hello, Sage" (test 1b): the first program that runs with no Linux.
 *
 * Signals you can see:
 *   1. on start, the frontlight switches OFF          -> "Nihil is running"
 *   2. press PageUp or PageDown (after ~2 s), the Sage reboots right away
 *                                                     -> "Nihil can read buttons"
 *   3. no button pressed: the Sage reboots by itself after 20 s
 *                                                     -> "Nihil runs, and its clock and reboot work"
 * The screen keeps its last image (e-ink), so it looks frozen. That is expected.
 */
#include "sage.h"

/* ---- time: the ARM "generic timer", a counter that ticks at a fixed rate ---- */
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
    return f ? f : 24000000u;      /* Allwinner chips run it at 24 MHz */
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

/* ---- port L pins ---- */
static uint32_t port_l_reg(uint32_t offset)
{
    return R_PIO_BASE + PORT_L * 0x24u + offset;
}

static void port_l_mode(int pin, uint32_t mode)
{
    uint32_t reg = port_l_reg((uint32_t)(pin / 8) * 4u);
    uint32_t shift = (uint32_t)(pin % 8) * 4u;
    REG32(reg) = (REG32(reg) & ~(0xFu << shift)) | (mode << shift);
}

static int port_l_read(int pin)
{
    return (REG32(port_l_reg(0x10)) >> pin) & 1;
}

static void port_l_write(int pin, int value)
{
    if (value)
        REG32(port_l_reg(0x10)) |= (1u << pin);
    else
        REG32(port_l_reg(0x10)) &= ~(1u << pin);
}

/* Same steps as Kobo's own kernel (drivers/watchdog/sunxi_wdt.c, sunxi_wdt_restart). */
static void reboot(void)
{
    REG32(WDT_CFG) = (REG32(WDT_CFG) & ~0x3u) | 0x1u;        /* reset whole system */
    REG32(WDT_MODE) = (REG32(WDT_MODE) & ~(0xFu << 4)) | 1u;  /* 0.5 s, enable */
    REG32(WDT_CTRL) = (1u << 0) | (0x0A57u << 1);             /* start counting */
    for (;;) {
        sleep_ms(5);
        REG32(WDT_MODE) |= 1u;
    }
}

int main(void)
{
    enum { IN = 0, OUT = 1 };
    uint64_t start = ticks();

    /* Signal 1: frontlight off. */
    port_l_mode(FRONTLIGHT_EN_PIN, OUT);
    port_l_write(FRONTLIGHT_EN_PIN, 0);

    /* Buttons are already inputs with pull-ups (set by Linux); make sure. */
    port_l_mode(BTN_PAGEUP_PIN, IN);
    port_l_mode(BTN_PAGEDOWN_PIN, IN);

    /* Ignore buttons for 2 s, in case one is still held. */
    sleep_ms(2000);

    int hits = 0;
    for (;;) {
        /* Signal 2: a page button held for 3 checks in a row (~30 ms). */
        int pressed = !port_l_read(BTN_PAGEUP_PIN) || !port_l_read(BTN_PAGEDOWN_PIN);
        hits = pressed ? hits + 1 : 0;
        if (hits >= 3)
            reboot();

        /* Signal 3: give up after 20 s. */
        if (ticks() - start > ms_to_ticks(20000))
            reboot();

        sleep_ms(10);
    }
}
