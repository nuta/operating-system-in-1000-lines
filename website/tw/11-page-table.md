# 分頁表（Page Table）

## 記憶體管理與虛擬位址（Memory management and virtual addressing）

當程式存取記憶體時，CPU 會將指定的位址（*虛擬*位址）轉換為實體位址。這個將虛擬位址對應到實體位址的對照表稱為「分頁表（page table）」。透過切換頁表，即使是相同的虛擬位址，也能對應到不同的實體位址。

這種機制可以實現記憶體空間的隔離（虛擬位址空間隔離），達到核心與應用程式區域分離的效果，進而提升系統安全性。

在本章中，我們將實作基於硬體的記憶體隔離機制。

## 虛擬位址的結構（Structure of virtual address）

本書採用 RISC-V 的一種分頁機制：Sv32。Sv32 是一種使用兩層頁表的設計，其虛擬位址長度為 32 位元，並被拆分為：

- 第一層頁表索引（`VPN[1]`）
- 第二層頁表索引（`VPN[0]`）
- 分頁內偏移量（page offset）

你可以試試這個工具 [RISC-V Sv32 虛擬位址拆解工具](https://riscv-sv32-virtual-address.vercel.app/)，來看看虛擬位址是如何被拆解成頁表索引與偏移量的。
以下是一些範例：

| 虛擬位址（Virtual Address） | `VPN[1]` (10 bits) | `VPN[0]` (10 bits) | 位移量 Offset (12 bits) |
| --------------- | ------------------ | ------------------ | ---------------- |
| 0x1000_0000     | 0x040              | 0x000              | 0x000            |
| 0x1000_1000     | 0x040              | 0x001              | 0x000            |
| 0x1000_f000     | 0x040              | 0x00f              | 0x000            |
| 0x2000_f0ab     | 0x080              | 0x00f              | 0x0ab            |
| 0x2000_f012     | 0x080              | 0x00f              | 0x012            |
| 0x2000_f034     | 0x080              | 0x00f              | 0x034            |
| 0x20f0_f034     | 0x083              | 0x30f              | 0x034            |

> [!TIP]
>
> 從上面的例子中，我們可以觀察到索引結構的幾個特徵：
>
> - 改變中間的位元（`VPN[0]`）不會影響第一層頁表索引，代表相近的位址會集中在同一個第一層頁表中。
> - 改變最下面的位元（offset）不會影響 `VPN[1]` 或 `VPN[0]`，代表同一個 4KB 頁面內的位址都對應到同一筆頁表項。
>
> 這樣的結構利用了[區域性原則（locality of reference）](https://en.wikipedia.org/wiki/Locality_of_reference)，能讓頁表更小、更有效率地運用 TLB（Translation Lookaside Buffer，轉譯後備緩衝器）。

當 CPU 存取記憶體時，會先根據虛擬位址計算出 `VPN[1]` 與 `VPN[0]`，找出對應的頁表項（PTE），接著讀取其對應的實體頁面基底位址，再加上 `offset` 得到最終的實體位址。

## 建立頁表（Constructing the page table）

現在我們來實際建立一份 Sv32 的頁表。首先，我們會定義一些巨集（macros）。`SATP_SV32` 是 `satp` 暫存器中的一個位元，用來表示「啟用 Sv32 分頁模式」。而 `PAGE_*` 則是要設定在頁表項目（Page Table Entry, PTE）中的各種旗標。

```c [kernel.h]
#define SATP_SV32 (1u << 31)
#define PAGE_V    (1 << 0)   // "Valid" bit (entry is enabled)
#define PAGE_R    (1 << 1)   // Readable
#define PAGE_W    (1 << 2)   // Writable
#define PAGE_X    (1 << 3)   // Executable
#define PAGE_U    (1 << 4)   // User (accessible in user mode)
```

## 頁面映射（Mapping pages）

以下的 `map_page` 函式會接收：第一層頁表（`table1`）、虛擬位址（`vaddr`）、實體位址（`paddr`）、以及頁表項旗標（`flags`）：

```c [kernel.c]
void map_page(uint32_t *table1, uint32_t vaddr, paddr_t paddr, uint32_t flags) {
    if (!is_aligned(vaddr, PAGE_SIZE))
        PANIC("unaligned vaddr %x", vaddr);

    if (!is_aligned(paddr, PAGE_SIZE))
        PANIC("unaligned paddr %x", paddr);

    uint32_t vpn1 = (vaddr >> 22) & 0x3ff;
    if ((table1[vpn1] & PAGE_V) == 0) {
        // Create the 1st level page table if it doesn't exist.
        uint32_t pt_paddr = alloc_pages(1);
        table1[vpn1] = ((pt_paddr / PAGE_SIZE) << 10) | PAGE_V;
    }

    // Set the 2nd level page table entry to map the physical page.
    uint32_t vpn0 = (vaddr >> 12) & 0x3ff;
    uint32_t *table0 = (uint32_t *) ((table1[vpn1] >> 10) * PAGE_SIZE);
    table0[vpn0] = ((paddr / PAGE_SIZE) << 10) | flags | PAGE_V;
}
```

這個函式會確保第一層頁表中的對應項目存在（如果需要會建立第二層頁表），然後設定第二層頁表項，將虛擬頁面對應到實體頁面。

它會將 `paddr` 除以 `PAGE_SIZE`，因為頁表項中應該儲存的是「實體頁框號（physical page number, PPN）」，而不是完整的實體位址。

> [!IMPORTANT]
>
> 實體位址（physical address）與實體頁框號（PPN）是不同的概念。在設定分頁表時請特別注意，不要將它們搞混。

## 映射核心記憶體區域（Mapping kernel memory area）

分頁表不只能為應用程式（使用者空間）而設，核心本身也需要有對應的頁表設定。

在本書中，我們採用的做法是：讓核心虛擬位址與實體位址相同（即 `vaddr == paddr`）。這樣的設定可以讓分頁機制啟用後，核心原本的程式碼與資料繼續照常運作。

首先，我們需要修改核心的 linker script，來定義核心程式碼的起始位址（`__kernel_base`）：

```ld [kernel.ld] {5}
ENTRY(boot)

SECTIONS {
    . = 0x80200000;
    __kernel_base = .;
```

> [!WARNING]
>
> 請務必在 `. = 0x80200000` 這一行「之後」定義 `__kernel_base`。如果順序錯了，`__kernel_base` 的值會是 0！

接著，新增一個欄位到 process 結構中，這會是指向第一層頁表的指標：

```c [kernel.h] {5}
struct process {
    int pid;
    int state;
    vaddr_t sp;
    uint32_t *page_table;
    uint8_t stack[8192];
};
```

最後，在 `create_process` 函式中將核心頁面加入對應的頁表。這些核心頁面從 `__kernel_base` 到 `__free_ram_end`，確保核心能夠存取靜態配置的區段（例如 `.text`），以及動態分配的記憶體區段（由 `alloc_pages` 管理）。

```c [kernel.c] {1,6-11,15}
extern char __kernel_base[];

struct process *create_process(uint32_t pc) {
    /* omitted */

    // Map kernel pages.
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

## 切換分頁表

讓我們在上下文切換（context switching）時切換該行程（process）的分頁表（page table）：

```c [kernel.c] {5-7,10-11}
void yield(void) {
    /* omitted */

    __asm__ __volatile__(
        "sfence.vma\n"
        "csrw satp, %[satp]\n"
        "sfence.vma\n"
        "csrw sscratch, %[sscratch]\n"
        :
        // Don't forget the trailing comma!
        : [satp] "r" (SATP_SV32 | ((uint32_t) next->page_table / PAGE_SIZE)),
          [sscratch] "r" ((uint32_t) &next->stack[sizeof(next->stack)])
    );

    switch_context(&prev->sp, &next->sp);
}
```

我們可以透過在 `satp` 中指定第一層的分頁表，來切換分頁表。請注意，我們要除以 `PAGE_SIZE`，因為這裡指的是實體頁號（physical page number）。

在設定分頁表前後加入 `sfence.vma` 指令有兩個目的：

1. 確保對分頁表的變更已經正確完成（類似記憶體欄柵的作用）。
2. 清除分頁表快取（即 TLB，轉譯快取）。

> [!TIP]
>
> 當核心啟動時，預設是未啟用分頁功能（`satp` 暫存器尚未設置），此時虛擬位址的行為就像是與實體位址完全相同。

## 測試分頁機制

讓我們來試試看，看看它是如何運作的！

```
$ ./run.sh

starting process A
Astarting process B
BABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABAB
```

輸出結果與前一章（上下文切換）完全相同。即使啟用了分頁功能，表面上也沒有任何變化。為了確認我們是否正確地設置了分頁表，我們來使用 QEMU monitor 進行檢查吧！

## 檢查分頁表內容

我們來看看 `0x80200000` 附近的虛擬位址是如何被對應的。如果設置正確，這些位址應該會對應到相同的實體位址，也就是` (虛擬位址) == (實體位址)`。

```
QEMU 8.0.2 monitor - type 'help' for more information
(qemu) stop
(qemu) info registers
 ...
 satp     80080253
 ...
```

你可以看到 `satp` 的值是 `0x80080253`。根據 RISC-V Sv32 模式的規範，這個值代表第一層分頁表的起始實體位址：`(0x80080253 & 0x3fffff) * 4096 = 0x80253000`。

接下來，我們要檢查第一層分頁表的內容，目的是找出對應虛擬位址 `0x80200000` 的第二層分頁表。QEMU 提供用來顯示記憶體內容的指令（memory dump）。`xp` 指令可以用來轉儲指定實體位址的記憶體內容。我們要查看第 512 個項目，因為 `VPN[1] = 0x80200000 >> 22 = 512`。由於每個分頁表項目是 4 個位元組（bytes），所以我們要乘以 4：

```
(qemu) xp /x 0x80253000+512*4
0000000080253800: 0x20095001
```

第一欄顯示的是實體位址，接下來的欄位則是該位址對應的記憶體數值。我們可以看到有些項目被設定為非零值。`/x` 選項代表以十六進位格式顯示內容。在 `x` 前面加上一個數字（例如 `/1024x`）表示要顯示的項目數量。

> [!TIP]
>
> 使用 `x` 指令（而非 `xp`）可以用來查看「虛擬」位址對應的記憶體內容。
這在檢查使用者空間（應用程式）的記憶體時特別有用，因為使用者空間的虛擬位址不等於實體位址，這點與我們目前的核心空間不同。根據規範，第二層分頁表位於：`(0x20095000 >> 10) * 4096 = 0x80254000`。我們再次查看第 512 個項目，因為 `VPN[0] = (0x80200000 >> 12) & 0x3ff = 512`。

```
(qemu) xp /x 0x80254000+512*4
0000000080254800: 0x200800cf
```

數值 `0x200800cf` 對應的實體頁號為 `0x200800cf >> 10 = 0x80200`（根據規範，我們會忽略最低的 10 個位元，因為那是用來表示權限旗標的）。這代表虛擬位址 `0x80200000` 已成功對應到實體位址 `0x80200000`，正是我們想要的結果！我們也來轉儲（dump）整個第一層分頁表（共 1024 個項目）：

```
(qemu) xp /1024x 0x80253000
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

前面的分頁表項目都是 0，但從第 512 個項目（`254800`）開始就出現了非零值。這是因為 `__kernel_base` 為 `0x80200000`，而對應的 `VPN[1]` 是 `0x200`（也就是 512）。

雖然我們剛剛是手動讀取記憶體轉儲（memory dumps），但其實 QEMU 提供了一個指令，可以用人類可讀的格式顯示目前的分頁表對應情況。如果你想要最終確認分頁映射是否正確，可以使用 `info mem` 指令：

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

這些欄位依序代表：虛擬位址、實體位址、大小（以十六進位位元組表示），以及屬性（attributes）。

屬性由以下字母組合表示：`r`（可讀）、`w`（可寫）、`x`（可執行）、`a`（已被存取）、`d`（已被寫入），其中 `a` 表示 CPU 曾「存取過該頁」，而 `d` 則表示 CPU 曾「寫入過該頁」。這些是作業系統用來追蹤哪些頁面實際被使用或修改的輔助資訊。

> [!TIP]
>
> 對初學者來說，除錯分頁表可能會相當困難。如果你的系統沒有如預期運作，可以參考「附錄：分頁除錯（Debugging paging）」章節來排查問題。

## 附錄：分頁機制除錯指南

建立分頁表是一件不容易的事，而且錯誤往往不容易發現。在這個附錄中，我們會介紹一些常見的分頁錯誤，以及該如何進行除錯。

### 忘記設定分頁模式

假設我們忘了在 `satp` 暫存器中設定分頁模式：

```c [kernel.c] {6}
    __asm__ __volatile__(
        "sfence.vma\n"
        "csrw satp, %[satp]\n"
        "sfence.vma\n"
        :
        : [satp] "r" (((uint32_t) next->page_table / PAGE_SIZE)) // Missing SATP_SV32!
    );
```

然而，當你執行作業系統時，會發現它照常運作。這是因為分頁功能尚未啟用，所以記憶體位址仍被當作實體位址來使用，就像以前一樣。

要除錯這種情況，可以在 QEMU monitor 中執行 `info mem` 指令。你會看到類似下面的輸出：

```
(qemu) info mem
No translation or protection
```

### 錯誤地指定了實體位址而非實體頁號

假設我們不小心將實體「位址」當作實體「頁號（page number）」來指定分頁表的起始位置：

```c [kernel.c] {6}
    __asm__ __volatile__(
        "sfence.vma\n"
        "csrw satp, %[satp]\n"
        "sfence.vma\n"
        :
        : [satp] "r" (SATP_SV32 | ((uint32_t) next->page_table)) // Forgot to shift!
    );
```

在這種情況下，`info mem` 指令將不會顯示任何映射結果（mappings）：

```
$ ./run.sh

QEMU 8.0.2 monitor - type 'help' for more information
(qemu) stop
(qemu) info mem
vaddr    paddr            size     attr
-------- ---------------- -------- -------
```

要除錯這個問題，可以轉儲（dump）CPU 暫存器，來觀察處理器目前的狀態：

```
(qemu) info registers

CPU#0
 V      =   0
 pc       80200188
 ...
 scause   0000000c
 ...
```

根據 `llvm-addr2line` 的結果，`80200188` 是例外處理器（exception handler）的起始位址。`scause` 中顯示的例外原因是「指令頁面錯誤（Instruction page fault）」。

讓我們透過檢查 QEMU 的記錄（logs）來更進一步了解發生了什麼事：

```bash [run.sh] {2}
$QEMU -machine virt -bios default -nographic -serial mon:stdio --no-reboot \
    -d unimp,guest_errors,int,cpu_reset -D qemu.log \  # new!
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

以下是你可以從這些日誌中推斷出來的資訊：

- `epc`（發生例外時的程式計數器）是 `0x80200580`。根據 `llvm-objdump` 的分析，它指向的是設定完 `satp` 暫存器後的下一條指令。這表示一啟用分頁功能就立即發生頁面錯誤。

- 所有後續的頁面錯誤都顯示相同的位址：`0x80200188`，這是例外處理器（exception handler）的起始位址。因為這個錯誤訊息不斷重複，代表當系統試圖執行例外處理器時本身又發生了頁面錯誤，進入了無限錯誤循環。

- 查看 QEMU monitor 中的 `info registers`，發現 `satp` 的值是 `0x80253000`。
根據 RISC-V 規範換算實體頁號後的實體位址：`(0x80253000 & 0x3fffff) * 4096 = 0x253000000`，這個值超出了 32 位元位址空間的上限，代表 `satp` 被設成了一個不合法的數值。

總結來說，你可以透過檢查 QEMU 的日誌、暫存器轉儲與記憶體內容來找出問題所在。但最重要的一點是：_「仔細閱讀規範」_，因為忽略細節或誤解規範是非常常見的錯誤來源。
