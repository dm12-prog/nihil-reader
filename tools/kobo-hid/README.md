# kobo-hid: Kobo Sage as a USB pen tablet + trackpad

Plug the Kobo into a computer with a USB cable and it shows up as a standard
USB mouse. Nothing is installed on the computer.

- **Pen**: the pointer goes to the same spot of the screen (like a drawing
  tablet). Pen touching = left button held (click, drag, draw). Pen side
  button = right button.
- **One finger**: trackpad (moves the pointer). Tap = click.
  Tap, then touch again and hold or move = drag (button held until you lift).
  Two quick taps = double click.
- **Two fingers**: tap = right click, move = scroll, spread / pinch = zoom
  in / out (Cmd + / Cmd -).
- **Page buttons**: lower = next app (Cmd+Tab), upper = previous app; press
  again within 0.9 s to move further in the app switcher.
- **Screen**: a dot grid (every 5 mm) to judge distances.
- **Stop**: hold a finger in any screen corner for 2 s, or hold both page
  buttons for 1.5 s.
- First start: two pen taps (top-left, then top-right corner, as you hold the
  Kobo) tell it which way the Kobo is held. Menu entry
  "Mac tablet - pen direction" asks again.

Settings in `.adds/kobohid/config`: `SCREEN=16:10` (shape of the computer
screen; the pen area is the biggest rectangle of that shape in the middle of
the Kobo; `0` = whole Kobo), `SPEED=150` (trackpad, percent), `FINGER=0`
(pen only), `SCROLL=-1` (flip scrolling), `ZOOM=2` (pinch sends Cmd + scroll
instead of Cmd +/-; `0` = off), `GRID=5` (dot spacing in mm, `0` = no grid), `BUTTONS=0`
(page buttons off). The Mac may ask once to identify a keyboard (the shortcut keys): close it.

Not there yet: pen pressure (the computer gets a mouse, which has no pressure).

## How it works
The Kobo's kernel leaves out the USB HID gadget (`# CONFIG_USB_CONFIGFS_F_HID is not set`).
`usb_f_hid.ko` is the kernel's own `drivers/usb/gadget/function/f_hid.c`, built out of tree
against the Kobo's published source (github.com/kobolabs/b300-sage) with the Kobo's live
config and `device.symvers`, with `f_hid-kobo.patch`:

1. one IN endpoint only (the Sage USB controller has a single interrupt endpoint, ep4);
2. the upstream fix 072684e8c58d "USB: gadget: f_hid: fix deadlock in f_hidg_write()"
   (Linux 5.1, 2019). Without it the Kobo froze as soon as the computer accepted the device
   (22 Nov 2026): the Sage controller completes a short transfer inside `usb_ep_queue()`
   and the completion handler waits for a lock that `f_hidg_write()` is holding.

`kobo-hid.sh` loads the module, builds the gadget through configfs (one HID function,
`pad.desc`: report 1 = absolute pointer for the pen, report 2 = relative mouse + wheel +
horizontal pan for the trackpad, report 3 = keyboard for the shortcuts), ends Kobo's watchdog "sickel" (as KOReader does; it is back after the next restart) and pauses Nickel (otherwise its idle sleep hangs while the gadget is up and sickel restarts the Kobo), then runs `kobo-hid`, which takes the touch
screen (EVIOCGRAB) and turns it into HID reports on `/dev/hidg0`. Everything is in RAM:
stopping or a restart removes it and resumes Nickel.

## Files on the Kobo
`.adds/kobohid/`: `kobo-hid`, `usb_f_hid.ko`, `pad.desc`, `kobo-hid.sh`
(`config`, `start.log`, `kobo-hid.log`, `dmesg-live.log` are created when it runs).
`.adds/nm/kobohid`: NickelMenu entries "Mac tablet - start", "Mac tablet - pen direction",
"Mac tablet - stop" (NickelMenu labels must not contain a colon).

## Build
    make                      # kobo-hid
    make module KDIR=... SYMVERS=...   # usb_f_hid.ko, see Makefile

Uses the same USB port as drive mode and the Nihil cable link: one at a time.
