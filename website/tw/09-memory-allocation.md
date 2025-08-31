# 記憶體分配（Memory Allocation）

在本章中，我們將實作一個簡單的記憶體分配器（memory allocator）。

## 回顧 Linker Script

在實作記憶體分配器之前，讓我們先定義好要由分配器管理的記憶體區段（memory regions）：

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

這段修改在 linker script 中新增了兩個符號：`__free_ram` 與 `__free_ram_end`，用來定義一塊堆疊空間之後的可用記憶體區域。這個區域的大小是 64MB，雖然是任意指定的，但透過 `. = ALIGN(4096)` 可確保它的起始位址是 4KB 對齊（頁對齊）。

透過在 linker script 中定義這些區域，而不是在程式中硬編碼位址（hardcode），可以讓 linker 自動決定這些區域的位置，以避免與核心的靜態資料區域重疊。

> [!TIP]
>
> 實際的 x86-64 作業系統會在開機時透過硬體取得可用的記憶體區域資訊（例如透過 UEFI 的 `GetMemoryMap` 函式）。

## 世界上最簡單的記憶體分配演算法

讓我們來實作一個可以動態分配記憶體的函式。我們不會像 `malloc` 那樣以「位元組（byte）」為單位來分配，而是以較大的單位，稱為「頁面（page）」來分配。一頁的大小通常是 4KB（4096 位元組）。

> [!TIP]
>
> 4KB = 4096 = 0x1000（十六進位表示）。因此，頁面對齊的位址在十六進位表示下會呈現出整齊的對齊效果。

以下的 `alloc_pages` 函式會動態分配 `n` 頁的記憶體，並回傳起始位址：

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

`PAGE_SIZE` 代表一頁的大小。我們在 `common.h` 中定義這個常數：

```c [common.h]
#define PAGE_SIZE 4096
```

你會發現以下幾個重點：

- `next_paddr` 被定義為 `static` 的變數。這代表它與區域變數不同，其值會在函式呼叫之間保留。換句話說，它會像全域變數那樣運作。
- `next_paddr` 指向「下一塊待分配區域」的起始位址（即尚未使用的區域）。每次分配記憶體時，`next_paddr` 都會依照分配的大小向後推進。
- `next_paddr` 的初始值是 `__free_ram` 的位址。這代表記憶體會從 `__free_ram` 開始依序分配。
- 由於 linker script 中使用了 `ALIGN(4096)`，`__free_ram` 會被放在 4KB 對齊的邊界上。因此，`alloc_pages` 函式回傳的位址一定是 4KB 對齊的。
- 如果嘗試分配超過 `__free_ram_end` 的範圍，換句話說，如果記憶體用完了，系統就會觸發核心恐慌（kernel panic）。
- `memset` 函式會確保分配的記憶體區域始終填滿零值。這是為了避免未初始化的記憶體造成難以除錯的問題。

是不是很簡單呢？但這種記憶體分配演算法有個很大的問題：已分配的記憶體無法釋放！不過，對我們這個簡單的作業系統來說已經足夠了。

> [!TIP]
>
> 此演算法被稱為「Bump 分配器（Bump allocator）」或「線性分配器（Linear allocator）」，實際上在某些不需要釋放記憶體的場景中會被使用。它是一種只需幾行就能實作、速度非常快的記憶體分配方式。
>
> 當需要實作釋放機制時，通常會採用基於 bitmap 的演算法，或者稱為夥伴系統（Buddy System）的方法。

## 來試試看記憶體分配

現在來測試我們實作的記憶體分配函式。在 `kernel_main` 中加入以下程式碼來測試：

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

請確認第一個位址（`paddr0`）是否與 `__free_ram` 的位址相同，以及下一個位址（`paddr1`）是否等於 `paddr0` 加上 8KB 的位址。

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
