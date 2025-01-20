---
title: 页表
---

# 页表

## 内存管理和虚拟寻址

当程序访问内存时，CPU 会将指定的地址（*虚拟*地址）转换为物理地址。将虚拟地址映射到物理地址的表称为*页表*。通过切换页表，相同的虚拟地址可以指向不同的物理地址。这允许隔离内存空间（虚拟地址空间）并分离内核和应用程序内存区域，从而增强系统安全性。

在本章中，我们将实现基于硬件的内存隔离机制。

## 虚拟地址的结构

在本书中，我们使用 RISC-V 的一种分页机制，称为 Sv32，它使用两级页表。32位虚拟地址被分为一级页表索引（`VPN[1]`）、二级索引（`VPN[0]`）和页内偏移。

试用 **[RISC-V Sv-32 Virtual Address Breakdown](https://riscv-sv32-virtual-address.vercel.app/)** 来查看虚拟地址是如何被分解为页表索引和偏移的。

以下是一些示例：

| 虚拟地址 | `VPN[1]` (10 位) | `VPN[0]` (10 位) | 偏移 (12 位) |
| --------------- | ------------------ | ------------------ | ---------------- |
| 0x1000_0000     | 0x040              | 0x000              | 0x000            |
| 0x1000_0000     | 0x040              | 0x000              | 0x000            |
| 0x1000_1000     | 0x040              | 0x001              | 0x000            |
| 0x1000_f000     | 0x040              | 0x00f              | 0x000            |
| 0x2000_f0ab     | 0x080              | 0x00f              | 0x0ab            |
| 0x2000_f012     | 0x080              | 0x00f              | 0x012            |
| 0x2000_f034     | 0x080              | 0x00f              | 0x045            |

> [!TIP]
>
> 从上面的示例中，我们可以看到索引的以下特征：
>
> - 改变中间位（`VPN[0]`）不会影响一级索引。这意味着相邻地址的页表项集中在同一个一级页表中。
> - 改变低位不会影响 `VPN[1]` 或 `VPN[0]`。这意味着在同一个 4KB 页面内的地址位于同一个页表项中。
>
> 这种结构利用了[局部性原理](https://en.wikipedia.org/wiki/Locality_of_reference)，允许更小的页表大小和对转换后备缓冲区（TLB）的更有效使用。

当访问内存时，CPU 计算 `VPN[1]` 和 `VPN[0]` 以识别相应的页表项，读取映射的基本物理地址，并加上 `offset` 得到最终的物理地址。

## 构建页表

让我们在 Sv32 中构建页表。首先，我们定义一些宏。`SATP_SV32` 是 `satp` 寄存器中表示“在 Sv32 模式下启用分页”的单个位，而 `PAGE_*` 是要在页表项中设置的标志。

```c [kernel.h]
#define SATP_SV32 (1u << 31)
#define PAGE_V    (1 << 0)   // "Valid" 位（表项已启用）
#define PAGE_R    (1 << 1)   // 可读
#define PAGE_W    (1 << 2)   // 可写
#define PAGE_X    (1 << 3)   // 可执行
#define PAGE_U    (1 << 4)   // 用户（用户模式可访问）
```

## 映射页面

以下 `map_page` 函数接受一级页表（`table1`）、虚拟地址（`vaddr`）、物理地址（`paddr`）和页表项标志（`flags`）：

```c [kernel.c]
void map_page(uint32_t *table1, uint32_t vaddr, paddr_t paddr, uint32_t flags) {
    if (!is_aligned(vaddr, PAGE_SIZE))
        PANIC("unaligned vaddr %x", vaddr);

    if (!is_aligned(paddr, PAGE_SIZE))
        PANIC("unaligned paddr %x", paddr);

    uint32_t vpn1 = (vaddr >> 22) & 0x3ff;
    if ((table1[vpn1] & PAGE_V) == 0) {
        // 创建不存在的二级页表。
        uint32_t pt_paddr = alloc_pages(1);
        table1[vpn1] = ((pt_paddr / PAGE_SIZE) << 10) | PAGE_V;
    }

    // 设置二级页表项以映射物理页面。
    uint32_t vpn0 = (vaddr >> 12) & 0x3ff;
    uint32_t *table0 = (uint32_t *) ((table1[vpn1] >> 10) * PAGE_SIZE);
    table0[vpn0] = ((paddr / PAGE_SIZE) << 10) | flags | PAGE_V;
}
```

此函数准备二级页表，并填充二级中的页表项。

它将 `paddr` 除以 `PAGE_SIZE`，因为条目应该包含物理页号，而不是物理地址本身。不要混淆这两者！

## 映射内核内存区域

页表必须不仅为应用程序（用户空间）配置，还必须为内核配置。

在本书中，内核内存映射被配置为内核的虚拟地址与物理地址匹配（即 `vaddr == paddr`）。这允许在启用分页后继续运行相同的代码。

首先，让我们修改内核的链接脚本以定义内核使用的起始地址（`__kernel_base`）：

```ld [kernel.ld] {5}
ENTRY(boot)

SECTIONS {
    . = 0x80200000;
    __kernel_base = .;
```

> [!WARNING]
>
> 在 `. = 0x80200000` 行之后定义 `__kernel_base`。如果顺序相反，`__kernel_base` 的值将为零。

接下来，将页表添加到进程结构体中。这将是指向一级页表的指针。

```c [kernel.h] {5}
struct process {
    int pid;
    int state;
    vaddr_t sp;
    uint32_t *page_table;
    uint8_t stack[8192];
};
```

最后，在 `create_process` 函数中映射内核页面。内核页面从 `__kernel_base` 跨越到 `__free_ram_end`。这种方法确保内核始终可以访问静态分配的区域（如 `.text`）和由 `alloc_pages` 管理的动态分配区域：

```c [kernel.c] {1,6-11,15}
extern char __kernel_base[];

struct process *create_process(uint32_t pc) {
    /* 省略 */

    // 映射内核页面。
    uint32_t *page_table = (uint32_t *) alloc_pages(1);
    for (paddr_t paddr = (paddr_t) __kernel_base;
         paddr < (paddr_t) __free_ram_end; paddr += PAGE_SIZE)
        map_page(page_table, paddr, paddr, PAGE_R | PAGE_W | PAGE_X);

    proc->pid = i + 1;
    proc->state = PROC_RUNNABLE;
    proc->sp = (uint32_t) sp;
    proc->page_table = page_table;
    return proc;
}
```

## 切换页表

让我们在上下文切换时切换进程的页表：

```c [kernel.c] {5-7,10-11}
void yield(void) {
    /* 省略 */

    __asm__ __volatile__(
        "sfence.vma\n"
        "csrw satp, %[satp]\n"
        "sfence.vma\n"
        "csrw sscratch, %[sscratch]\n"
        :
        // 不要忘记尾随逗号！
        : [satp] "r" (SATP_SV32 | ((uint32_t) next->page_table / PAGE_SIZE)),
          [sscratch] "r" ((uint32_t) &next->stack[sizeof(next->stack)])
    );

    switch_context(&prev->sp, &next->sp);
}
```

我们可以通过在 `satp` 中指定一级页表来切换页表。注意，我们除以 `PAGE_SIZE`，因为它是物理页号。

在设置页表前后添加的 `sfence.vma` 指令有两个目的：

1. 确保页表的更改正确完成（类似于内存栅栏）。
2. 清除页表项的缓存（TLB）。

> [!TIP]
>
> 当内核启动时，默认禁用分页（未设置 `satp` 寄存器）。虚拟地址的行为就像它们与物理地址匹配一样。

## 测试分页

让我们试试看它是如何工作的！

```
$ ./run.sh

starting process A
Astarting process B
BABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABAB
```

输出与上一章（上下文切换）完全相同。即使在启用分页后也没有明显的变化。要检查我们是否正确设置了页表，让我们用 QEMU monitor 来检查它！

## 检查页表内容

让我们看看 `0x80000000` 附近的虚拟地址是如何映射的。如果设置正确，它们应该被映射为 `(虚拟地址) == (物理地址)`。

```
QEMU 8.0.2 monitor - type 'help' for more information
(qemu) stop
(qemu) info registers
 ...
 satp     80080253
 ...
```

你可以看到 `satp` 是 `0x80080253`。根据规范（RISC-V Sv32 模式），解释这个值给我们一级页表的起始物理地址：`(0x80080253 & 0x3fffff) * 4096 = 0x80253000`。

接下来，让我们检查一级页表的内容。我们想知道对应于虚拟地址 `0x80000000` 的二级页表。QEMU 提供了显示内存内容的命令（内存转储）。`xp` 命令在指定的物理地址转储内存。转储第 512 个条目，因为 `0x80000000 >> 22 = 512`。由于每个条目是 4 字节，我们乘以 4：

```
(qemu) xp /x 0x80253000+512*4
0000000080253800: 0x20095001
```

第一列显示物理地址，后续列显示内存值。我们可以看到设置了一些非零值。`/x` 选项指定十六进制显示。在 `x` 前添加数字（例如，`/1024x`）指定要显示的条目数。

> [!TIP]
>
> 使用 `x` 命令而不是 `xp` 允许你查看指定**虚拟**地址的内存转储。这在检查用户空间（应用程序）内存时很有用，因为与我们的内核空间不同，虚拟地址不匹配物理地址。

根据规范，二级页表位于 `(0x20095000 >> 10) * 4096 = 0x80254000`。让我们转储整个二级表（1024 个条目）：

```
(qemu) xp /1024x 0x80254000
0000000080254000: 0x00000000 0x00000000 0x00000000 0x00000000
0000000080254010: 0x00000000 0x00000000 0x00000000 0x00000000
0000000080254020: 0x00000000 0x00000000 0x00000000 0x00000000
0000000080254030: 0x00000000 0x00000000 0x00000000 0x00000000
...
00000000802547f0: 0x00000000 0x00000000 0x00000000 0x00000000
0000000080254800: 0x2008004f 0x2008040f 0x2008080f 0x20080c0f
0000000080254810: 0x2008100f 0x2008140f 0x2008180f 0x20081c0f
0000000080254820: 0x2008200f 0x2008240f 0x2008280f 0x20082c0f
0000000080254830: 0x2008300f 0x2008340f 0x2008380f 0x20083c0f
0000000080254840: 0x200840cf 0x2008440f 0x2008484f 0x20084c0f
0000000080254850: 0x200850cf 0x2008540f 0x200858cf 0x20085c0f
0000000080254860: 0x2008600f 0x2008640f 0x2008680f 0x20086c0f
0000000080254870: 0x2008700f 0x2008740f 0x2008780f 0x20087c0f
0000000080254880: 0x200880cf 0x2008840f 0x2008880f 0x20088c0f
...
```

初始条目都填充为零，但从第 512 个条目（`254800`）开始出现值。这是因为 `__kernel_base` 是 `0x80200000`，而 `VPN[1]` 是 `0x200`。

我们已经手动读取了内存转储，但是 QEMU 实际上提供了一个命令，可以以人类可读的格式显示当前页表映射。如果你想最后检查一下映射是否正确，可以使用 `info mem` 命令：

```
(qemu) info mem
vaddr    paddr            size     attr
-------- ---------------- -------- -------
80200000 0000000080200000 00001000 rwx--a-
80201000 0000000080201000 0000f000 rwx----
80210000 0000000080210000 00001000 rwx--ad
80211000 0000000080211000 00001000 rwx----
80212000 0000000080212000 00001000 rwx--a-
80213000 0000000080213000 00001000 rwx----
80214000 0000000080214000 00001000 rwx--ad
80215000 0000000080215000 00001000 rwx----
80216000 0000000080216000 00001000 rwx--ad
80217000 0000000080217000 00009000 rwx----
80220000 0000000080220000 00001000 rwx--ad
80221000 0000000080221000 0001f000 rwx----
80240000 0000000080240000 00001000 rwx--ad
80241000 0000000080241000 001bf000 rwx----
80400000 0000000080400000 00400000 rwx----
80800000 0000000080800000 00400000 rwx----
80c00000 0000000080c00000 00400000 rwx----
81000000 0000000081000000 00400000 rwx----
81400000 0000000081400000 00400000 rwx----
81800000 0000000081800000 00400000 rwx----
81c00000 0000000081c00000 00400000 rwx----
82000000 0000000082000000 00400000 rwx----
82400000 0000000082400000 00400000 rwx----
82800000 0000000082800000 00400000 rwx----
82c00000 0000000082c00000 00400000 rwx----
83000000 0000000083000000 00400000 rwx----
83400000 0000000083400000 00400000 rwx----
83800000 0000000083800000 00400000 rwx----
83c00000 0000000083c00000 00400000 rwx----
84000000 0000000084000000 00241000 rwx----
```

这些列分别表示：虚拟地址、物理地址、大小（以十六进制字节为单位）和属性。

属性由 `r`（可读）、`w`（可写）、`x`（可执行）、`a`（已访问）和 `d`（已写入）的组合表示，其中 `a` 和 `d` 表示 CPU 已经“访问过该页面”和“写入过该页面”。它们是用于操作系统跟踪哪些页面实际被使用/修改的辅助信息。

> [!TIP]
>
> 对于初学者来说，调试页表可能相当具有挑战性。如果事情不如预期那样工作，请参考“附录：调试分页”部分。

## 附录：调试分页

设置页表可能很棘手，而且错误可能难以察觉。在这个附录中，我们将看一些常见的分页错误以及如何调试它们。

### 忘记设置分页模式

假设我们忘记在 `satp` 寄存器中设置模式：

```c [kernel.c] {6}
    __asm__ __volatile__(
        "sfence.vma\n"
        "csrw satp, %[satp]\n"
        "sfence.vma\n"
        :
        : [satp] "r" (((uint32_t) next->page_table / PAGE_SIZE)) // 缺少 SATP_SV32！
    );
```

然而，当你运行操作系统时，你会发现它照常工作。这是因为分页仍然处于禁用状态，内存地址像以前一样被视为物理地址。

要调试这种情况，请尝试在 QEMU monitor 中使用 `info mem` 命令。你会看到类似这样的内容：

```
(qemu) info mem
No translation or protection
```

### 指定物理地址而不是物理页号

假设我们错误地使用物理*地址*而不是物理*页号*来指定页表：

```c [kernel.c] {6}
    __asm__ __volatile__(
        "sfence.vma\n"
        "csrw satp, %[satp]\n"
        "sfence.vma\n"
        :
        : [satp] "r" (SATP_SV32 | ((uint32_t) next->page_table)) // 忘记移位！
    );
```

在这种情况下，`info mem` 将不会打印任何映射：

```
$ ./run.sh

QEMU 8.0.2 monitor - type 'help' for more information
(qemu) stop
(qemu) info mem
vaddr    paddr            size     attr
-------- ---------------- -------- -------
```

要调试这个问题，转储寄存器以查看 CPU 在做什么：

```
(qemu) info registers

CPU#0
 V      =   0
 pc       80200188
 ...
 scause   0000000c
 ...
```

根据 `llvm-addr2line`，`80200188` 是异常处理程序的起始地址。`scause` 中的异常原因对应于“指令页面错误”。

让我们通过检查 QEMU 日志来更仔细地看看具体发生了什么：

```bash [run.sh] {2}
$QEMU -machine virt -bios default -nographic -serial mon:stdio --no-reboot \
    -d unimp,guest_errors,int,cpu_reset -D qemu.log \  # 新增！
    -kernel kernel.elf
```

```
Invalid read at addr 0x253000800, size 4, region '(null)', reason: rejected
riscv_cpu_do_interrupt: hart:0, async:0, cause:0000000c, epc:0x80200580, tval:0x80200580, desc=exec_page_fault
Invalid read at addr 0x253000800, size 4, region '(null)', reason: rejected
riscv_cpu_do_interrupt: hart:0, async:0, cause:0000000c, epc:0x80200188, tval:0x80200188, desc=exec_page_fault
Invalid read at addr 0x253000800, size 4, region '(null)', reason: rejected
riscv_cpu_do_interrupt: hart:0, async:0, cause:0000000c, epc:0x80200188, tval:0x80200188, desc=exec_page_fault
```

从日志中可以推断出以下信息：

- `epc` 指示页面错误异常的位置，是 `0x80200580`。`llvm-objdump` 显示它指向设置 `satp` 寄存器后的指令。这意味着在启用分页后立即发生页面错误。

- 所有后续页面错误显示相同的值。异常发生在 `0x80200188`，指向异常处理程序的起始地址。因为这个日志无限继续，所以在尝试执行异常处理程序时发生异常（页面错误）。

- 查看 QEMU monitor 中的 `info registers`，`satp` 是 `0x80253000`。根据规范计算物理地址：`(0x80253000 & 0x3fffff) * 4096 = 0x253000000`，它不适合 32 位地址空间。这表明设置了异常值。

总之，你可以通过检查 QEMU 日志、寄存器转储和内存转储来调查出了什么问题。然而，最重要的是要_"仔细阅读规范"_。很容易忽视或误解它。