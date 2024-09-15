---
title: Application
layout: chapter
lang: en
---

In this chapter, we'll step away from the kernel and look at the first userland program and how to build it.

## Memory layout

In the previous chapter, we implemented independent virtual address spaces for each process using the paging mechanism. In this chapter, we'll consider where to place the application in the virtual address space.

Let's create a new linker script (`user.ld`) that defines where to place the application's executable file:


```plain:user.ld
ENTRY(start)

SECTIONS {
    . = 0x1000000;

    .text :{
        KEEP(*(.text.start));
        *(.text .text.*);
    }

    .rodata : ALIGN(4) {
        *(.rodata .rodata.*);
    }

    .data : ALIGN(4) {
        *(.data .data.*);
    }

    .bss : ALIGN(4) {
        *(.bss .bss.* .sbss .sbss.*);

        . = ALIGN(16);
        . += 64 * 1024; /* 64KB */
        __stack_top = .;

       ASSERT(. < 0x1800000, "too large executable");
    }
}
```

We'll place the application's data in an area that doesn't overlap with kernel addresses (between `0x1000000` and `0x1800000`). It looks pretty much the same as the kernel's linker script, isn't it?

The `ASSERT` appearing here will cause the linking process (the clang command in this book) to fail if the condition in the first argument is not met. Here, we're checking that the end of the `.bss` section, which is the end of the application, does not exceed `0x1800000`. This is to ensure that the executable file doesn't accidentally become too large.

## Userland library

Next, let's create a library for userland. For now, we'll just write the processes necessary for launching the application.

```c:user.c
#include "user.h"

extern char __stack_top[];

__attribute__((noreturn)) void exit(void) {
    for (;;);
}

void putchar(char c) {
    /* TODO */
}

__attribute__((section(".text.start")))
__attribute__((naked))
void start(void) {
    __asm__ __volatile__(
        "mv sp, %[stack_top]\n"
        "call main\n"
        "call exit\n" ::[stack_top] "r"(__stack_top));
}
```

The execution of the application starts from the `start` function. Similar to the kernel's boot process, it sets up the stack pointer and calls the application's `main` function.

We'll also prepare an `exit` function to terminate the application. However, for now, we'll just have it perform an infinite loop.

We'll also define the `putchar` function that the `printf` function in `common.c` refers to. We'll implement this later.

Unlike the kernel's initialization process, we don't clear the `.bss` section with zeros (zero clearing). This is because the kernel guarantees that it has already filled it with zeros (in the `alloc_pages` function).

> [!TIP]
>
> Zero clearing is a process that's also done in practical OSes. If you don't fill it with zeros, information from other processes that previously used that memory area might remain. It would be problematic if sensitive information like passwords were left behind.

Also, let's prepare a header file (`user.h`) for the userland library:

```c:user.h
#pragma once
#include "common.h"

__attribute__((noreturn)) void exit(void);
void putchar(char ch);
```

## First application

We'll prepare the following as our first application (`shell.c`). Displaying characters requires some extra steps, so for now, we'll just have it perform an infinite loop.

```c:shell.c
#include "user.h"

void main(void) {
    for (;;);
}
```

## Building the application

Lastly, here's the process for building the application:

```bash:run.sh {1,3-6,10}
OBJCOPY=/opt/homebrew/opt/llvm/bin/llvm-objcopy

# Build the shell (application)
$CC $CFLAGS -Wl,-Tuser.ld -Wl,-Map=shell.map -o shell.elf shell.c user.c common.c
$OBJCOPY --set-section-flags .bss=alloc,contents -O binary shell.elf shell.bin
$OBJCOPY -Ibinary -Oelf32-littleriscv shell.bin shell.bin.o

# Build the kernel
$CC $CFLAGS -Wl,-Tkernel.ld -Wl,-Map=kernel.map -o kernel.elf \
    kernel.c common.c shell.bin.o
```

The first `$CC` call is the same as in the kernel, where clang performs both compilation and linking in one step.

The first `$OBJCOPY` command converts the built executable file (in ELF format) to raw binary format. First, let's explain what a raw binary is: it contains the actual content that will be expanded in memory from the base address (in this case, 0x1000000). The OS can deploy the application in memory simply by copying the contents of the raw binary. Common OSes use formats like ELF, where the destination definition and memory data are separate, but in this book, we'll use raw binary for simplicity.

The second `$OBJCOPY` command converts the raw binary execution image into a format that can be embedded in C language. Let's take a look at what's inside using the `llvm-nm` command:

```plain
$ llvm-nm shell.bin.o
00010260 D _binary_shell_bin_end
00010260 A _binary_shell_bin_size
00000000 D _binary_shell_bin_start
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

This program outputs the file size of `shell.bin` and the first byte of its contents. In other words, you can treat the `_binary_shell_bin_start` variable as if it contains the file contents, like this:

```c
char _binary_shell_bin_start[] = "<shell.bin contents here>";
```

Also, the `_binary_shell_bin_size` variable contains the file size. However, it's used in a slightly unusual way. Let's check with `llvm-nm` again:

```plain
$ llvm-nm shell.bin.o | grep _binary_shell_bin_size
00010454 A _binary_shell_bin_size

$ ls -al shell.bin   ← shell.bin.oではなくshell.binであることに注意
-rwxr-xr-x 1 seiya staff 66644 Oct 24 13:35 shell.bin

$ python3 -c 'print(0x10454)'
66644
```

The first column in the output is the address of the symbol. This `10260` value matches the file size, but this is not a coincidence. Generally, the values of each address in a `.o` file are determined by the linker. However, `_binary_shell_bin_size` is special.

The `A` in the second column indicates that the address of `_binary_shell_bin_size` is a type of symbol (absolute) that is not changed by the linker.
By defining it as an array of an arbitrary type like `char _binary_shell_bin_size[]`, `_binary_shell_bin_size` will be treated as a pointer storing its address. However, since we're embedding the file size as an address here, casting it will result in the file size. This is a common trick (or a dirty hack) that exploits the object file format.

Finally, we've added `shell.bin.o` to the `clang` arguments in the kernel compiling. It embeds the first application's executable into the kernel image.

## Disassemble the executable

In disassembly, we can see that, as defined in the linker script, the `.text.start` section is placed at the beginning of the executable file, with the `start` function located at `0x1000000`:

```plain
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
