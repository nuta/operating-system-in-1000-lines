---
title: Boot
layout: chapter
lang: en
---

> [!NOTE]
>
> **Translation of this English version is in progress.**

The first thing we need is the boot process. For applications, the OS conveniently calls the `main` function, but for kernels, we need to write our own initialization process according to hardware specifications.

## Supervisor Binary Interface (SBI)

The Supervisor Binary Interface (SBI) is very useful when implementing a RISC-V OS. SBI is essentially an "API for kernels". Just as APIs define functions provided to applications such as displaying characters and reading/writing files, SBI defines functions that firmware provides to the OS.

The SBI specification is [published on GitHub](https://github.com/riscv-non-isa/riscv-sbi-doc/releases). It defines useful features such as displaying characters on the debug console (e.g., serial port), reboot/shutdown, and timer settings.

A famous example of SBI implementation is [OpenSBI](https://github.com/riscv-software-src/opensbi). In QEMU, OpenSBI starts by default, performs hardware-specific initialization, and then boots the kernel.

## Let's boot OpenSBI

First, let's see how OpenSBI starts. Create a shell script named `run.sh` as follows:

```plain
$ touch run.sh
$ chmod +x run.sh
```

```bash:run.sh
#!/bin/bash
set -xue

# QEMU file path
QEMU=qemu-system-riscv32

# Start QEMU
$QEMU -machine virt -bios default -nographic -serial mon:stdio --no-reboot
```

The QEMU startup options are as follows:

- `-machine virt`: Start as a virt machine. You can check supported environments with the `-machine '?'` option.
- `-bios default`: Use the default BIOS (in this case, OpenSBI).
- `-nographic`: Start QEMU without a window.
- `-serial mon:stdio`: Connect QEMU's standard input/output to the virtual machine's serial port. Specifying `mon:` also allows switching to the QEMU monitor.
- `--no-reboot`: If the virtual machine crashes, stop without rebooting (useful for debugging).

> [!TIP]
>
> For macOS Homebrew version of QEMU, you can check the file path with the following command:
>
> ```plain
> $ ls $(brew --prefix)/bin/qemu-system-riscv32
> /opt/homebrew/bin/qemu-system-riscv32
> ```

````

When you start it, you'll see a log like this:

```plain
$ ./run.sh

OpenSBI v1.2
   ____                    _____ ____ _____
  / __ \                  / ____|  _ \_   _|
 | |  | |_ __   ___ _ __ | (___ | |_) || |
 | |  | | '_ \ / _ \ '_ \ \___ \|  _ < | |
 | |__| | |_) |  __/ | | |____) | |_) || |_
  \____/| .__/ \___|_| |_|_____/|____/_____|
        | |
        |_|

Platform Name             : riscv-virtio,qemu
Platform Features         : medeleg
Platform HART Count       : 1
Platform IPI Device       : aclint-mswi
Platform Timer Device     : aclint-mtimer @ 10000000Hz
...
````

After the large OpenSBI banner is displayed, various execution environment information is shown.

You may have noticed that nothing is displayed when you enter characters. This is because the `-serial mon:stdio` option connects QEMU's standard input/output to the virtual machine's serial port. When you enter characters here, they are sent to the OS. However, since the OS is not running at this point and OpenSBI is ignoring input, no characters are displayed.

Press <kbd>Ctrl</kbd>+<kbd>A</kbd> followed by <kbd>C</kbd> to switch to the QEMU debug console (QEMU monitor). You can exit QEMU by executing the `q` command in the monitor:

```plain
QEMU 8.0.2 monitor - type 'help' for more information
(qemu) q
```

> [!TIP]
>
> <kbd>Ctrl</kbd>+<kbd>A</kbd> has several functions besides switching to the QEMU monitor (<kbd>C</kbd> key). For example, pressing the <kbd>X</kbd> key will immediately exit QEMU.
>
> ```plain
> C-a h    print this help
> C-a x    exit emulator
> C-a s    save disk data back to file (if -snapshot)
> C-a t    toggle console timestamps
> C-a b    send break (magic sysrq)
> C-a c    switch between console and monitor
> C-a C-a  sends C-a
> ```

## Linker script

A linker script is a file that defines how each data section of a program is placed in memory.

Let's create a new file called `kernel.ld` as follows. When the linker links the program, it determines the final memory addresses of each function and variable according to this file.

```plain:kernel.ld
ENTRY(boot)

SECTIONS {
    . = 0x80200000;

    .text :{
        KEEP(*(.text.boot));
        *(.text .text.*);
    }

    .rodata : ALIGN(4) {
        *(.rodata .rodata.*);
    }

    .data : ALIGN(4) {
        *(.data .data.*);
    }

    .bss : ALIGN(4) {
        __bss = .;
        *(.bss .bss.* .sbss .sbss.*);
        __bss_end = .;
    }

    . = ALIGN(4);
    . += 128 * 1024; /* 128KB */
    __stack_top = .;
}
```

In this linker script, the following are defined:

- The entry point of the kernel is the `boot` function.
- The base address is `0x80200000`.
- The `.text.boot` section is always placed at the beginning.
- Each section is placed in the order of `.text`, `.rodata`, `.data`, and `.bss`.
- At the end of the `.bss` section, a stack area used during boot is placed.

The `.text`, `.rodata`, `.data`, and `.bss` sections appearing here are data areas with the following roles:

- `.text`: Code area.
- `.rodata`: Constant data area. Read-only.
- `.data`: Read/write data area.
- `.bss`: Read/write data area. Unlike `.data`, it places variables with an initial value of zero.

Let's look at some of the syntax of the linker script. First, `ENTRY(boot)` declares that the entry point (starting point of the program) is the `boot` function. Then, within `SECTIONS`, the placement of each section is defined.

A description like `*(.text .text.*)` means placing the `.text` section and sections starting with `.text.` from all files (`*`) there.

`.` represents a variable-like entity that indicates the "current address". The address is automatically incremented whenever data is placed with `*(.text)`, etc. `. += 128 * 1024` means "advance the current address by 128KB". Also, `ALIGN(4)` means "adjust the address to a 4-byte boundary".

A description like `__bss = .` means assigning the current address to the symbol `__bss`. **A "symbol"** represents a function or static variable, and in C language, you can refer to a defined symbol with `extern char symbol_name`.

> [!TIP]
>
> Linker scripts have many convenient features, especially for kernel development. Try searching GitHub for real-world examples!

## Minimal Kernel

Let's start by creating a minimal kernel. Create a C language source code file named `kernel.c` as follows:

```c:kernel.c
typedef unsigned char uint8_t;
typedef unsigned int uint32_t;
typedef uint32_t size_t;

extern char __bss[], __bss_end[], __stack_top[];

void *memset(void *buf, char c, size_t n) {
    uint8_t *p = (uint8_t *) buf;
    while (n--)
        *p++ = c;
    return buf;
}

void kernel_main(void) {
    memset(__bss, 0, (size_t) __bss_end - (size_t) __bss);

    for (;;);
}

__attribute__((section(".text.boot")))
__attribute__((naked))
void boot(void) {
    __asm__ __volatile__(
        "mv sp, %[stack_top]\n"
        "j kernel_main\n"
        :
        : [stack_top] "r" (__stack_top)
    );
}
```

The first function to be executed is `boot`. It sets the stack pointer (`sp`) to the end address of the stack area prepared in the linker script and jumps to the `kernel_main` function. Note that the stack grows towards zero (it is decremented as it is used), so make sure to set the end address of the area.

`__attribute__((naked))` instructs the compiler not to generate unnecessary code ([Wikipedia](https://en.wikipedia.org/wiki/Function_prologue_and_epilogue)) before and after the function body. This ensures that the content of the function is output "as is" when written in inline assembly.

Additionally, since OpenSBI jumps to the base address of the execution image (`0x80200000`), the entry point of the kernel needs to be placed at `0x80200000`. The `__attribute__((section(".text.boot")))` attribute places it in a dedicated section, ensuring it comes first in the linker script.

At the beginning of the file, each symbol defined in the linker script is declared with `extern char`. Here, we are only interested in the addresses of the symbols, so we use the `char` type.

There is no problem with declaring `extern char __bss;`, but writing `__bss` would mean "the value at the 0th byte of the `.bss` section" rather than "the start address of the `.bss` section". Therefore, it is recommended to add `[]` to ensure that `__bss` returns an address, thus preventing careless mistakes.

In the `kernel_main` function, the `.bss` section is first initialized to zero using the `memset` function. Although some bootloaders may recognize and zero-clear the `.bss` section, it is recommended to initialize it manually since this cannot be guaranteed. Finally, the function enters an infinite loop and terminates.

## Let's run!

Finally, let's add kernel compilation and QEMU startup options (`-kernel kernel.elf`) to `run.sh` as follows:

```bash:run.sh {6-13,17}
#!/bin/bash
set -xue

QEMU=qemu-system-riscv32

# Path to clang (use CC=clang for Ubuntu)
CC=/opt/homebrew/opt/llvm/bin/clang

CFLAGS="-std=c11 -O2 -g3 -Wall -Wextra --target=riscv32 -ffreestanding -nostdlib"

# Build the kernel
$CC $CFLAGS -Wl,-Tkernel.ld -Wl,-Map=kernel.map -o kernel.elf \
    kernel.c

# Start QEMU
$QEMU -machine virt -bios default -nographic -serial mon:stdio --no-reboot \
    -kernel kernel.elf
```

> [!TIP]
>
> You can check the file path of the Homebrew version of clang on macOS with the following command:
>
> ```
> $ ls $(brew --prefix)/opt/llvm/bin/clang
> /opt/homebrew/opt/llvm/bin/clang
> ```

The options specified for clang (`CFLAGS` variable) are as follows:

- `-std=c11`: Use C11.
- `-O2`: Enable optimizations to generate efficient machine code.
- `-g3`: Output the maximum amount of debug information.
- `-Wall`: Enable major warnings.
- `-Wextra`: Enable additional warnings.
- `--target=riscv32`: Compile for 32-bit RISC-V.
- `-ffreestanding`: Do not use the standard library of the host environment (development environment).
- `-nostdlib`: Do not link the standard library.
- `-Wl,-Tkernel.ld`: Specify the linker script.
- `-Wl,-Map=kernel.map`: Output a map file (linker allocation result).

`-Wl,` means passing options to the linker (LLD) instead of the C compiler. clang handles invoking both the compiler and the linker collectively.

## First Kernel Debugging

When you run `run.sh`, the kernel just loops indefinitely, so there will be no change in the display. It's common in OS development to not know if the OS is running correctly. This is where QEMU's debugging features come into play. They are especially useful when there is no functionality to output text, as is the case now.

Open the QEMU monitor and execute the `info registers` command. This will display the current values of the registers as shown below.

```plain
QEMU 8.0.2 monitor - type 'help' for more information
(qemu) info registers

CPU#0
 V      =   0
 pc       80200014  ← Address of the instruction to be executed (Program Counter)
 ...
 x0/zero  00000000 x1/ra    8000a084 x2/sp    80220018 x3/gp    00000000  ← Values of each general-purpose register
 x4/tp    80033000 x5/t0    00000001 x6/t1    00000002 x7/t2    00000000
 x8/s0    80032f50 x9/s1    00000001 x10/a0   80220018 x11/a1   87e00000
 x12/a2   00000007 x13/a3   00000019 x14/a4   00000000 x15/a5   00000001
 x16/a6   00000001 x17/a7   00000005 x18/s2   80200000 x19/s3   00000000
 x20/s4   87e00000 x21/s5   00000000 x22/s6   80006800 x23/s7   8001c020
 x24/s8   00002000 x25/s9   8002b4e4 x26/s10  00000000 x27/s11  00000000
 x28/t3   616d6569 x29/t4   8001a5a1 x30/t5   000000b4 x31/t6   00000000
```

> [!TIP]
>
> The exact values may differ depending on the versions of clang and QEMU.

`pc 80200014` indicates the current Program Counter, the address of the instruction being executed. Let's use the disassembler (`llvm-objdump`) to check the content of `kernel.c` from the CPU's perspective.

```plain
$ llvm-objdump -d kernel.elf

kernel.elf:     file format elf32-littleriscv

Disassembly of section .text:

80200000 <boot>:  ← boot function
80200000: 37 05 22 80   lui     a0, 524832
80200004: 13 05 85 01   addi    a0, a0, 24
80200008: 2a 81         mv      sp, a0
8020000a: 6f 00 60 00   j       0x80200010 <kernel_main>
8020000e: 00 00         unimp

80200010 <kernel_main>:  ← kernel_main function
80200010: 73 00 50 10   wfi
80200014: f5 bf         j       0x80200010 <kernel_main>  ← Program Counter is here
```

From left to right: address, hexadecimal dump of machine code, and disassembled instructions. `pc 80200014` means the currently executed instruction is `j 0x80200010`. This confirms that QEMU has correctly reached the `kernel_main` function.

Let's also check if the stack pointer (sp register) is set to the value of `__stack_top` defined in the linker script. The register dump shows `x2/sp 80220018`. To see where the linker placed `__stack_top`, check `kernel.map`.

```plain
     VMA      LMA     Size Align Out     In      Symbol
       0        0 80200000     1 . = 0x80200000
80200000 80200000       16     4 .text
...
80200016 80200016        2     1 . = ALIGN ( 4 )
80200018 80200018    20000     1 . += 128 * 1024
80220018 80220018        0     1 __stack_top = .
```

You can also check the addresses of each function and variable using `llvm-nm`.

You can also check the addresses of each function and variable using `llvm-nm`.

```plain
$ llvm-nm kernel.elf
80200010 t .LBB0_1
00000000 N .Lline_table_start0
80220018 T __stack_top
80200000 T boot
80200010 T kernel_main
```

The first column is the address where they are placed (VMA). You can see that `__stack_top` is placed at `0x80220018`. This confirms that the stack pointer is correctly set in the `boot` function.

As execution progresses, the results of `info registers` will change. If you want to temporarily stop the process, you can use the `stop` command.

```plain
(qemu) stop             ← The process stops
(qemu) info registers   ← You can observe the state at the stop
(qemu) cont             ← The process resumes
```
