# kobo-hid: Kobo Sage as a USB keyboard + pen tablet

Plug the Kobo into a computer and it shows up as a keyboard and a pen tablet.
Nothing is installed on the computer.

- Page buttons: Left / Right arrow keys (lower button = Right).
- Stylus: moves the computer's pointer. Pen down = left click, side button held = right click.
- Both page buttons, short press: next pen orientation (8 of them, saved in `config`).
- Both page buttons held 1.5 s, or the power button: stop.

## How it works
The Kobo's kernel leaves out the USB HID gadget (`# CONFIG_USB_CONFIGFS_F_HID is not set`).
`usb_f_hid.ko` is the kernel's own `drivers/usb/gadget/function/f_hid.c`, built out of tree
against the Kobo's published source (github.com/kobolabs/b300-sage) with the Kobo's live
config and `device.symvers`. `kobo-hid.sh` loads it, builds a two-function HID gadget
(boot keyboard + absolute pointer) through configfs, then runs `kobo-hid`, which grabs the
page buttons and touch panel and turns them into HID reports on `/dev/hidg0` / `/dev/hidg1`.
Everything is in RAM: `kobo-hid.sh stop` or a restart removes it.

## Files on the Kobo
`.adds/kobohid/`: `kobo-hid`, `usb_f_hid.ko`, `kbd.desc`, `ptr.desc`, `kobo-hid.sh`
(`config`, `start.log`, `kobo-hid.log` are created on first run).
`.adds/nm/kobohid`: NickelMenu entries "Mac tablet: start" / "Mac tablet: stop".

## Build
    make                      # kobo-hid
    make module KDIR=... SYMVERS=...   # usb_f_hid.ko, see Makefile

Uses the same USB port as drive mode and the Nihil cable link: one at a time.
