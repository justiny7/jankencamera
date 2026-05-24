# toolchain
CC      = arm-none-eabi-gcc
LD      = arm-none-eabi-ld
OBJCOPY = arm-none-eabi-objcopy

# submodule
RPI_OS_DIR = rpi_os
RPI_OS_INC = $(RPI_OS_DIR)/include
RPI_OS_LIB = $(RPI_OS_DIR)/build/librpi_os.a
LINKER_SCRIPT = $(RPI_OS_DIR)/linker.ld

# compilation flags
CFLAGS  = -mcpu=arm1176jzf-s -mfloat-abi=hard -mfpu=vfp -fpic -ffreestanding -O2 -Wall -Wextra -nostdlib -Iinclude -I$(RPI_OS_INC)
ASFLAGS = -mcpu=arm1176jzf-s -mfpu=vfp
LDFLAGS = -T $(LINKER_SCRIPT) -nostdlib -mfloat-abi=hard -mfpu=vfp

# directories
SRC_DIR   = src
KERNEL_DIR = kernels
BUILD_DIR = build

# sources
SRCS_QASM = $(wildcard $(KERNEL_DIR)/*.qasm)
SRCS_C = $(wildcard $(SRC_DIR)/*.c) $(SRCS_QASM:.qasm=.c)
SRCS_S = $(wildcard $(SRC_DIR)/*.S)

# objects
OBJS = \
  $(patsubst %.S,$(BUILD_DIR)/%.S.o,$(SRCS_S)) \
  $(patsubst %.c,$(BUILD_DIR)/%.c.o,$(SRCS_C))

# targets
all: kernel.img

# build OS library first (skip if exists)
$(RPI_OS_LIB):
	@if [ ! -f $(RPI_OS_LIB) ]; then $(MAKE) -C $(RPI_OS_DIR) lib; fi

kernel.img: $(BUILD_DIR)/jankencamera.elf
	$(OBJCOPY) $< -O binary $@

$(BUILD_DIR)/jankencamera.elf: $(RPI_OS_LIB) $(OBJS)
	$(CC) $(LDFLAGS) $(OBJS) -L$(dir $(RPI_OS_LIB)) -lrpi_os -lgcc -o $@

# ASM rules
$(BUILD_DIR)/%.S.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(ASFLAGS) -c $< -o $@

# C rules
$(BUILD_DIR)/%.c.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# qasm rules
.PRECIOUS: $(KERNEL_DIR)/%.c
$(KERNEL_DIR)/%.c $(KERNEL_DIR)/%.h &: $(KERNEL_DIR)/%.qasm
	/opt/homebrew/opt/vc4asm/bin/vc4asm -h $(KERNEL_DIR)/$*.h -c $(KERNEL_DIR)/$*.c $<

$(OBJS): $(SRCS_QASM:$(KERNEL_DIR)/%.qasm=$(KERNEL_DIR)/%.h)

clean:
	rm -rf $(BUILD_DIR) kernel.img
	$(MAKE) -C $(RPI_OS_DIR) clean

.PHONY: all clean
