---
title: Hello World!
---

# Hello World!

前章では初めてのカーネルの起動に成功しました。レジスタダンプを読むことで確認できたとはいえ、なんだか物足りません。そこで今回は、カーネルから文字列を出力してみましょう。

## 初めてのSBI

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

加えて、新しく`kernel.h`を作成し、SBIの処理結果を返すための構造体を定義しましょう。

```c [kernel.h]
#pragma once

struct sbiret {
    long error;
    long value;
};
```

新たに`sbi_call`関数を追加しました。この関数は、SBIの仕様に沿ってOpenSBIを呼び出すためのものです。具体的な呼び出し規約は以下のとおりです。

> **Chapter 3. Binary Encoding**
>
> All SBI functions share a single binary encoding, which facilitates the mixing of SBI extensions. The SBI specification follows the below calling convention.
>
> - An `ECALL` is used as the control transfer instruction between the supervisor and the SEE.
> - `a7` encodes the SBI extension ID (**EID**),
> - `a6` encodes the SBI function ID (**FID**) for a given extension ID encoded in `a7` for any SBI extension defined in or after SBI v0.2.
> - All registers except `a0` & `a1` must be preserved across an SBI call by the callee.
> - SBI functions must return a pair of values in `a0` and `a1`, with `a0` returning an error code. This is analogous to returning the C structure
>
> ```c
> struct sbiret {
>     long error;
>     long value;
> };
> ```
>
> -- "RISC-V Supervisor Binary Interface Specification" v2.0-rc1 より引用

> [!TIP]
>
> 呼び出し規約中にある「All registers except `a0` & `a1` must be preserved across an SBI call by the callee.」は、「`a0`と`a1`以外のレジスタの値を呼び出し先 (OpenSBI側) が変更してはならない」という意味です。つまり、カーネルからすると、`a2`から`a7`までのレジスタの値は呼び出し後もそのままであることが保証されています。
>
> ちなみに、callee (呼び出し先) の反対はcaller (呼び出し元) です。

