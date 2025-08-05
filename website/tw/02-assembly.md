# RISC-V 101

就像網頁瀏覽器會隱藏 Windows/macOS/Linux 之間的差異一樣，作業系統則會隱藏不同 CPU 之間的差異。換句話說，作業系統就是一個控制 CPU 的程式，並且為應用程式提供抽象層。

在這本書中，我選擇 RISC-V 作為目標學習的 CPU，原因如下：

- RISC-V 的 [specification](https://riscv.org/technical/specifications/) 簡單且適合新手。
- 它是近年來很熱門的 ISA（Instruction Set Architecture，指令集架構），並與 x86、Arm 並列。
- 設計中的決策思維有完整記錄在規格中，閱讀起來也很有趣。

我們將會為 **32 位元**的 RISC-V 撰寫作業系統。當然你也可以透過一些小改動將其改寫為 64 位元的版本，不過 64 位元的位址空間較長，會稍微增加複雜度與閱讀難度。

## QEMU virt machine

一台電腦是由各種裝置組成的：CPU、記憶體、網卡、硬碟等等。例如，iPhone 和 Raspberry Pi 雖然都使用 Arm CPU，但我們會認為它們是不同的電腦。

在這本書中，我們選擇支援 QEMU 的 `virt` 機器（[documentation](https://www.qemu.org/docs/master/system/riscv/virt.html)），原因如下：

- 雖然它在現實中並不存在，但架構簡單，且與真實裝置非常相似。
- 你可以不用買硬體，而是用 QEMU 免費地模擬它。
- 遇到除錯問題時，你可以閱讀 QEMU 原始碼，或將除錯器附加到 QEMU 上來分析問題。

## RISC-V 組合語言入門

RISC-V，或稱 RISC-V ISA（指令集架構）定義了 CPU 可以執行哪些指令。對程式設計師來說，這很像 API 或程式語言的規格。當你寫 C 程式時，編譯器會把它轉成 RISC-V 組合語言。不過寫作業系統時，還是免不了要寫一些組合語言。不過別擔心，組合語言其實沒有你想像的那麼難。

> [!TIP]
>
> **試試看 Compiler Explorer!**
>
> 一個很好學組合語言的工具是 [Compiler Explorer](https://godbolt.org/)，它是一個線上編譯器。你撰寫 C 程式碼時，它就會顯示對應的組合語言。
>
> 它預設使用的是 x86-64，你可以在右側選擇 `RISC-V rv32gc clang (trunk)`，來產生 32 位元 RISC-V 組合語言。
>
> 你也可以嘗試設定 `-O0`（關閉最佳化）或 `-O2`（最佳化等級 2），觀察組合語言的變化。

### 組合語言的基本語法

組合語言（Assembly）大致上是機器碼的直接表示。先個簡單的例子：

```asm
addi a0, a1, 123
```

每一行組合語言通常對應一條指令。第一欄（addi）是指令名稱（**opcode**），後面的欄位（`a0, a1, 123`）是操作元（**operands**），也就是指令的參數。 這行意思是：把暫存器 `a1` 的值加上常數 `123`，並將結果存入 `a0` 暫存器。

### 暫存器

暫存器（Register）就像 CPU 內部的變數，速度比記憶體快很多。CPU 會從記憶體讀取資料到暫存器、在暫存器中做運算，然後再寫回記憶體或其他暫存器。

以下是 RISC-V 中常見的暫存器：

| 暫存器 | ABI 名稱（別名） | 說明 |
|---| -------- | ----------- |
| `pc` | `pc`       | Program counter 程式計數器（下一條要執行的指令位置） |
| `x0` |`zero`     | 永遠是 0 的暫存器 |
| `x1` |`ra`         | return address（函式回傳位址） |
| `x2` |`sp`         | stack pointer（堆疊指標） |
| `x5` - `x7` | `t0` - `t2` | Temporary registers |
| `x8` | `fp`      | 堆疊框架指標（frame pointer） |
| `x10` - `x11` | `a0` - `a1`  | 函式引數／回傳值 |
| `x12` - `x17` | `a2` - `a7`  | 函式引數 |
| `x18` - `x27` | `s0` - `s11` | 跨呼叫儲存的臨時暫存器 |
| `x28` - `x31` | `t3` - `t6` | 臨時暫存器 |

> [!TIP]
>
> **呼叫慣例（Calling Convention）:**
>
> 一般來說，雖然你可以自由地使用暫存器，但為了跟其他軟體互通，使用暫存器的方法是有規定的 ― 這被稱為「呼叫慣例」。
>
> 舉例來說，`x10` 到 `x11` 這些暫存器是用來放函式引數與回傳值的。為了可讀性，在 ABI（應用二進位介面）中會給它們如 `a0` 和 `a1` 這樣的別名。更多細節可參考 [spec](https://riscv.org/wp-content/uploads/2015/01/riscv-calling.pdf)。

### 記憶體存取（Memory access）

暫存器的存取非常快速，但數量有限。大多數資料實際上會儲存在記憶體中。程式通常使用 `lw`（load word，載入一個 32 位元資料）和 `sw`（store word，儲存一個 32 位元資料）指令來讀寫記憶體：

```asm
lw a0, (a1)  // Read a word (32-bits) from address in a1
             // and store it in a0. In C, this would be: a0 = *a1;
```

```asm
sw a0, (a1)  // Store a word in a0 to the address in a1.
             // In C, this would be: *a1 = a0;
```

你可以把 `(...)` 想成是 C 語言中的指標解參考（dereference）。在這個例子中，`a1` 就是一個指向 32 位元整數的指標。

### 分支指令（Branch instructions）

分支指令用來改變程式的控制流程，通常用來實作像 `if`、`for`、`while` 這類陳述句（statement）。

```asm
    bnez    a0, <label>   // Go to <label> if a0 is not zero
    // If a0 is zero, continue here

<label>:
    // If a0 is not zero, continue here
```

`bnez` 的意思是「如果不等於零就跳躍（branch if not equal to zero）」。其他常見的分支指令還包括 `beq`（相等就跳躍，原文為 branch if equal）與 `blt`（小於就跳躍，原文為 branch if less than）。這些指令類似於 C 語言中的 `goto`，但具備條件限制。

### 函式呼叫（Function calls）

`jal`（jump and link）與 `ret`（return）指令被用來進行函式的呼叫與回傳：

```asm
    li  a0, 123      // Load 123 to a0 register (function argument)
    jal ra, <label>  // Jump to <label> and store the return address
                     // in the ra register.

    // After the function call, continue here...

// int func(int a) {
//   a += 1;
//   return a;
// }
<label>:
    addi a0, a0, 1    // Increment a0 (first argument) by 1

    ret               // Return to the address stored in ra.
                      // a0 register has the return value.
```

根據呼叫慣例，函式的引數會放在 `a0` 至 `a7` 的暫存器中，回傳值則存放於 `a0`。

### 堆疊（Stack）

堆疊是一種後進先出（LIFO, Last-In-First-Out）的記憶體空間，用來儲存函式呼叫時的資料與區域變數。堆疊會「向下」成長（位址從高到低），而 `sp`（stack pointer）則會指向目前堆疊的頂端。

若要將值儲存到堆疊中（即 *"push"* 操作），需要先遞減堆疊指標，再進行儲存：

```asm
    addi sp, sp, -4  // Move the stack pointer down by 4 bytes
                     // (i.e. stack allocation).

    sw   a0, (sp)    // Store a0 to the stack
```

若要從堆疊中載入值（即 *"pop"* 操作），則需先載入，再遞增堆疊指標：

```asm
    lw   a0, (sp)    // Load a0 from the stack
    addi sp, sp, 4   // Move the stack pointer up by 4 bytes
                     // (i.e. stack deallocation).
```

> [!TIP]
>
> 在 C 語言中，堆疊的操作會由編譯器自動產生，不需要你手動撰寫這些組合語言。

## CPU 模式（CPU Modes）

CPU 有多種運作模式，每種模式擁有不同的權限。在 RISC-V 架構中，共有三種主要模式：

| 模式   | 概要                            |
| ------ | ----------------------------------- |
| M-mode | 機器模式，OpenSBI（即 BIOS）在此模式下執行     |
| S-mode | 監督者模式，也稱為「核心模式（kernel mode）」，作業系統在此模式下執行 |
| U-mode | 也稱使用者模式（user mode），應用程式在此模式下執行  |

## 特權指令（Privileged Instructions）

在所有 CPU 指令中，有一類稱為「特權指令」的指令只能在 S-mode 或 M-mode 下執行，U-mode（使用者模式）無法執行它們。本書中會使用以下幾個常見的特權指令：

| 指令與操作元（Opcode and operands） | 概要                                                                   | 對應的偽代碼（Pseudocode）                       |
| ------------------------ | -------------------------------------------------------------------------- | -------------------------------- |
| `csrr rd, csr`           | 從 CSR 讀取資料                                                            | `rd = csr;`                      |
| `csrw csr, rs`           | 將資料寫入 CSR                                                               | `csr = rs;`                      |
| `csrrw rd, csr, rs`      | 同時讀取並寫入 CSR                                         | `tmp = csr; csr = rs; rd = tmp;` |
| `sret`                   | 從陷阱（trap）處理器返回，恢復程式計數器與模式等狀態 |                                  |
| `sfence.vma`             | 清除 TLB（轉譯後備緩衝區）快取                                   |                                  |

「控制與狀態暫存器（Control and Status Register，簡寫為 CSR）」是用來儲存 CPU 設定與狀態的重要暫存器。
完整的 CSR 清單可參見官方的 [RISC-V Privileged Specification](https://riscv.org/specifications/privileged-isa/)。

> [!TIP]
>
> 有些特權指令（例如 `sret`）會進行較複雜的系統狀態還原操作。若你想更深入了解其實際行為，可參考 RISC-V 模擬器的原始碼。 特別推薦 [rvemu](https://github.com/d0iasm/rvemu)，它的設計直觀易讀（如這段 [sret 的實作](https://github.com/d0iasm/rvemu/blob/f55eb5b376f22a73c0cf2630848c03f8d5c93922/src/cpu.rs#L3357-L3400)）。

##  嵌入式組合語言（Inline assembly）

在後續章節中，你會看到一些特殊的 C 語言語法，例如：

```c
uint32_t value;
__asm__ __volatile__("csrr %0, sepc" : "=r"(value));
```

這是「內嵌組合語言（inline assembly）」，一種在 C 程式碼中嵌入組合語言的方式。雖然你可以把組合語言寫在獨立的 `.S` 檔案中，但使用內嵌組合語言通常比較方便，原因如下：

- 可以在組合語言中使用 C 的變數，也能將組合語言的結果指定給 C 變數。
- 暫存器的配置可以交給編譯器處理，不用手動保存與還原暫存器。

### 如何撰寫 Inline Assembly

語法格式如下：

```c
__asm__ __volatile__("assembly" : output operands : input operands : clobbered registers);
```

| 欄位               | 說明                                                                 |
| ------------------ | --------------------------------------------------------------------------- |
| `__asm__`          | 表示這是一段內嵌組合語言                                           |
| `__volatile__`     | 告訴編譯器不要最佳化這段內嵌組合語言                         |
| `"assembly"`       | 以字串形式書寫的組合語言內容                                  |
| 輸出操作元（output operands）  | 組合語言執行結果要存入的 C 變數                           |
| 輸入操作元（input operands）   | 組合語言中使用的輸入值（例如常數或變數）             |
| 被破壞的暫存器（clobbered registers） | 在組合語言中會被改寫的暫存器，需列出讓編譯器避免使用 |

輸出與輸入操作元用逗號分隔，格式為：`constraint (C expression)`，例如：

- `=r` 表示輸出到某個暫存器
- `r` 表示輸入一個放在暫存器裡的值

組合語言中的 `%0`、`%1`、`%2` 等對應到上述操作元的順序（先輸出再輸入）。

### Examples

```c
uint32_t value;
__asm__ __volatile__("csrr %0, sepc" : "=r"(value));
```
這段使用 `csrr` 指令從 `sepc` CSR 讀取值，並存入 `value` 變數。其中 `%0` 對應到 `value`。

```c
__asm__ __volatile__("csrw sscratch, %0" : : "r"(123));
```

這段會把常數 `123` 寫入 `sscratch` CSR。`%0` 對應到放有 `123` 的暫存器（由編譯器安排）。實際展開可能長這樣：

```
li    a0, 123        // Set 123 to a0 register
csrw  sscratch, a0   // Write the value of a0 register to sscratch register
```

雖然內嵌組合語言中只寫了 `csrw`，但編譯器會自動補上 `li` 指令，滿足 `"r"` 條件（值需放在暫存器中）。是不是很方便！

> [!TIP]
>
> 內嵌組合語言是編譯器提供的擴充功能，不屬於 C 語言標準的一部分。你可以參考 [GCC 官方文件](https://gcc.gnu.org/onlinedocs/gcc/Extended-Asm.html)了解完整語法。不過因為每種 CPU 架構的語法限制不同，新手可能會感到困難。
>
> 新手建議多看實例學習，例如 [HinaOS](https://github.com/nuta/microkernel-book/blob/52d66bd58cd95424f009e2df8bc1184f6ffd9395/kernel/riscv32/asm.h) 的 asm.h 與 [xv6-riscv](https://github.com/mit-pdos/xv6-riscv/blob/riscv/kernel/riscv.h) 的 riscv.h 都是不錯的參考資料。
