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

/* ---------- screen (milestone 2) ---------- */
#define PANEL_W 1920u
#define PANEL_H 1440u
#define SY7636_ADDR 0x62          /* screen power chip on twi1 (sys_config fex) */

/* AWF waveform file preloaded by U-Boot (live /proc/cmdline waveform_p=, 2026-11-19) */
#define WAVEFORM_PHYS 0x5FABAA00u

/* Boot parameters: the loader module writes them just above the black box. */
#define PARAMS_PHYS  (BLACKBOX_PHYS + 0x8000u)
#define PARAMS_MAGIC 0x4E50524Du  /* "NPRM" */

/* Our RAM use while drawing (all inside 0x50000000-0x5F9FFFFF, free once Linux is gone) */
#define IMG_LAST     0x50000000u  /* old picture, 1 byte per pixel */
#define IMG_CUR      0x50400000u  /* new picture */
#define IDX_OLD      0x50800000u  /* 2 bytes per pixel */
#define IDX_NEW      0x51000000u
#define WAV_TEMPLATE 0x51800000u  /* control signals only = an "empty" frame */
#define WAV_BUF0     0x52000000u  /* one buffer per waveform frame */
#define WAV_BUF_STEP 0x00200000u
#define MAX_FRAMES   100u
#define WAV_PITCH    1296u        /* bytes per line seen in Linux's EDMA setting */
#define WAV_LINES_MAX 1500u

/* Black-box tags (format 3 "address" column) */
#define T_STAGE 0xE0000000u       /* value = ms since start */
#define T_PARAM 0xE1000000u
#define T_CHK   0xE2000000u
#define T_WF    0xE3000000u
#define T_PMIC  0xE4000000u
#define T_MMU   0xE5000000u
#define T_IDX   0xE6000000u
#define T_DEC   0xE7000000u
#define T_TCON  0xE8000000u
#define T_PWR   0xE9000000u
#define T_PLAY  0xEA000000u

/* End codes (bb[4] high half) */
#define END_REHEARSAL_OK 0x600D0000u
#define END_DRAWN_OK     0xD0AE0000u
#define END_BAD_PARAMS   0xBAD10000u
#define END_BAD_TIMING   0xBAD20000u
#define END_BAD_WAVEFORM 0xBAD30000u
#define END_IDX_TIMEOUT  0xBAD40000u
#define END_DEC_TIMEOUT  0xBAD50000u
#define END_NO_POWER_GOOD 0xBAD60000u
#define END_PLAY_ABORT   0xBAD70000u
