---
title: アプリケーション
---

# アプリケーション

本章では、カーネルから一旦離れて、最初のユーザーランドのプログラムとそのビルド方法を見ていきます。

## メモリレイアウト

前章ではページングという仕組みを使ってプロセスごとの独立した仮想アドレス空間を実現しました。本章では、アプリケーションを仮想アドレス空間上のどこに配置するかを考えます。

アプリケーションの実行ファイルをどこに配置するかを定義する、新しいリンカスクリプト (`user.ld`) を作成しましょう。

```ld [user.ld]
ENTRY(start)

SECTIONS {
    . = 0x1000000;

    .text :{
        KEEP(*(.text.start));
        *(.text .text.*);
    }

    .rodata : ALIGN(4) {
        *(.rodata .rodata.*);
    }

    .data : ALIGN(4) {
        *(.data .data.*);
    }

    .bss : ALIGN(4) {
        *(.bss .bss.* .sbss .sbss.*);

        . = ALIGN(16); /* https://github.com/nuta/operating-system-in-1000-lines/pull/23 */
        . += 64 * 1024; /* 64KB */
        __stack_top = .;

       ASSERT(. < 0x1800000, "too large executable");
    }
}
```

筆者が適当に決めた、カーネルアドレスと被らない領域 (`0x1000000`から `0x1800000`の間) にアプリケーションの各データを配置することにします。大体カーネルのリンカスクリプトと同じではないでしょうか。

ここで登場している`ASSERT`は、第一引数の条件が満たされていなければリンク処理 (本書ではclangコマンド) を失敗させるものです。ここでは、`.bss`セクションの末尾、つまりアプリケーションの末尾が `0x1800000` を超えていないことを確認しています。実行ファイルが意図せず大きすぎることのないようにするためです。

## ユーザーランド用ライブラリ

次にユーザーランド用ライブラリを作成しましょう。まずはアプリケーションの起動に必要な処理だけを書きます。

```c [user.c]
#include "user.h"

extern char __stack_top[];

__attribute__((noreturn)) void exit(void) {
    for (;;);
}

void putchar(char ch) {
    /* 後で実装する */
}

__attribute__((section(".text.start")))
__attribute__((naked))
void start(void) {
    __asm__ __volatile__(
        "mv sp, %[stack_top]\n"
        "call main\n"
        "call exit\n" ::[stack_top] "r"(__stack_top));
}
```

アプリケーションの実行は`start`関数から始まります。カーネルのブート処理と同じように、スタックポインタを設定し、アプリケーションの`main`関数を呼び出します。

アプリケーションを終了する`exit`関数も用意しておきます。ただし、ここでは無限ループを行うだけにとどめておきます。

また、`common.c` の `printf` 関数が参照している `putchar` 関数も定義しておきます。のちほど実装します。

カーネルの初期化処理と異なる点として、`.bss`セクションをゼロで埋める処理 (ゼロクリア) をしていません。これは、カーネルがゼロで埋めていることを保証してくるからです (`alloc_pages`関数)。

> [!TIP]
>
> ゼロクリアは実用的なOSでも行われている処理で、ゼロで埋めないと以前そのメモリ領域を使っていた他のプロセスの情報が残ってしまうためです。パスワードのような機密情報が残ってしまっていたら大変です。

加えて、ユーザランド用ライブラリのヘッダファイル (`user.h`) も用意しておきましょう。

```c [user.h]
#pragma once
#include "common.h"

__attribute__((noreturn)) void exit(void);
void putchar(char ch);
```

## 最初のアプリケーション

最初のアプリケーション (`shell.c`) は次のものを用意します。カーネルの時と同じく、文字を表示するのにも一手間必要なので、無限ループを行うだけにとどめておきます。

```c [shell.c]
#include "user.h"

void main(void) {
    for (;;);
}
```

## アプリケーションのビルド

最後にアプリケーションのビルド処理です。

```bash [run.sh] {1,3-6,10}
OBJCOPY=/opt/homebrew/opt/llvm/bin/llvm-objcopy

# シェルをビルド
$CC $CFLAGS -Wl,-Tuser.ld -Wl,-Map=shell.map -o shell.elf shell.c user.c common.c
$OBJCOPY --set-section-flags .bss=alloc,contents -O binary shell.elf shell.bin
$OBJCOPY -Ibinary -Oelf32-littleriscv shell.bin shell.bin.o

# カーネルをビルド
$CC $CFLAGS -Wl,-Tkernel.ld -Wl,-Map=kernel.map -o kernel.elf \
    kernel.c common.c shell.bin.o
```

