#!/bin/bash
set -xue

QEMU=qemu-system-riscv32

if [[ -n "$IN_NIX_SHELL" ]]; then
CC=clang
OBJCOPY=llvm-objcopy
else
TOOLCHAIN="${TOOLCHAIN:-}"
if [[ ! -f $TOOLCHAIN/clang ]]; then
if [[ -f /run/current-system/sw/bin/clang ]]; then
TOOLCHAIN=/run/current-system/sw/bin
elif [[ -f /opt/homebrew/opt/llvm/bin/clang ]]; then
TOOLCHAIN=/opt/homebrew/opt/llvm/bin/
else
echo "Cannot find toolchain"
exit 1
fi
fi
echo "Using toolchain at ${TOOLCHAIN}"
CC=$TOOLCHAIN/clang
OBJCOPY=$TOOLCHAIN/llvm-objcopy
fi

CFLAGS="-std=c11 -O2 -gdwarf-4 -g3 -Wall -Wextra --target=riscv32-unknown-elf -fno-stack-protector -ffreestanding -nostdlib"

# シェルをビルド
$CC $CFLAGS -Wl,-Tuser.ld -Wl,-Map=shell.map -o shell.elf shell.c user.c common.c

$OBJCOPY --set-section-flags .bss=alloc,contents -O binary shell.elf shell.bin
$OBJCOPY -Ibinary -Oelf32-littleriscv shell.bin shell.bin.o

# カーネルをビルド
$CC $CFLAGS -Wl,-Tkernel.ld -Wl,-Map=kernel.map -o kernel.elf \
    kernel.c common.c shell.bin.o

(cd disk && tar cf ../disk.tar --format=ustar *.txt)

$QEMU -machine virt -bios default -nographic -serial mon:stdio --no-reboot \
    -d unimp,guest_errors,int,cpu_reset -D qemu.log \
    -drive id=drive0,file=disk.tar,format=raw,if=none \
    -device virtio-blk-device,drive=drive0,bus=virtio-mmio-bus.0 \
    -kernel kernel.elf
