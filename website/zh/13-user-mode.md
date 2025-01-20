---
title: 用户模式
---

# 用户模式

在本章中，我们将运行上一章创建的应用程序。

## 提取可执行文件

在像 ELF 这样的可执行文件格式中，加载地址存储在其文件头(ELF 中的程序头)中。但是，由于我们的应用程序的执行镜像是原始二进制文件，我们需要使用固定值来准备它：

```c [kernel.h]
// 应用程序镜像的基础虚拟地址。这需要与 `user.ld` 中定义的起始地址匹配。
#define USER_BASE 0x1000000
```

接下来，定义符号以使用嵌入的 `shell.bin.o` 原始二进制文件：

```c [kernel.c]
extern char _binary_shell_bin_start[], _binary_shell_bin_size[];
```

同时，更新 `create_process` 函数以启动应用程序：

```c [kernel.c] {1-3,5,11,20-33}
void user_entry(void) {
    PANIC("not yet implemented");
}

struct process *create_process(const void *image, size_t image_size) {
    /* 省略 */
    *--sp = 0;                      // s3
    *--sp = 0;                      // s2
    *--sp = 0;                      // s1
    *--sp = 0;                      // s0
    *--sp = (uint32_t) user_entry;  // ra (已更改!)

    uint32_t *page_table = (uint32_t *) alloc_pages(1);

    // 映射内核页。
    for (paddr_t paddr = (paddr_t) __kernel_base;
         paddr < (paddr_t) __free_ram_end; paddr += PAGE_SIZE)
        map_page(page_table, paddr, paddr, PAGE_R | PAGE_W | PAGE_X);

    // 映射用户页。
    for (uint32_t off = 0; off < image_size; off += PAGE_SIZE) {
        paddr_t page = alloc_pages(1);

        // 处理要复制的数据小于页面大小的情况。
        size_t remaining = image_size - off;
        size_t copy_size = PAGE_SIZE <= remaining ? PAGE_SIZE : remaining;

        // 填充并映射页面。
        memcpy((void *) page, image + off, copy_size);
        map_page(page_table, USER_BASE + off, page,
                 PAGE_U | PAGE_R | PAGE_W | PAGE_X);
    }
```

我们修改了 `create_process` 以接受执行镜像的指针(`image`)和镜像大小(`image_size`)作为参数。它按指定大小逐页复制执行镜像并将其映射到进程的页表中。同时，它将第一次上下文切换的跳转目标设置为 `user_entry`。目前，我们将保持这个函数为空。

> [!WARNING]
>
> 如果直接映射执行镜像而不复制它，同一应用程序的进程最终会共享相同的物理页面。这会破坏内存隔离！

最后，修改 `create_process` 函数的调用者，使其创建一个用户进程：

```c [kernel.c] {8,12}
void kernel_main(void) {
    memset(__bss, 0, (size_t) __bss_end - (size_t) __bss);

    printf("\n\n");

    WRITE_CSR(stvec, (uint32_t) kernel_entry);

    idle_proc = create_process(NULL, 0); // 已更新!
    idle_proc->pid = -1; // idle
    current_proc = idle_proc;

    // 新增!
    create_process(_binary_shell_bin_start, (size_t) _binary_shell_bin_size);

    yield();
    PANIC("switched to idle process");
}
```

让我们试一试，并用 QEMU monitor 检查执行镜像是否按预期映射：

```
(qemu) info mem
vaddr    paddr            size     attr
-------- ---------------- -------- -------
01000000 0000000080265000 00001000 rwxu---
01001000 0000000080267000 00010000 rwxu---
```

我们可以看到物理地址 `0x80265000` 被映射到虚拟地址 `0x1000000`(`USER_BASE`)。让我们看看这个物理地址的内容。要显示物理内存的内容，使用 `xp` 命令：

```
(qemu) xp /32b 0x80265000
0000000080265000: 0x37 0x05 0x01 0x01 0x13 0x05 0x05 0x26
0000000080265008: 0x2a 0x81 0x19 0x20 0x29 0x20 0x00 0x00
0000000080265010: 0x01 0xa0 0x00 0x00 0x82 0x80 0x01 0xa0
0000000080265018: 0x09 0xca 0xaa 0x86 0x7d 0x16 0x13 0x87
```

似乎存在一些数据。检查 `shell.bin` 的内容以确认它们确实匹配：

