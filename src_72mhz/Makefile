BINARY      = cp2112_emulator
OBJS        = cp2112_emulator.o descriptors.o init.o i2c_utils.o

DEVICE      = stm32f103c8t6
# Now libopencm3 is one level above src1
OPENCM3_DIR := $(realpath ../libopencm3)

PREFIX      = arm-none-eabi
CC          = $(PREFIX)-gcc
OBJCOPY     = $(PREFIX)-objcopy
SIZE        = $(PREFIX)-size
OBJDUMP     = $(PREFIX)-objdump

# Add the current directory (.) to the includes to see local headers
CFLAGS      += -Os -g -I. -ffunction-sections -fdata-sections \
               -fno-unwind-tables -fno-asynchronous-unwind-tables
LDFLAGS     += -Wl,--gc-sections -nostartfiles

ifneq ($(V),1)
Q           := @
MAKEFLAGS   += --no-print-directory
endif

# First, we include the libopencm3 configuration
include $(OPENCM3_DIR)/mk/genlink-config.mk
include $(OPENCM3_DIR)/mk/gcc-config.mk

.PHONY: all clean flash size

# list of targets 
all: $(BINARY).elf $(BINARY).bin $(BINARY).list size

# rules for linker script gereration
include $(OPENCM3_DIR)/mk/genlink-rules.mk

# rules for compiler
include $(OPENCM3_DIR)/mk/gcc-rules.mk

# rules for listing file generation
$(BINARY).list: $(BINARY).elf
	@printf "  LIST    $<\n"
	$(Q)$(OBJDUMP) -S $< > $@

# Override CC after all includes (the library sometimes resets it)
override CC = $(PREFIX)-gcc

# Explicit rule for size
size: $(BINARY).elf
	@printf "  SIZE    $<\n"
	$(Q)$(SIZE) --format=berkeley $(BINARY).elf

clean:
	@printf "  CLEAN PROJECT FILES IN src\n"
	$(Q)rm -f *.o *.d *.elf *.bin *.map *.list generated.*

flash: $(BINARY).bin
	@printf "  FLASH   $<\n"
	$(Q)st-flash write $(BINARY).bin 0x8000000

.PRECIOUS: %.o
