# Out-of-tree module build against Kobo's published kernel source
# (github.com/kobolabs/b300-sage, linux-4.9, sun8iw15p1smp_kobo_defconfig).
#   make KDIR=/path/to/linux-4.9 CROSS_COMPILE=arm-linux-gnueabihf-
obj-m := vbg_chainload.o

KDIR ?= ../../third_party/linux-4.9

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) ARCH=arm CROSS_COMPILE=$(CROSS_COMPILE) modules

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) ARCH=arm clean
