# The directory that contains FreeRTOS source code
FREERTOS_ROOT := ../FreeRTOS/FreeRTOS


# Demo code
DEMO_PROJECT := .


# FreeRTOS kernel
KERNEL_DIR := $(FREERTOS_ROOT)/Source
KERNEL_PORT_DIR := $(KERNEL_DIR)/portable/GCC/ARM_CM3


# Where to store all the generated files (objects, elf and map)
OUTPUT_DIR := ./Output
TEST_OUTPUT_DIR := ./Test_Output


DEMO_NAME := demo
TEST_NAME := test_suite


ELF := $(OUTPUT_DIR)/$(DEMO_NAME).elf
TEST_ELF := $(TEST_OUTPUT_DIR)/$(TEST_NAME).elf


MAP := $(OUTPUT_DIR)/$(DEMO_NAME).map
TEST_MAP := $(TEST_OUTPUT_DIR)/$(TEST_NAME).map


# Compiler toolchain
CC := arm-none-eabi-gcc
LD := arm-none-eabi-gcc
SIZE := arm-none-eabi-size


# Target embedded board and CPU
MACHINE := mps2-an385
CPU := cortex-m3




# Use -s to connect to gdb port 1234 and -S to wait before executing
QEMU_FLAGS_DBG = -s -S


INCLUDE_DIRS = -I$(KERNEL_DIR)/include -I$(KERNEL_PORT_DIR)
INCLUDE_DIRS += -I$(DEMO_PROJECT)


VPATH += $(KERNEL_DIR) $(KERNEL_PORT_DIR) $(KERNEL_DIR)/portable/MemMang
VPATH += $(DEMO_PROJECT)


# Include paths. See INCLUDE_DIRS
CFLAGS = $(INCLUDE_DIRS)


CFLAGS += -MMD -MP #added




# Don't include standard libraries
CFLAGS += -ffreestanding


# Target ARM cortex M3 processor
CFLAGS += -mcpu=$(CPU)
# Use the thumb 16 bits instruction set. Required for cortex-M
CFLAGS += -mthumb


# Print all the most common warnings
CFLAGS += -Wall


# Print extra warnings, not included in -Wall
CFLAGS += -Wextra


# Print warnings about variable shadowing (var name = outer scope var name)
CFLAGS += -Wshadow


# Include debug information (-g) with maximum level of detail (3)
CFLAGS += -g3


# Optimize (-O) the size (s) of the generated executable
CFLAGS += -Os


# Place each function in a dedicated section (see -gc-sections in LDFLAGS)
CFLAGS += -ffunction-sections


# Place each variable in a dedicated section (see -gc-sections in LDFLAGS)
CFLAGS += -fdata-sections


# Specify the linker script
LDFLAGS = -T ./mps2_m3.ld


# Don't use the standard start-up files
LDFLAGS += -nostartfiles


# Optimize compilation to reduce memory
LDFLAGS += -specs=nano.specs


# Optimize compilation targeting a system without any operating system
LDFLAGS += -specs=nosys.specs


# Generate map file with memory layout. Pass option to the linker (-Xlinker)
LDFLAGS += -Xlinker -Map=$(MAP)


# Remove unused sections. Used with -ffunction-sections and -fdata-sections
# optimized memory. Pass option to the linker (-Xlinker)
LDFLAGS += -Xlinker --gc-sections


# Link specifically for cortex M3 and thumb instruction set
LDFLAGS += -mcpu=$(CPU) -mthumb








# Kernel files
SOURCE_FILES += $(KERNEL_DIR)/list.c
SOURCE_FILES += $(KERNEL_DIR)/tasks.c
SOURCE_FILES += $(KERNEL_DIR)/event_groups.c
SOURCE_FILES += $(KERNEL_DIR)/queue.c #added
SOURCE_FILES += $(KERNEL_DIR)/portable/MemMang/heap_4.c
SOURCE_FILES += $(KERNEL_DIR)/portable/GCC/ARM_CM3/port.c


# Demo files
SOURCE_FILES += $(DEMO_PROJECT)/main.c
SOURCE_FILES += $(DEMO_PROJECT)/uart.c
#SOURCE_FILES += $(DEMO_PROJECT)/logger.c #added
SOURCE_FILES += $(DEMO_PROJECT)/test_suite.c




