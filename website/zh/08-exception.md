---
title: 异常
---

# 异常

异常（Exception） 是一个 CPU 功能，允许内核处理各种事件，如无效内存访问（也就是页面错误）、非法指令和系统调用。

Exception 类似于 C++ 或 Java 中的硬件辅助 `try-catch` 机制。在 CPU 遇到需要内核干预的情况之前，它会继续执行程序。与 `try-catch` 的主要区别在于，内核可以从发生异常的地方恢复执行，就像什么都没发生过一样。这听起来是不是很酷的 CPU 功能？

Exception 也可以在内核模式下触发，它们大多是致命的内核错误。如果 QEMU 意外重置或内核无法按预期工作，很可能是发生了异常。我建议尽早实现异常处理程序以优雅地崩溃并显示内核错误。这类似于在 JavaScript 开发中首先添加未处理的拒绝处理程序。

## 异常的生命周期

在 RISC-V 中，异常将按以下方式处理：

1. CPU 检查 `medeleg` 寄存器以确定哪个操作模式应该处理异常。在我们的情况下，OpenSBI 已经配置为在 S-Mode 的处理程序中处理 U-Mode/S-mode 异常。
2. CPU 将其状态（寄存器）保存到各种 CSR 中（见下文）。
3. `stvec` 寄存器的值被设置为程序计数器，跳转到内核的异常处理程序。
4. 异常处理程序保存通用寄存器（即程序状态），并处理异常。
5. 完成后，异常处理程序恢复保存的执行状态并调用 `sret` 指令，从发生异常的地方恢复执行。

步骤 2 中更新的 CSR 主要如下。内核的异常根据 CSR 确定必要的操作：

| 寄存器名称 | 内容 |
| ------------- | ----------------------------------------------------------------------------------------------------------------------------------------------- |
| `scause` | 异常类型。内核读取此项以识别异常类型。 |
| `stval` | 关于异常的附加信息（例如，导致异常的内存地址）。取决于异常类型。 |
| `sepc` | 发生异常时的程序计数器。 |
| `sstatus` | 发生异常时的操作模式（U-Mode/S-Mode）。 |

## 异常处理程序

现在来编写你的第一个异常处理程序！以下是要在 `stvec` 寄存器中注册的异常处理程序的入口点：

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

这里有一些关键点：

- `sscratch` 寄存器用作临时存储，用于保存异常发生时的堆栈指针，后续会恢复。
- 浮点寄存器在内核中不使用，因此这里不需要保存它们。通常，它们在线程切换期间保存和恢复。
- 堆栈指针被设置在 `a0` 寄存器中，并调用 `handle_trap` 函数。此时，堆栈指针指向的地址包含按照后面描述的 `trap_frame` 结构存储的寄存器值。
- 添加 `__attribute__((aligned(4)))` 将函数的起始地址对齐到 4 字节边界。这是因为 `stvec` 寄存器不仅保存异常处理程序的地址，而且在其低 2 位中有表示模式的标志。

> [!NOTE]
>
> 异常处理程序的入口点是内核中最关键和最容易出错的部分之一。仔细阅读代码，你会注意到通用寄存器的*原始*值被保存到堆栈中，甚至使用 `sscratch` 保存 `sp`。
>
> 如果你不小心覆盖了 `a0` 寄存器，可能会导致难以调试的问题，比如“局部变量值无缘无故地改变”。完美地保存程序状态，不要把你宝贵的周六晚上浪费在调试上！

在入口点中，调用以下 `handle_trap` 函数，以便在我们喜欢的 C 语言中处理异常：

```c [kernel.c]
void handle_trap(struct trap_frame *f) {
    uint32_t scause = READ_CSR(scause);
    uint32_t stval = READ_CSR(stval);
    uint32_t user_pc = READ_CSR(sepc);

    PANIC("unexpected trap scause=%x, stval=%x, sepc=%x\n", scause, stval, user_pc);
}
```

它读取异常发生的原因，并为调试目的触发内核恐慌。

让我们在 `kernel.h` 中定义这里使用的各种宏：

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

`trap_frame` 结构体表示在 `kernel_entry` 中保存的程序状态。`READ_CSR` 和 `WRITE_CSR` 宏是用于读写 CSR 寄存器的便捷宏。

我们需要做的最后一件事是告诉 CPU 异常处理程序的位置。这是通过在 `kernel_main` 函数中设置 `stvec` 寄存器来完成的：

```c [kernel.c] {4-5}
void kernel_main(void) {
    memset(__bss, 0, (size_t) __bss_end - (size_t) __bss);

    WRITE_CSR(stvec, (uint32_t) kernel_entry); // 新增
    __asm__ __volatile__("unimp"); // 新增
```

除了设置 `stvec` 寄存器外，它还执行 `unimp` 指令。这是一个会触发非法指令异常的伪指令。

> [!NOTE]
>
> **`unimp` 是一个"伪"指令**。
>
> 根据 [RISC-V Assembly Programmer's Manual](https://github.com/riscv-non-isa/riscv-asm-manual/blob/main/src/asm-manual.adoc#instruction-aliases)，汇编器将 `unimp` 转换为以下指令：
>
> ```
> csrrw x0, cycle, x0
> ```
>
> 这会将 `cycle` 寄存器读取并写入 `x0`。由于 `cycle` 是只读寄存器，CPU 判断该指令无效并触发非法指令异常。

## 运行试试

让我们运行它并确认异常处理程序被调用：

```
$ ./run.sh
Hello World!
PANIC: kernel.c:47: unexpected trap scause=00000002, stval=ffffff84, sepc=8020015e
```

根据规范，当 `scause` 的值为 2 时，表示“非法指令”，意味着程序试图执行无效指令。这正是 `unimp` 指令的预期行为！

让我们也检查一下 `sepc` 的值指向哪里。如果它指向调用 `unimp` 指令的行，那么一切都如预期：

```
$ llvm-addr2line -e kernel.elf 8020015e
/Users/seiya/os-from-scratch/kernel.c:129
```
