---
title: 内核恐慌（Kernel Panic）
---

# Kernel Panic

内核恐慌（kernel panic）发生在内核遇到不可恢复的错误时，类似于 Go 或 Rust 中的 `panic` 概念。你是否在 Windows 上见过蓝屏？让我们在我们的内核中实现相同的概念来处理致命错误。

以下 `PANIC` 宏是内核恐慌的实现：

```c [kernel.h]
#define PANIC(fmt, ...)                                                        \
    do {                                                                       \
        printf("PANIC: %s:%d: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__);  \
        while (1) {}                                                           \
    } while (0)
```

它打印出恐慌发生的位置，然后进入一个无限循环来停止处理。我们在这里将其定义为宏。这样做的原因是为了正确显示源文件名（`__FILE__`）和行号（`__LINE__`）。如果我们将其定义为函数，`__FILE__` 和 `__LINE__` 将显示 `PANIC` 被定义的文件名和行号，而不是它被调用的位置。

这个宏还使用了两个惯用语：

第一个惯用语是 `do-while` 语句。由于它是 `while (0)`，这个循环只执行一次。这是定义由多个语句组成的宏的常见方式。简单地用 `{ ...}` 封装可能会在与 `if` 等语句组合时导致意外的行为（参见[这个清晰的例子](https://www.jpcert.or.jp/sc-rules/c-pre10-c.html)）。另外，注意每行末尾的反斜杠（`\`）。虽然宏是在多行上定义的，但在展开时换行符会被忽略。

第二个惯用语是 `##__VA_ARGS__`。这是一个用于定义接受可变数量参数的宏的有用编译器扩展（参考：[GCC 文档](https://gcc.gnu.org/onlinedocs/gcc/Variadic-Macros.html)）。当可变参数为空时，`##` 会删除前面的 `,`。这使得即使只有一个参数，如 `PANIC("booted!")`，编译也能成功。

## 让我们试试

让我们试试使用 `PANIC`。你可以像使用 `printf` 一样使用它：

```c [kernel.c] {4-5}
void kernel_main(void) {
    memset(__bss, 0, (size_t) __bss_end - (size_t) __bss);

    PANIC("booted!");
    printf("unreachable here!\n");
}
```

在 QEMU 中试试，确认显示了正确的文件名和行号，并且 `PANIC` 之后的处理没有执行（即，没有显示 `"unreachable here!"`）：

```
$ ./run.sh
PANIC: kernel.c:46: booted!
```

Windows 的蓝屏和 Linux 的内核恐慌都很可怕，但在你自己的内核中，你不觉得这是一个很好的功能吗？这是一个“优雅崩溃”的机制，带有人类可读的提示。
