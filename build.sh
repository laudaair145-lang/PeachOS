#!/bin/bash

echo "Assembling bootloader..."
nasm -f elf32 boot.s -o boot.o

echo "Compiling C kernel and applications..."
CFLAGS="-m32 -ffreestanding -fno-pie -fno-stack-protector -O2"

gcc $CFLAGS -c kernel.c -o kernel.o
gcc $CFLAGS -c app_explorer.c -o app_explorer.o
gcc $CFLAGS -c app_editor.c -o app_editor.o
gcc $CFLAGS -c app_sysmon.c -o app_sysmon.o
gcc $CFLAGS -c app_matrix.c -o app_matrix.o
gcc $CFLAGS -c app_diskutil.c -o app_diskutil.o
gcc $CFLAGS -c app_bf.c -o app_bf.o
gcc $CFLAGS -c app_hexview.c -o app_hexview.o
gcc $CFLAGS -c app_installer.c -o app_installer.o

echo "Linking kernel..."
ld -m elf_i386 -T linker.ld -o os.bin boot.o kernel.o app_explorer.o app_editor.o app_sysmon.o app_matrix.o app_diskutil.o app_bf.o app_hexview.o app_installer.o

echo "Checking for valid disk image..."
if [ ! -s peach-test.img ]; then
    echo "Creating a fresh 10MB raw disk image for Peach OS..."
    dd if=/dev/zero of=peach-test.img bs=1M count=10
fi

echo "Launching Peach OS..."
qemu-system-i386 -kernel os.bin -drive file=peach-test.img,format=raw,if=ide