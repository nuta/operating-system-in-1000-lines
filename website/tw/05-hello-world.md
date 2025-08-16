# Hello World!

在前一章，我們成功啟動了第一個 kernel。雖然透過暫存器的傾印可以確認它有在執行，但這樣的驗證仍然有點不夠直觀。

在本章中，我們要讓它更「有感」― 直接讓 kernel 輸出一段字串！

## 向 SBI 說聲「哈囉」

在前一章，我們提到 SBI（Supervisor Binary Interface）是作業系統的 API。我們會使用 `ecall` 指令來呼叫 SBI 的功能。

```c [kernel.c] {1, 5-26, 29-32}
#include "kernel.h"

extern char __bss[], __bss_end[], __stack_top[];

struct sbiret sbi_call(long arg0, long arg1, long arg2, long arg3, long arg4,
                       long arg5, long fid, long eid) {
    register long a0 __asm__("a0") = arg0;
    register long a1 __asm__("a1") = arg1;
    register long a2 __asm__("a2") = arg2;
    register long a3 __asm__("a3") = arg3;
    register long a4 __asm__("a4") = arg4;
    register long a5 __asm__("a5") = arg5;
    register long a6 __asm__("a6") = fid;
    register long a7 __asm__("a7") = eid;

    __asm__ __volatile__("ecall"
                         : "=r"(a0), "=r"(a1)
                         : "r"(a0), "r"(a1), "r"(a2), "r"(a3), "r"(a4), "r"(a5),
                           "r"(a6), "r"(a7)
                         : "memory");
    return (struct sbiret){.error = a0, .value = a1};
}

void putchar(char ch) {
    sbi_call(ch, 0, 0, 0, 0, 0, 0, 1 /* Console Putchar */);
}

void kernel_main(void) {
    const char *s = "\n\nHello World!\n";
    for (int i = 0; s[i] != '\0'; i++) {
        putchar(s[i]);
    }

    for (;;) {
        __asm__ __volatile__("wfi");
    }
}
```

另外，建立一個新的 `kernel.h` 檔案，並定義回傳值的結構：

```c [kernel.h]
#pragma once

struct sbiret {
    long error;
    long value;
};
```

這樣我們就新增了一個 `sbi_call` 函式，它會依據 SBI 的規格呼叫 OpenSBI。其具體的呼叫慣例如下：

> **Chapter 3. Binary Encoding**
>
> 所有的 SBI 函式共用同一種二進位編碼格式，這使得不同的 SBI 擴充功能能夠混合使用。SBI 規格遵循以下呼叫慣例：
>
> - `ECALL` 指令被用作 Supervisor 模式（S 模式）與 SEE（SBI 執行環境，例如 OpenSBI）之間的控制轉移指令。
> - 暫存器 `a7` 用來編碼 SBI 擴充功能的 ID（**EID**）。
> - 暫存器 `a6` 用來編碼 SBI 函式的 ID（**FID**），它對應到 `a7` 所指定的擴充功能 ID（適用於 SBI v0.2 及之後版本中定義的所有擴充功能）。
> - 除了 `a0` 和 `a1`，所有其他暫存器都必須由被呼叫方（callee）保留其值，不可更動。
> - SBI 函式必須透過 `a0` 和 `a1` 回傳一對的值，其中 `a0` 回傳錯誤碼（error code）。這與回傳下列 C 語言結構類似：
>
> ```c
> struct sbiret {
>     long error;
>     long value;
> };
> ```
>
> -- 摘自《RISC-V Supervisor Binary Interface Specification》v2.0-rc1 版本

> [!TIP]
>
> 「除了 `a0` 和 `a1` 之外，所有暫存器都必須由被呼叫方保留其值」的意思是被呼叫端（例如 OpenSBI）只能改動 `a0` 和 `a1`，不可更動其他暫存器的值。換句話說，對於 kernel 來說，可以保證在呼叫結束後，`a2` 到 `a7` 的值仍會維持不變。

