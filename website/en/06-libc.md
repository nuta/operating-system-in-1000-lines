# C Standard Library

In this chapter, let's implement basic types and memory operations, as well as string manipulation functions. In this book, for the purpose of learning, we'll create these from scratch instead of using C standard library.

> [!TIP]
>
> The concepts introduced in this chapter are very common in C programming, so ChatGPT would provide solid answers. If you struggle with implementation or understanding any part, feel free to try asking it or ping me.

## Basic types

First, let's define some basic types and convenient macros in `common.h`:


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

Most of these are available in the standard library, but we've added a few useful ones:

- `paddr_t`: A type representing physical memory addresses.
- `vaddr_t`: A type representing virtual memory addresses. Equivalent to `uintptr_t` in the standard library.
- `align_up`: Rounds up `value` to the nearest multiple of `align`. `align` must be a power of 2.
- `is_aligned`: Checks if `value` is a multiple of `align`. `align` must be a power of 2.
- `offsetof`: Returns the offset of a member within a structure (how many bytes from the start of the structure).

`align_up` and `is_aligned` are useful when dealing with memory alignment. For example, `align_up(0x1234, 0x1000)` returns `0x2000`. Also, `is_aligned(0x2000, 0x1000)` returns true, but `is_aligned(0x2f00, 0x1000)` is false.

The functions starting with `__builtin_` used in each macro are Clang-specific extensions (built-in functions). See [Clang built-in functions and macros](https://clang.llvm.org/docs/LanguageExtensions.html).

> [!TIP]
>
> These macros can also be implemented in C without built-in functions. The pure C implementation of `offsetof` is particularly interesting ;)

## Memory operations

Next, we implement the following memory operation functions.

The `memcpy` function copies `n` bytes from `src` to `dst`:

```c [common.c]
void *memcpy(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t *) dst;
    const uint8_t *s = (const uint8_t *) src;
    while (n--)
        *d++ = *s++;
    return dst;
}
```

The `memset` function fills the first `n` bytes of `buf` with `c`. This function has already been implemented in Chapter 4 for initializing the bss section. Let's move it from `kernel.c` to `common.c`:

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
> `*p++ = c;` does pointer dereferencing and pointer manipulation in a single statement. For clarity, it's equivalent to:
>
> ```c
> *p = c;    // Dereference the pointer
> p = p + 1; // Advance the pointer after the assignment
> ```
>
> This is an idiom in C.

## String operations

Let's start with `strcpy`. This function copies the string from `src` to `dst`:

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
> The `strcpy` function continues copying even if `src` is longer than the memory area of `dst`. This can easily lead to bugs and vulnerabilities, so it's generally recommended to use alternative functions instead of `strcpy`. Never use it in production!
>
> For simplicity, we'll use `strcpy` in this book, but if you have the capacity, try implementing and using an alternative function (`strcpy_s`) instead.

Next function is the `strcmp` function. It compares `s1` and `s2` and returns:

| Condition | Result |
| --------- | ------ |
| `s1` == `s2` | 0 |
| `s1` > `s2` | Positive value |
| `s1` < `s2` | Negative value |

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
> The casting to `unsigned char *` when comparing is done to conform to the [POSIX specification](https://www.man7.org/linux/man-pages/man3/strcmp.3.html#:~:text=both%20interpreted%20as%20type%20unsigned%20char).

The `strcmp` function is often used to check if two strings are identical. It's a bit counter-intuitive, but the strings are identical when `!strcmp(s1, s2)` is true (i.e., when the function returns zero):

```c
if (!strcmp(s1, s2))
    printf("s1 == s2\n");
else
    printf("s1 != s2\n");
```
