# Builds the Sage image with clang + lld (no ARM gcc needed).
#   make            -> build/sage.bin  (raw image, also a valid "zImage" for kexec/bootz)
#   make clean

TARGET  := --target=armv7a-none-eabi -mcpu=cortex-a7 -mfloat-abi=soft
CFLAGS  := $(TARGET) -O2 -Wall -Wextra -ffreestanding -fno-builtin -nostdlib -fno-unwind-tables -fno-asynchronous-unwind-tables -Iplatform/sage
LDFLAGS := -T platform/sage/linker.ld --no-undefined

SRCS := platform/sage/start.S platform/sage/main.c platform/sage/libc.c
OBJS := $(patsubst %,build/%.o,$(SRCS))

build/sage.bin: build/sage.elf
	llvm-objcopy -O binary $< $@
	@echo "built $@ ($$(stat -c %s $@) bytes)"

build/sage.elf: $(OBJS) platform/sage/linker.ld
	ld.lld $(LDFLAGS) -o $@ $(OBJS)

build/%.o: %
	@mkdir -p $(dir $@)
	clang $(CFLAGS) -c $< -o $@

clean:
	rm -rf build

.PHONY: clean

# PC simulator: same reader code, pictures instead of a screen.
#   make sim && build/sim book.txt nnnb out
sim:
	@mkdir -p build
	cc -O2 -Wall -Wextra tools/sim/sim.c -o build/sim
.PHONY: sim

# PC test of the Sage storage driver against a stand-in controller + chip:
#   make mmc-test && build/mmc_test disk.img "/Folder/Book.epub" Book.epub
mmc-test:
	@mkdir -p build
	cc -O2 -Wall -Wextra tools/sim/mmc_test.c -o build/mmc_test
.PHONY: mmc-test

# PC test of the Sage frontlight code with a real Kobo table file:
#   make light-test && build/light_test ntxfw-E80P04-B1.bin
light-test:
	@mkdir -p build
	cc -O2 -Wall -Wextra tools/sim/light_test.c -o build/light_test
.PHONY: light-test
