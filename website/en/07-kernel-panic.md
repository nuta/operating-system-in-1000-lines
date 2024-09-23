---
title: Kernel Panic
layout: chapter
lang: en
---

> [!NOTE]
>
> **Translation of this English version is in progress.**

A kernel panic occurs when the kernel encounters an unrecoverable error, similar to the concept of `panic` in Go or Rust. The following `PANIC` macro is the implementation of kernel panic:

```c:kernel.h
#define PANIC(fmt, ...)                                                        \
    do {                                                                       \
        printf("PANIC: %s:%d: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__);  \
        while (1) {}                                                           \
    } while (0)
```

After displaying the error content, it enters an infinite loop to halt processing. We define it as a macro here. The reason for this is to correctly display the source file name (`__FILE__`) and line number (`__LINE__`). If we defined this as a function, `__FILE__` and `__LINE__` would show the file name and line number where `PANIC` is defined, not where it's called.

This macro introduces two [idioms](https://en.wikipedia.org/wiki/Programming_idiom):

The first is the `do-while` statement. Since it's `while (0)`, this loop is only executed once. This is a common way to define macros consisting of multiple statements. Simply enclosing with `{ ...}` can lead to unintended behavior when combined with statements like `if` (see [this clear example](https://www.jpcert.or.jp/sc-rules/c-pre10-c.html)). Also, note the backslash (`\`) at the end of each line. Although the macro is defined over multiple lines, line breaks are ignored when expanded.

The second idiom is `##__VA_ARGS__`. This is a useful compiler extension for defining macros that accept a variable number of arguments (reference: [GCC documentation](https://gcc.gnu.org/onlinedocs/gcc/Variadic-Macros.html)). The `##` removes the preceding `,` when the variable arguments are empty. This allows compilation to succeed even when there's only one argument, like `PANIC("booted!")`.

Now that we understand how to write macros, let's try using `PANIC`. It's used in the same way as `printf`:

```c:kernel.c {4-5}
void kernel_main(void) {
    memset(__bss, 0, (size_t) __bss_end - (size_t) __bss);

    PANIC("booted!");
    printf("unreachable here!\n");
}
```

Try running it and confirm that the correct file name and line number are displayed, and that the processing after `PANIC` is not executed (i.e., `"unreachable here!"` is not displayed):

```plain
$ ./run.sh
PANIC: kernel.c:46: booted!
```