# Start-up code
SOURCE_FILES += ./startup.c

# Logger library
SOURCE_FILES += $(KERNEL_DIR)/logger.c

# Create list of object files with the same names of the sources
OBJS = $(SOURCE_FILES:%.c=%.o)


# Remove path from object filename
OBJS_NOPATH = $(notdir $(OBJS))


# Prepend output dir to object filenames
OBJS_OUTPUT = $(OBJS_NOPATH:%.o=$(OUTPUT_DIR)/%.o)
TEST_OBJS_OUTPUT := $(OBJS_NOPATH:%.o=$(TEST_OUTPUT_DIR)/%.o) #added






all: $(ELF)


$(ELF): $(OBJS_OUTPUT) ./mps2_m3.ld Makefile
	echo "\n\n--- Final linking ---\n"
	$(LD) $(LDFLAGS) $(OBJS_OUTPUT) -o $(ELF)
	$(SIZE) $(ELF)


$(OUTPUT_DIR)/%.o : %.c  Makefile $(OUTPUT_DIR)
	$(CC) $(CFLAGS) -c $< -o $@


$(TEST_OUTPUT_DIR)/%.o : %.c  Makefile $(TEST_OUTPUT_DIR)
	$(CC) $(CFLAGS) -DTEST_SUITE -c $< -o $@


$(OUTPUT_DIR) $(TEST_OUTPUT_DIR):
	mkdir -p $@


cleanobj:
	rm -f $(OUTPUT_DIR)/*.o $(TEST_OUTPUT_DIR)/*.o


clean:
	rm -rf $(ELF) $(MAP) $(OUTPUT_DIR)/*.o $(OUTPUT_DIR) $(TEST_OUTPUT_DIR) $(TEST_MAP) $(TEST_ELF)


qemu_start:
	qemu-system-arm -machine $(MACHINE) -cpu $(CPU) -kernel \
	$(ELF) -monitor none -nographic -serial stdio


qemu_debug:
	qemu-system-arm -machine $(MACHINE) -cpu $(CPU) -kernel \
	$(ELF) -monitor none -nographic -serial stdio $(QEMU_FLAGS_DBG)


gdb_start:
   gdb-multiarch \
       -ex "target remote localhost:1234" \
       -ex "layout src" \
       $(ELF)




# test suite part




test_build: $(TEST_ELF)




$(TEST_ELF): $(TEST_OBJS_OUTPUT) ./mps2_m3.ld Makefile
	@echo "\n\n--- Linking TEST SUITE ---\n"
	$(LD) -T ./mps2_m3.ld -nostartfiles -specs=nano.specs -specs=nosys.specs \
		-Xlinker -Map=$(TEST_MAP) -Xlinker --gc-sections -mcpu=$(CPU) -mthumb \
    	$(TEST_OBJS_OUTPUT) -o $(TEST_ELF)
	$(SIZE) $(TEST_ELF)




test_run: test_build
	@echo "\n---Running Test suite in QEMU--\n"
	qemu-system-arm -machine $(MACHINE) -cpu $(CPU) -kernel \
	$(TEST_ELF) -monitor none -nographic -serial stdio \
	-semihosting


test_clean:
	rm -f $(TEST_ELF) $(TEST_MAP) $(TEST_OUTPUT_DIR)/*.o


test_qemu_start:
	qemu-system-arm -machine $(MACHINE) -cpu $(CPU) -kernel \
	$(TEST_ELF) -monitor none -nographic -serial stdio


test_qemu_debug: test_build
	qemu-system-arm -machine $(MACHINE) -cpu $(CPU) -kernel \
	$(TEST_ELF) -monitor none -nographic -serial stdio $(QEMU_FLAGS_DBG)


test_gdb_start:
	gdb-multiarch \
		-ex "target remote localhost:1234" \
		-ex "layout src" \
		$(TEST_ELF)


test: test_build test_run


.PHONY: all clean cleanobj qemu_start qemu_debug gdb_start test_build test_run test test_clean test_gdb_start test_qemu_debug test_qemu_start







