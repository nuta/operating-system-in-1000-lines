# 行程（Process）

「行程（Process）」是一個應用程式的實例。每個行程都有自己獨立的執行上下文與資源，例如虛擬記憶體空間。

> [!NOTE]
>
> 實際的作業系統中，執行上下文會以「執行緒（Thread）」作為獨立的概念來處理。為了簡化說明，本書中會假設每個行程僅包含一個執行緒。

## 行程控制區塊（Process control block）

以下的 `process` 結構體定義了一個行程物件，也就是所謂的「行程控制區塊（Process Control Block, PCB）」。

```c [kernel.h]
#define PROCS_MAX 8       // Maximum number of processes

#define PROC_UNUSED   0   // Unused process control structure
#define PROC_RUNNABLE 1   // Runnable process

struct process {
    int pid;             // Process ID
    int state;           // Process state: PROC_UNUSED or PROC_RUNNABLE
    vaddr_t sp;          // Stack pointer
    uint8_t stack[8192]; // Kernel stack
};
```

核心堆疊（kernel stack）中包含了儲存的 CPU 暫存器、返回位址（也就是從哪裡被呼叫）、以及區域變數。透過為每個行程準備一個核心堆疊，我們就可以實作「上下文切換（context switching）」：儲存與還原 CPU 暫存器，以及切換堆疊指標。

