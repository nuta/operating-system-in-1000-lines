# 使用者模式

在本章中，我們將執行前一章所建立的應用程式。

## 擷取可執行檔內容

在像 ELF 這樣的可執行檔格式中，載入位址（load address）會被記錄在檔案的檔頭（ELF 中是 program header）。然而，由於我們的應用程式執行映像是純二進位格式（raw binary），所以我們需要手動以固定的位址來處理它，如下所示：

```c [kernel.h]
// The base virtual address of an application image. This needs to match the
// starting address defined in `user.ld`.
#define USER_BASE 0x1000000
```

接下來，我們要定義符號，以便在程式中使用嵌入在 `shell.bin.o` 裡的 raw binary：

```c [kernel.c]
extern char _binary_shell_bin_start[], _binary_shell_bin_size[];
```

另外，也要更新 `create_process` 函式，讓它能夠啟動應用程式：

```c [kernel.c] {1-3,5,11,20-33}
void user_entry(void) {
    PANIC("not yet implemented");
}

struct process *create_process(const void *image, size_t image_size) {
    /* omitted */
    *--sp = 0;                      // s3
    *--sp = 0;                      // s2
    *--sp = 0;                      // s1
    *--sp = 0;                      // s0
    *--sp = (uint32_t) user_entry;  // ra (changed!)

    uint32_t *page_table = (uint32_t *) alloc_pages(1);

    // Map kernel pages.
    for (paddr_t paddr = (paddr_t) __kernel_base;
         paddr < (paddr_t) __free_ram_end; paddr += PAGE_SIZE)
        map_page(page_table, paddr, paddr, PAGE_R | PAGE_W | PAGE_X);

    // Map user pages.
    for (uint32_t off = 0; off < image_size; off += PAGE_SIZE) {
        paddr_t page = alloc_pages(1);

        // Handle the case where the data to be copied is smaller than the
        // page size.
        size_t remaining = image_size - off;
        size_t copy_size = PAGE_SIZE <= remaining ? PAGE_SIZE : remaining;

        // Fill and map the page.
        memcpy((void *) page, image + off, copy_size);
        map_page(page_table, USER_BASE + off, page,
                 PAGE_U | PAGE_R | PAGE_W | PAGE_X);
    }
```

我們已經修改了 `create_process` 函式，讓它接受執行映像的指標（`image`）以及映像大小（`image_size`）作為參數。它會根據指定的大小，逐頁複製執行映像，並將這些頁面映射到該行程的分頁表中。此外，它會將第一次上下文切換（context switch）時要跳躍的目標位址設定為 `user_entry`。目前我們會先將 `user_entry` 留空，作為預留的跳躍點。

> [!WARNING]
>
> 如果你直接映射執行映像而沒有進行複製，那麼執行相同應用程式的多個行程將會共用相同的實體頁面，這會破壞記憶體的隔離機制！

最後，請修改呼叫 `create_process` 的地方，讓它能夠建立一個使用者行程（user process）：

```c [kernel.c] {8,12}
void kernel_main(void) {
    memset(__bss, 0, (size_t) __bss_end - (size_t) __bss);

    printf("\n\n");

    WRITE_CSR(stvec, (uint32_t) kernel_entry);

    idle_proc = create_process(NULL, 0); // updated!
    idle_proc->pid = 0; // idle
    current_proc = idle_proc;

    // new!
    create_process(_binary_shell_bin_start, (size_t) _binary_shell_bin_size);

    yield();
    PANIC("switched to idle process");
}
```

讓我們來試試看，並使用 QEMU monitor 檢查執行映像是否正確被映射到記憶體中：

```
(qemu) info mem
vaddr    paddr            size     attr
-------- ---------------- -------- -------
01000000 0000000080265000 00001000 rwxu---
01001000 0000000080267000 00010000 rwxu---
```

我們可以看到實體位址 `0x80265000` 已被映射到虛擬位址 `0x1000000`（也就是 `USER_BASE`）。接著我們來查看這個實體位址的內容。要顯示實體記憶體的內容，可以使用 `xp` 指令：

```
(qemu) xp /32b 0x80265000
0000000080265000: 0x37 0x05 0x01 0x01 0x13 0x05 0x05 0x26
0000000080265008: 0x2a 0x81 0x19 0x20 0x29 0x20 0x00 0x00
0000000080265010: 0x01 0xa0 0x00 0x00 0x82 0x80 0x01 0xa0
0000000080265018: 0x09 0xca 0xaa 0x86 0x7d 0x16 0x13 0x87
```

