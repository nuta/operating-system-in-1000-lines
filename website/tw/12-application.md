# 應用程式

在本章中，我們將準備第一個應用程式的可執行檔，讓它能夠在我們的核心上執行。

## 記憶體配置

在前一章中，我們已經使用分頁機制實作了隔離的虛擬位址空間。現在我們要考慮要將應用程式放在虛擬位址空間中的哪個位置。

接下來，請建立一個新的 linker script（命名為 `user.ld`），用來定義應用程式在記憶體中的放置位置：

```ld [user.ld]
ENTRY(start)

SECTIONS {
    . = 0x1000000;

    /* machine code */
    .text :{
        KEEP(*(.text.start));
        *(.text .text.*);
    }

    /* read-only data */
    .rodata : ALIGN(4) {
        *(.rodata .rodata.*);
    }

    /* data with initial values */
    .data : ALIGN(4) {
        *(.data .data.*);
    }

    /* data that should be zero-filled at startup */
    .bss : ALIGN(4) {
        *(.bss .bss.* .sbss .sbss.*);

        . = ALIGN(16);
        . += 64 * 1024; /* 64KB */
        __stack_top = .;

       ASSERT(. < 0x1800000, "too large executable");
    }
}
```

它看起來和核心的 linker script 幾乎一樣，對吧？關鍵的差異在於基底位址（`0x1000000`），這是為了避免應用程式與核心的位址空間發生重疊。

`ASSERT` 是一個斷言（assertion），如果第一個參數中的條件不成立，連結器（linker）就會中止。在這裡，它是用來確保 `.bss` 區段（也就是應用程式記憶體的結尾）不會超過 `0x1800000`。這樣做是為了確保應用程式的可執行檔不會意外變得太大。

## 使用者空間函式庫（Userland Library）

接下來，我們要為使用者空間的程式建立一個函式庫。為了簡化，我們會先實作一個最小功能集合來啟動應用程式：

```c [user.c]
#include "user.h"

extern char __stack_top[];

__attribute__((noreturn)) void exit(void) {
    for (;;);
}

void putchar(char ch) {
    /* TODO */
}

__attribute__((section(".text.start")))
__attribute__((naked))
void start(void) {
    __asm__ __volatile__(
        "mv sp, %[stack_top] \n"
        "call main           \n"
        "call exit           \n"
        :: [stack_top] "r" (__stack_top)
    );
}
```

應用程式的執行會從 `start` 函式開始。和核心的開機流程類似，它會先設定堆疊指標（stack pointer），接著呼叫應用程式的 `main` 函式。

我們會準備一個 `exit` 函式來終止應用程式。不過目前暫時讓它執行一個無窮迴圈。

另外，我們會定義一個 `putchar` 函式，這是 `common.c` 中 `printf` 所呼叫的，我們會在之後實作它。

不同於核心的初始化流程，我們這裡不會手動將 `.bss` 區段清為 0，因為核心已經在 `alloc_pages` 函式中保證會將分配的記憶體清為 0。

> [!TIP]
>
> 在一般的作業系統中，分配出來的記憶體區域通常也都會被清成 0。否則，這些記憶體可能還殘留著來自其他程序的敏感資訊（例如憑證），這樣會造成嚴重的安全性問題。
> 最後，請為使用者空間的函式庫準備一個標頭檔（`user.h`）：

```c [user.h]
#pragma once
#include "common.h"

__attribute__((noreturn)) void exit(void);
void putchar(char ch);
```

## 第一個應用程式

現在是時候建立我們的第一個應用程式了！可惜的是，我們目前還沒有顯示字元的方式，所以沒辦法從「Hello, World!」開始。取而代之的是，我們會建立一個簡單的無窮迴圈程式：

```c [shell.c]
#include "user.h"

void main(void) {
    for (;;);
}
```

## 建置應用程式

應用程式會與核心分開建置。我們來建立一個新的建置腳本（`run.sh`），用來編譯應用程式：

```bash [run.sh] {1,3-6,10}
OBJCOPY=/opt/homebrew/opt/llvm/bin/llvm-objcopy

# Build the shell (application)
$CC $CFLAGS -Wl,-Tuser.ld -Wl,-Map=shell.map -o shell.elf shell.c user.c common.c
$OBJCOPY --set-section-flags .bss=alloc,contents -O binary shell.elf shell.bin
$OBJCOPY -Ibinary -Oelf32-littleriscv shell.bin shell.bin.o

# Build the kernel
$CC $CFLAGS -Wl,-Tkernel.ld -Wl,-Map=kernel.map -o kernel.elf \
    kernel.c common.c shell.bin.o
```

