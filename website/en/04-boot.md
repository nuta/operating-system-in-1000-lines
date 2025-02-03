# Booting the Kernel

When a computer is turned on, the CPU initializes itself and starts executing the OS. OS initializes the hardware and starts the applications. This process is called "booting".

What happens before the OS starts? In PCs, BIOS (or UEFI in modern PCs) initializes the hardware, displays the splash screen, and loads the OS from the disk. In QEMU `virt` machine, OpenSBI is the equivalent of BIOS/UEFI.

## Supervisor Binary Interface (SBI)

The Supervisor Binary Interface (SBI) is an API for OS kernels, but defines what the firmware (OpenSBI) provides to an OS.

The SBI specification is [published on GitHub](https://github.com/riscv-non-isa/riscv-sbi-doc/releases). It defines useful features such as displaying characters on the debug console (e.g., serial port), reboot/shutdown, and timer settings.

A famous SBI implementation is [OpenSBI](https://github.com/riscv-software-src/opensbi). In QEMU, OpenSBI starts by default, performs hardware-specific initialization, and boots the kernel.

## Let's boot OpenSBI

First, let's see how OpenSBI starts. Create a shell script named `run.sh` as follows:

```
$ touch run.sh
$ chmod +x run.sh
```

```bash [run.sh]
#!/bin/bash
set -xue

# QEMU file path
QEMU=qemu-system-riscv32

# Start QEMU
$QEMU -machine virt -bios default -nographic -serial mon:stdio --no-reboot
```

QEMU takes various options to start the virtual machine. Here are the options used in the script:

- `-machine virt`: Start a `virt` machine. You can check other supported machines with the `-machine '?'` option.
- `-bios default`: Use the default firmware (OpenSBI in this case).
- `-nographic`: Start QEMU without a GUI window.
- `-serial mon:stdio`: Connect QEMU's standard input/output to the virtual machine's serial port. Specifying `mon:` allows switching to the QEMU monitor by pressing <kbd>Ctrl</kbd>+<kbd>A</kbd> then <kbd>C</kbd>.
- `--no-reboot`: If the virtual machine crashes, stop the emulator without rebooting (useful for debugging).

> [!TIP]
>
> In macOS, you can check the path to Homebrew's QEMU with the following command:
>
> ```
> $ ls $(brew --prefix)/bin/qemu-system-riscv32
> /opt/homebrew/bin/qemu-system-riscv32
> ```

Run the script and you will see the following banner:

```
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
```

OpenSBI displays the OpenSBI version, platform name, features, number of HARTs (CPU cores), and more for debugging purposes.

When you press any key, nothing will happen. This is because QEMU's standard input/output is connected to the virtual machine's serial port, and the characters you type are being sent to the OpenSBI. However, no one reads the input characters.

Press <kbd>Ctrl</kbd>+<kbd>A</kbd> then <kbd>C</kbd> to switch to the QEMU debug console (QEMU monitor). You can exit QEMU by `q` command in the monitor:

```
QEMU 8.0.2 monitor - type 'help' for more information
(qemu) q
```

> [!TIP]
>
> <kbd>Ctrl</kbd>+<kbd>A</kbd> has several features besides switching to the QEMU monitor (<kbd>C</kbd> key). For example, pressing the <kbd>X</kbd> key will immediately exit QEMU.
>
> ```
> C-a h    print this help
> C-a x    exit emulator
> C-a s    save disk data back to file (if -snapshot)
> C-a t    toggle console timestamps
> C-a b    send break (magic sysrq)
> C-a c    switch between console and monitor
> C-a C-a  sends C-a
> ```

## Linker script

A linker script is a file which defines the memory layout of executable files. Based on the layout, the linker assigns memory addresses to functions and variables.

Let's create a new file named `kernel.ld`:

```ld [kernel.ld]
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
Here are the key points of the linker script:

- The entry point of the kernel is the `boot` function.
- The base address is `0x80200000`.
- The `.text.boot` section is always placed at the beginning.
- Each section is placed in the order of `.text`, `.rodata`, `.data`, and `.bss`.
- The kernel stack comes after the `.bss` section, and its size is 128KB.

`.text`, `.rodata`, `.data`, and `.bss` sections mentioned here are data areas with specific roles:

| Section   | Description                                                  |
| --------- | ------------------------------------------------------------ |
| `.text`   | This section contains the code of the program.               |
| `.rodata` | This section contains constant data that is read-only.       |
| `.data`   | This section contains read/write data.                       |
| `.bss`    | This section contains read/write data with an initial value of zero. |

Let's take a closer look at the syntax of the linker script. First, `ENTRY(boot)` declares that the `boot` function is the entry point of the program. Then, the placement of each section is defined within the `SECTIONS` block.

The `*(.text .text.*)` directive places the `.text` section and any sections starting with `.text.` from all files (`*`) at that location.

The `.` symbol represents the current address. It automatically increments as data is placed, such as with `*(.text)`. The statement `. += 128 * 1024` means "advance the current address by 128KB". The `ALIGN(4)` directive ensures that the current address is adjusted to a 4-byte boundary.

Finally, `__bss = .` assigns the current address to the symbol `__bss`. In C language, you can refer to a defined symbol using `extern char symbol_name`.

> [!TIP]
>
> Linker scripts offer many convenient features, especially for kernel development. You can find real-world examples on GitHub!


## Minimal kernel

We're now ready to start writing the kernel. Let's start by creating a minimal one! Create a C language source code file named `kernel.c`:

```c [kernel.c]
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
        "mv sp, %[stack_top]\n" // Set the stack pointer
        "j kernel_main\n"       // Jump to the kernel main function
        :
        : [stack_top] "r" (__stack_top) // Pass the stack top address as %[stack_top]
    );
}
```

Let's explore the key points one by one:

### The kernel entry point

The execution of the kernel starts from the `boot` function, which is specified as the entry point in the linker script. In this function, the stack pointer (`sp`) is set to the end address of the stack area defined in the linker script. Then, it jumps to the `kernel_main` function. It's important to note that the stack grows towards zero, meaning it is decremented as it is used. Therefore, the end address (not the start address) of the stack area must be set.

### `boot` function attributes

The `boot` function has two special attributes. The `__attribute__((naked))` attribute instructs the compiler not to generate unnecessary code before and after the function body, such as a return instruction. This ensures that the inline assembly code is the exact function body.

The `boot` function also has the `__attribute__((section(".text.boot")))` attribute, which controls the placement of the function in the linker script. Since OpenSBI simply jumps to `0x80200000` without knowing the entry point, the `boot` function needs to be placed at `0x80200000`.

### `extern char` to get linker script symbols

At the beginning of the file, each symbol defined in the linker script is declared using `extern char`. Here, we are only interested in obtaining the addresses of the symbols, so using `char` type is not that important.

We can also declare it as `extern char __bss;`, but `__bss` alone means *"the value at the 0th byte of the `.bss` section"* instead of *"the start address of the `.bss` section"*. Therefore, it is recommended to add `[]` to ensure that `__bss` returns an address and prevent any careless mistakes.

### `.bss` section initialization

In the `kernel_main` function, the `.bss` section is first initialized to zero using the `memset` function. Although some bootloaders may recognize and zero-clear the `.bss` section, but we initialize it manually just in case. Finally, the function enters an infinite loop and the kernel terminates.

## Let's run!

Add a kernel build command and a new QEMU option (`-kernel kernel.elf`) to `run.sh`:

```bash [run.sh] {6-12,16}
#!/bin/bash
set -xue

