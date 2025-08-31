# 例外處理（Exception）

例外（Exception）是 CPU 提供的一項功能，讓作業系統核心可以處理各種事件，例如無效的記憶體存取（又稱為分頁錯誤，原文為 page fault）、非法指令，以及系統呼叫。

例外就像是 C++ 或 Java 中的 `try-catch` 機制，但它是由硬體輔助實現的。在 CPU 運行程式的過程中，除非遇到需要核心介入的情況，否則它會持續執行下去。和 `try-catch` 的主要差別在於：例外處理完畢後，核心可以讓程式從發生例外的那一點繼續執行，就好像什麼事都沒發生過一樣。這聽起來是不是很酷的 CPU 特性呢？

例外也可能在核心模式（kernel mode）下被觸發，而這通常代表是致命的核心錯誤（kernel bug）。如果你發現 QEMU 突然重開機，或是核心沒有如預期地執行，很可能就是發生了例外。我建議你一開始就實作一個例外處理器（exception handler），這樣即使發生錯誤，也能透過 kernel panic 優雅地當機。這就像在開發 JavaScript 時，會先加上一個 Unhandled Rejection 的處理器一樣，是基本容錯手段。

## 例外處理的生命週期（Life of an exception）

在 RISC-V 架構中，例外處理（Exception）的處理流程如下：

1. CPU 首先會檢查 `medeleg` 暫存器，以決定該由哪個操作模式（operation mode）來處理該例外。在我們的情境中，OpenSBI 已經設定好讓 S 模式（S-Mode）處理來自使用者模式（U-Mode）與 S 模式本身的例外。
2. CPU 會將當前狀態（如暫存器）儲存到一些 CSR 中（見後方描述）。
3. CPU 將 `stvec`（Supervisor Trap Vector）暫存器中的值設定為新的程式計數器（program counter），並跳躍到核心所定義的例外處理器。
4. 例外處理器會進一步儲存所有通用暫存器（也就是目前的程式狀態），然後處理該例外。
5. 處理完成後，例外處理器會還原原本儲存的執行狀態，並透過 `sret` 指令（Supervisor Return）讓 CPU 從發生例外的地方繼續執行。

第 2 步中所更新的 CSR 暫存器主要如下，而核心會根據這些 CSR 的內容來決定如何處理該例外：

| 暫存器名稱 | 內容                                                                     |
| -------------------------- | ------------------------------------------------------------------------ |
| `scause`                   | 例外的類型。核心會讀取這個暫存器來判斷發生的是哪一種例外事件。           |
| `stval`                    | 與例外相關的補充資訊，例如造成例外的記憶體位址。實際內容依例外類型而定。 |
| `sepc`                     | 發生例外當下程式計數器的值，也就是觸發例外的那一行指令的位址。     |
| `sstatus`                  | 發生例外時的操作模式（使用者模式 U-Mode 或監督模式 S-Mode）等狀態資訊。  |

## 例外處理器（Exception Handler）

現在來撰寫我們的第一個例外處理器吧！以下是會登錄在 `stvec` 暫存器中的例外處理器進入點（entry point）：

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

以下是一些重點：

- `sscratch` 暫存器被用來暫存發生例外時的堆疊指標（stack pointer），稍後會被還原。
- 核心中並不使用浮點暫存器，因此這裡不需要儲存它們。一般而言，它們會在執行緒切換（thread switching）時儲存與還原。
- 堆疊指標會被存入 `a0` 暫存器中，接著呼叫 `handle_trap` 函式。在這個時間點，堆疊指標所指向的位址會包含一些暫存器的值，這些值的排列方式與後面會定義的 `trap_frame` 結構一致。
- 加上 `__attribute__((aligned(4)))` 是為了讓函式起始位址對齊至 4-byte 邊界。這是因為 `stvec` 暫存器除了保存例外處理器的位址外，低兩個位元還會表示例外處理的模式（mode）。

> [!NOTE]
>
> 例外處理器的進入點是整個核心中最關鍵且最容易出錯的地方之一。細讀程式碼你會發現，所有通用暫存器（general-purpose registers）「原始」的值都會被保存到堆疊中，就連 `sp` 也會透過 `sscratch` 暫存。
> 
> 如果你不小心覆寫了 `a0` 暫存器的值，可能會導致非常難以除錯的問題，例如「區域變數的值莫名其妙被改變」。請務必完美儲存執行狀態，才不會在週六晚上浪費時間 debug。

在這個進入點中，我們會呼叫用我們最熟悉的 C 語言所寫的 `handle_trap` 函式來處理例外狀況：

```c [kernel.c]
void handle_trap(struct trap_frame *f) {
    uint32_t scause = READ_CSR(scause);
    uint32_t stval = READ_CSR(stval);
    uint32_t user_pc = READ_CSR(sepc);

    PANIC("unexpected trap scause=%x, stval=%x, sepc=%x\n", scause, stval, user_pc);
}
```

它會讀取造成例外的原因，並觸發一次核心恐慌（kernel panic），以便除錯使用。

接下來，我們在 `kernel.h` 中定義這裡所使用的各種巨集（macros）：

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

`trap_frame` 結構代表了在 `kernel_entry` 中儲存的程式狀態。`READ_CSR` 與 `WRITE_CSR` 是用來讀寫 CSR 的便利巨集。

最後一件要做的事，就是告訴 CPU 我們的例外處理器在哪裡。這可以透過在 `kernel_main` 函式中設定 `stvec` 暫存器來完成：

```c [kernel.c] {4-5}
void kernel_main(void) {
    memset(__bss, 0, (size_t) __bss_end - (size_t) __bss);

    WRITE_CSR(stvec, (uint32_t) kernel_entry); // new
    __asm__ __volatile__("unimp"); // new
```

除了設定 `stvec` 暫存器之外，它還執行了 `unimp` 指令。這是一個偽指令（pseudo-instruction），會觸發非法指令例外（illegal instruction exception）。

> [!NOTE]
>
> **unimp 是一個「偽指令」。**
>
> 根據 [RISC-V 組合語言程式設計手冊](https://github.com/riscv-non-isa/riscv-asm-manual/blob/main/src/asm-manual.adoc#instruction-aliases)，組譯器會將 `unimp` 轉換為以下指令：
>
> ```
> csrrw x0, cycle, x0
> ```
>
> 這行指令試圖將 `cycle` 暫存器的值寫入 `x0`，同時從 `x0` 讀出。但由於 `cycle` 是唯讀（read-only）暫存器，CPU 會判定這是無效指令，並觸發非法指令例外。

## 試試看吧！

讓我們實際執行它，確認例外處理器是否有被呼叫：

```
$ ./run.sh
Hello World!
PANIC: kernel.c:47: unexpected trap scause=00000002, stval=ffffff84, sepc=8020015e
```

根據規範，當 `scause` 的值為 2 時，表示「非法指令（Illegal instruction）」，也就是程式嘗試執行一條無效的指令。這正是我們使用 `unimp` 指令所預期觸發的行為！

接下來，我們也來檢查 `sepc`（Supervisor Exception Program Counter）的值是指向哪裡。如果它指向的是執行 `unimp` 指令的那一行，那就表示整個例外處理流程運作正確：

```
$ llvm-addr2line -e kernel.elf 8020015e
/Users/seiya/os-from-scratch/kernel.c:129
```
