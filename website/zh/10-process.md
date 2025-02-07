---
title: 进程
---

# 进程

进程是应用程序的一个实例。每个进程都有其独立的执行上下文和资源，如虚拟地址空间。

> [!NOTE]
>
> 实际的操作系统将执行上下文作为一个称为*“线程”*的独立概念提供。为简单起见，在本书中我们将每个进程视为只有一个线程。

## 进程控制块

以下`process`结构定义了一个进程对象。它也被称为 _"Process Control Block (PCB)"_。

```c [kernel.c]
#define PROCS_MAX 8       // 最大进程数量

#define PROC_UNUSED   0   // 未使用的进程控制结构
#define PROC_RUNNABLE 1   // 可运行的进程

struct process {
    int pid;             // 进程 ID
    int state;           // 进程状态: PROC_UNUSED 或 PROC_RUNNABLE
    vaddr_t sp;          // 栈指针
    uint8_t stack[8192]; // 内核栈
};
```

内核栈包含保存的 CPU 寄存器、返回地址（从何处调用）和局部变量。通过为每个进程准备一个内核栈，我们可以通过保存和恢复 CPU 寄存器并切换栈指针来实现上下文切换。

> [!TIP]
>
> 还有一种称为*“单一内核栈”*的方法。每个 CPU 只有一个栈，而不是为每个进程(或线程)配备一个内核栈。[seL4 采用了这种方法](https://trustworthy.systems/publications/theses_public/05/Warton%3Abe.abstract)。
>
> 这个*“在哪里存储程序上下文”*的问题在 Go 和 Rust 等编程语言的异步运行时中也有讨论。如果你感兴趣，可以搜索*"stackless async"*。

## 上下文切换

切换进程执行上下文称为*“上下文切换”*。以下`switch_context`函数是上下文切换的实现：

```c [kernel.c]
__attribute__((naked)) void switch_context(uint32_t *prev_sp,
                                           uint32_t *next_sp) {
    __asm__ __volatile__(
        // 将被调用者保存寄存器保存到当前进程的栈上
        "addi sp, sp, -13 * 4\n" // 为13个4字节寄存器分配栈空间 
        "sw ra,  0  * 4(sp)\n"   // 仅保存被调用者保存的寄存器
        "sw s0,  1  * 4(sp)\n"
        "sw s1,  2  * 4(sp)\n"
        "sw s2,  3  * 4(sp)\n"
        "sw s3,  4  * 4(sp)\n"
        "sw s4,  5  * 4(sp)\n"
        "sw s5,  6  * 4(sp)\n"
        "sw s6,  7  * 4(sp)\n"
        "sw s7,  8  * 4(sp)\n"
        "sw s8,  9  * 4(sp)\n"
        "sw s9,  10 * 4(sp)\n"
        "sw s10, 11 * 4(sp)\n"
        "sw s11, 12 * 4(sp)\n"

        // 切换栈指针
        "sw sp, (a0)\n"         // *prev_sp = sp;
        "lw sp, (a1)\n"         // 在这里切换栈指针(sp)

        // 从下一个进程的栈中恢复被调用者保存的寄存器
        "lw ra,  0  * 4(sp)\n"  // 仅恢复被调用者保存的寄存器
        "lw s0,  1  * 4(sp)\n"
        "lw s1,  2  * 4(sp)\n"
        "lw s2,  3  * 4(sp)\n"
        "lw s3,  4  * 4(sp)\n"
        "lw s4,  5  * 4(sp)\n"
        "lw s5,  6  * 4(sp)\n"
        "lw s6,  7  * 4(sp)\n"
        "lw s7,  8  * 4(sp)\n"
        "lw s8,  9  * 4(sp)\n"
        "lw s9,  10 * 4(sp)\n"
        "lw s10, 11 * 4(sp)\n"
        "lw s11, 12 * 4(sp)\n"
        "addi sp, sp, 13 * 4\n"  // 我们已从栈中弹出13个4字节寄存器
        "ret\n"
    );
}
```

`switch_context`将被调用者保存的寄存器保存到栈上，切换栈指针，然后从栈中恢复被调用者保存的寄存器。换句话说，执行上下文作为临时局部变量存储在栈上。或者，你也可以将上下文保存在`struct process`中，但这种基于栈的方法不是很简洁吗？

被调用者保存的寄存器是被调用函数在返回前必须恢复的寄存器。在 RISC-V 中，`s0`到`s11`是被调用者保存的寄存器。其他像`a0`这样的寄存器是调用者保存的寄存器，已经由调用者保存在栈上。这就是为什么`switch_context`只处理部分寄存器。

`naked`属性告诉编译器不要生成除内联汇编以外的任何代码。没有这个属性也应该能工作，但当你手动修改栈指针时使用它是个好习惯，可以避免意外行为。

> [!TIP]
>
> 被调用者/调用者保存的寄存器在[调用约定](https://riscv.org/wp-content/uploads/2015/01/riscv-calling.pdf)中定义。编译器根据这个约定生成代码。

接下来，让我们实现进程初始化函数`create_process`。它以入口点为参数，并返回指向创建的`process`结构的指针：

```c
struct process procs[PROCS_MAX]; // 所有进程控制结构

struct process *create_process(uint32_t pc) {
    // 查找未使用的进程控制结构
    struct process *proc = NULL;
    int i;
    for (i = 0; i < PROCS_MAX; i++) {
        if (procs[i].state == PROC_UNUSED) {
            proc = &procs[i];
            break;
        }
    }

    if (!proc)
        PANIC("no free process slots");

    // 设置被调用者保存的寄存器。这些寄存器值将在 switch_context 
    // 中的第一次上下文切换时被恢复。
    uint32_t *sp = (uint32_t *) &proc->stack[sizeof(proc->stack)];
    *--sp = 0;                      // s11
    *--sp = 0;                      // s10
    *--sp = 0;                      // s9
    *--sp = 0;                      // s8
    *--sp = 0;                      // s7
    *--sp = 0;                      // s6
    *--sp = 0;                      // s5
    *--sp = 0;                      // s4
    *--sp = 0;                      // s3
    *--sp = 0;                      // s2
    *--sp = 0;                      // s1
    *--sp = 0;                      // s0
    *--sp = (uint32_t) pc;          // ra

    // 初始化字段
    proc->pid = i + 1;
    proc->state = PROC_RUNNABLE;
    proc->sp = (uint32_t) sp;
    return proc;
}
```

## 测试上下文切换

我们已经实现了进程的最基本功能 - 多个程序的并发执行。让我们创建两个进程：

```c [kernel.c] {1-25,32-34}
void delay(void) {
    for (int i = 0; i < 30000000; i++)
        __asm__ __volatile__("nop"); // 什么都不做
}

struct process *proc_a;
struct process *proc_b;

void proc_a_entry(void) {
    printf("starting process A\n");
    while (1) {
        putchar('A');
        switch_context(&proc_a->sp, &proc_b->sp);
        delay();
    }
}

void proc_b_entry(void) {
    printf("starting process B\n");
    while (1) {
        putchar('B');
        switch_context(&proc_b->sp, &proc_a->sp);
        delay();
    }
}

void kernel_main(void) {
    memset(__bss, 0, (size_t) __bss_end - (size_t) __bss);

    WRITE_CSR(stvec, (uint32_t) kernel_entry);

    proc_a = create_process((uint32_t) proc_a_entry);
    proc_b = create_process((uint32_t) proc_b_entry);
    proc_a_entry();

    PANIC("unreachable here!");
}
```

`proc_a_entry`函数和`proc_b_entry`函数分别是进程 A 和进程 B 的入口点。使用`putchar`函数显示单个字符后，它们使用`switch_context`函数切换到另一个进程。

`delay`函数实现了一个忙等待，以防止字符输出过快导致终端无响应。`nop`指令是一个“什么都不做”的指令。添加它是为了防止编译器优化删除循环。

现在，让我们试一试！启动消息将各显示一次，然后"ABABAB..."会永远持续：

```
$ ./run.sh

starting process A
Astarting process B
BABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABAQE
```

## 调度器

在前面的实验中，我们直接调用`switch_context`函数来指定“下一个要执行的进程”。然而，随着进程数量的增加，这种方法在确定下一个要切换的进程时会变得复杂。为了解决这个问题，让我们实现一个*“调度器”*，一个决定下一个进程的内核程序。

以下`yield`函数是调度器的实现：

> [!TIP]
>
> "yield"这个词经常被用作允许进程主动让出 CPU 给另一个进程的 API 名称。

```c [kernel.c]
struct process *current_proc; // 当前运行的进程
struct process *idle_proc;    // 空闲进程

void yield(void) {
    // 搜索可运行的进程
    struct process *next = idle_proc;
    for (int i = 0; i < PROCS_MAX; i++) {
        struct process *proc = &procs[(current_proc->pid + i) % PROCS_MAX];
        if (proc->state == PROC_RUNNABLE && proc->pid > 0) {
            next = proc;
            break;
        }
    }

    // 如果除了当前进程外没有可运行的进程，返回并继续处理
    if (next == current_proc)
        return;

    // 上下文切换
    struct process *prev = current_proc;
    current_proc = next;
    switch_context(&prev->sp, &next->sp);
}
```

这里，我们引入了两个全局变量。`current_proc`指向当前运行的进程。`idle_proc`指向空闲进程，即“当没有可运行进程时要运行的进程”。`idle_proc`在启动时创建为进程 ID 为`-1`的进程，如下所示：

```c [kernel.c] {8-10,15-16}
void kernel_main(void) {
    memset(__bss, 0, (size_t) __bss_end - (size_t) __bss);

    printf("\n\n");

    WRITE_CSR(stvec, (uint32_t) kernel_entry);

    idle_proc = create_process((uint32_t) NULL);
    idle_proc->pid = -1; // idle
    current_proc = idle_proc;

    proc_a = create_process((uint32_t) proc_a_entry);
    proc_b = create_process((uint32_t) proc_b_entry);

    yield();
    PANIC("switched to idle process");
}
```

这个初始化过程的关键点是`current_proc = idle_proc`。这确保了引导进程的执行上下文被保存并作为空闲进程的上下文恢复。在第一次调用`yield`函数期间，它从空闲进程切换到进程 A，当切换回空闲进程时，它的行为就像从这个`yield`函数调用返回一样。

最后，修改`proc_a_entry`和`proc_b_entry`如下，调用`yield`函数而不是直接调用`switch_context`函数：

```c [kernel.c] {5,13}
void proc_a_entry(void) {
    printf("starting process A\n");
    while (1) {
        putchar('A');
        yield();
    }
}

void proc_b_entry(void) {
    printf("starting process B\n");
    while (1) {
        putchar('B');
        yield();
    }
}
```

如果 "A" 和 "B" 像以前一样打印，说明工作完美！

## 异常处理程序的变更

在异常处理程序中，它将执行状态保存到栈上。然而，由于我们现在为每个进程使用独立的内核栈，我们需要稍微更新它。

首先，在进程切换期间在`sscratch`寄存器中设置当前执行进程的内核栈的初始值。

```c [kernel.c] {4-8}
void yield(void) {
    /* 省略 */

    __asm__ __volatile__(
        "csrw sscratch, %[sscratch]\n"
        :
        : [sscratch] "r" ((uint32_t) &next->stack[sizeof(next->stack)])
    );

    // 上下文切换
    struct process *prev = current_proc;
    current_proc = next;
    switch_context(&prev->sp, &next->sp);
}
```

由于栈指针向低地址延伸，我们将地址设置在`sizeof(next->stack)`字节处作为内核栈的初始值。

对异常处理程序的修改如下：

```c [kernel.c] {3-4,38-44}
void kernel_entry(void) {
    __asm__ __volatile__(
        // 从sscratch中获取运行进程的内核栈
        "csrrw sp, sscratch, sp\n"

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

        // 获取并保存异常发生时的sp
        "csrr a0, sscratch\n"
        "sw a0,  4 * 30(sp)\n"

        // 重置内核栈
        "addi a0, sp, 4 * 31\n"
        "csrw sscratch, a0\n"

        "mv a0, sp\n"
        "call handle_trap\n"
```

第一条`csrrw`指令简单来说是一个交换操作：

```
tmp = sp;
sp = sscratch;
sscratch = tmp;
```

因此，现在`sp`指向当前运行进程的*内核*（不是*用户*）栈。此外，`sscratch`现在保存异常发生时原始的`sp`值（用户栈）。

在将其他寄存器保存到内核栈之后，我们从`sscratch`恢复原始的`sp`值并保存到内核栈上。然后，计算`sscratch`的初始值并恢复它。

这里的关键点是每个进程都有自己独立的内核栈。通过在上下文切换期间切换`sscratch`的内容，我们可以从进程被中断的点恢复执行，就像什么都没发生一样。

> [!TIP]
>
> 我们已经实现了“内核”栈的上下文切换机制。应用程序使用的栈（所谓的*用户栈*）将与内核栈分开分配。这将在后面的章节中实现。

## 附录：为什么我们需要重置栈指针？

在前一节中，你可能想知道为什么我们需要通过调整`sscratch`来切换到内核栈。

这是因为我们不能信任异常发生时的栈指针。在异常处理程序中，我们需要考虑以下三种模式：

1. 在内核模式下发生异常。
2. 在处理另一个异常时在内核模式下发生异常（嵌套异常）。
3. 在用户模式下发生异常。

在情况(1)中，即使我们不重置栈指针通常也没有问题。在情况(2)中，我们会覆盖保存区域，但我们的实现在嵌套异常时会触发内核恐慌，所以没关系。

问题出在情况(3)。在这种情况下，`sp`指向“用户（应用程序）栈区域”。如果我们实现时直接使用（信任）`sp`，可能会导致使内核崩溃的漏洞。

让我们在完成本书第17章的所有实现后，通过运行以下应用程序来做个实验：

```c
// 应用程序示例
#include "user.h"

void main(void) {
    __asm__ __volatile__(
        "li sp, 0xdeadbeef\n"  // 将无效地址设置给sp
        "unimp"                // 触发异常
    );
}
```

如果我们在不应用本章修改的情况下运行它（即不从`sscratch`恢复内核栈），内核会在不显示任何内容的情况下挂起，你会在 QEMU 的日志中看到以下输出：

```
epc:0x0100004e, tval:0x00000000, desc=illegal_instruction <- unimp 触发陷阱处理程序
epc:0x802009dc, tval:0xdeadbe73, desc=store_page_fault <- 对栈的写入中止 (0xdeadbeef)
epc:0x802009dc, tval:0xdeadbdf7, desc=store_page_fault <- 对栈的写入中止 (0xdeadbeef) (2)
epc:0x802009dc, tval:0xdeadbd7b, desc=store_page_fault <- 对栈的写入中止 (0xdeadbeef) (3)
epc:0x802009dc, tval:0xdeadbcff, desc=store_page_fault <- 对栈的写入中止 (0xdeadbeef) (4)
...
```

首先，`unimp`伪指令引发无效指令异常，转到内核的陷阱处理程序。但是，由于栈指针指向未映射的地址（`0xdeadbeef`），在尝试保存寄存器时会发生异常，跳回到陷阱处理程序的开头。这变成一个无限循环，导致内核挂起。为了防止这种情况，我们需要从`sscratch`获取一个可信的栈区域。

另一个解决方案是使用多个异常处理程序。在 RISC-V 版本的 xv6（一个著名的教育用 UNIX 类操作系统）中，情况(1)和(2)（[`kernelvec`](https://github.com/mit-pdos/xv6-riscv/blob/f5b93ef12f7159f74f80f94729ee4faabe42c360/kernel/kernelvec.S#L13-L14)）以及情况(3)（[`uservec`](https://github.com/mit-pdos/xv6-riscv/blob/f5b93ef12f7159f74f80f94729ee4faabe42c360/kernel/trampoline.S#L74-L75)）有独立的异常处理程序。在前一种情况下，它继承异常时的栈指针，在后一种情况下，它获取一个单独的内核栈。在进入和退出内核时会[切换](https://github.com/mit-pdos/xv6-riscv/blob/f5b93ef12f7159f74f80f94729ee4faabe42c360/kernel/trap.c#L44-L46)陷阱处理程序。

> [!TIP]
>
> 在谷歌开发的操作系统 Fuchsia 中，曾经出现过一个允许从用户设置任意程序计数器值的 API [成为漏洞](https://blog.quarkslab.com/playing-around-with-the-fuchsia-operating-system.html)的情况。不信任来自用户（应用程序）的输入是内核开发中一个极其重要的习惯。

## 下一步

我们现在已经实现了并发运行多个进程的能力，实现了一个多任务操作系统。

然而，就目前而言，进程可以自由地读写内核的内存空间。这非常不安全！在接下来的章节中，我们将探讨如何安全地运行应用程序，换句话说，如何隔离内核和应用程序。
