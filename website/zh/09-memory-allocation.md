---
title: 内存分配
---

# 内存分配

在本章中，我们将实现一个简单的内存分配器。

## 重新审视链接器脚本

在实现内存分配器之前，让我们定义要由分配器管理的内存区域：

```ld [kernel.ld] {5-8}
    . = ALIGN(4);
    . += 128 * 1024; /* 128KB */
    __stack_top = .;

    . = ALIGN(4096);
    __free_ram = .;
    . += 64 * 1024 * 1024; /* 64MB */
    __free_ram_end = .;
}
```

这添加了两个新符号：`__free_ram` 和 `__free_ram_end`。它定义了栈空间之后的内存区域。空间大小（64MB）是一个任意值，而 `. = ALIGN(4096)` 确保它与 4KB 边界对齐。

通过在链接器脚本中定义而不是硬编码地址，链接器可以确定位置以避免与内核的静态数据重叠。

> [!TIP]
>
> 在 x86-64 上的实际操作系统是在启动时通过从硬件获取信息来确定可用内存区域的（例如，UEFI 的 `GetMemoryMap`）。

## 世界上最简单的内存分配算法

我们来实现一个动态分配内存的函数。它不像 `malloc` 那样按字节分配，而是以一个更大的单位 *“页”* 来分配。1 页通常是 4KB（4096 字节）。

> [!TIP]
>
> 4KB = 4096 = 0x1000（十六进制）。因此，页对齐的地址在十六进制中看起来整齐对齐。

下面的 `alloc_pages` 函数动态分配 `n` 页内存并返回起始地址：

```c [kernel.c]
extern char __free_ram[], __free_ram_end[];

paddr_t alloc_pages(uint32_t n) {
    static paddr_t next_paddr = (paddr_t) __free_ram;
    paddr_t paddr = next_paddr;
    next_paddr += n * PAGE_SIZE;

    if (next_paddr > (paddr_t) __free_ram_end)
        PANIC("out of memory");

    memset((void *) paddr, 0, n * PAGE_SIZE);
    return paddr;
}
```

`PAGE_SIZE` 表示一页的大小。在 `common.h` 中定义它：

```c [common.h]
#define PAGE_SIZE 4096
```

你会发现以下关键点：

- `next_paddr` 被定义为 `static` 变量。这意味着，与局部变量不同，它的值在函数调用之间保持不变。也就是说，它的行为像全局变量。
- `next_paddr` 指向“下一个要分配区域”（空闲区域）的起始地址。在分配时，`next_paddr` 前进分配大小的距离。
- `next_paddr` 最初保存 `__free_ram` 的地址。这意味着内存从 `__free_ram` 开始顺序分配。
- 由于链接器脚本中的 `ALIGN(4096)`，`__free_ram` 被放置在 4KB 边界上。因此，`alloc_pages` 函数总是返回与 4KB 对齐的地址。
- 如果它试图分配超出 `__free_ram_end` 的内存，换句话说，如果内存耗尽，就会发生内核恐慌。
- `memset` 函数确保分配的内存区域总是用零填充。这是为了避免未初始化内存导致的难以调试的问题。

很简单，不是吗？然而，这个内存分配算法有一个大问题：分配的内存不能被释放！话虽如此，对于我们简单的业余操作系统来说，这已经足够了。

> [!TIP]
>
> 这个算法被称为**凹凸分配器**或**线性分配器**，它实际上在不需要释放的场景中使用。这是一个很有吸引力的分配算法，只需几行代码就能实现，而且速度非常快。
>
> 在实现释放时，通常使用基于位图的算法或使用称为伙伴系统的算法。

## 试试内存分配

接下来测试我们实现的内存分配函数。在 `kernel_main` 中添加一些代码：

```c [kernel.c] {4-7}
void kernel_main(void) {
    memset(__bss, 0, (size_t) __bss_end - (size_t) __bss);

    paddr_t paddr0 = alloc_pages(2);
    paddr_t paddr1 = alloc_pages(1);
    printf("alloc_pages test: paddr0=%x\n", paddr0);
    printf("alloc_pages test: paddr1=%x\n", paddr1);

    PANIC("booted!");
}
```

验证第一个地址（`paddr0`）与 `__free_ram` 的地址匹配，并且下一个地址（`paddr1`）与 `paddr0` 之后 8KB 的地址匹配：

```
$ ./run.sh
Hello World!
alloc_pages test: paddr0=80221000
alloc_pages test: paddr1=80223000
```

```
$ llvm-nm kernel.elf | grep __free_ram
80221000 R __free_ram
84221000 R __free_ram_end
```