QEMU=qemu-system-riscv32

# Path to clang and compiler flags
CC=/opt/homebrew/opt/llvm/bin/clang  # Ubuntu users: use CC=clang
CFLAGS="-std=c11 -O2 -g3 -Wall -Wextra --target=riscv32-unknown-elf -fno-stack-protector -ffreestanding -nostdlib"

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

The specified clang options (`CFLAGS` variable) are as follows:

| Option | Description |
| ------ | ----------- |
| `-std=c11` | Use C11. |
| `-O2` | Enable optimizations to generate efficient machine code. |
| `-g3` | Generate the maximum amount of debug information. |
| `-Wall` | Enable major warnings. |
| `-Wextra` | Enable additional warnings. |
| `--target=riscv32-unknown-elf` | Compile for 32-bit RISC-V. |
| `-ffreestanding` | Do not use the standard library of the host environment (your development environment). |
| `-fno-stack-protector` | Disable unnecessary [stack protection](https://wiki.osdev.org/Stack_Smashing_Protector) to avoid unexpected behavior in stack manipulation (see [#31](https://github.com/nuta/operating-system-in-1000-lines/issues/31#issuecomment-2613219393)). |
| `-nostdlib` | Do not link the standard library. |
| `-Wl,-Tkernel.ld` | Specify the linker script. |
| `-Wl,-Map=kernel.map` | Output a map file (linker allocation result). |

`-Wl,` means passing options to the linker instead of the C compiler. `clang` command does C compilation and executes the linker internally.

## Your first kernel debugging

When you run `run.sh`, the kernel enters an infinite loop. There are no indications that the kernel is running correctly. But don't worry, this is quite common in low-level development! This is where QEMU's debugging features come in.

To get more information about the CPU registers, open the QEMU monitor and execute the `info registers` command:

```
QEMU 8.0.2 monitor - type 'help' for more information
(qemu) info registers

CPU#0
 V      =   0
 pc       80200014  ← Address of the instruction to be executed (Program Counter)
 ...
 x0/zero  00000000 x1/ra    8000a084 x2/sp    80220018 x3/gp    00000000  ← Values of each register
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

`pc 80200014` shows the current program counter, the address of the instruction being executed. Let's use the disassembler (`llvm-objdump`) to narrow down the specific line of code:

```
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
80200014: f5 bf         j       0x80200010 <kernel_main>  ← pc is here
```

Each line corresponds to an instruction. Each column represents:

- The address of the instruction.
- Hexadecimal dump of the machine code.
- Disassembled instructions.

`pc 80200014` means the currently executed instruction is `j 0x80200010`. This confirms that QEMU has correctly reached the `kernel_main` function.

Let's also check if the stack pointer (sp register) is set to the value of `__stack_top` defined in the linker script. The register dump shows `x2/sp 80220018`. To see where the linker placed `__stack_top`, check `kernel.map` file:

```
     VMA      LMA     Size Align Out     In      Symbol
       0        0 80200000     1 . = 0x80200000
80200000 80200000       16     4 .text
...
80200016 80200016        2     1 . = ALIGN ( 4 )
80200018 80200018    20000     1 . += 128 * 1024
80220018 80220018        0     1 __stack_top = .
```

Alternatively, you can also check the addresses of functions/variables using `llvm-nm`:

```
$ llvm-nm kernel.elf
80200010 t .LBB0_1
00000000 N .Lline_table_start0
80220018 T __stack_top
80200000 T boot
80200010 T kernel_main
```

The first column is the address where they are placed (VMA). You can see that `__stack_top` is placed at `0x80220018`. This confirms that the stack pointer is correctly set in the `boot` function. Nice!

As execution progresses, the results of `info registers` will change. If you want to temporarily stop the emulation, you can use the `stop` command in the QEMU monitor:

```
(qemu) stop             ← The process stops
(qemu) info registers   ← You can observe the state at the stop
(qemu) cont             ← The process resumes
```

Now you've successfully written your first kernel!