最初の`$CC`を呼び出している箇所はカーネルと同じで、clangがコンパイル・リンク処理を一括して行います。

1つ目の`$OBJCOPY`は、ビルドした実行ファイル (ELF形式) を生バイナリ形式 (raw binary) に変換する処理です。まず、生バイナリとは何かというと、ベースアドレス (ここでは0x1000000) から実際にメモリ上に展開される内容が入ったものです。OSは生バイナリの内容をそのままコピーするだけで、アプリケーションをメモリ上に展開できます。一般的なOSでは、ELFのような展開先の定義とメモリ上のデータが分かれた形式を使いますが、本書では簡単のために生バイナリを使います。

2つ目の`$OBJCOPY`は、生バイナリ形式の実行イメージを、C言語に埋め込める形式に変換する処理です。`llvm-nm`コマンドで何が入っているかを見てみましょう。

```
$ llvm-nm shell.bin.o
00010260 D _binary_shell_bin_end
00010260 A _binary_shell_bin_size
00000000 D _binary_shell_bin_start
```

`_binary_`という接頭辞に続いて、ファイル名、そして`start`、`end`、`size`が続いています。それぞれ、実行イメージの先頭、終端、サイズを示すシンボルです。実際には次のように利用します。

```c
extern char _binary_shell_bin_start[];
extern char _binary_shell_bin_size[];

void main(void) {
    uint8_t *shell_bin = (uint8_t *) _binary_shell_bin_start;
    printf("shell_bin size = %d\n", (int) _binary_shell_bin_size);
    printf("shell_bin[0] = %x (%d bytes)\n", shell_bin[0]);
}
```

このプログラムは、`shell.bin`のファイルサイズと、ファイル内容の1バイト目を出力します。つまり、次のように`_binary_shell_bin_start`変数にファイル内容が入っているかように扱えます。

```c
char _binary_shell_bin_start[] = "shell.binのファイル内容";
```

また、`_binary_shell_bin_size`変数には、ファイルサイズが入っています。ただし少し変わった使い方をします。もう一度`llvm-nm`で確認してみましょう。

```
$ llvm-nm shell.bin.o | grep _binary_shell_bin_size
00010454 A _binary_shell_bin_size

$ ls -al shell.bin   ← shell.bin.oではなくshell.binであることに注意
-rwxr-xr-x 1 seiya staff 66644 Oct 24 13:35 shell.bin

$ python3 -c 'print(0x10454)'
66644
```

出力の1列目は、シンボルのアドレスです。この`10454`という16進数はファイルの大きさと一致しますが、これは偶然ではありません。一般的に、`.o`ファイルの各アドレスの値はリンカによって決定されます。しかし、`_binary_shell_bin_size`は特別なのです。

2列目の`A`は、`_binary_shell_bin_size`のアドレスがリンカによって変更されない種類のシンボル (absolute) であることを示しています。
`char _binary_shell_bin_size[]`という適当な型の配列として定義することで、`_binary_shell_bin_size`はそのアドレスを格納したポインタとして扱われることになります。ただし、ここではファイルサイズをアドレスとして埋め込んでいるので、キャストするとファイルサイズになるのです。オブジェクトファイルの仕組みをうまく使った、ちょっとした小技が使われています。

最後に、カーネルのclangへの引数に、生成した `shell.bin.o` を追加しています。これで、最初に起動すべきアプリケーションの実行ファイルを、カーネルイメージに埋め込めるようになりました。

## 逆アセンブリを見てみる

逆アセンブリしてみると、リンカスクリプトに定義されている通り、`.text.start`セクションは実行ファイルの先頭に配置され、`0x1000000`に`start`関数が配置されていることがわかります。

```
$ llvm-objdump -d shell.elf

shell.elf:	file format elf32-littleriscv

Disassembly of section .text:

01000000 <start>:
 1000000: 37 05 01 01  	lui	a0, 4112
 1000004: 13 05 05 26  	addi	a0, a0, 608
 1000008: 2a 81        	mv	sp, a0
 100000a: 19 20        	jal	0x1000010 <main>
 100000c: 29 20        	jal	0x1000016 <exit>
 100000e: 00 00        	unimp

01000010 <main>:
 1000010: 01 a0        	j	0x1000010 <main>
 1000012: 00 00        	unimp

01000016 <exit>:
 1000016: 01 a0        	j	0x1000016 <exit>
```