> [!TIP]
>
> 有另一種做法叫做「單一核心堆疊（single kernel stack）」。這種方式不是每個行程（或執行緒）都擁有自己的核心堆疊，而是每顆 CPU 僅使用一個堆疊空間。[seL4 就採用了這種方式](https://trustworthy.systems/publications/theses_public/05/Warton%3Abe.abstract)。
>
> 這個「程式上下文要存在哪裡」的問題，也出現在像 Go 和 Rust 這類語言的非同步執行框架中。如果你有興趣，可以搜尋 *"stackless async"* 來了解更多。

## 上下文切換（Context switch）

切換行程的執行上下文稱為「上下文切換（context switching）」。以下這個 `switch_context` 函式就是上下文切換的實作：

```c [kernel.c]
__attribute__((naked)) void switch_context(uint32_t *prev_sp,
                                           uint32_t *next_sp) {
    __asm__ __volatile__(
        // Save callee-saved registers onto the current process's stack.
        "addi sp, sp, -13 * 4\n" // Allocate stack space for 13 4-byte registers
        "sw ra,  0  * 4(sp)\n"   // Save callee-saved registers only
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

        // Switch the stack pointer.
        "sw sp, (a0)\n"         // *prev_sp = sp;
        "lw sp, (a1)\n"         // Switch stack pointer (sp) here

        // Restore callee-saved registers from the next process's stack.
        "lw ra,  0  * 4(sp)\n"  // Restore callee-saved registers only
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
        "addi sp, sp, 13 * 4\n"  // We've popped 13 4-byte registers from the stack
        "ret\n"
    );
}
```

`switch_context` 會將「被呼叫者需要保留的暫存器（callee-saved registers）」儲存到堆疊上，然後切換堆疊指標，最後再從堆疊中還原那些暫存器。換句話說，執行上下文是以暫時的區域變數形式，儲存在堆疊上。你也可以選擇把上下文存在 `struct process` 結構裡，不過這種基於堆疊的做法簡單又優雅，不是嗎？

所謂「callee-saved registers」是指被呼叫的函式在返回前必須恢復的暫存器。在 RISC-V 架構中，`s0` 到 `s11` 就是這類暫存器。而像 `a0` 這種暫存器則是由呼叫者負責保存（caller-saved），通常已由呼叫端儲存在堆疊上了。這也是為什麼 `switch_context` 只處理部分暫存器的原因。

`naked` 屬性告訴編譯器不要產生任何除了 inline assembly 以外的其他程式碼。即使不加這個屬性也可能運作正常，但當你需要手動操作堆疊指標時，加上 `naked` 是一種良好習慣，可以避免預期外的行為。

> [!TIP]
>
> Callee／Caller saved 暫存器的定義可以參考 [Calling Convention](https://riscv.org/wp-content/uploads/2015/01/riscv-calling.pdf)。編譯器會依據這個慣例產生程式碼。

接下來，我們來實作建立程序的初始化函式 `create_process`。它會接收一個進入點（entry point）作為參數，並回傳一個指向建立後的 `process` 結構體的指標：

```c
struct process procs[PROCS_MAX]; // All process control structures.

struct process *create_process(uint32_t pc) {
    // Find an unused process control structure.
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

    // Stack callee-saved registers. These register values will be restored in
    // the first context switch in switch_context.
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

    // Initialize fields.
    proc->pid = i + 1;
    proc->state = PROC_RUNNABLE;
    proc->sp = (uint32_t) sp;
    return proc;
}
```

## 測試上下文切換

我們已經實作了程序最基本的功能 ― 讓多個行程可以同時執行（並行）。現在我們來建立兩個程序：

```c [kernel.c] {1-25,32-34}
void delay(void) {
    for (int i = 0; i < 30000000; i++)
        __asm__ __volatile__("nop"); // do nothing
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

`proc_a_entry` 和 `proc_b_entry` 函式分別是 A 程序與 B 程序的進入點。它們會透過 `putchar` 函式輸出一個字元後，利用 `switch_context` 函式切換到另一個程序。

`delay` 函式實作的是「忙等（busy wait）」，用來避免輸出速度太快導致終端機變得無回應。`nop` 指令代表「不做任何事」，加上它是為了避免編譯器最佳化把迴圈刪掉。

現在我們測試一下！執行後會先顯示啟動訊息各一次，接著終端機會無限輸出 "ABABAB..."：

```
$ ./run.sh

starting process A
Astarting process B
BABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABAQE
```

## 排程器（Scheduler）

在前一個實驗中，我們是直接呼叫 `switch_context` 函式，來指定「下一個要執行的程序」。但當程序數量變多時，這種做法會變得複雜。為了解決這個問題，我們將實作一個 「排程器（scheduler）」，這是一個由核心負責的程式，用來決定下一個要執行的程序。

以下的 `yield` 函式就是排程器的實作：

> [!TIP]
>
> 「yield」這個詞，常被用來表示一個 API，它允許目前的程序主動讓出 CPU 給其他程序使用。

```c [kernel.c]
struct process *current_proc; // Currently running process
struct process *idle_proc;    // Idle process

void yield(void) {
    // Search for a runnable process
    struct process *next = idle_proc;
    for (int i = 0; i < PROCS_MAX; i++) {
        struct process *proc = &procs[(current_proc->pid + i) % PROCS_MAX];
        if (proc->state == PROC_RUNNABLE && proc->pid > 0) {
            next = proc;
            break;
        }
    }

    // If there's no runnable process other than the current one, return and continue processing
    if (next == current_proc)
        return;

    // Context switch
    struct process *prev = current_proc;
    current_proc = next;
    switch_context(&prev->sp, &next->sp);
}
```

在這裡，我們會引入兩個全域變數：`current_proc` 指向目前正在執行的程序；而 `idle_proc` 則代表「當沒有任何程序可以執行時，要執行的閒置程序」。`idle_proc` 會在開機時建立，並作為程序 ID 為 `0` 的特殊程序，如下所示：

```c [kernel.c] {8-10,15-16}
void kernel_main(void) {
    memset(__bss, 0, (size_t) __bss_end - (size_t) __bss);

    printf("\n\n");

    WRITE_CSR(stvec, (uint32_t) kernel_entry);

    idle_proc = create_process((uint32_t) NULL);
    idle_proc->pid = 0; // idle
    current_proc = idle_proc;

    proc_a = create_process((uint32_t) proc_a_entry);
    proc_b = create_process((uint32_t) proc_b_entry);

    yield();
    PANIC("switched to idle process");
}
```

這段初始化的關鍵在於 `current_proc = idle_proc`。這樣可以確保開機階段的執行上下文被保存並作為 idle 程序的上下文還原。第一次呼叫 `yield` 函式時，會從 idle 程序切換到 A 程序；而之後再切換回 idle 時，會如同從當初那個 `yield` 呼叫中返回一樣。

最後，請將 `proc_a_entry` 和 `proc_b_entry` 中原本直接呼叫 `switch_context` 的部分，改為呼叫 `yield` 函式：

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

如果畫面如預期地印出 "A" 與 "B"，那就表示一切運作正常！

## 例外處理器的更新

在例外處理器（exception handler）中，我們會將執行狀態儲存到堆疊中。不過，現在我們為每個程序使用獨立的核心堆疊，因此這邊需要稍作調整。

首先，在程序切換時，將目前執行中程序的核心堆疊底部指標儲存到 `sscratch` 暫存器中。

接著在例外處理器中讀取這個值（更多細節請見[附錄：為什麼我們要重設堆疊指標？](#appendix-why-do-we-reset-the-stack-pointer)）。

```c [kernel.c] {4-8}
void yield(void) {
    /* omitted */

    __asm__ __volatile__(
        "csrw sscratch, %[sscratch]\n"
        :
        : [sscratch] "r" ((uint32_t) &next->stack[sizeof(next->stack)])
    );

    // Context switch
    struct process *prev = current_proc;
    current_proc = next;
    switch_context(&prev->sp, &next->sp);
}
```

由於堆疊指標（stack pointer）是往低位址方向延伸的，我們將核心堆疊的初始值設為第 `sizeof(next->stack)` 個位元組的位址。

以下是修改後的例外處理器邏輯：

```c [kernel.c] {3-4,38,42-44}
void kernel_entry(void) {
    __asm__ __volatile__(
        // Retrieve the kernel stack of the running process from sscratch.
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

        // Retrieve and save the sp at the time of exception.
        "csrr a0, sscratch\n"
        "sw a0,  4 * 30(sp)\n"

        // Reset the kernel stack.
        "addi a0, sp, 4 * 31\n"
        "csrw sscratch, a0\n"

        "mv a0, sp\n"
        "call handle_trap\n"
```

第一個 `csrrw` 指令的作用其實就是一種交換操作（swap）：

```
tmp = sp;
sp = sscratch;
sscratch = tmp;
```

因此，`sp` 現在指向的是目前執行中的程序的「核心（而非使用者）」堆疊。而 `sscratch` 則保存了例外發生當下原本的 `sp` 值（也就是使用者堆疊指標）。

在將其他暫存器存入核心堆疊後，我們會從 `sscratch` 中還原原本的 `sp`，並將它也存入核心堆疊。然後再計算出例外處理器被呼叫時 `sscratch` 原本的值並還原它。

> [!NOTE]
>
> 在儲存這些暫存器時，我們會覆寫核心堆疊底部的 31 個 word。我們這個簡易 OS 並不支援中斷巢狀處理（nested interrupt handling）。
> 當 CPU 進入 `stvec` 處理器（即 `kernel_entry`）時，它會自動停用中斷直到返回使用者模式。而且我們的核心在發生例外時會直接 panic。
> 如果要支援巢狀中斷處理，可以考慮為使用者模式與核心模式分別設計不同的 `stvec` 處理器。

重點是：每個程序都有自己獨立的核心堆疊。透過在上下文切換時交換 `sscratch` 的內容，我們就能在程序被中斷後再恢復執行，就像什麼事都沒發生一樣。

> [!TIP]
>
> 我們目前已經完成的是針對「核心」堆疊的上下文切換機制。應用程式使用的堆疊（即所謂的「使用者堆疊」）會與核心堆疊分開配置，這部分會在後續章節中實作。透過在上下文切換時交換 `sscratch` 的內容，我們就能在程序被中斷後再恢復執行，就像什麼事都沒發生一樣。

## 附錄：為什麼我們要重設堆疊指標？

在上一節中，你可能會疑惑：為什麼我們需要透過調整 `sscratch` 來切換至核心堆疊？

這是因為我們不能信任發生例外時的堆疊指標。在例外處理函式中，我們需要考慮以下三種情境：

1. 在核心模式（kernel mode）中發生例外。
2. 在處理其他例外的過程中再次發生例外（巢狀例外）。
3. 在使用者模式（user mode）中發生例外。

情況 (1) 中，即使不重設 `sp`，通常也不會有問題。情況 (2) 中，可能會覆寫之前儲存的區域，不過我們的實作在遇到巢狀例外時會觸發核心 panic，因此也可以接受。

問題出現在情況 (3)。此時 `sp` 指向的是「使用者（應用程式）的堆疊區域」。如果我們照原樣使用 `sp`，就可能讓核心誤用不可信任的記憶體，造成漏洞，甚至讓惡意使用者導致核心崩潰。

我們來實驗看看：在完成本書第 17 章的所有實作後，試著執行以下應用程式來觀察這個現象：

```c
// An example of applications
#include "user.h"

void main(void) {
    __asm__ __volatile__(
        "li sp, 0xdeadbeef\n"  // Set an invalid address to sp
        "unimp"                // Trigger an exception
    );
}
```

如果你在沒有套用本章修改（也就是沒有從 `sscratch` 還原核心堆疊）的情況下執行這段程式，核心會直接卡住，什麼都不會顯示，並且你會在 QEMU 的日誌中看到以下輸出：

```
epc:0x0100004e, tval:0x00000000, desc=illegal_instruction <- unimp triggers the trap handler
epc:0x802009dc, tval:0xdeadbe73, desc=store_page_fault <- an aborted write to the stack  (0xdeadbeef)
epc:0x802009dc, tval:0xdeadbdf7, desc=store_page_fault <- an aborted write to the stack  (0xdeadbeef) (2)
epc:0x802009dc, tval:0xdeadbd7b, desc=store_page_fault <- an aborted write to the stack  (0xdeadbeef) (3)
epc:0x802009dc, tval:0xdeadbcff, desc=store_page_fault <- an aborted write to the stack  (0xdeadbeef) (4)
...
```

首先，`unimp` 偽指令會觸發一個「非法指令」例外（invalid instruction exception），並轉移到核心的 trap handler。然而，因為堆疊指標指向了一個未映射的位址（例如 `0xdeadbeef`），在嘗試儲存暫存器時會再次觸發例外，導致再次跳回 trap handler 的開頭。這會形成無窮迴圈，使核心卡死。為了避免這種情況，我們必須從 `sscratch` 中取得一個可信任的堆疊區域來使用。

另一種解法是使用多組例外處理函式。在 RISC-V 版本的 xv6（一個著名的教育用途 UNIX 類作業系統）中，針對情況 (1)/(2) 和 (3) 分別使用了不同的處理函式：
處理情況 (1)/(2) 的 [`kernelvec`](https://github.com/mit-pdos/xv6-riscv/blob/f5b93ef12f7159f74f80f94729ee4faabe42c360/kernel/kernelvec.S#L13-L14) 與處理情況 (3) 的 [`uservec`](https://github.com/mit-pdos/xv6-riscv/blob/f5b93ef12f7159f74f80f94729ee4faabe42c360/kernel/trampoline.S#L74-L75)。前者會直接沿用例外發生時的堆疊指標，後者則會改用分配好的核心堆疊。當 CPU 進入或離開核心時，trap handler 的進入點會被[切換](https://github.com/mit-pdos/xv6-riscv/blob/f5b93ef12f7159f74f80f94729ee4faabe42c360/kernel/trap.c#L44-L46)。

> [!TIP]
>
> 在 Google 開發的作業系統 Fuchsia 中，就曾發生過[一個漏洞](https://blog.quarkslab.com/playing-around-with-the-fuchsia-operating-system.html)：允許使用者任意設定程式計數器（program counter）的 API 成為攻擊入口。在核心開發中，不信任使用者（應用程式）輸入是非常重要的習慣。

## 接下來的目標

現在我們已經成功讓多個程序能夠同時執行，實現了一個具備「多工處理」能力的作業系統。
然而，目前的實作下，程序仍然可以任意讀寫核心的記憶體區域，這是非常危險的！
在接下來的章節中，我們將探討如何安全地執行應用程式，換句話說，也就是如何將核心與應用程式「隔離」。
