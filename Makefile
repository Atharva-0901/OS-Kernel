# Makefile for Simple OS Kernel

ASM = nasm
CC = gcc
LD = ld

ASMFLAGS = -f elf32
CFLAGS = -m32 -ffreestanding -O2 -Wall -Wextra -fno-exceptions -fno-pie -fno-stack-protector
LDFLAGS = -m elf_i386 -T linker.ld -nostdlib

OBJECTS = boot.o kernel.o
KERNEL = kernel.bin
ISO = os.iso

.PHONY: all clean run iso

all: $(KERNEL)

$(KERNEL): $(OBJECTS)
	$(LD) $(LDFLAGS) -o $@ $^

boot.o: boot.asm
	$(ASM) $(ASMFLAGS) $< -o $@

kernel.o: kernel.c
	$(CC) $(CFLAGS) -c $< -o $@

iso: $(KERNEL)
	mkdir -p isodir/boot/grub
	cp $(KERNEL) isodir/boot/
	echo 'set timeout=0' > isodir/boot/grub/grub.cfg
	echo 'set default=0' >> isodir/boot/grub/grub.cfg
	echo 'menuentry "SimpleOS" {' >> isodir/boot/grub/grub.cfg
	echo '    multiboot /boot/kernel.bin' >> isodir/boot/grub/grub.cfg
	echo '    boot' >> isodir/boot/grub/grub.cfg
	echo '}' >> isodir/boot/grub/grub.cfg
	grub-mkrescue -o $(ISO) isodir

run: iso
	qemu-system-i386 -cdrom $(ISO)

clean:
	rm -f $(OBJECTS) $(KERNEL) $(ISO)
	rm -rf isodir
