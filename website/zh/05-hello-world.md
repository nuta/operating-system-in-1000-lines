---
title: Hello World!
---

# Hello World!

在上一章中，我们成功启动了第一个内核。尽管我们可以通过读取寄存器转储来确认它正常工作，但这种方式仍然感觉不够直观。

在本章中，让我们通过从内核输出字符串来使其更加明显。

## 向 SBI 说“hello”

在上一章中，我们了解到 SBI 是一个“操作系统的 API”。要调用 SBI 使用其功能，我们使用 `ecall` 指令：

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

同时，创建一个新的 `kernel.h` 文件并定义返回值结构：

```c [kernel.h]
#pragma once

struct sbiret {
    long error;
    long value;
};
```

我们新添加了 `sbi_call` 函数。这个函数设计用于按照 SBI 规范调用 OpenSBI。具体的调用约定如下：

> **第 3 章 二进制编码**
>
> 所有 SBI 函数共享一个统一的二进制编码，这便于混合使用 SBI 扩展。SBI 规范遵循以下调用约定：
>
> - 使用 `ECALL` 作为管理模式和 SEE 之间的控制转移指令。
> - `a7` 编码 SBI 扩展 ID（**EID**）
> - 对于在 SBI v0.2 中或之后定义的任何 SBI 扩展，`a6` 编码给定扩展 ID 的 SBI 功能 ID（**FID**）
> - 除了 `a0` 和 `a1` 外，所有寄存器必须在被调用方的 SBI 调用过程中保持不变。
> - SBI 函数必须在 `a0` 和 `a1` 中返回一对值，其中 `a0` 返回错误代码。这类似于返回 C 结构体
>
> ```c
> struct sbiret {
>     long error;
>     long value;
> };
> ```
>
> -- "RISC-V Supervisor Binary Interface Specification" v2.0-rc1

> [!TIP]
>
> *"除了 `a0` 和 `a1` 外，所有寄存器必须在被调用方的 SBI 调用过程中保持不变"*意味着被调用方（OpenSBI 端）不得更改***除了*** `a0` 和 `a1` 之外的寄存器值。换句话说，从内核的角度来看，可以保证调用后寄存器（`a2` 到 `a7`）将保持不变。

