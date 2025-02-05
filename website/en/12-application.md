# Application

In this chapter, we'll prepare the first application executable to run on our kernel.

## Memory layout

In the previous chapter, we implemented isolated virtual address spaces using the paging mechanism. Let's  consider where to place the application in the address space.

Create a new linker script (`user.ld`) that defines where to place the application in memory:

```ld [user.ld]
ENTRY(start)

SECTIONS {
    . = 0x1000000;

    /* machine code */
    .text :{
        KEEP(*(.text.start));
        *(.text .text.*);
    }

    /* read-only data */
    .rodata : ALIGN(4) {
        *(.rodata .rodata.*);
    }

    /* data with initial values */
    .data : ALIGN(4) {
        *(.data .data.*);
    }

    /* data that should be zero-filled at startup */
    .bss : ALIGN(4) {
        *(.bss .bss.* .sbss .sbss.*);

        . = ALIGN(16);
        . += 64 * 1024; /* 64KB */
        __stack_top = .;

       ASSERT(. < 0x1800000, "too large executable");
    }
}
```

It looks pretty much the same as the kernel's linker script, isn't it?  The key difference is the base address (`0x1000000`) so that the application doesn't overlap with the kernel's address space.

`ASSERT` is an assertion which aborts the linker if the condition in the first argument is not met. Here, it ensures that the end of the `.bss` section, which is the end of the application memory, does not exceed `0x1800000`. This is to ensure that the executable file doesn't accidentally become too large.

## Userland library

Next, let's create a library for userland programs. For simplicity, we'll start with a minimal feature set to start the application:

```c [user.c]
#include "user.h"

extern char __stack_top[];

__attribute__((noreturn)) void exit(void) {
    for (;;);
}

void putchar(char ch) {
    /* TODO */
}

__attribute__((section(".text.start")))
__attribute__((naked))
void start(void) {
    __asm__ __volatile__(
        "mv sp, %[stack_top] \n"
        "call main           \n"
        "call exit           \n"
        :: [stack_top] "r" (__stack_top)
    );
}
```

The execution of the application starts from the `start` function. Similar to the kernel's boot process, it sets up the stack pointer and calls the application's `main` function.

We prepare the `exit` function to terminate the application. However, for now, we'll just have it perform an infinite loop.

Also, we define the `putchar` function that the `printf` function in `common.c` refers to. We'll implement this later.

Unlike the kernel's initialization process, we don't clear the `.bss` section with zeros. This is because the kernel guarantees that it has already filled it with zeros (in the `alloc_pages` function).

> [!TIP]
>
> Allocated memory regions are already filled with zeros in typical operating systems too. Otherwise, the memory may contain sensitive information (e.g. credentials) from other processes, and it could lead to a critical security issue.

Lastly, prepare a header file (`user.h`) for the userland library:

```c [user.h]
#pragma once
#include "common.h"

__attribute__((noreturn)) void exit(void);
void putchar(char ch);
```

## First application

It's time to create the first application! Unfortunately, we still don't have a way to display characters, we can't start with a "Hello, World!" program. Instead, we'll create a simple infinite loop:

```c [shell.c]
#include "user.h"

void main(void) {
    for (;;);
}
```

## Building the application

Applications will be built separately from the kernel. Let's create a new script (`run.sh`) to build the application:

```bash [run.sh] {1,3-6,10}
OBJCOPY=/opt/homebrew/opt/llvm/bin/llvm-objcopy

# Build the shell (application)
$CC $CFLAGS -Wl,-Tuser.ld -Wl,-Map=shell.map -o shell.elf shell.c user.c common.c
$OBJCOPY --set-section-flags .bss=alloc,contents -O binary shell.elf shell.bin
$OBJCOPY -Ibinary -Oelf32-littleriscv shell.bin shell.bin.o

# Build the kernel
$CC $CFLAGS -Wl,-Tkernel.ld -Wl,-Map=kernel.map -o kernel.elf \
    kernel.c common.c shell.bin.o
```

