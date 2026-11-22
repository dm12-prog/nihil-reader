# kobo-hid: Kobo Sage as a USB pen tablet + trackpad.
#   make         -> kobo-hid (static ARM program)
#   make module KDIR=<Kobo linux-4.9 tree, config = Kobo's live config plus
#               CONFIG_USB_G_HID=m, after "make modules_prepare">
#               SYMVERS=<device.symvers>
#                -> usb_f_hid.ko (the kernel's own f_hid.c, built out of tree,
#                   patched: one IN endpoint (the Sage UDC has a single
#                   interrupt endpoint, ep4) + upstream write-deadlock fix)
CC = arm-linux-gnueabihf-gcc

all: kobo-hid

kobo-hid: kobo-hid.c
	$(CC) -O2 -static -Wall -o $@ $<
	arm-linux-gnueabihf-strip $@

module:
	mkdir -p build-mod
	cp $(KDIR)/drivers/usb/gadget/function/f_hid.c build-mod/
	patch build-mod/f_hid.c f_hid-kobo.patch
	printf 'obj-m := usb_f_hid.o\nusb_f_hid-y := f_hid.o\nccflags-y := -I$$(KDIR)/drivers/usb/gadget -I$$(KDIR)/drivers/usb/gadget/function\n' > build-mod/Kbuild
	$(MAKE) -C $(KDIR) M=$(CURDIR)/build-mod KDIR=$(KDIR) ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- HOSTCFLAGS=-fcommon KCFLAGS=-Wno-error KBUILD_EXTRA_SYMBOLS=$(SYMVERS) modules
	arm-linux-gnueabihf-strip --strip-debug build-mod/usb_f_hid.ko
	cp build-mod/usb_f_hid.ko .