在每个局部变量声明中使用的 `register` 和 `__asm__("register name")` 要求编译器将值放在指定的寄存器中。这在系统调用调用过程中是一个常见用法（例如，[Linux 系统调用调用过程](https://git.musl-libc.org/cgit/musl/tree/arch/riscv64/syscall_arch.h)）。

准备好参数后，在内联汇编中执行 `ecall` 指令。当这个指令被调用时，CPU 的执行模式从内核模式（S-Mode）切换到 OpenSBI 模式（M-Mode），并调用 OpenSBI 的处理程序。完成后，它切回内核模式，并在 `ecall` 指令之后继续执行。

当应用程序调用内核（系统调用）时也使用 `ecall` 指令。这个指令的行为类似于对更高权限 CPU 模式的函数调用。

要显示字符，我们可以使用 `Console Putchar` 函数：

> 5.2. 扩展：Console Putchar（EID #0x01）
>
> ```c
>   long sbi_console_putchar(int ch)
> ```
>
> 将 ch 中的数据写入调试控制台。
>
> 与 sbi_console_getchar() 不同，如果还有任何待传输的字符或者接收终端尚未准备好接收字节，此 SBI 调用将阻塞。但是，如果控制台根本不存在，则字符将被丢弃。
>
> 此 SBI 调用在成功时返回 0，或返回实现特定的负数错误代码。
>
> -- "RISC-V Supervisor Binary Interface Specification" v2.0-rc1

`Console Putchar` 是一个将作为参数传递的字符输出到调试控制台的函数。

### 试一试

试一试你的实现。如果它正常运行，你应该看到 `Hello World!`：

```
$ ./run.sh
...

Hello World!
```

> [!TIP]
>
> **Hello World 的生命周期：**
>
> 当调用 SBI 时，字符将按以下方式显示：
>
> 1. 内核执行 `ecall` 指令。CPU 跳转到 M 模式陷阱处理程序（`mtvec` 寄存器），这是由 OpenSBI 在启动期间设置的。
> 2. 保存寄存器后，调用[用 C 编写的陷阱处理程序](https://github.com/riscv-software-src/opensbi/blob/0ad866067d7853683d88c10ea9269ae6001bcf6f/lib/sbi/sbi_trap.c#L263)。
> 3. 根据 `eid`，调用[相应的 SBI 处理函数](https://github.com/riscv-software-src/opensbi/blob/0ad866067d7853683d88c10ea9269ae6001bcf6f/lib/sbi/sbi_ecall_legacy.c#L63C2-L65)。
> 4. [设备驱动程序](https://github.com/riscv-software-src/opensbi/blob/0ad866067d7853683d88c10ea9269ae6001bcf6f/lib/utils/serial/uart8250.c#L77)（用于 8250 UART([Wikipedia](https://en.wikipedia.org/wiki/8250_UART))）将字符发送到 QEMU。
> 5. QEMU 的 8250 UART 模拟实现接收字符并将其发送到标准输出。
> 6. 终端模拟器显示字符。
>
> 也就是说，调用 `Console Putchar` 函数根本不是魔法 - 它只是使用了在 OpenSBI 中实现的设备驱动程序！

## `printf` 函数

我们已经成功打印了一些字符。下一步是实现 `printf` 函数。

`printf` 函数接受一个格式字符串和要嵌入输出的值。例如，`printf("1 + 2 = %d", 1 + 2)` 将显示 `1 + 2 = 3`。

虽然 C 标准库中的 `printf` 有非常丰富的功能集，但让我们从最简单的开始。具体来说，我们将实现一个支持三种格式说明符的 `printf`：`%d`（十进制）、`%x`（十六进制）和 `%s`（字符串）。

由于我们也会在应用程序中使用 `printf`，让我们创建一个新文件 `common.c` 用于内核和用户空间之间共享的代码。

以下是 `printf` 函数的实现：

```c [common.c]
#include "common.h"

void putchar(char ch);

void printf(const char *fmt, ...) {
    va_list vargs;
    va_start(vargs, fmt);

    while (*fmt) {
        if (*fmt == '%') {
            fmt++; // 跳过 '%'
            switch (*fmt) { // 读取下一个字符
                case '\0': // 当 '%' 作为格式字符串的末尾
                    putchar('%');
                    goto end;
                case '%': // 打印 '%'
                    putchar('%');
                    break;
                case 's': { // 打印以 NULL 结尾的字符串
                    const char *s = va_arg(vargs, const char *);
                    while (*s) {
                        putchar(*s);
                        s++;
                    }
                    break;
                }
                case 'd': { // 以十进制打印整型
                    int value = va_arg(vargs, int);
                    if (value < 0) {
                        putchar('-');
                        value = -value;
                    }

                    int divisor = 1;
                    while (value / divisor > 9)
                        divisor *= 10;

                    while (divisor > 0) {
                        putchar('0' + value / divisor);
                        value %= divisor;
                        divisor /= 10;
                    }

                    break;
                }
                case 'x': { // 以十六进制打印整型
                    int value = va_arg(vargs, int);
                    for (int i = 7; i >= 0; i--) {
                        int nibble = (value >> (i * 4)) & 0xf;
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

它看起来出奇的简洁，对吧？它逐个字符遍历格式字符串，如果遇到 `%`，我们查看下一个字符并执行相应的格式化操作。除 `%` 之外的字符按原样打印。

对于十进制数字，如果 `value` 为负，我们首先输出一个 `-` 然后获取其绝对值。然后我们计算除数以获取最高位数字，并逐个输出数字。

对于十六进制数字，我们从最高位 *nibble*（一个十六进制数字，4 位）到最低位输出。这里，`nibble` 是一个从 0 到 15 的整数，所以我们使用它作为字符串 `"0123456789abcdef"` 中的索引来获取相应的字符。

`va_list` 和相关宏在 C 标准库的 `<stdarg.h>` 中定义。在本书中，我们直接使用编译器内置功能，而不依赖标准库。具体来说，我们将在 `common.h` 中定义它们如下：

```c [common.h]
#pragma once

#define va_list  __builtin_va_list
#define va_start __builtin_va_start
#define va_end   __builtin_va_end
#define va_arg   __builtin_va_arg

void printf(const char *fmt, ...);
```

我们只是将这些定义为带有 `__builtin_` 前缀的版本的别名。它们是编译器（clang）本身提供的内置功能（[参考：clang 文档](https://clang.llvm.org/docs/LanguageExtensions.html#variadic-function-builtins)）。编译器将适当处理剩余的部分，所以我们不需要担心它。

现在我们已经实现了 `printf`。让我们添加一个来自内核的"Hello World"：

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

另外，将 `common.c` 添加到编译目标：

```bash [run.sh] {2}
$CC $CFLAGS -Wl,-Tkernel.ld -Wl,-Map=kernel.map -o kernel.elf \
    kernel.c common.c
```

现在，让我们试试运行它！你将看到如下所示的 `Hello World!` 和 `1 + 2 = 3, 1234abcd`：

```
$ ./run.sh

Hello World!
1 + 2 = 3, 1234abcd
```

强大的盟友“printf 调试”已经加入你的操作系统！