```
$ hexdump -C shell.bin | head
00000000  37 05 01 01 13 05 05 26  2a 81 19 20 29 20 00 00  |7......&*.. ) ..|
00000010  01 a0 00 00 82 80 01 a0  09 ca aa 86 7d 16 13 87  |............}...|
00000020  16 00 23 80 b6 00 ba 86  75 fa 82 80 01 ce aa 86  |..#.....u.......|
00000030  03 87 05 00 7d 16 85 05  93 87 16 00 23 80 e6 00  |....}.......#...|
00000040  be 86 7d f6 82 80 03 c6  05 00 aa 86 01 ce 85 05  |..}.............|
00000050  2a 87 23 00 c7 00 03 c6  05 00 93 06 17 00 85 05  |*.#.............|
00000060  36 87 65 fa 23 80 06 00  82 80 03 46 05 00 15 c2  |6.e.#......F....|
00000070  05 05 83 c6 05 00 33 37  d0 00 93 77 f6 0f bd 8e  |......37...w....|
00000080  93 b6 16 00 f9 8e 91 c6  03 46 05 00 85 05 05 05  |.........F......|
00000090  6d f2 03 c5 05 00 93 75  f6 0f 33 85 a5 40 82 80  |m......u..3..@..|
```

嗯，以十六进制形式很难理解。让我们反汇编机器代码，看看它是否匹配预期的指令：

```
(qemu) xp /8i 0x80265000
0x80265000:  01010537          lui                     a0,16842752
0x80265004:  26050513          addi                    a0,a0,608
0x80265008:  812a              mv                      sp,a0
0x8026500a:  2019              jal                     ra,6                    # 0x80265010
0x8026500c:  2029              jal                     ra,10                   # 0x80265016
0x8026500e:  0000              illegal
0x80265010:  a001              j                       0                       # 0x80265010
0x80265012:  0000              illegal
```

它计算/填充初始栈指针值，然后调用两个不同的函数。如果我们将其与 `shell.elf` 的反汇编结果进行比较，我们可以确认它们确实匹配：

```
$ llvm-objdump -d shell.elf | head -n20

shell.elf:      file format elf32-littleriscv

Disassembly of section .text:

01000000 <start>:
 1000000: 37 05 01 01   lui     a0, 4112
 1000004: 13 05 05 26   addi    a0, a0, 608
 1000008: 2a 81         mv      sp, a0
 100000a: 19 20         jal     0x1000010 <main>
 100000c: 29 20         jal     0x1000016 <exit>
 100000e: 00 00         unimp

01000010 <main>:
 1000010: 01 a0         j       0x1000010 <main>
 1000012: 00 00         unimp
```

## 过渡到用户模式

要运行应用程序，我们使用一个称为*用户模式*的 CPU 模式，在 RISC-V 中称为 *U-Mode*。切换到 U-Mode 非常简单。以下是方法：

```c [kernel.h]
#define SSTATUS_SPIE (1 << 5)
```

```c [kernel.c]
// ↓ __attribute__((naked)) 非常重要!
__attribute__((naked)) void user_entry(void) {
    __asm__ __volatile__(
        "csrw sepc, %[sepc]        \n"
        "csrw sstatus, %[sstatus]  \n"
        "sret                      \n"
        :
        : [sepc] "r" (USER_BASE),
          [sstatus] "r" (SSTATUS_SPIE)
    );
}
```

从 S-Mode 到 U-Mode 的切换是通过 `sret` 指令完成的。然而，在改变操作模式之前，它对 CSR 进行了两次写入：

- 在 `sepc` 寄存器中设置转换到 U-Mode 时的程序计数器。也就是 `sret` 跳转的地方。
- 在 `sstatus` 寄存器中设置 `SPIE` 位。设置这个位在进入 U-Mode 时启用硬件中断，并且会调用在 `stvec` 寄存器中设置的处理程序。

> [!TIP]
>
> 在本书中，我们不使用硬件中断而是使用轮询，所以设置 `SPIE` 位并不是必需的。然而，明确设置比默默忽略中断要好。

## 尝试用户模式

现在让我们试一试！但是，因为 `shell.c` 只是无限循环，我们无法在屏幕上判断它是否正常工作。相反，让我们用 QEMU monitor 看看：

```
(qemu) info registers

CPU#0
 V      =   0
 pc       01000010
```

看起来 CPU 在持续执行 `0x1000010`。它似乎工作正常，但总觉得不太令人满意。所以，让我们看看是否可以观察到 U-Mode 特有的行为。在 `shell.c` 中添加一行：

```c [shell.c] {4}
#include "user.h"

void main(void) {
    *((volatile int *) 0x80200000) = 0x1234; // 新增!
    for (;;);
}
```

这个 `0x80200000` 是内核使用的内存区域，它在页表上映射。然而，由于它是一个未设置页表项中 `U` 位的内核页，应该会发生异常(页面错误)，内核应该会 panic。让我们试试：

```
$ ./run.sh

PANIC: kernel.c:71: unexpected trap scause=0000000f, stval=80200000, sepc=0100001a
```

第 15 个异常(`scause = 0xf = 15`)对应于"Store/AMO page fault"。看起来预期的异常确实发生了！另外，`sepc` 中的程序计数器指向我们添加到 `shell.c` 的那一行：

```
$ llvm-addr2line -e shell.elf 0x100001a
/Users/seiya/dev/os-from-scratch/shell.c:4
```

恭喜！你已经成功执行了你的第一个应用程序！实现用户模式是不是非常简单？内核与应用程序非常相似 - 它只是拥有更多的特权。
