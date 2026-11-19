# Vibogenes OS (working name)

A reading system for the **Kobo Sage** that runs with **no Linux underneath**:
no Wi-Fi, no touchscreen, just the screen, the page buttons, the frontlight and
your books. Think [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader)
(open-source firmware for small ESP32 e-readers), but on a big 8" screen.
Later: other e-readers that have at least two page-turn buttons.

## Status

| Milestone | What it proves | State |
|---|---|---|
| 0. Toolchain + first image | We can build code for the Sage's chip | done |
| 1. Hello, Sage | Our code runs with Linux gone (frontlight blinks, buttons work) | code written, not run yet |
| 2. First pixels | We can drive the e-ink screen ourselves | not started |
| 3. Read the books | We can read files from the Sage's storage (read-only) | not started |
| 4. Reader | CrossPoint's EPUB engine running on the Sage | not started |
| 5. Battery | Deep sleep, wake on button, battery % | not started |
| 6. Boot choice | Start without going through Kobo's software first | needs backup + Diogo's OK |

Details: [docs/PLAN.md](docs/PLAN.md). Hardware facts with sources: [docs/HARDWARE.md](docs/HARDWARE.md).

## Safety rules

1. Nothing in this repo writes to the Kobo's internal storage until milestone 6.
2. Until then our code is started *from* the running Kobo software (see
   `tools/chainload`). A reboot always brings the normal Kobo back.
3. Before milestone 6: a full backup of the internal storage, and an explicit yes.

## Build

Needs `clang`, `ld.lld`, `llvm-objcopy` (any recent LLVM).

    make            # -> build/sage.bin

The loader module (`tools/chainload`) builds against Kobo's published kernel
source, see its Makefile.

## Layout

    platform/sage/   Sage-specific code (start-up, buttons, later: screen, storage)
    core/            device-independent code (later: the reader itself)
    tools/chainload/ Linux kernel module that hands the Sage over to our image
    docs/            plan, hardware notes, sources
