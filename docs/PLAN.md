# Plan

Each milestone ends with something you can see on the device. Nothing writes to
the Kobo's internal storage before milestone 6.

## How we start our code (no storage writes)

The Kobo's kernel can't run another kernel directly (`CONFIG_KEXEC` is off in
Kobo's `sun8iw15p1smp_kobo_defconfig`), but it can load kernel modules. So:

1. Copy `sage.bin` and `nihil_chainload.ko` to the Kobo (USB drive or SSH).
2. `insmod nihil_chainload.ko` (later: one tap in NickelMenu).
3. The module stops 3 of the 4 CPU cores and calls the kernel's own
   `soft_restart()` (what kexec uses internally) to jump into our image.
4. Linux is gone; our code owns the whole device. Hold Power or let our code
   reboot, and the normal Kobo comes back.

Known gap: Kobo's kernel checks each module's symbol "fingerprints"
(`CONFIG_MODVERSIONS=y`). We fix that by matching them to a module already on the
device (e.g. `/drivers/b300-ntx/g_ether.ko`) or to a full build of Kobo's kernel.

## Where things stand (20 Nov 2026)

"On the Kobo" = ran on Diogo's Sage. "PC only" = written and tested on a
computer (simulator or stand-in hardware), not yet run on the Kobo.

| Piece | State |
|---|---|
| Start without Linux, timer, buttons, black-box log, reboot | On the Kobo (milestone 1) |
| Draw on the e-ink screen (16 greys, full refresh) | On the Kobo (milestone 2) |
| Page turning, portrait, real text, Libron font | On the Kobo (tests 3a-3d) |
| EPUB reader, library browser, menu, cover = soft sleep | PC only (simulator), builds for the Sage |
| FAT32 reader (`core/fat.h`, `core/library.h`) | PC only, tested on disk images (MBR, GPT, long and accented names) |
| Storage chip driver, read-only (`platform/sage/mmc.h`) | PC only, 22 checks against a stand-in chip (`make mmc-test`) |
| Frontlight (`platform/sage/light.h`) | PC only, 22 checks with Kobo's real table file (`make light-test`) |
| Core naps while waiting (WFE + timer event stream) | Written, cannot be tested on a PC |
| Cable link for development (USB network + SSH) | Scripts written, not installed |
| Real low-power sleep (RAM self-refresh), saving the reading position, direct boot | Not started |

Each new piece has its own switch in `tools/kobo/run-test1.sh` (STORE, LIGHT,
NAP), so it can be tried alone and switched off again.

## Milestone 1: Hello, Sage

Proves: our code runs with no Linux.
- Frontlight blinks 3 times on start, Page buttons toggle it, Power reboots.
- To confirm: buttons read 0 when pressed; frontlight keeps Linux's brightness.
- Risk: very low. Worst case the screen freezes and we hold Power to restart.

## Milestone 2: First pixels

Proves: we can drive the e-ink screen.
- Power the screen through the SY7636 chip (I2C), set up the B300's EINK engine
  and panel timings (all values in the D00 `.fex` file), load the waveform
  (a copy is on the device; Kobo's code reads it from storage).
- Reference code, all GPL C: Kobo's Linux driver and Kobo's U-Boot, which already
  draws the boot logo on this screen without Linux.
- First goal: full white, full black, then a test pattern.
- This is the biggest unknown of the project.

## Milestone 3: Read the books (read-only)

- eMMC driver (reference: U-Boot `drivers/mmc/sun8iw15p1/`), read-only mode.
- FAT32 reader for the user partition (where `Books/` lives).
- Show a list of book titles on screen.

## Milestone 4: Reader

- Port CrossPoint Reader's engine (MIT license; EPUB, fonts, layout, button UI).
  CrossPoint already hides hardware behind a small `lib/hal` layer
  (`HalDisplay`, `HalGPIO`, `HalStorage`, `HalPowerManager`, ...). We write a
  Sage version of that layer.
- Scale its layouts from 480x800 to 1440x1920.
- Saving progress/bookmarks needs writing to the *user* partition (not the boot
  area): separate yes from Diogo when we get there.

## Milestone 5: Battery

- Between page turns: CPU sleeps, screen power off.
- Long sleep: RAM self-refresh, wake on button or cover magnet.
- Battery % and charging from the BD71828; frontlight levels via the AW99703 chips.

## Milestone 6: Boot choice

Today every start goes Kobo software first (about 20+ s) then ours. To start
ours directly, something on the internal storage must change (U-Boot settings or
a boot image). Only after: full eMMC backup, a tested way back, Diogo's explicit OK.
Alternative that never writes: keep starting from NickelMenu forever.

## Milestone 7: Other devices

Keep `core/` free of Sage details. Good next targets: other Allwinner Kobos
and PocketBooks (same family of chips), and the ESP32 readers CrossPoint already
supports.
