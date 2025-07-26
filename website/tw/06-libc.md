# C 標準函式庫

在本章中，我們將實作一些基本的資料型態與記憶體操作，以及字串處理相關的函式。為了學習的目的，本書將從零開始撰寫這些功能，而不是使用 C 標準函式庫。

> [!TIP]
>
> 本章介紹的概念在 C 語言中非常常見，因此你問 ChatGPT 通常都能得到不錯的解釋。如果你在實作或理解上遇到困難，隨時可以試著詢問 ChatGPT，或直接來問我。

## 基本型別

首先，我們在 `common.h` 中定義一些基本型別與便利的巨集（macros）：

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

這些大多數在標準函式庫中都有，但我們另外加了一些實用的類型與巨集：

- `paddr_t`：代表實體記憶體位址的型別。
- `vaddr_t`：代表虛擬記憶體位址的型別，等同於標準函式庫中的 `uintptr_t`。
- `align_up`：將 `value` 向上取整至最接近的 `align` 倍數。`align` 必須是 2 的次方。
- `is_aligned`：檢查 `value` 是否為 `align` 的倍數。`align` 同樣必須是 2 的次方。
- `offsetof`：回傳某個結構體成員的偏移量（也就是從該結構起始位址開始算起的位元組數）。

`align_up` 與 `is_aligned` 在處理記憶體對齊時非常有用。舉例來說，`align_up(0x1234, 0x1000)` 會回傳 `0x2000`。另外，`is_aligned(0x2000, 0x1000)` 為真，而 `is_aligned(0x2f00, 0x1000)` 為假。

在這些巨集中使用的 `__builtin_` 開頭的函式，都是 clang 提供的編譯器內建擴充功能。請參閱 [Clang built-in functions and macros](https://clang.llvm.org/docs/LanguageExtensions.html)。

> [!TIP]
>
> 這些巨集其實也可以不用編譯器內建函式，而是用純 C 的方式實作。其中 `offsetof` 的純 C 實作尤其有趣 ;)

## 記憶體操作

接下來我們要實作幾個基本的記憶體操作函式。

`memcpy` 函式的作用是：將 `src` 所指的記憶體區塊中的 `n` 個位元組（bytes）複製到 `dst` 所指的記憶體區塊中：

```c [common.c]
void *memcpy(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t *) dst;
    const uint8_t *s = (const uint8_t *) src;
    while (n--)
        *d++ = *s++;
    return dst;
}
```

`memset` 函式會將 `buf` 起始的前 `n` 個位元組（bytes）填入值 `c`。這個函式其實在第 4 章中就已經為了初始化 BSS 區段而實作過了。現在，我們要把它從 `kernel.c` 移動到` common.c` 中，以便共用。

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
> `*p++ = c;` 這行程式碼同時進行了「指標解參考（dereferencing）」與「指標遞增」的操作。為了更清楚說明，它等價於：
>
> ```c
> *p = c;    // 解參考指標，將 c 賦值給目前指向的記憶體位置
> p = p + 1; // 指標前進到下一個位置
> ```
>
> 這是一個常見的 C 語言慣用寫法（idiom）。

## 字串操作（String operations）

我們從 `strcpy` 函式開始。這個函式會將字串從 `src` 複製到 `dst`：

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
> `strcpy` 函式在 `src` 的長度超過 `dst` 的記憶體區域時，仍會繼續複製。這很容易導致錯誤與安全漏洞，因此實務上強烈建議不要使用 `strcpy`，應改用其他替代函式。在正式產品中絕對不要使用 `strcpy`！
>
> 為了簡化學習，本書將暫時使用 `strcpy`，但如果你有餘裕，請嘗試實作並使用 `strcpy_s` 等替代函式。

接下來是 `strcmp` 函式。它會比較 `s1` 和`s2`，並根據以下條件傳回值：

| 條件 | 結果 |
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
> 將比較的字元轉型成 `unsigned char *` 是為了符合 [POSIX 規範](https://www.man7.org/linux/man-pages/man3/strcmp.3.html#:~:text=both%20interpreted%20as%20type%20unsigned%20char)
> 
> 這麼做是因為 `char` 在 C 語言中可能是 `signed` 或 `unsigned`，這會影響比較的結果。使用 `unsigned char` 可以確保比較時的行為一致，特別是在處理非 ASCII 字元時。

`strcmp` 函式通常用來判斷兩個字串是否相同。不過使用方式有點不直覺，當你寫 `!strcmp(s1, s2)` 時，代表兩個字串完全一致（因為 `strcmp` 回傳值為 0）：

```c
if (!strcmp(s1, s2))
    printf("s1 == s2\n");
else
    printf("s1 != s2\n");
```
