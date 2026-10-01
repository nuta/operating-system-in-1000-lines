#!/bin/bash
set -xue

QEMU=qemu-system-riscv32
CC=/opt/homebrew/opt/llvm/bin/clang
OBJCOPY=/opt/homebrew/opt/llvm/bin/llvm-objcopy

# macOS için LLD linker'ını kullanıyoruz (yüklü: brew install lld)
# --ld-path ile LLD'yi explicitly belirtiyoruz
CFLAGS="-std=c11 -O2 -g3 -Wall -Wextra --target=riscv32-unknown-elf --ld-path=/opt/homebrew/bin/ld.lld -fno-stack-protector -ffreestanding -nostdlib"

# Build the shell.
$CC $CFLAGS -Wl,-Tuser.ld -o shell.elf shell.c user.c common.c
$OBJCOPY --set-section-flags .bss=alloc,contents -O binary shell.elf shell.bin
$OBJCOPY -Ibinary -Oelf32-littleriscv shell.bin shell.bin.o

# Build the kernel.
$CC $CFLAGS -Wl,-Tkernel.ld -o kernel.elf \
    kernel.c common.c shell.bin.o

if [ ! -f disk.tar ]; then
    (cd disk && tar cf ../disk.tar --format=ustar *.txt)
fi

$QEMU -machine virt -bios default -nographic -serial mon:stdio --no-reboot \
    -d unimp,guest_errors,int,cpu_reset -D qemu.log \
    -drive id=drive0,file=disk.tar,format=raw,if=none \
    -device virtio-blk-device,drive=drive0,bus=virtio-mmio-bus.0 \
    -kernel kernel.elf