各ローカル変数の宣言に使われている`register`と`__asm__("レジスタ名")`は、指定したレジスタに値を入れるようコンパイラに指示するものです。システムコール呼び出し等でよく登場するイディオムです (例: [Linuxのシステムコール呼び出し処理](https://git.musl-libc.org/cgit/musl/tree/arch/riscv64/syscall_arch.h))。本来であればインラインアセンブラで指定できればいいものなのですが、C言語 (正確にはGCC/clangの独自拡張) ではできないためこのトリックを使うことが多いです。

引数を用意したあとに、インラインアセンブラで`ecall`命令を実行します。これを呼び出すと、CPUの実行モードをカーネル用 (S-Mode) からOpenSBI用 (M-Mode) に切り替わり、OpenSBIの処理ハンドラが呼び出されます。OpenSBIの処理が終わると、再びカーネル用に切り替わり、`ecall`命令の次の行から実行が再開されます。ちなみに、`ecall`命令はアプリケーションからカーネルを呼び出す際 (システムコール) にも使われます。「ひとつ下のレイヤを呼び出す」という機能を持つのがこの命令です。

文字の表示には、次の`Console Putchar`機能を使います。

> 5.2. Extension: Console Putchar (EID #0x01)
>
> ```c
>   long sbi_console_putchar(int ch)
> ```
>
> Write data present in ch to debug console.
>
> Unlike sbi_console_getchar(), this SBI call will block if there remain any pending characters to be transmitted or if the receiving terminal is not yet ready to receive the byte. However, if the console doesn’t exist at all, then the character is thrown away.
>
> This SBI call returns 0 upon success or an implementation specific negative error code.
>
> -- "RISC-V Supervisor Binary Interface Specification" v2.0-rc1 より引用

`Console Putchar`は、引数に渡した文字をデバッグコンソールに出力する機能です。この機能を使って、文字列を1文字ずつ出力していきます。

実装ができたら、`run.sh`で実行してみましょう。次のように、`Hello World!`と表示されたら成功です。

```
$ ./run.sh
...

Hello World!
```

> [!TIP]
>
> SBIが呼ばれると、次のような流れで文字が表示されます。
>
> 1. OSが`ecall`命令を実行すると、CPUはM-modeのトラップハンドラ (`mtvec`レジスタ) へジャンプする。トラップハンドラはOpenSBIが起動時に設定している。
> 2. レジスタの保存などを済ませたのちに、Cで書かれた [トラップハンドラ](https://github.com/riscv-software-src/opensbi/blob/0ad866067d7853683d88c10ea9269ae6001bcf6f/lib/sbi/sbi_trap.c#L263) が呼ばれる。
> 3. `eid` に応じた[SBI処理関数が呼ばれる](https://github.com/riscv-software-src/opensbi/blob/0ad866067d7853683d88c10ea9269ae6001bcf6f/lib/sbi/sbi_ecall_legacy.c#L63C2-L65)。
> 4. 8250 UART ([Wikipedia](https://ja.wikipedia.org/wiki/8250_UART)) の[デバイスドライバ](https://github.com/riscv-software-src/opensbi/blob/0ad866067d7853683d88c10ea9269ae6001bcf6f/lib/utils/serial/uart8250.c#L77) がQEMUへ文字を送信する。
> 5. QEMUの8250 UARTエミュレーション実装が文字を受け取り、標準出力に文字を送る。
> 6. 端末エミュレータがその文字を表示する。

## `printf`関数

ようやくカーネル開発っぽくなってきました！文字が表示できたら、次に欲しいのが`printf`関数です。

`printf`関数は、第1引数にフォーマット文字列を取り、第2引数以降にフォーマット文字列に埋め込む値を取ります。例えば、`printf("1 + 2 = %d", 1 + 2)`とすると、`1 + 2 = 3`と表示されます。

C標準ライブラリに入っているような`printf`関数は非常に豊富な機能を持っていますが、今回は最低限の機能に絞って実装してみましょう。具体的には`%d` (10進数)、`%x` (16進数)、`%s` (文字列) の3つのフォーマット文字列に対応した`printf`関数を実装します。

`printf`関数は将来アプリケーション側でも使いたいので、`kernel.c`ではなくカーネル・ユーザーランド共通のコード用のファイル`common.c`を新しく作ることにします。以下が`printf`関数の全体像です。

```c [common.c]
#include "common.h"

void putchar(char ch);

void printf(const char *fmt, ...) {
    va_list vargs;
    va_start(vargs, fmt);

    while (*fmt) {
        if (*fmt == '%') {
            fmt++;
            switch (*fmt) {
                case '\0':
                    putchar('%');
                    goto end;
                case '%':
                    putchar('%');
                    break;
                case 's': {
                    const char *s = va_arg(vargs, const char *);
                    while (*s) {
                        putchar(*s);
                        s++;
                    }
                    break;
                }
                case 'd': {
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
                case 'x': {
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

案外スッキリしているのではないでしょうか。1文字ずつ見ていき、「`%`」であれば次の文字を見てフォーマット文字列に応じた処理を行います。`%`以外の文字はそのまま出力します。

10進数の場合は、まず`value`が負の数であれば`-`を出力してから、その絶対値を`value`に代入します。次に、`value`の最上位の桁を求めるために「何桁まであるか」を計算して`divisor` (英語で「割る数」) に入れておきます。あとは、`divisor`を使って`value`の最上位の桁から順に出力していきます。

16進数の場合は、最上位のニブル (16進数の1桁、4ビット) から順に出力していきます。ここで`nibble`は0から15の整数になるので、`"0123456789abcdef"`という文字列の何文字目かで`nibble`に対応する文字を取り出しています。

`va_list`などは、C標準ライブラリの`<stdarg.h>`に定義されているマクロですが、本書では標準ライブラリに頼らずに自前で用意します。具体的には`common.h`に次のように定義しておきます。

```c [common.h]
#pragma once

#define va_list  __builtin_va_list
#define va_start __builtin_va_start
#define va_end   __builtin_va_end
#define va_arg   __builtin_va_arg

void printf(const char *fmt, ...);
```

単純に`__builtin_`がついたものの別名として定義しています。では、`__builtin_`がついたものは誰が用意するのかというと、コンパイラ (clang) が用意しているものです ([参考: clangのドキュメント](https://clang.llvm.org/docs/LanguageExtensions.html#variadic-function-builtins))。あとはコンパイラがよしなにやってくれるので、特に気にする必要はありません。

これで`printf`関数が使えるようになりました。`kernel.c`にいくつか`printf`関数を使ったコードを書いてみましょう。

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

最後に`common.c`をコンパイル対象に追加します。

```bash [run.sh] {2}
$CC $CFLAGS -Wl,-Tkernel.ld -Wl,-Map=kernel.map -o kernel.elf \
    kernel.c common.c
```

では`run.sh`を実行してみましょう。次のように、`Hello World!`と`1 + 2 = 3, 1234abcd`が表示されたら成功です。

```
$ ./run.sh

Hello World!
1 + 2 = 3, 1234abcd
```

これでプログラミングの強い味方「printfデバッグ」が仲間に加わりました！
