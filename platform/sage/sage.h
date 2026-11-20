/*
 * Kobo Sage hardware facts. Every number here has a source in docs/HARDWARE.md.
 *
 * Pin numbers were checked LIVE on Diogo's Sage (board D00) on 2026-11-19 via
 * /sys/kernel/debug/gpio. They differ from Kobo's published C2 device tree:
 * on D00 both page buttons sit on port L.
 */
#pragma once
#include <stdint.h>

#define REG32(addr) (*(volatile uint32_t *)(uintptr_t)(addr))

/* GPIO controllers (sun8iw15p1-pinctrl.dtsi, confirmed in /proc/iomem) */
#define PIO_BASE    0x0300B000u   /* ports A..H.. (Linux gpio 0-255)   */
#define R_PIO_BASE  0x07022000u   /* port L      (Linux gpio 352-383) */

/* Each port has a 0x24-byte register block:
 *   +0x00..0x0C CFG0..3 (4 bits per pin), +0x10 DATA, +0x14 DRV, +0x1C PULL */
#define PORT_L 0                  /* first (only) port of R_PIO */

/* Buttons: idle = 1, pressed = 0 (live: "in hi" while not pressed) */
#define BTN_PAGEDOWN_PIN   5      /* PL5  = gpio-357 "PageDown" */
#define BTN_PAGEUP_PIN     11     /* PL11 = gpio-363 "PageUp"   */
#define HALL_PIN           9      /* PL9  = gpio-361 "Hall" (cover magnet) */
/* Power button is not a GPIO on D00: it goes through the BD71828 power chip. */

/* Frontlight driver enable line, AW99703 "hwen" = PL0 = gpio-352.
 * Pulling it low switches the frontlight off. Pulling it high again does NOT
 * bring the light back: the chip then needs to be set up again over I2C. */
#define FRONTLIGHT_EN_PIN  0

/* Watchdog (sun6i-a31 style, watchdog@030090a0, confirmed in /proc/iomem) */
#define WDT_BASE    0x030090A0u
#define WDT_CTRL    (WDT_BASE + 0x10)
#define WDT_CFG     (WDT_BASE + 0x14)
#define WDT_MODE    (WDT_BASE + 0x18)

/* GPIO port H (main pio) */
#define PORT_H 7

/* Black box: 0x5FA00000-0x5FAB7BFF sits between the end of Linux's RAM
 * (mem=506M -> 0x5F9FFFFF) and the first block U-Boot loads (ntxfw at
 * 0x5FAB7C00), per the live /proc/cmdline and /proc/iomem (2026-11-19). */
#define BLACKBOX_PHYS        0x5FA00000u
#define BLACKBOX_MAGIC       0x4E49484Cu   /* "NIHL" */
#define BLACKBOX_EVENTS      40            /* first event word */
#define BLACKBOX_MAX_EVENTS  200
#define BLACKBOX_WORDS       (BLACKBOX_EVENTS + BLACKBOX_MAX_EVENTS * 11)
#define BLACKBOX_MAX_PAIRS   1000          /* format 3: (address, value) pairs */

/* Screen hardware (sun8iw15p1.dtsi "disp@01000000", clk-sun8iw15.h) */
#define CCU_BASE    0x03001000u   /* clock controller */
#define EINK_BASE   0x01400000u   /* EINK engine + EDMA (de_eink.c) */
#define TCON_BASE   0x06511000u   /* timing controller tcon_lcd0 */
#define DISP_TOP    0x06510000u
#define IOMMU_BASE  0x030F0000u