在每個區域變數的宣告中使用 `register` 和 `__asm__("register name")`，用於要求編譯器將該變數的值放入指定的暫存器中。這是一種在系統呼叫實作中常見的寫法（例如 [Linux 的 system call 呼叫流程](https://git.musl-libc.org/cgit/musl/tree/arch/riscv64/syscall_arch.h)）。

準備好參數後，就會在內嵌組語中執行 `ecall` 指令。當 `ecall` 執行時，CPU 的執行模式會從核心模式（S-mode）切換到 OpenSBI 模式（M-mode），並呼叫 OpenSBI 的處理函式。處理完畢後，系統會切回核心模式（S-Mode），然後從 `ecall` 指令之後繼續執行。

當應用程式呼叫核心（例如執行系統呼叫）時，也會使用 `ecall` 指令。這個指令的行為就像是呼叫一個更高特權等級的 CPU 模式的函式一樣。

要顯示字元，我們可以使用 `Console Putchar` 這個函式：

> 5.2. Extension: Console Putchar (EID #0x01)
>
> ```c
>   long sbi_console_putchar(int ch)
> ```
>
> 將 `ch` 中的資料輸出至除錯用的主控台（debug console）
>
> 和 `sbi_console_getchar()` 不同，如果仍有待傳輸的字元，或是接收端終端機尚未準備好接收該位元組，這個 SBI 呼叫會阻塞（block）。不過，如果系統中根本不存在主控台，則這個字元會直接被丟棄。
>
> 若成功，這個 SBI 呼叫會回傳 0；失敗則回傳實作相關的負數錯誤碼。
>
> -- 摘自《RISC-V Supervisor Binary Interface Specification》v2.0-rc1

`Console Putchar` 是一個將傳入字元輸出到偵錯主控台的函式。

### 試著執行看看

讓我們試著執行你的程式。如果一切正常，你應該會看到 `Hello World!`：

```
$ ./run.sh
...

Hello World!
```

> [!TIP]
>
> **Hello World 的生命歷程：**
>
> 當你呼叫 SBI 時，字元會經過以下流程被顯示出來：
>
> 1. 核心執行 `ecall` 指令，此時 CPU 跳躍至由 OpenSBI 在啟動時設定的 M-mode 陷阱處理函式（trap handler），該處理函式的位址保存在 `mtvec` 暫存器中。
> 2. 儲存暫存器內容後，會呼叫[以 C 語言撰寫的陷阱處理函式](https://github.com/riscv-software-src/opensbi/blob/0ad866067d7853683d88c10ea9269ae6001bcf6f/lib/sbi/sbi_trap.c#L263)。
> 3. 根據 `eid`，呼叫[對應的 SBI 處理函式](https://github.com/riscv-software-src/opensbi/blob/0ad866067d7853683d88c10ea9269ae6001bcf6f/lib/sbi/sbi_ecall_legacy.c#L63C2-L65)。
> 4. [8250 UART 的裝置驅動程式](https://github.com/riscv-software-src/opensbi/blob/0ad866067d7853683d88c10ea9269ae6001bcf6f/lib/utils/serial/uart8250.c#L77)（[Wikipedia](https://en.wikipedia.org/wiki/8250_UAR)）將字元送給 QEMU。
> 5. QEMU 的 8250 UART 模擬器收到這個字元，並將它送到標準輸出（stdout）。
> 6. 最終，終端機模擬器顯示該字元。
>
> 換句話說，呼叫 `Console Putchar` 函式其實不是魔法 ― 它只是利用了 OpenSBI 已經實作好的裝置驅動程式！

## `printf` 函式

我們已經成功印出了一些字元，下一步要做的是實作 `printf` 函式。

`printf` 可以接收格式字串和數個參數，並將其嵌入於輸出中。例如 `printf("1 + 2 = %d", 1 + 2);` 會輸出 `1 + 2 = 3`

雖然 C 標準函式庫中的 `printf` 功能非常完整，但我們先從一個最小版本開始。 具體來說，我們要實作一個支援以下三種格式指定符（format specifier）的 `printf`：

- `%d`：十進位整數
- `%x`：十六進位整數
- `%s`：字串

由於我們之後在應用程式（application）中也會用到 `printf`，所以這邊就新建一個 `common.c` 檔案，放置核心與 user space 之間共用的程式碼。

以下為 `printf` 的實作：

```c [common.c]
#include "common.h"

void putchar(char ch);

void printf(const char *fmt, ...) {
    va_list vargs;
    va_start(vargs, fmt);

    while (*fmt) {
        if (*fmt == '%') {
            fmt++; // Skip '%'
            switch (*fmt) { // Read the next character
                case '\0': // '%' at the end of the format string
                    putchar('%');
                    goto end;
                case '%': // Print '%'
                    putchar('%');
                    break;
                case 's': { // Print a NULL-terminated string.
                    const char *s = va_arg(vargs, const char *);
                    while (*s) {
                        putchar(*s);
                        s++;
                    }
                    break;
                }
                case 'd': { // Print an integer in decimal.
                    int value = va_arg(vargs, int);
                    unsigned magnitude = value; // https://github.com/nuta/operating-system-in-1000-lines/issues/64
                    if (value < 0) {
                        putchar('-');
                        magnitude = -magnitude;
                    }

                    unsigned divisor = 1;
                    while (magnitude / divisor > 9)
                        divisor *= 10;

                    while (divisor > 0) {
                        putchar('0' + magnitude / divisor);
                        magnitude %= divisor;
                        divisor /= 10;
                    }

                    break;
                }
                case 'x': { // Print an integer in hexadecimal.
                    unsigned value = va_arg(vargs, unsigned);
                    for (int i = 7; i >= 0; i--) {
                        unsigned nibble = (value >> (i * 4)) & 0xf;
                        putchar("0123456789abcdef"[nibble]);
                    }
                }
            }
        } else {
            putchar(*fmt);
        }

        fmt++;
    }

end:
    va_end(vargs);
}
```

這段程式碼是不是出奇地簡潔呢？它會逐字元讀取格式字串，當遇到 `%` 符號時，就讀取下一個字元並執行相對應的格式化操作。除了 `%` 以外的字元則直接以原樣輸出。

對於十進位整數來說，如果 `value` 是負數，我們會先輸出一個 `-`，接著取其絕對值。然後計算一個除數（divisor），用來取得最高位數的數字，並逐位印出。我們使用 `unsigned` 類型的 `magnitude` 來處理 `INT_MIN` 的特殊情況。詳情可參考此 [issue](https://github.com/nuta/operating-system-in-1000-lines/issues/64)。

對於十六進位整數（`%x`），我們從最高位的 *nibble*（4 位元，也就是一個十六進位數字）輸出到最低位。這裡 `nibble` 的值介於 0 到 15，所以我們把它當成字串 `"0123456789abcdef"` 的索引來取得對應字元。

`va_list` 及其相關的 marco 本來定義在 C 標準函式庫的 `<stdarg.h>`。但在本書中，我們直接使用編譯器提供的內建版本，避免依賴標準函式庫。具體來說，我們會在 `common.h` 中如下定義它們：

```c [common.h]
#pragma once

#define va_list  __builtin_va_list
#define va_start __builtin_va_start
#define va_end   __builtin_va_end
#define va_arg   __builtin_va_arg

void printf(const char *fmt, ...);
```

我們只是把它們定義成帶有 `__builtin_` 前綴的別名而已。這些是由編譯器（clang）內建提供的功能（參考：[clang 文件](https://clang.llvm.org/docs/LanguageExtensions.html#variadic-function-builtins)）。其餘細節由編譯器妥善處理，我們不需擔心其底層實作。

現在我們已經完成了 `printf` 的實作，接下來就來讓核心印出一句「Hello World」吧：

```c [kernel.c] {2,5-6}
#include "kernel.h"
#include "common.h"

void kernel_main(void) {
    printf("\n\nHello %s\n", "World!");
    printf("1 + 2 = %d, %x\n", 1 + 2, 0x1234abcd);

    for (;;) {
        __asm__ __volatile__("wfi");
    }
}
```

另外，記得要將 `common.c` 加入到編譯的目標檔案中：

```bash [run.sh] {2}
$CC $CFLAGS -Wl,-Tkernel.ld -Wl,-Map=kernel.map -o kernel.elf \
    kernel.c common.c
```

現在來試試看吧！如果一切順利，你會看到如下所示的 `Hello World!` 和 `1 + 2 = 3, 1234abcd`：

```
$ ./run.sh

Hello World!
1 + 2 = 3, 1234abcd
```

強力盟友「printf 除錯功能」，現在加入你的作業系統了！
