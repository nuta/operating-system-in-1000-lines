---
title: Exception
layout: chapter
lang: en
---

> [!NOTE]
>
> **Translation of this English version is in progress.**

An exception is a mechanism that switches processing to a program (exception handler) pre-set by the OS when a "program execution cannot continue" state occurs, such as invalid memory access (e.g., null pointer dereference).

Exception occurrences can be confirmed in QEMU's debug log (`-d` option), but reading through lengthy logs is tedious, and it's not easy for beginners to notice when QEMU resets due to an exception. Therefore, it's recommended to implement an exception handler early on that outputs the exception occurrence location and triggers a kernel panic. For those familiar with JavaScript, this might feel similar to "adding an Unhandled Rejection handler as a first step".

## Exception handling flow

When an exception occurs in RISC-V, the process follows this flow:

1. The CPU checks the `medeleg` register to determine which operation mode should handle the exception. In this book, OpenSBI is configured so that if major exceptions occur in either U-Mode (userland) or S-Mode (kernel), they are handled in S-Mode.
2. The CPU state at the time of the exception is saved to various CSRs.
3. The value of the `stvec` register is set to the program counter, jumping to the kernel's exception handling program (exception handler).
4. The exception handler cleverly uses the `sscratch` register (which the kernel can freely use) to save the values of general-purpose registers (i.e., the execution state at the time of the exception), and performs processing according to the type of exception.
5. After completing exception processing, it restores the saved execution state and calls the `sret` instruction to resume execution from the point where the exception occurred.

The CSRs updated in step 2 are mainly as follows. The kernel's exception handler uses this information to determine necessary actions, and to save and restore the state at the time of the exception.

| Register Name | Content                                                                                                                                         |
| ------------- | ----------------------------------------------------------------------------------------------------------------------------------------------- |
| `scause`      | Type of exception. The kernel reads this to identify the type of exception.                                                                     |
| `stval`       | Additional information about the exception (e.g., memory address that caused the exception). Specific content depends on the type of exception. |
| `sepc`        | Program counter at the point where the exception occurred                                                                                       |
| `sstatus`     | Operation mode (U-Mode/S-Mode) at the time of exception                                                                                         |

The most important thing to be careful about in implementing the kernel's exception handler is correctly saving and restoring the state at the time of the exception. For example, if you accidentally overwrite the a0 register, it can lead to hard-to-debug problems like "local variable values change for no apparent reason".

## Exception Handler

Now that we're prepared, let's receive an exception. First, here's the entry point that gets executed first. We'll set the address of this `kernel_entry` function to the `stvec` register later:

```c:kernel.c
__attribute__((naked))
__attribute__((aligned(4)))
void kernel_entry(void) {
    __asm__ __volatile__(
        "csrw sscratch, sp\n"
        "addi sp, sp, -4 * 31\n"
        "sw ra,  4 * 0(sp)\n"
        "sw gp,  4 * 1(sp)\n"
        "sw tp,  4 * 2(sp)\n"
        "sw t0,  4 * 3(sp)\n"
        "sw t1,  4 * 4(sp)\n"
        "sw t2,  4 * 5(sp)\n"
        "sw t3,  4 * 6(sp)\n"
        "sw t4,  4 * 7(sp)\n"
        "sw t5,  4 * 8(sp)\n"
        "sw t6,  4 * 9(sp)\n"
        "sw a0,  4 * 10(sp)\n"
        "sw a1,  4 * 11(sp)\n"
        "sw a2,  4 * 12(sp)\n"
        "sw a3,  4 * 13(sp)\n"
        "sw a4,  4 * 14(sp)\n"
        "sw a5,  4 * 15(sp)\n"
        "sw a6,  4 * 16(sp)\n"
        "sw a7,  4 * 17(sp)\n"
        "sw s0,  4 * 18(sp)\n"
        "sw s1,  4 * 19(sp)\n"
        "sw s2,  4 * 20(sp)\n"
        "sw s3,  4 * 21(sp)\n"
        "sw s4,  4 * 22(sp)\n"
        "sw s5,  4 * 23(sp)\n"
        "sw s6,  4 * 24(sp)\n"
        "sw s7,  4 * 25(sp)\n"
        "sw s8,  4 * 26(sp)\n"
        "sw s9,  4 * 27(sp)\n"
        "sw s10, 4 * 28(sp)\n"
        "sw s11, 4 * 29(sp)\n"

        "csrr a0, sscratch\n"
        "sw a0, 4 * 30(sp)\n"

        "mv a0, sp\n"
        "call handle_trap\n"

        "lw ra,  4 * 0(sp)\n"
        "lw gp,  4 * 1(sp)\n"
        "lw tp,  4 * 2(sp)\n"
        "lw t0,  4 * 3(sp)\n"
        "lw t1,  4 * 4(sp)\n"
        "lw t2,  4 * 5(sp)\n"
        "lw t3,  4 * 6(sp)\n"
        "lw t4,  4 * 7(sp)\n"
        "lw t5,  4 * 8(sp)\n"
        "lw t6,  4 * 9(sp)\n"
        "lw a0,  4 * 10(sp)\n"
        "lw a1,  4 * 11(sp)\n"
        "lw a2,  4 * 12(sp)\n"
        "lw a3,  4 * 13(sp)\n"
        "lw a4,  4 * 14(sp)\n"
        "lw a5,  4 * 15(sp)\n"
        "lw a6,  4 * 16(sp)\n"
        "lw a7,  4 * 17(sp)\n"
        "lw s0,  4 * 18(sp)\n"
        "lw s1,  4 * 19(sp)\n"
        "lw s2,  4 * 20(sp)\n"
        "lw s3,  4 * 21(sp)\n"
        "lw s4,  4 * 22(sp)\n"
        "lw s5,  4 * 23(sp)\n"
        "lw s6,  4 * 24(sp)\n"
        "lw s7,  4 * 25(sp)\n"
        "lw s8,  4 * 26(sp)\n"
        "lw s9,  4 * 27(sp)\n"
        "lw s10, 4 * 28(sp)\n"
        "lw s11, 4 * 29(sp)\n"
        "lw sp,  4 * 30(sp)\n"
        "sret\n"
    );
}
```

