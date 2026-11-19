# Kobo Sage hardware, from the sources

Main source: Kobo's own published code for the Sage,
[github.com/kobolabs/b300-sage](https://github.com/kobolabs/b300-sage)
(one big archive split in 44 parts; inside: `linux-4.9/`, `brandy/` = Allwinner's
bootloaders incl. U-Boot 2014.07, `tools/pack/` = board config files).
Board name inside that code: **E80P04**. Diogo's Sage is revision **D00**
(found 2026-11-17 from the device itself).

| Part | What it is | Where it says so |
|---|---|---|
| Main chip | Allwinner **B300** = `sun8iw15p1`, 4x ARM Cortex-A7, up to 1.8 GHz | `linux-4.9/arch/arm/boot/dts/sun8iw15p1-*.dts`, CPU table in `sun8iw15p1-E80P04-C2.dts` |
| RAM | LPDDR4 (`dram_type = 8`), 792 MHz | `tools/pack/.../ntx_boards/sys_config_E80P04-D00-nanyalpddr4.fex`, `[dram_para]` |
| Screen | E Ink ED080KH1, 1920 x 1440, 85 Hz scan, 5-bit pixels | same file, `[lcd0]` `eink_*` lines; waveform `400_B014_HH0501_ED080KH1C1_VD1400-HOD_TC.awf` |
| Screen controller | Built into the B300 ("EINK" engine + "EDMA" at 0x01400000) | `sun8iw15p1-eink_128m.dtsi`; driver source (GPL C) in `linux-4.9/drivers/video/fbdev/sunxi/disp2/disp/de/` and in U-Boot `brandy/u-boot-2014.07/drivers/video/sunxi/disp2/` |
| Screen power chip | Silergy **SY7636**, I2C bus 1 address 0x62, enable pins PH6/PE4/PE3/PE5, power-good PH7 | `.fex` `[sy7636]`; driver `linux-4.9/drivers/hwmon/sy7636.c`, `brandy/u-boot-2014.07/include/power/sy7636.h` |
| Power manager (battery, regulators) | ROHM **BD71828**, I2C bus 2 address 0x4B | `.fex` `[bd71828-i2c]`; also confirmed live on Diogo's Sage 2026-11-17 |
| Frontlight | 2x Awinic AW99703, I2C bus 0 and 1, address 0x36, enable pin PL0 | `sun8iw15p1-E80P04-C2.dts`, `&twi0` / `&twi1` |
| Buttons | On Diogo's D00 (checked live 2026-11-19, `/sys/kernel/debug/gpio`): Page Up = **PL11**, Page Down = **PL5**, cover magnet (Hall) = **PL9**; Power goes through the BD71828 chip, not a GPIO. (The published C2 tree says PH9/PH8/PL4: different board.) | live device; `sun8iw15p1-E80P04-C2.dts` |
| Storage | eMMC on controller `sdc2` (0x04022000), 8-bit bus | `sun8iw15p1-eink_128m.dtsi` |
| GPIO controllers | `pio` at 0x0300B000, `r_pio` (port L) at 0x07022000 | `sun8iw15p1-pinctrl.dtsi` |
| Watchdog (used to reboot) | 0x030090A0, Allwinner A31 style | `sun8iw15p1-eink_128m.dtsi` |
| RAM size | 512 MB: 0x40000000-0x5FFFFFFF. U-Boot preloads the **waveform** at 0x5FABAA00 (5,526,360 bytes) and hwconfig at 0x5FFFFE00 before Linux starts | live `/proc/cmdline` and `/proc/iomem`, 2026-11-19 |

## The one closed-source piece, and why it doesn't block us

`libeink-E80P04.a_shipped` (9.5 KB, by E Ink Holdings) is the only binary-only part
of the screen driver. Its header (`libeink.h`) shows it is an optional
image "clean-up" step (`eink_process()`), not the driver. Its symbol table
(checked with `llvm-nm`, 2026-11-19) needs nothing from Linux, so it could even be
linked into our code as-is if we want it.

## Things not in the published code

- **DRAM start-up** for this chip is not in the source (only other chips have it).
  We don't need it: Kobo's own first-stage loader (boot0) sets up the RAM before
  anything of ours runs.
- Diogo's board is revision D00; the published device tree is for C2 (Ricoh power
  chip). The D00 config file *is* published and says BD71828, matching the device.

## Boot chain (how the Sage starts)

1. Chip's built-in ROM -> 2. boot0 (Allwinner, sets up RAM) -> 3. U-Boot 2014.07
(Netronix/Kobo version, source in `brandy/`) -> 4. Linux 4.9.56 -> 5. Kobo's app (Nickel).

Kobo's U-Boot has a **fastboot** mode: with Kobo's board settings, holding Power
while USB is connected at start-up enters it
(`brandy/u-boot-2014.07/board/sunxi/common/ntx_comm.c`, `_detect_bootmode()`).
`fastboot boot` (run an image from RAM, no storage write) only works if fastboot is
"unlocked" (`usb_sunxi/usb_fastboot.c`), which is unknown on retail units.
