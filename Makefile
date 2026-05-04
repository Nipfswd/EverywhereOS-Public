# Makefile for Everywhere OS Kernel -- multi-module version

CC      = gcc
LD      = ld
NASM    = nasm

CFLAGS  = -c -ffreestanding -fno-builtin -fno-stack-protector -nostdlib \
          -m32 -Wall -Wextra \
          -I./base/ntos/inc \
          -I./base/ntos/mm \
          -I./shell/explorer \
          -I./base/fs/evryfs

LDFLAGS = -m elf_i386 -T kernel.ld
ASFLAGS = -f elf32

BUILD = build
ISO   = iso
DISK_IMG = $(BUILD)/disk.img

ENTRY_SRC = entry.asm
ENTRY_OBJ = $(BUILD)/entry.o

# Kernel core (base\ntos\ke)
NTOS_SRC = base/ntos/ke/io.c \
           base/ntos/ke/video.c \
           base/ntos/ke/font.c \
           base/ntos/ke/mouse.c \
           base/ntos/ke/keyboard.c \
           base/ntos/ke/window.c

# Memory Manager (base\ntos\mm)
MM_SRC = base/ntos/mm/mminit.c \
         base/ntos/mm/allocpag.c

# File System (base\fs\evryfs)
FS_SRC = base/fs/evryfs/ata.c \
         base/fs/evryfs/evryfs.c
HAL_SRC = base/hals/halx86/halinit.c
HAL_ASM_SRC = base/hals/halx86/irq12.asm \
              base/hals/halx86/i386/ixclock.asm
HAL_ASM_OBJ = $(BUILD)/base/hals/halx86/irq12.o \
              $(BUILD)/base/hals/halx86/i386/ixclock.o

# Shell / Explorer (userspace)
SHELL_SRC = shell/explorer/desktop.c \
            shell/explorer/taskbar.c \
            shell/explorer/shell.c \
            shell/explorer/notes.c \
            shell/explorer/snake.c \
            shell/explorer/input.c \
            shell/explorer/files.c

# Main entry
MAIN_SRC = kernel.c

ALL_C_SRC = $(NTOS_SRC) $(MM_SRC) $(HAL_SRC) $(FS_SRC) $(SHELL_SRC) $(MAIN_SRC)
ALL_C_OBJ = $(patsubst %.c,$(BUILD)/%.o,$(ALL_C_SRC))

KERNEL_ELF = $(BUILD)/kernel.elf
OS_ISO     = $(BUILD)/os.iso

# MM regression test kernel
#
# The test binary is a standalone Multiboot ELF that contains only the MM
# sources and the test suite.  It is loaded directly by QEMU via -kernel
# (no GRUB or ISO required) and runs entirely headless, writing all output
# to COM1 which is forwarded to the host terminal via -serial stdio.
#
# Build and run: make test
# The suite prints [ PASS ] / [ FAIL ] per case and a final PASS or FAIL
# banner.  Non-zero exit from make test indicates at least one failure.

TEST_ENTRY_SRC  = base/ntos/mm/tests/entry.asm
TEST_ENTRY_OBJ  = $(BUILD)/base/ntos/mm/tests/entry.o
TEST_MMTEST_SRC = base/ntos/mm/tests/mmtest.c
TEST_MMTEST_OBJ = $(BUILD)/base/ntos/mm/tests/mmtest.o
TEST_ELF        = $(BUILD)/mmtest.elf

QEMU_TESTFLAGS  = -display none -m 64M -no-reboot

.PHONY: all clean run test

$(shell mkdir -p $(BUILD))
$(shell mkdir -p $(BUILD)/base/ntos/ke)
$(shell mkdir -p $(BUILD)/base/ntos/mm)
$(shell mkdir -p $(BUILD)/base/ntos/mm/tests)
$(shell mkdir -p $(BUILD)/base/hals/halx86)
$(shell mkdir -p $(BUILD)/base/fs/evryfs)
$(shell mkdir -p $(BUILD)/shell/explorer)
$(shell mkdir -p $(ISO)/boot/grub)

all: $(OS_ISO)

$(ENTRY_OBJ): $(ENTRY_SRC)
	$(NASM) $(ASFLAGS) $< -o $@

$(BUILD)/%.o: %.c
	$(CC) $(CFLAGS) $< -o $@

$(BUILD)/base/hals/halx86/irq12.o: base/hals/halx86/irq12.asm
	$(NASM) $(ASFLAGS) $< -o $@

$(BUILD)/base/hals/halx86/i386/ixclock.o: base/hals/halx86/i386/ixclock.asm
	@mkdir -p $(BUILD)/base/hals/halx86/i386
	$(NASM) $(ASFLAGS) $< -o $@

$(KERNEL_ELF): $(ENTRY_OBJ) $(ALL_C_OBJ) $(HAL_ASM_OBJ)
	$(LD) $(LDFLAGS) $^ -o $@

$(ISO)/boot/kernel.elf: $(KERNEL_ELF)
	cp $< $@

$(ISO)/boot/grub/grub.cfg:
	echo 'set timeout=0' > $@
	echo 'set default=0' >> $@
	echo '' >> $@
	echo 'menuentry "Everywhere OS" {' >> $@
	echo '    multiboot /boot/kernel.elf' >> $@
	echo '    boot' >> $@
	echo '}' >> $@

$(OS_ISO): $(ISO)/boot/kernel.elf $(ISO)/boot/grub/grub.cfg
	grub-mkrescue -o $@ $(ISO)

$(DISK_IMG):
	qemu-img create -f raw $@ 1M

run: $(OS_ISO) $(DISK_IMG)
	qemu-system-i386 -cdrom $(OS_ISO) -hda $(DISK_IMG) -full-screen

$(TEST_ENTRY_OBJ): $(TEST_ENTRY_SRC)
	$(NASM) $(ASFLAGS) $< -o $@

$(TEST_MMTEST_OBJ): $(TEST_MMTEST_SRC)
	$(CC) $(CFLAGS) $< -o $@

$(TEST_ELF): $(TEST_ENTRY_OBJ) \
             $(BUILD)/base/ntos/mm/mminit.o \
             $(BUILD)/base/ntos/mm/allocpag.o \
             $(TEST_MMTEST_OBJ)
	$(LD) $(LDFLAGS) $^ -o $@

test: $(TEST_ELF)
	timeout 30 qemu-system-i386 -kernel $< $(QEMU_TESTFLAGS) \
	    -serial file:$(BUILD)/test.log || true
	@cat $(BUILD)/test.log
	grep -q "^PASS" $(BUILD)/test.log

clean:
	rm -rf $(BUILD) $(ISO)