# Builds the Sage image with clang + lld (no ARM gcc needed).
#   make            -> build/sage.bin  (raw image, also a valid "zImage" for kexec/bootz)
#   make clean

TARGET  := --target=armv7a-none-eabi -mcpu=cortex-a7 -mfloat-abi=soft
CFLAGS  := $(TARGET) -O2 -Wall -Wextra -ffreestanding -fno-builtin -nostdlib -fno-unwind-tables -fno-asynchronous-unwind-tables -Iplatform/sage
LDFLAGS := -T platform/sage/linker.ld --no-undefined

SRCS := platform/sage/start.S platform/sage/main.c
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