The key points of implementation are as follows:

- The `sscratch` register is used to save the stack pointer at the time of exception occurrence, which is later restored. In this way, the `sscratch` register can be used for temporary storage.
- Floating-point registers are not used within the kernel, so there's no need to save them here. Generally, they are saved and restored during thread switching.
- The stack pointer is set in the `a0` register, and the `handle_trap` function is called. At this point, the address pointed to by the stack pointer contains register values stored in the same structure as the `trap_frame` structure described later.
- Adding `__attribute__((aligned(4)))` aligns the function's starting address to a 4-byte boundary. This is because the `stvec` register not only holds the address of the exception handler but also has flags representing the mode in its lower 2 bits.

The function called within this function is the following `handle_trap` function:

```c:kernel.c
void handle_trap(struct trap_frame *f) {
    uint32_t scause = READ_CSR(scause);
    uint32_t stval = READ_CSR(stval);
    uint32_t user_pc = READ_CSR(sepc);

    PANIC("unexpected trap scause=%x, stval=%x, sepc=%x\n", scause, stval, user_pc);
}
```

It retrieves the cause of the exception (`scause`) and the program counter at the time of exception (`sepc`), and triggers a kernel panic for debugging purposes. Let's define the various macros used here in `kernel.h` as follows:

```c:kernel.h
#include "common.h"

struct trap_frame {
    uint32_t ra;
    uint32_t gp;
    uint32_t tp;
    uint32_t t0;
    uint32_t t1;
    uint32_t t2;
    uint32_t t3;
    uint32_t t4;
    uint32_t t5;
    uint32_t t6;
    uint32_t a0;
    uint32_t a1;
    uint32_t a2;
    uint32_t a3;
    uint32_t a4;
    uint32_t a5;
    uint32_t a6;
    uint32_t a7;
    uint32_t s0;
    uint32_t s1;
    uint32_t s2;
    uint32_t s3;
    uint32_t s4;
    uint32_t s5;
    uint32_t s6;
    uint32_t s7;
    uint32_t s8;
    uint32_t s9;
    uint32_t s10;
    uint32_t s11;
    uint32_t sp;
} __attribute__((packed));

#define READ_CSR(reg)                                                          \
    ({                                                                         \
        unsigned long __tmp;                                                   \
        __asm__ __volatile__("csrr %0, " #reg : "=r"(__tmp));                  \
        __tmp;                                                                 \
    })

#define WRITE_CSR(reg, value)                                                  \
    do {                                                                       \
        uint32_t __tmp = (value);                                              \
        __asm__ __volatile__("csrw " #reg ", %0" ::"r"(__tmp));                \
    } while (0)
```

The `trap_frame` structure represents the structure of the original execution state stacked on the stack. The `READ_CSR` macro and `WRITE_CSR` macro are convenient macros for reading and writing CSR registers.

The last thing we need to do is to tell the CPU where the exception handler is located. Let's write the address of the exception handler to the `stvec` register in the `main` function as follows:

```c:kernel.c {4-5}
void kernel_main(void) {
    memset(__bss, 0, (size_t) __bss_end - (size_t) __bss);

    WRITE_CSR(stvec, (uint32_t) kernel_entry);
    __asm__ __volatile__("unimp"); // 無効な命令
```

In addition to setting the `stvec` register, we will execute the `unimp` instruction. This instruction is a fictitious instruction that doesn't exist in the RISC-V instruction set. It's a somewhat useful compiler feature that outputs a binary sequence that the CPU will recognize as an invalid machine code. (Reference: [Specific implementation of the unimp instruction](https://github.com/llvm/llvm-project/commit/26403def69f72c7938889c1902d62121095b93d7#diff-1d077b8beaff531e8d78ba5bb21c368714f270f1b13ba47bb23d5ad2a5d1f01bR410-R414))

Let's run it and confirm that the exception handler is called:

```plain
$ ./run.sh
Hello World!
PANIC: kernel.c:47: unexpected trap scause=00000002, stval=ffffff84, sepc=8020015e
```

According to the specification, when the value of `scause` is 2, it indicates an "Illegal instruction," meaning an attempt was made to execute an invalid instruction. This is precisely the expected behavior of the `unimp` instruction.

Let's also check where the value of `sepc` is pointing. If it's pointing to the line where the `unimp` instruction is called, then everything is working correctly:

```plain
$ llvm-addr2line -e kernel.elf 8020015e
/Users/seiya/os-from-scratch/kernel.c:129
```