看起來確實有一些資料被載入了。請檢查 `shell.bin` 的內容，來確認它是否與記憶體中的資料相符：

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

嗯，用十六進位看起來不太直觀。我們來反組譯機器碼，看看是否與預期的指令相符：

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

它會計算並填入初始的堆疊指標（stack pointer）值，然後呼叫兩個不同的函式。如果我們將這些內容與 `shell.elf` 的反組譯結果相比對，就可以確認它們確實一致。

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

## 切換到使用者模式

為了執行應用程式，我們需要使用一種 CPU 模式稱為使用者模式（*user mode*），在 RISC-V 中則稱為 *U-Mode*。

```c [kernel.h]
#define SSTATUS_SPIE (1 << 5)
```

```c [kernel.c]
// ↓ __attribute__((naked)) is very important!
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

從 S-Mode 切換到 U-Mode 是透過 `sret` 指令完成的。但在實際切換模式之前，需要對兩個 CSR（控制暫存器）進行寫入：

> [!NOTE]
>
> 精確地說，`sret` 指令會在 `sstatus` 的 SPP 位元為 0 時轉換到使用者模式。詳細資訊請參閱 RISC-V 規格書中的 [12.1.1. Supervisor Status Register (`sstatus`)](https://riscv.github.io/riscv-isa-manual/snapshot/privileged/#sstatus:~:text=When%20an%20SRET%20instruction%20(see%20Section%203.3.2)%20is%20executed%20to%20return%20from%20the%20trap%20handler%2C%20the%20privilege%20level%20is%20set%20to%20user%20mode%20if%20the%20SPP%20bit%20is%200)。

- 設定 `sepc` 暫存器，指定切換到 U-Mode 時的程式計數器（program counter）位置。也就是說，這是 `sret` 執行後會跳躍到的位址。
- 設定 `sstatus` 暫存器中的 `SPIE` 位元。啟用這個位元表示進入 U-Mode 時會允許硬體中斷，並在發生中斷時跳躍到 `stvec` 中指定的中斷處理函式（handler）。

> [!TIP]
>
> 在本書中，我們並不使用硬體中斷，而是改用輪詢（polling）方式，所以其實不需要設 `SPIE` 位元。不過，明確地關閉中斷會比默默忽略來得更清楚，是比較好的做法。

## 嘗試進入使用者模式

現在就來試試看吧！不過因為 `shell.c` 只是執行一個無窮迴圈，所以從畫面上其實看不出來它有沒有正常執行。我們可以改用 QEMU monitor 來觀察：

```
(qemu) info registers

CPU#0
 V      =   0
 pc       01000010
```

看起來 CPU 正在不斷地執行 `0x1000010` 這個位址的指令。雖然看起來一切運作正常，但總覺得不滿意。所以我們來試試看能不能觀察到一些只有在 U-Mode 才會出現的行為。請在 `shell.c` 中加上一行程式碼：

```c [shell.c] {4}
#include "user.h"

void main(void) {
    *((volatile int *) 0x80200000) = 0x1234; // new!
    for (;;);
}
```

`0x80200000` 是核心所使用的記憶體區域，並已在分頁表中被映射。但由於這是一個未設定 `U`（User）位元的核心頁面，當使用者模式嘗試存取時，應該會發生例外（page fault），然後核心應該會 panic。讓我們來試試看吧：

```
$ ./run.sh

PANIC: kernel.c:71: unexpected trap scause=0000000f, stval=80200000, sepc=0100001a
```

第 15 號例外（`scause = 0xf = 15`）對應的是「Store/AMO 頁面錯誤（Store/AMO page fault）」。而且 `sepc` 中的程式計數器（program counter）也正好指向我們在 `shell.c` 中加上的那一行：

```
$ llvm-addr2line -e shell.elf 0x100001a
/Users/seiya/dev/os-from-scratch/shell.c:4
```

恭喜你！你已經成功執行了你的第一個應用程式！是不是覺得很驚訝，其實實作使用者模式（User Mode）這麼簡單？其實核心和一般應用程式非常相似，只是它擁有更多的特權（privileges）罷了。
