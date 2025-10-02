# 核心啟動（Booting the Kernel）

當電腦啟動時，CPU 首先會初始化自身，接著開始執行作業系統。作業系統會初始化硬體、啟動應用程式。這整個流程就叫做「開機（booting）」。

那麼在作業系統開始前發生了什麼事呢？在 PC 中，BIOS（或較新的 UEFI）會初始化硬體、顯示開機畫面、並從磁碟載入 OS。而在 QEMU 的 `virt` 虛擬機中，OpenSBI 就扮演著 BIOS/UEFI 的角色。

## Supervisor Binary Interface (SBI)

Supervisor Binary Interface（SBI）是一種提供給作業系統核心的 API，它定義了 firmware（例如 OpenSBI）對作業系統所提供的功能。

SBI 的規格可以在 [GitHub](https://github.com/riscv-non-isa/riscv-sbi-doc/releases) 找到。它定義了許多有用的功能，例如在除錯用序列埠上顯示字元、重開機／關機、定時器設定等。

目前最常見的 SBI 實作就是 [OpenSBI](https://github.com/riscv-software-src/opensbi)。在 QEMU 中，OpenSBI 會在開機時預設啟動，先執行硬體相關的初始化，再啟動核心。

## 讓我們啟動 OpenSBI 吧！

首先建立一個名為 `run.sh` 的 shell 腳本：

```
$ touch run.sh
$ chmod +x run.sh
```

內容如下：

```bash [run.sh]
#!/bin/bash
set -xue

# QEMU file path
QEMU=qemu-system-riscv32

# Start QEMU
$QEMU -machine virt -bios default -nographic -serial mon:stdio --no-reboot
```

這段命令會用 QEMU 啟動一台虛擬機，參數說明如下：

- `machine virt`：使用 `virt` 虛擬機（可用 `-machine '?'` 查詢支援的其他機種）
- `bios default`：使用預設的 BIOS（在 QEMU 中就是 OpenSBI）
- `nographic`：不開啟 GUI 視窗
- `serial mon:stdio`：將 QEMU 標準輸入／輸出接到虛擬機的序列埠，可按 <kbd>Ctrl</kbd>+<kbd>A</kbd> 再按 <kbd>C</kbd> 進入 QEMU monitor
- `-no-reboot`：當虛擬機崩潰時不要自動重開（方便除錯）

> [!TIP]
>
> 在 macOS 上，可以用以下命令查看 Homebrew 安裝的 QEMU 路徑：
>
> ```
> $ ls $(brew --prefix)/bin/qemu-system-riscv32
> /opt/homebrew/bin/qemu-system-riscv32
> ```

執行 `./run.sh`，你會看到以下畫面：

```
$ ./run.sh

OpenSBI v1.2
   ____                    _____ ____ _____
  / __ \                  / ____|  _ \_   _|
 | |  | |_ __   ___ _ __ | (___ | |_) || |
 | |  | | '_ \ / _ \ '_ \ \___ \|  _ < | |
 | |__| | |_) |  __/ | | |____) | |_) || |_
  \____/| .__/ \___|_| |_|_____/|____/_____|
        | |
        |_|

Platform Name             : riscv-virtio,qemu
Platform Features         : medeleg
Platform HART Count       : 1
Platform IPI Device       : aclint-mswi
Platform Timer Device     : aclint-mtimer @ 10000000Hz
...
```

OpenSBI 會顯示版本、平台名稱、支援功能、CPU 核心數（HART）、定時器裝置等資訊。

這個階段下，你在鍵盤上按下任何鍵都不會有反應，因為 QEMU 的標準輸入／輸出被接到了虛擬機的序列埠，而你輸入的字元會被傳送到 OpenSBI，但目前沒有任何程式會去讀取這些輸入字元。

現在讓我們按下 <kbd>Ctrl</kbd>+<kbd>A</kbd> 再按 <kbd>C</kbd> 以進入 QEMU 的除錯監控介面（QEMU monitor）。你可以透過輸入 `q` 命令離開該介面：

```
QEMU 8.0.2 monitor - type 'help' for more information
(qemu) q
```

> [!TIP]
>
> <kbd>Ctrl</kbd>+<kbd>A</kbd> 除了用來切換到 QEMU 的監控介面（<kbd>C</kbd> 鍵）之外，還有許多其他的功能。例如，按下 <kbd>X</kbd> 鍵可以立即關閉 QEMU。
>
> ```
> C-a h    顯示此幫助畫面
> C-a x    離開模擬器
> C-a s    將磁碟資料儲存回檔案（若使用 -snapshot）
> C-a t    切換是否顯示主控台時間戳
> C-a b    傳送中斷（magic sysrq）
> C-a c    在主控台與監控介面之間切換
> C-a C-a  傳送 C-a 自身字元
> ```

## 連結器腳本（Linker Script）

連結器腳本是一種用來定義可執行檔的記憶體配置的檔案。連結器會根據這個配置，幫每個函式與變數分配記憶體位址。

現在讓我們建立一個新檔案 `kernel.ld`：

```ld [kernel.ld]
ENTRY(boot)

SECTIONS {
    . = 0x80200000;

    .text :{
        KEEP(*(.text.boot));
        *(.text .text.*);
    }

    .rodata : ALIGN(4) {
        *(.rodata .rodata.*);
    }

    .data : ALIGN(4) {
        *(.data .data.*);
    }

    .bss : ALIGN(4) {
        __bss = .;
        *(.bss .bss.* .sbss .sbss.*);
        __bss_end = .;
    }

    . = ALIGN(4);
    . += 128 * 1024; /* 128KB */
    __stack_top = .;
}
```

重點說明：

- 核心的進入點是 `boot` 函式。
- 程式的起始位址為 `0x80200000`。
- `.text.boot` 區段永遠會被放在最前面。
- 區段會按照 `.text`、`.rodata`、`.data` 與 `.bss` 的順序擺放。
- 核心堆疊配置在 `.bss` 後面，大小為 128KB。

這裡提到的 `.text`、`.rodata`、`.data` 與 `.bss` 區段，是具有特定用途的資料區域：

| 區段   | 說明                                                  |
| --------- | ------------------------------------------------------------ |
| `.text`   | 此區段包含程式的機器碼（code）。               |
| `.rodata` | 此區段包含唯讀的常數資料。       |
| `.data`   | 此區段包含可讀寫的已初始化資料。                       |
| `.bss`    | 此區段包含可讀寫但初始值為 0 的資料。 |

讓我們來更仔細地看看連結器腳本的語法。首先，`ENTRY(boot)` 宣告程式的進入點是 `boot` 函式。接著，各區段（section）的配置會在 `SECTIONS` 區塊中定義。

`*(.text .text.*)` 會將所有檔案（`*`）中的 `.text` 區段，以及任何以 `.text.` 開頭的區段，都放到此處。

符號 `.` 代表當前位址，當配置資料時（像是 `*(.text)`），它會自動遞增。而 `. += 128 * 1024` 的意思是「將當前位址往後推移 128KB」。`ALIGN(4)` 則確保當前位址對齊到 4-byte 的邊界。

最後，`__bss = .` 表示將當前位址指定給符號 `__bss`。在 C 語言中，你可以透過 `extern char symbol_name` 來參考連結器腳本中定義的符號。

> [!TIP]
>
> 連結器腳本提供了許多方便的功能，尤其在開發核心（kernel）時非常有用。你可以在 GitHub 上找到許多實際範例！

## 最小化核心（Minimal kernel）

我們現在準備好開始撰寫作業系統核心了。先從建立一個最小化的版本開始吧！請建立一個名為 `kernel.c` 的 C 語言原始碼檔案：

```c [kernel.c]
typedef unsigned char uint8_t;
typedef unsigned int uint32_t;
typedef uint32_t size_t;

extern char __bss[], __bss_end[], __stack_top[];

void *memset(void *buf, char c, size_t n) {
    uint8_t *p = (uint8_t *) buf;
    while (n--)
        *p++ = c;
    return buf;
}

void kernel_main(void) {
    memset(__bss, 0, (size_t) __bss_end - (size_t) __bss);

    for (;;);
}

__attribute__((section(".text.boot")))
__attribute__((naked))
void boot(void) {
    __asm__ __volatile__(
        "mv sp, %[stack_top]\n" // Set the stack pointer
        "j kernel_main\n"       // Jump to the kernel main function
        :
        : [stack_top] "r" (__stack_top) // Pass the stack top address as %[stack_top]
    );
}
```

接下來我們逐一說明重點：

### 核心進入點（Entry Point）

核心的執行從 `boot` 函式開始，這個函式已在連結器腳本中被指定為進入點了。在此函式中，堆疊指標（`sp`）會被設定為連結器腳本中定義的堆疊區域的結尾位址。接著，它會跳躍至 `kernel_main()` 函式。請注意，堆疊是往位址遞減的方向成長的（往 0 成長），也就是使用時會遞減位址。因此我們要設定的是堆疊區域的結尾位址（而不是起始位址）。

### `boot` 函式的屬性（function attributes）

`boot` 函式有兩個特殊屬性（attribute）。屬性 `__attribute__((naked))` 指示編譯器不要在函式本體的前後生成多餘的程式碼（例如 return 指令），這樣可以確保我們寫得內嵌組語就是函式的完整內容。

`boot` 函式還有 `__attribute__((section(".text.boot")))` 屬性，用來控制函式在連結器腳本中的擺放位置。由於 OpenSBI 只會單純跳到 `0x80200000`，而不會知道實際的進入點，因此 `boot` 函式必須放在 `0x80200000`

### 使用 `extern char` 取得連結器腳本中的符號

在檔案開頭，我們會用 `extern char` 宣告連結器腳本中定義的每個符號（symbol）。這裡我們只需要取得符號的「位址」，所以使用 `char` 型別即可。

我們也可以寫成 `extern char __bss;`，但單寫 `__bss` 代表的是「`.bss` 區段第 0 位元組的值」，而不是「取得 `.bss` 區段的起始位址」，因此建議加上 `[]` 確保 `__bss` 代表的是位址，避免誤用。

### `.bss` 區段初始化

在 `kernel_main()` 中，我們使用 `memset()` 將 `.bss` 區段清成 0。雖然有些 bootloader（開機載入器）會自動將 `.bss` 清 0，但我們還是自己做一次保險。最後，函式會進入無窮迴圈，表示核心初始化完成、進入穩定狀態。

## 執行核心！

在 `run.sh` 中加入一條建構核心（kernel）的命令，以及一個新的 QEMU 選項（`-kernel kernel.elf`）：

```bash [run.sh] {6-12,16}
#!/bin/bash
set -xue

QEMU=qemu-system-riscv32

# Path to clang and compiler flags
CC=/opt/homebrew/opt/llvm/bin/clang  # Ubuntu users: use CC=clang
CFLAGS="-std=c11 -O2 -g3 -Wall -Wextra --target=riscv32-unknown-elf -fuse-ld=lld -fno-stack-protector -ffreestanding -nostdlib"

# Build the kernel
$CC $CFLAGS -Wl,-Tkernel.ld -Wl,-Map=kernel.map -o kernel.elf \
    kernel.c

# Start QEMU
$QEMU -machine virt -bios default -nographic -serial mon:stdio --no-reboot \
    -kernel kernel.elf
```

> [!TIP]
>
> macOS 用戶可使用以下命令確認 clang 的路徑：
>
> ```
> $ ls $(brew --prefix)/opt/llvm/bin/clang
> /opt/homebrew/opt/llvm/bin/clang
> ```

這些 clang 編譯選項（即 `CFLAGS`）的說明如下：

| 選項 | 說明 |
| ------ | ----------- |
| `-std=c11` | 使用 C11 標準 |
| `-O2` | 開啟最佳化，以產生更有效率的機器碼 |
| `-g3` | 產生最多的除錯資訊 |
| `-Wall` | 開啟主要警告訊息 |
| `-Wextra` | 開啟額外警告訊息 |
| `--target=riscv32-unknown-elf` | 目標平台為 32-bit RISC-V |
| `-ffreestanding` | 不使用主機環境（開發環境）的標準函式庫 |
| `-fuse-ld=lld` | 使用 LLVM linker （`ld.lld`） |
| `-fno-stack-protector` | 關閉 [stack protection](https://wiki.osdev.org/Stack_Smashing_Protector)，避免影響底層堆疊操作（詳見 [#31](https://github.com/nuta/operating-system-in-1000-lines/issues/31#issuecomment-2613219393)） |
| `-nostdlib` | 不連結標準函式庫 |
| `-Wl,-Tkernel.ld` | 使用指定的 linker script |
| `-Wl,-Map=kernel.map` | 輸出 linker 分配結果（map 檔） |

`-Wl,` 前綴表示該參數要傳遞給 linker，而不是 C 編譯器本身。`clang` 命令會先進行 C 的編譯，接著在內部執行 linker。

## 初次核心除錯（Your first kernel debugging）

當你執行 `run.sh` 時，核心會進入無窮迴圈，畫面上不會有任何輸出以讓你知道核心正在正確地執行。但別擔心，這在較低階的開發中非常常見！這時就輪到 QEMU 的偵錯功能派上用場了。

要查看更多 CPU 暫存器的資訊，你可以開啟 QEMU monitor，並執行 `info registers` 命令：

```
QEMU 8.0.2 monitor - type 'help' for more information
(qemu) info registers

CPU#0
 V      =   0
 pc       80200014  ← Address of the instruction to be executed (Program Counter)
 ...
 x0/zero  00000000 x1/ra    8000a084 x2/sp    80220018 x3/gp    00000000  ← Values of each register
 x4/tp    80033000 x5/t0    00000001 x6/t1    00000002 x7/t2    00000000
 x8/s0    80032f50 x9/s1    00000001 x10/a0   80220018 x11/a1   87e00000
 x12/a2   00000007 x13/a3   00000019 x14/a4   00000000 x15/a5   00000001
 x16/a6   00000001 x17/a7   00000005 x18/s2   80200000 x19/s3   00000000
 x20/s4   87e00000 x21/s5   00000000 x22/s6   80006800 x23/s7   8001c020
 x24/s8   00002000 x25/s9   8002b4e4 x26/s10  00000000 x27/s11  00000000
 x28/t3   616d6569 x29/t4   8001a5a1 x30/t5   000000b4 x31/t6   00000000
```

> [!TIP]
>
> 具體數值會因 clang 與 QEMU 的版本不同而有所差異。

`pc 80200014` 表示目前的程式計數器（Program Counter），也就是正在執行的指令位址。讓我們使用反組譯工具（`llvm-objdump`）把範圍縮小到對應的那一行程式碼：

```
$ llvm-objdump -d kernel.elf

kernel.elf:     file format elf32-littleriscv

Disassembly of section .text:

80200000 <boot>:  ← boot function
80200000: 37 05 22 80   lui     a0, 524832
80200004: 13 05 85 01   addi    a0, a0, 24
80200008: 2a 81         mv      sp, a0
8020000a: 6f 00 60 00   j       0x80200010 <kernel_main>
8020000e: 00 00         unimp

80200010 <kernel_main>:  ← kernel_main function
80200010: 73 00 50 10   wfi
80200014: f5 bf         j       0x80200010 <kernel_main>  ← pc is here
```

每一行都對應一條指令。每一欄分別代表：

- 指令所在的記憶體位址
- 該指令的十六進位機器碼
- 對應的反組譯指令

`pc 80200014` 表示目前執行的指令是 `j 0x80200010`，也就是跳回 `kernel_main`，這證實 QEMU 已正確地到達了 `kernel_main()` 函式。

我們也來確認一下堆疊指標（sp 暫存器）是否被正確地設定為連結器腳本中定義的 `__stack_top` 值。從暫存器的傾印中可以看到 `x2/sp 80220018`。要確認 linker 實際將 `__stack_top` 放在哪個位置，可以查看 `kernel.map` 檔案中的內容：

```
     VMA      LMA     Size Align Out     In      Symbol
       0        0 80200000     1 . = 0x80200000
80200000 80200000       16     4 .text
...
80200016 80200016        2     1 . = ALIGN ( 4 )
80200018 80200018    20000     1 . += 128 * 1024
80220018 80220018        0     1 __stack_top = .
```

你也可以使用 `llvm-nm` 來列出符號對應的位址：

```
$ llvm-nm kernel.elf
80200010 t .LBB0_1
00000000 N .Lline_table_start0
80220018 T __stack_top
80200000 T boot
80200010 T kernel_main
```

第一欄代表各符號在記憶體中的位址（VMA）。可以看到 `__stack_top` 被放在 `0x80220018`，這證實堆疊指標在 `boot` 函式中被正確設定了。

隨著程式的執行，`info registers` 顯示的內容也會改變。若你想暫停模擬器執行，可以在 QEMU monitor 中使用 `stop` 命令：

```
(qemu) stop             ← The process stops
(qemu) info registers   ← You can observe the state at the stop
(qemu) cont             ← The process resumes
```

到這裡，你已經成功寫出你的第一個核心了！