The first `$CC` call is very similar to the kernel build script. Compile C files and link them with the `user.ld` linker script.

The first `$OBJCOPY` command converts the executable file (in ELF format) to raw binary format. A raw binary is the actual content that will be expanded in memory from the base address (in this case, `0x1000000`). The OS can prepare the application in memory simply by copying the contents of the raw binary. Common OSes use formats like ELF, where memory contents and their mapping information are separate, but in this book, we'll use raw binary for simplicity.

The second `$OBJCOPY` command converts the raw binary execution image into a format that can be embedded in C language. Let's take a look at what's inside using the `llvm-nm` command:

```
$ llvm-nm shell.bin.o
00000000 D _binary_shell_bin_start
00010260 D _binary_shell_bin_end
00010260 A _binary_shell_bin_size
```

The prefix `_binary_` is followed by the file name, and then `start`, `end`, and `size`. These are symbols that indicate the beginning, end, and size of the execution image, respectively. In practice, they are used as follows:

```c
extern char _binary_shell_bin_start[];
extern char _binary_shell_bin_size[];

void main(void) {
    uint8_t *shell_bin = (uint8_t *) _binary_shell_bin_start;
    printf("shell_bin size = %d\n", (int) _binary_shell_bin_size);
    printf("shell_bin[0] = %x (%d bytes)\n", shell_bin[0]);
}
```

This program outputs the file size of `shell.bin` and the first byte of its contents. In other words, you can treat the `_binary_shell_bin_start` variable as if it contains the file contents, like:

```c
char _binary_shell_bin_start[] = "<shell.bin contents here>";
```

`_binary_shell_bin_size` variable contains the file size. However, it's used in a slightly unusual way. Let's check with `llvm-nm` again:

```
$ llvm-nm shell.bin.o | grep _binary_shell_bin_size
00010454 A _binary_shell_bin_size

$ ls -al shell.bin   ← note: do not confuse with shell.bin.o!
-rwxr-xr-x 1 seiya staff 66644 Oct 24 13:35 shell.bin

$ python3 -c 'print(0x10454)'
66644
```

The first column in the `llvm-nm` output is the *address* of the symbol. This `10454` hexadecimal number matches the file size, but this is not a coincidence. Generally, the values of each address in a `.o` file are determined by the linker. However, `_binary_shell_bin_size` is special.

The `A` in the second column indicates that the address of `_binary_shell_bin_size` is a type of symbol (absolute) that should not be changed by the linker. That is, it embeds the file size as an address.

By defining it as an array of an arbitrary type like `char _binary_shell_bin_size[]`, `_binary_shell_bin_size` will be treated as a pointer storing its *address*. However, since we're embedding the file size as an address here, casting it will result in the file size. This is a common trick (or a dirty hack) that exploits the object file format.

Lastly, we've added `shell.bin.o` to the `clang` arguments in the kernel compiling. It embeds the first application's executable into the kernel image.

## Disassemble the executable

In disassembly, we can see that the `.text.start` section is placed at the beginning of the executable file. The `start` function should be placed at `0x1000000` as follows:

```
$ llvm-objdump -d shell.elf

shell.elf:	file format elf32-littleriscv

Disassembly of section .text:

01000000 <start>:
 1000000: 37 05 01 01  	lui	a0, 4112
 1000004: 13 05 05 26  	addi	a0, a0, 608
 1000008: 2a 81        	mv	sp, a0
 100000a: 19 20        	jal	0x1000010 <main>
 100000c: 29 20        	jal	0x1000016 <exit>
 100000e: 00 00        	unimp

01000010 <main>:
 1000010: 01 a0        	j	0x1000010 <main>
 1000012: 00 00        	unimp

01000016 <exit>:
 1000016: 01 a0        	j	0x1000016 <exit>
```
