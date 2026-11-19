/*
 * Kobo Sage hardware facts. Every number here has a source in docs/HARDWARE.md.
 */
#pragma once
#include <stdint.h>

#define REG32(addr) (*(volatile uint32_t *)(uintptr_t)(addr))

/* GPIO controllers (sun8iw15p1-pinctrl.dtsi) */
#define PIO_BASE    0x0300B000u   /* ports A..H.. */
#define R_PIO_BASE  0x07022000u   /* port L (always-on domain) */

/* Each port has a 0x24-byte register block:
 *   +0x00..0x0C CFG0..3 (4 bits per pin), +0x10 DATA, +0x14 DRV, +0x1C PULL */
#define PORT_H 7
#define PORT_L 0                  /* first (only) port of R_PIO */

/* Buttons (sun8iw15p1-E80P04-C2.dts, gpio-keys node) */
#define BTN_PAGEDOWN_PORT  PORT_H
#define BTN_PAGEDOWN_PIN   8      /* PH8 */
#define BTN_PAGEUP_PORT    PORT_H
#define BTN_PAGEUP_PIN     9      /* PH9 */
#define BTN_POWER_PIN      4      /* PL4, on R_PIO */
#define HALL_PIN           9      /* PL9, cover magnet sensor, on R_PIO */

/* Frontlight driver enable line, AW99703 "hwen" (same .dts, &twi0 node) */
#define FRONTLIGHT_EN_PIN  0      /* PL0, on R_PIO */

/* Watchdog (sun6i-a31 style, sun8iw15p1-eink_128m.dtsi watchdog@030090a0) */
#define WDT_BASE    0x030090A0u
#define WDT_CTRL    (WDT_BASE + 0x10)
#define WDT_CFG     (WDT_BASE + 0x14)
#define WDT_MODE    (WDT_BASE + 0x18)
