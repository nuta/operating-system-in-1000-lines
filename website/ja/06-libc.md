---
title: C標準ライブラリ
---

# C標準ライブラリ

Hello Worldを済ませたところで、基本的な型やメモリ操作、文字列操作関数を実装しましょう。一般的にはC言語の標準ライブラリ (例: `stdint.h` や `string.h`) を利用しますが、今回は勉強のためにゼロから作ります。

> [!TIP]
>
> 本章で紹介するものはC言語でごく一般的なものなので、ChatGPTに聞くとしっかりと答えてくれる領域です。実装や理解に手こずる部分があった時には試してみてください。便利な時代になりましたね。

## 基本的な型

まずは基本的な型といくつかのマクロを定義します。

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

ほとんどは標準ライブラリにあるものですが、いくつか便利なものを追加しています。

- `paddr_t`: 物理メモリアドレスを表す型。
- `vaddr_t`: 仮想メモリアドレスを表す型。標準ライブラリでいう`uintptr_t`。
- `align_up`: `value`を`align`の倍数に切り上げる。`align`は2のべき乗である必要がある。
- `is_aligned`: `value`が`align`の倍数かどうかを判定する。`align`は2のべき乗である必要がある。
- `offsetof`: 構造体のメンバのオフセット (メンバが構造体の先頭から何バイト目にあるか) を返す。

`align_up`と`is_aligned`は、メモリアラインメントを気にする際に便利です。例えば、`align_up(0x1234, 0x1000)`は`0x2000`を返します。また、`is_aligned(0x2000, 0x1000)`は真となります。

各マクロで使われている`__builtin_`から始まる関数はClangの独自拡張 (ビルトイン関数) です。これらの他にも、[さまざまなビルトイン関数・マクロ](https://clang.llvm.org/docs/LanguageExtensions.html) があります。

> [!TIP]
>
> なお、これらのマクロはビルトイン関数を使わなくても標準的なCのコードで実装することもできます。特に`offsetof`の実装手法は面白いので、興味のある方は検索してみてください。

## メモリ操作

メモリ操作関数を実装しましょう。

`memcpy`関数は`src`から`n`バイト分を`dst`にコピーします。

```c [common.c]
void *memcpy(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t *) dst;
    const uint8_t *s = (const uint8_t *) src;
    while (n--)
        *d++ = *s++;
    return dst;
}
```

`memset`関数は`buf`の先頭から`n`バイト分を`c`で埋めます。この関数は、bssセクションの初期化のために4章で実装済みです。`kernel.c`から`common.c`に移動させましょう。

```c [common.c]
void *memset(void *buf, char c, size_t n) {
    uint8_t *p = (uint8_t *) buf;
    while (n--)
        *p++ = c;
    return buf;
}
```

`*p++ = c;`のように、ポインタの間接参照とポインタの操作を一度にしている箇所がいくつかあります。わかりやすく分解すると次のようになります。C言語ではよく使われる表現です。

```c
*p = c;    //ポインタの間接参照を行う
p = p + 1; // 代入を済ませた後にポインタを進める
```

## 文字列操作

まずは、`strcpy`関数です。この関数は`src`の文字列を`dst`にコピーします。

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
> `strcpy`関数は`dst`のメモリ領域より`src`の方が長い時でも、`dst`のメモリ領域を越えてコピーを行います。バグや脆弱性に繋がりやすいため、一般的には`strcpy`ではなく代替の関数を使うことが推奨されています。
>
> 本書では簡単のため`strcpy`を使いますが、余力があれば代替の関数 (`strcpy_s`) を実装して代わりに使ってみてください。

次に`strcmp`関数です。`s1`と`s2`を比較します。`s1`と`s2`が等しい場合は0を、`s1`の方が大きい場合は正の値を、`s2`の方が大きい場合は負の値を返します。

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

比較する際に `unsigned char *` にキャストしているのは、比較する際は符号なし整数を使うという[POSIXの仕様](https://www.man7.org/linux/man-pages/man3/strcmp.3.html#:~:text=both%20interpreted%20as%20type%20unsigned%20char)に合わせるためです。

`strcmp`関数はよく文字列が同一であるかを判定したい時に使います。若干ややこしいですが、`!strcmp(s1, s2)` の場合 (ゼロが返ってきた場合に) に文字列が同一になります。

```c
if (!strcmp(s1, s2))
    printf("s1 == s2\n");
else
    printf("s1 != s2\n");
```
