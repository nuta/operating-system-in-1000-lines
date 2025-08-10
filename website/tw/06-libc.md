# C 標準函式庫

在本章中，我們將實作一些基本的資料型別、記憶體操作以及字串處理相關的函式。為了學習的目的，本書將從零開始撰寫這些功能，而不是使用 C 標準函式庫。

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

其中大多數在標準函式庫中都有，但我們另外加了一些實用的型別與巨集：

- `paddr_t`：代表實體記憶體位址的型別。
- `vaddr_t`：代表虛擬記憶體位址的型別，對應標準函式庫中的 `uintptr_t`。
- `align_up`：將 `value` 向上取整至最接近的 `align` 倍數。`align` 必須是 2 的次方。
- `is_aligned`：檢查 `value` 是否為 `align` 的倍數。`align` 必須是 2 的次方。
- `offsetof`：回傳結構體中某成員的偏移量（從結構開頭算起的位元組數）。

`align_up` 與 `is_aligned` 在處理記憶體對齊時非常有用。舉例來說，`align_up(0x1234, 0x1000)` 會回傳 `0x2000`。另外，`is_aligned(0x2000, 0x1000)` 會回傳 `true`，而 `is_aligned(0x2f00, 0x1000)` 則會回傳 `false`。

在這些巨集中使用的 `__builtin_` 開頭的函式，都是 clang 提供的編譯器內建擴充功能。請參閱 [Clang built-in functions and macros](https://clang.llvm.org/docs/LanguageExtensions.html)。

> [!TIP]
>
> 這些巨集其實也可以不用編譯器內建函式，而是用純 C 的方式實作。其中 `offsetof` 的純 C 實作尤其有趣 ;)

## 記憶體操作

接下來我們要實作幾個基本的記憶體操作函式。

`memcpy` 會將 `src` 指向的記憶體區塊中開頭的 `n` 個位元組複製到 `dst` 指向的記憶體區塊中：

```c [common.c]
void *memcpy(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t *) dst;
    const uint8_t *s = (const uint8_t *) src;
    while (n--)
        *d++ = *s++;
    return dst;
}
```

`memset` 函式會將 `buf` 開頭的前 `n` 個位元組填入字元 `c`。這個函式其實在第 4 章中就已經為了初始化 BSS 區段而實作過了。現在，我們要把它從 `kernel.c` 移動到` common.c` 中，以便共用：

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
> `*p++ = c;` 這行程式碼同時進行了「指標解參考（dereferencing）」與「指標位移」。為了更清楚說明，它等價於：
>
> ```c
> *p = c;    // Dereference the pointer
> p = p + 1; // Advance the pointer after the assignment
> ```
>
> 這是一個常見的 C 語言慣用寫法（idiom）。

## 字串操作（String operations）

我們先從 `strcpy` 函式開始。這個函式會將字串從 `src` 複製到 `dst`：

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
> `strcpy` 在複製時，即使 `src` 的長度超過了 `dst` 的記憶體空間，也會繼續複製。這很容易導致錯誤與安全漏洞，因此實務上強烈建議不要使用 `strcpy`，應改用其他替代函式。在正式產品中絕對不要使用 `strcpy`！
>
> 為了簡化學習，本書將暫時使用 `strcpy`，但如果你有餘裕，請嘗試實作並使用 `strcpy_s` 等替代函式。

接下來是 `strcmp` 函式。它會比較 `s1` 和`s2`，並根據以下條件回傳對應的值：

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

> [!INFO]
> 譯者補充：
> 
> 這麼做是因為 `char` 在 C 語言中可能是 `signed` 或 `unsigned`，這會影響比較的結果。使用 `unsigned char` 可以確保比較時的行為一致，特別是在處理非 ASCII 字元時。
> 
> 以下摘錄自標準：
>
> > [[N3550 6.2.5/20]](https://open-std.org/JTC1/SC22/WG14/www/docs/n3550.pdf)：The three types char, signed char, and unsigned char are collectively called the character types. The implementation shall define char to have the same range, representation, and behavior as either signed char or unsigned char.<sup>38)</sup>
>
> > <sup>38)</sup>`CHAR_MIN`, defined in `<limits.h>`, will have one of the values 0 or `SCHAR_MIN`, and this can be used to distinguish the two options. Irrespective of the choice made, `char` is a separate type from the other two and is not compatible with either.
> 
> 可見標準並沒定義一般的 `char` 是有號還是無號的，這個問題被留給了實作來解決，如 gcc 會根據目標平台來決定要用哪個，但你可以透過 [`-funsigned-char`](https://gcc.gnu.org/onlinedocs/gcc-9.2.0/gcc/C-Dialect-Options.html#index-funsigned-char) 來指定：
>
> > -funsigned-char
> > > Let the type char be unsigned, like unsigned char.
> 
> > > Each kind of machine has a default for what char should be. It is either like unsigned char by default or like signed char by default.

`strcmp` 函式常用於判斷兩個字串是否相同。不過使用方式有點不直覺：當 `!strcmp(s1, s2)` 為 `true` 時，表示兩個字串相同（即函式回傳 0）：

```c
if (!strcmp(s1, s2))
    printf("s1 == s2\n");
else
    printf("s1 != s2\n");
```