第一個 `$CC` 指令與核心的建置腳本非常相似：它會編譯 C 檔案，並使用 `user.ld` linker script 來連結。

第一個 `$OBJCOPY` 指令會將可執行檔（ELF 格式）轉換成純二進位格式（raw binary）。raw binary 是一種會直接從記憶體中基底位址（在這裡是 `0x1000000`）開始展開的實際內容。作業系統只需要把 raw binary 的內容直接複製到記憶體中，就能完成應用程式的載入。一般的作業系統通常使用像 ELF 這類格式，會將記憶體內容與其對應的對映資訊分開處理，但在這本書中，為了簡化，我們會使用 raw binary 格式。

第二個 `$OBJCOPY` 指令會將 raw binary 映像轉換成一種可以嵌入在 C 語言程式碼中的格式。我們可以使用 `llvm-nm` 指令來查看這個檔案裡面有哪些內容：

```
$ llvm-nm shell.bin.o
00000000 D _binary_shell_bin_start
00010260 D _binary_shell_bin_end
00010260 A _binary_shell_bin_size
```

前綴 `_binary_` 後面會接著檔案名稱，然後是 `start`、`end` 和 `size`。這些符號分別代表執行映像（execution image）的開始位址、結束位址以及大小。在實務上，它們會被這樣使用：

```c
extern char _binary_shell_bin_start[];
extern char _binary_shell_bin_size[];

void main(void) {
    uint8_t *shell_bin = (uint8_t *) _binary_shell_bin_start;
    printf("shell_bin size = %d\n", (int) _binary_shell_bin_size);
    printf("shell_bin[0] = %x (%d bytes)\n", shell_bin[0]);
}
```

這個程式會輸出 `shell.bin` 檔案的大小，以及它內容的第一個位元組。換句話說，你可以把 `_binary_shell_bin_start` 這個變數當作是包含整個檔案內容的起始位置，就像這樣使用：

```c
char _binary_shell_bin_start[] = "<shell.bin contents here>";
```
變數 `_binary_shell_bin_size` 包含的是檔案的大小。不過，它的使用方式有點不太常見。我們再用一次 `llvm-nm` 指令來檢查它的內容：

```
$ llvm-nm shell.bin.o | grep _binary_shell_bin_size
00010454 A _binary_shell_bin_size

$ ls -al shell.bin   ← note: do not confuse with shell.bin.o!
-rwxr-xr-x 1 seiya staff 66644 Oct 24 13:35 shell.bin

$ python3 -c 'print(0x10454)'
66644
```

在 `llvm-nm` 的輸出中，第一欄代表該符號（symbol）的「位址」。這個十六進位的數字 `10454` 剛好就是檔案的大小，但這並不是巧合。一般來說，`.o` 檔案中各符號的位址是由 linker 所決定的，但 `_binary_shell_bin_size` 是個特例。

第二欄的 `A` 表示 `_binary_shell_bin_size` 是一個絕對符號（absolute symbol），也就是說它的位址不應該被 linker 所更改。換句話說，它其實是把檔案大小嵌入為一個位址。

如果我們用 `char _binary_shell_bin_size[]` 這種方式來宣告，那麼 `_binary_shell_bin_size` 會被當作一個指標（pointer），其值就是這個絕對「位址」。但因為這裡我們是將檔案大小當成位址嵌入，所以如果對這個符號做轉型（cast），就會得到檔案大小。這是一種常見的技巧（也可以說是「dirty hack」），是利用物件檔格式的特性來達成的。

最後，我們將 `shell.bin.o` 加入了核心編譯時的 `clang` 參數中，這樣就可以把第一個應用程式的執行檔嵌入到核心映像檔裡。

## 反組譯可執行檔

在反組譯結果中，我們可以看到 `.text.start` 區段被放在可執行檔的開頭。`start` 函式應該被放置在 `0x1000000` 這個位址，如下所示：

```
$ llvm-objdump -d shell.elf

shell.elf:	file format elf32-littleriscv

Disassembly of section .text:

01000000 <start>:
 1000000: 37 05 01 01  	lui	a0, 4112
 1000004: 13 05 05 26  	addi	a0, a0, 608
 1000008: 2a 81        	mv	sp, a0
 100000a: 19 20        	jal	0x1000010 <main>
 100000c: 29 20        	jal	0x1000016 <exit>
 100000e: 00 00        	unimp

01000010 <main>:
 1000010: 01 a0        	j	0x1000010 <main>
 1000012: 00 00        	unimp

01000016 <exit>:
 1000016: 01 a0        	j	0x1000016 <exit>
```
