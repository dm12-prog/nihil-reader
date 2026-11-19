/*
 * Milestone 1, "hello, Sage": the first program that runs with no Linux at all.
 *
 * We can't draw on the screen yet, so we use two signals you can see:
 *   1. on start, the frontlight switches OFF   -> "our code is running"
 *      (turn the frontlight on before starting the test)
 *   2. press PageUp or PageDown, the Sage reboots into the normal Kobo
 *      software                                -> "we can read buttons"
 *
 * The screen keeps showing whatever was there before (e-ink holds its image
 * without power), so it will look frozen. That is expected.
 * If nothing happens on a button press: hold Power ~15 s to force a restart.
 */
#include "sage.h"

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

static void delay(volatile uint32_t n)
{
    while (n--)
        __asm__ volatile("nop");
}

/* Same steps as Kobo's own kernel (drivers/watchdog/sunxi_wdt.c, sunxi_wdt_restart). */
static void reboot(void)
{
    REG32(WDT_CFG) = (REG32(WDT_CFG) & ~0x3u) | 0x1u;        /* reset whole system */
    REG32(WDT_MODE) = (REG32(WDT_MODE) & ~(0xFu << 4)) | 1u;  /* 0.5 s, enable */
    REG32(WDT_CTRL) = (1u << 0) | (0x0A57u << 1);             /* start counting */
    for (;;) {
        delay(100000);
        REG32(WDT_MODE) |= 1u;
    }
}

int main(void)
{
    enum { IN = 0, OUT = 1 };

    /* Signal 1: frontlight off. */
    port_l_mode(FRONTLIGHT_EN_PIN, OUT);
    port_l_write(FRONTLIGHT_EN_PIN, 0);

    /* Buttons are already inputs with pull-ups (set by Linux); make sure. */
    port_l_mode(BTN_PAGEUP_PIN, IN);
    port_l_mode(BTN_PAGEDOWN_PIN, IN);

    /* Ignore buttons for ~1-2 s, in case one is still held from the menu tap. */
    delay(100000000);

    /* Signal 2: any page button -> reboot. Must read "pressed" 3 times in a row. */
    int hits = 0;
    for (;;) {
        int pressed = !port_l_read(BTN_PAGEUP_PIN) || !port_l_read(BTN_PAGEDOWN_PIN);
        hits = pressed ? hits + 1 : 0;
        if (hits >= 3)
            reboot();
        delay(1000000);
    }
}
