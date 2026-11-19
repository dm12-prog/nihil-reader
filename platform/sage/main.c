/*
 * Milestone 1, "hello, Sage": the first program that runs with no Linux at all.
 *
 * We can't draw on the screen yet, so the frontlight is our signal lamp:
 *   - on start:                 frontlight blinks 3 times  -> "our code is running"
 *   - press PageUp or PageDown: frontlight toggles         -> "we can read buttons"
 *   - press Power:              device reboots into normal Kobo software
 *
 * Assumptions to confirm on the device (see docs/PLAN.md, milestone 1):
 *   - buttons read 0 when pressed (pull-up, button shorts to ground)
 *   - the frontlight chip keeps the brightness Linux gave it, so toggling its
 *     enable line (PL0) switches the light on/off
 */
#include "sage.h"

static uint32_t port_base(int r_pio, int port)
{
    return (r_pio ? R_PIO_BASE : PIO_BASE) + (uint32_t)port * 0x24u;
}

static void gpio_mode(int r_pio, int port, int pin, uint32_t mode)
{
    uint32_t reg = port_base(r_pio, port) + (uint32_t)(pin / 8) * 4u;
    uint32_t shift = (uint32_t)(pin % 8) * 4u;
    REG32(reg) = (REG32(reg) & ~(0xFu << shift)) | (mode << shift);
}

static int gpio_read(int r_pio, int port, int pin)
{
    return (REG32(port_base(r_pio, port) + 0x10) >> pin) & 1;
}

static void gpio_write(int r_pio, int port, int pin, int value)
{
    uint32_t reg = port_base(r_pio, port) + 0x10;
    if (value)
        REG32(reg) |= (1u << pin);
    else
        REG32(reg) &= ~(1u << pin);
}

static void delay(volatile uint32_t n)
{
    while (n--)
        __asm__ volatile("nop");
}

static void reboot(void)
{
    REG32(WDT_CFG) = 1;            /* watchdog resets the whole system */
    REG32(WDT_MODE) = 1;           /* shortest timeout, enable */
    for (;;)
        __asm__ volatile("wfi");
}

static int pressed(int r_pio, int port, int pin)
{
    return gpio_read(r_pio, port, pin) == 0;
}

int main(void)
{
    enum { IN = 0, OUT = 1 };
    int light = 1;

    gpio_mode(1, PORT_L, FRONTLIGHT_EN_PIN, OUT);
    gpio_mode(0, BTN_PAGEUP_PORT, BTN_PAGEUP_PIN, IN);
    gpio_mode(0, BTN_PAGEDOWN_PORT, BTN_PAGEDOWN_PIN, IN);
    gpio_mode(1, PORT_L, BTN_POWER_PIN, IN);

    for (int i = 0; i < 3; i++) {
        gpio_write(1, PORT_L, FRONTLIGHT_EN_PIN, 0);
        delay(20000000);
        gpio_write(1, PORT_L, FRONTLIGHT_EN_PIN, 1);
        delay(20000000);
    }

    for (;;) {
        if (pressed(0, BTN_PAGEUP_PORT, BTN_PAGEUP_PIN) ||
            pressed(0, BTN_PAGEDOWN_PORT, BTN_PAGEDOWN_PIN)) {
            light = !light;
            gpio_write(1, PORT_L, FRONTLIGHT_EN_PIN, light);
            delay(10000000);       /* crude debounce */
        }
        if (pressed(1, PORT_L, BTN_POWER_PIN))
            reboot();
    }
}
