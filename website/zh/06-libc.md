---
title: C 标准库
---

# C 标准库

在本章中，让我们实现基本类型和内存操作，以及字符串操作函数。在本书中，出于学习目的，我们将从头开始创建这些功能，而不是使用 C 标准库。

> [!TIP]
>
> 本章介绍的概念在 C 编程中非常常见，因此 ChatGPT 会提供可靠的答案。如果你在实现或理解任何部分时遇到困难，随时可以尝试询问它或联系我。

## 基本类型

首先，让我们在 `common.h` 中定义一些基本类型和便捷宏：


```c [common.h] {1-15,21-24}
typedef int bool;
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;
typedef uint32_t size_t;
typedef uint32_t paddr_t;
typedef uint32_t vaddr_t;

#define true  1
#define false 0
#define NULL  ((void *) 0)
#define align_up(value, align)   __builtin_align_up(value, align)
#define is_aligned(value, align) __builtin_is_aligned(value, align)
#define offsetof(type, member)   __builtin_offsetof(type, member)
#define va_list  __builtin_va_list
#define va_start __builtin_va_start
#define va_end   __builtin_va_end
#define va_arg   __builtin_va_arg

void *memset(void *buf, char c, size_t n);
void *memcpy(void *dst, const void *src, size_t n);
char *strcpy(char *dst, const char *src);
int strcmp(const char *s1, const char *s2);
void printf(const char *fmt, ...);
```

这些大多数在标准库中都有，但我们添加了一些有用的类型：

- `paddr_t`：表示物理内存地址的类型。
- `vaddr_t`：表示虚拟内存地址的类型。相当于标准库中的 `uintptr_t`。
- `align_up`：将 `value` 向上舍入到 `align` 的最近倍数。`align` 必须是 2 的幂。
- `is_aligned`：检查 `value` 是否是 `align` 的倍数。`align` 必须是 2 的幂。
- `offsetof`：返回结构体中成员的偏移量（距离结构体开始的字节数）。

`align_up` 和 `is_aligned` 在处理内存对齐时很有用。例如，`align_up(0x1234, 0x1000)` 返回 `0x2000`。同样，`is_aligned(0x2000, 0x1000)` 返回 true，但 `is_aligned(0x2f00, 0x1000)` 是 false。

每个宏中使用的以 `__builtin_` 开头的函数是 Clang 特定的扩展（内置函数）。参见 [Clang 内置函数和宏](https://clang.llvm.org/docs/LanguageExtensions.html)。

> [!TIP]
>
> 这些宏也可以在不使用内置函数的情况下用 C 语言实现。`offsetof` 的纯 C 实现特别有趣 ;)

## 内存操作

接下来，我们实现以下内存操作函数。

`memcpy` 函数将 `n` 字节从 `src` 复制到 `dst`：

```c [common.c]
void *memcpy(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t *) dst;
    const uint8_t *s = (const uint8_t *) src;
    while (n--)
        *d++ = *s++;
    return dst;
}
```

`memset` 函数用 `c` 填充 `buf` 的前 `n` 个字节。这个函数已经在第 4 章中实现用于初始化 bss 段。让我们将它从 `kernel.c` 移到 `common.c`：

```c [common.c]
void *memset(void *buf, char c, size_t n) {
    uint8_t *p = (uint8_t *) buf;
    while (n--)
        *p++ = c;
    return buf;
}
```

> [!TIP]
>
> `*p++ = c;` 在一个语句中完成指针解引用和指针操作。为了清晰起见，它相当于：
>
> ```c
> *p = c;    // 解引用指针
> p = p + 1; // 赋值后移动指针
> ```
>
> 这是 C 语言中的一个惯用语。

## 字符串操作

让我们从 `strcpy` 开始。这个函数将字符串从 `src` 复制到 `dst`：

```c [common.c]
char *strcpy(char *dst, const char *src) {
    char *d = dst;
    while (*src)
        *d++ = *src++;
    *d = '\0';
    return dst;
}
```

> [!WARNING]
>
> `strcpy` 函数即使在 `src` 比 `dst` 的内存区域长时也会继续复制。这很容易导致 bug 和漏洞，所以通常建议使用替代函数而不是 `strcpy`。永远不要在生产环境中使用它！
>
> 为了简单起见，我们将在本书中使用 `strcpy`，但如果你有能力，请尝试实现并使用替代函数（`strcpy_s`）。

下一个函数是 `strcmp` 函数。它比较 `s1` 和 `s2` 并返回：

| 条件 | 结果 |
| --------- | ------ |
| `s1` == `s2` | 0 |
| `s1` > `s2` | 正值 |
| `s1` < `s2` | 负值 |

```c [common.c]
int strcmp(const char *s1, const char *s2) {
    while (*s1 && *s2) {
        if (*s1 != *s2)
            break;
        s1++;
        s2++;
    }

    return *(unsigned char *)s1 - *(unsigned char *)s2;
}
```

> [!TIP]
>
> 在比较时转换为 `unsigned char *` 是为了符合 [POSIX 规范](https://www.man7.org/linux/man-pages/man3/strcmp.3.html#:~:text=both%20interpreted%20as%20type%20unsigned%20char)。

`strcmp` 函数经常用于检查两个字符串是否相同。这有点反直觉，但当 `!strcmp(s1, s2)` 为真时（即当函数返回零时），字符串是相同的：

```c
if (!strcmp(s1, s2))
    printf("s1 == s2\n");
else
    printf("s1 != s2\n");
```
