# Exception

Exception is a CPU feature that allows the kernel to handle various events, such as invalid memory access (aka. page faults), illegal instructions, and system calls.

Exception is like a hardware-assisted `try-catch` mechanism in C++ or Java. Until CPU encounters the situation where kernel intervention is required, it continues to execute the program. The key difference from `try-catch` is that the kernel can resume the execution from the point where the exception occurred, as if nothing happened. Doesn't it sound like cool CPU feature?

Exception can also be triggered in kernel mode and mostly they are fatal kernel bugs. If QEMU resets unexpectedly or the kernel does not work as expected, it's likely that an exception occurred. I recommend to implement an exception handler early to crash gracefully with a kernel panic. It's similar to adding an unhandled rejection handler as the first step in JavaScript development.

## Life of an exception

In RISC-V, an exception will be handled as follows:

1. CPU checks the `medeleg` register to determine which operation mode should handle the exception. In our case, OpenSBI has already configured to handle U-Mode/S-mode exceptions in S-Mode's handler.
2. CPU saves its state (registers) into various CSRs (see below).
3. The value of the `stvec` register is set to the program counter, jumping to the kernel's exception handler.
4. The exception handler saves general-purpose registers (i.e. the program state), and handles the exception.
5. Once it's done, the exception handler restores the saved execution state and calls the `sret` instruction to resume execution from the point where the exception occurred.

The CSRs updated in step 2 are mainly as follows. The kernel's exception determines necessary actions based on the CSRs:

| Register Name | Content                                                                                                                                         |
| ------------- | ----------------------------------------------------------------------------------------------------------------------------------------------- |
| `scause`      | Type of exception. The kernel reads this to identify the type of exception.                                                                     |
| `stval`       | Additional information about the exception (e.g., memory address that caused the exception). Depends on the type of exception. |
| `sepc`        | Program counter at the point where the exception occurred.                                                                                       |
| `sstatus`     | Operation mode (U-Mode/S-Mode) when the exception has occurred.                                                                                        |

## Exception Handler

Now let's write your first exception handler! Here's the entry point of the exception handler to be registered in the `stvec` register:

```c [kernel.c]
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

Here are some key points:

- `sscratch` register is used as a temporary storage to save the stack pointer at the time of exception occurrence, which is later restored.
- Floating-point registers are not used within the kernel, and thus there's no need to save them here. Generally, they are saved and restored during thread switching.
- The stack pointer is set in the `a0` register, and the `handle_trap` function is called. At this point, the address pointed to by the stack pointer contains register values stored in the same structure as the `trap_frame` structure described later.
- Adding `__attribute__((aligned(4)))` aligns the function's starting address to a 4-byte boundary. This is because the `stvec` register not only holds the address of the exception handler but also has flags representing the mode in its lower 2 bits.

> [!NOTE]
>
> The entry point of exception handlers is one of most critical and error-prone parts of the kernel. Reading the code closely, you'll notice that *original* values of general-purpose registers are saved onto the stack, even `sp` by using `sscratch`.
>
> If you accidentally overwrite `a0` register, it can lead to hard-to-debug problems like "local variable values change for no apparent reason". Save the program state perfectly not to spend your precious Saturday night debugging!

In the entry point, the following `handle_trap` function is called to handle the exception in our favorite C language:

```c [kernel.c]
void handle_trap(struct trap_frame *f) {
    uint32_t scause = READ_CSR(scause);
    uint32_t stval = READ_CSR(stval);
    uint32_t user_pc = READ_CSR(sepc);

    PANIC("unexpected trap scause=%x, stval=%x, sepc=%x\n", scause, stval, user_pc);
}
```

It reads why the exception has occurred, and triggers a kernel panic for debugging purposes.

Let's define the various macros used here in `kernel.h`:

```c [kernel.h]
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

The `trap_frame` struct represents the program state saved in `kernel_entry`. `READ_CSR` and `WRITE_CSR` macros are convenient macros for reading and writing CSR registers.

The last thing we need to do is to tell the CPU where the exception handler is located. It's done by setting the `stvec` register in the `kernel_main` function:

```c [kernel.c] {4-5}
void kernel_main(void) {
    memset(__bss, 0, (size_t) __bss_end - (size_t) __bss);

    WRITE_CSR(stvec, (uint32_t) kernel_entry); // new
    __asm__ __volatile__("unimp"); // new
```

In addition to setting the `stvec` register, it executes `unimp` instruction. it's a pseudo instruction which triggers an illegal instruction exception.

> [!NOTE]
>
> **`unimp` is a "pseudo" instruction**.
>
> According to [RISC-V Assembly Programmer's Manual](https://github.com/riscv-non-isa/riscv-asm-manual/blob/main/src/asm-manual.adoc#instruction-aliases), the assembler translates `unimp` to the following instruction:
>
> ```
> csrrw x0, cycle, x0
> ```
>
> This reads and writes the `cycle` register into `x0`. Since `cycle` is a read-only register, CPU determines that the instruction is invalid and triggers an illegal instruction exception.

## Let's try it

Let's try running it and confirm that the exception handler is called:

```
$ ./run.sh
Hello World!
PANIC: kernel.c:47: unexpected trap scause=00000002, stval=ffffff84, sepc=8020015e
```

According to the specification, when the value of `scause` is 2, it indicates an "Illegal instruction," meaning that program tried to execute an invalid instruction. This is precisely the expected behavior of the `unimp` instruction!

Let's also check where the value of `sepc` is pointing. If it's pointing to the line where the `unimp` instruction is called,  everything is working correctly:

```
$ llvm-addr2line -e kernel.elf 8020015e
/Users/seiya/os-from-scratch/kernel.c:129
```
