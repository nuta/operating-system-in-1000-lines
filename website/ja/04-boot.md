---
title: ブート
---

# ブート

まず最初に必要なもの、それは起動 (ブート) 処理です。アプリケーションであればOSがいい感じに`main`関数を呼び出してくれますが、カーネルはハードウェアの仕様に応じた初期化処理を自分で書く必要があります。

## Supervisor Binary Interface (SBI)

RISC-VのOSを実装する上で非常に便利なのが Supervisor Binary Interface (SBI) です。SBIは、いわば「カーネルのためのAPI」です。APIが文字の表示やファイルの読み書きといったアプリケーションに提供する機能を定義しているように、SBIはファームウェアがOSに提供する機能を定義しています。

SBIの仕様書は[GitHub上で公開されています](https://github.com/riscv-non-isa/riscv-sbi-doc/releases)。デバッグコンソール (例: シリアルポート) 上での文字の表示や、再起動・シャットダウン、タイマーの設定など、あると便利な機能が定義されています。

SBIの実装例として有名なのが[OpenSBI](https://github.com/riscv-software-src/opensbi)です。QEMUではデフォルトでOpenSBIが起動し、ハードウェア特有の初期化処理を済ませた後、カーネルを起動してくれます。

## OpenSBIを起動しよう

まずは、OpenSBIが起動する様子を見てみましょう。次のように`run.sh`という名前のシェルスクリプトを作成しましょう。

```
$ touch run.sh
$ chmod +x run.sh
```

```bash [run.sh]
#!/bin/bash
set -xue

# QEMUのファイルパス
QEMU=qemu-system-riscv32

# QEMUを起動
$QEMU -machine virt -bios default -nographic -serial mon:stdio --no-reboot
```

QEMUの起動オプションは次の通りです。

- `-machine virt`: virtマシンとして起動する。ちなみに`-machine '?'`オプションで対応している環境を確認できる。
- `-bios default`: デフォルトのBIOS (ここではOpenSBI) を使用する。
- `-nographic`: QEMUをウィンドウなしで起動する。
- `-serial mon:stdio`: QEMUの標準入出力を仮想マシンのシリアルポートに接続する。`mon:`を指定することで、QEMUモニターへの切り替えも可能になる。
- `--no-reboot`: 仮想マシンがクラッシュしたら、再起動せずに停止させる (デバッグに便利)。

> [!TIP]
>
> macOSのHomebrew版 QEMUのファイルパスは、次のコマンドで確認できます。
>
> ```
> $ ls $(brew --prefix)/bin/qemu-system-riscv32
> /opt/homebrew/bin/qemu-system-riscv32
> ```

起動すると、次のようなログが表示されます。

```
$ ./run.sh

OpenSBI v1.2
   ____                    _____ ____ _____
  / __ \                  / ____|  _ \_   _|
 | |  | |_ __   ___ _ __ | (___ | |_) || |
 | |  | | '_ \ / _ \ '_ \ \___ \|  _ < | |
 | |__| | |_) |  __/ | | |____) | |_) || |_
  \____/| .__/ \___|_| |_|_____/|____/_____|
        | |
        |_|

Platform Name             : riscv-virtio,qemu
Platform Features         : medeleg
Platform HART Count       : 1
Platform IPI Device       : aclint-mswi
Platform Timer Device     : aclint-mtimer @ 10000000Hz
...
```

大きなOpenSBIのバナーが表示され後に、様々な実行環境の情報が表示されています。

ここで、文字を入力しても全く表示されないことに気づいたでしょうか。`-seral mon:stdio`オプションを指定しているため、QEMUの標準入出力が仮想マシンのシリアルポートに接続されています。ここで文字を入力すると、OSへ文字が送られることになります。ただ、現時点ではOSが起動しておらず、OpenSBIも入力を無視しているため、文字が表示されないのです。

<kbd>Ctrl</kbd>+<kbd>A</kbd>を押下した直後に、<kbd>C</kbd>を入力すると、QEMUのデバッグコンソール (QEMUモニター) に移行します。モニター上で`q`コマンドを実行すると、QEMUを終了できます。

```
QEMU 8.0.2 monitor - type 'help' for more information
(qemu) q
```

> [!TIP]
>
> <kbd>Ctrl</kbd>+<kbd>A</kbd>には、QEMUモニターへの移行 (<kbd>C</kbd>キー) の他にもいくつかの機能があります。例えば、<kbd>X</kbd>キーを押下すると、QEMUを即座に終了します。
>
> ```
> C-a h    print this help
> C-a x    exit emulator
> C-a s    save disk data back to file (if -snapshot)
> C-a t    toggle console timestamps
> C-a b    send break (magic sysrq)
> C-a c    switch between console and monitor
> C-a C-a  sends C-a
> ```

## リンカスクリプト

リンカスクリプトは、プログラムの各データ領域をメモリ上にどう配置するかを定義するファイルです。

新たに、次のように`kernel.ld`というファイルを作成しましょう。リンカはプログラムをリンクする際に、このファイルに従って各関数・変数の最終的なメモリアドレスを決定します。

```ld [kernel.ld]
ENTRY(boot)

SECTIONS {
    . = 0x80200000;

    .text :{
        KEEP(*(.text.boot));
        *(.text .text.*);
    }

    .rodata : ALIGN(4) {
        *(.rodata .rodata.*);
    }

    .data : ALIGN(4) {
        *(.data .data.*);
    }

    .bss : ALIGN(4) {
        __bss = .;
        *(.bss .bss.* .sbss .sbss.*);
        __bss_end = .;
    }

    . = ALIGN(4);
    . += 128 * 1024; /* 128KB */
    __stack_top = .;
}
```

このリンカスクリプトでは、次のようなことを定義しています。

- カーネルのエントリポイントは`boot`関数である。
- ベースアドレスは`0x80200000`である。
- 必ず`.text.boot`セクションを先頭にする。
- `.text`、`.rodata`、`.data`、`.bss`の順に各セクションを配置する。
- `.bss`セクションの末尾に、ブート時に使うスタック領域を配置する。

ここで登場している`.text`、`.rodata`、`.data`、`.bss`は、それぞれ次のような役割を持つデータ領域です。

- `.text`: コード領域。
- `.rodata`: 定数データ領域。読み取り専用。
- `.data`: 読み書き可能データ領域。
- `.bss`: 読み書き可能データ領域。`.data`と違い、初期値がゼロの変数を配置する。

いくつかリンカスクリプトの文法を見ていきましょう。まず、「`ENTRY(boot)`」はエントリーポイント (プログラムの開始地点) は`boot`関数である、という宣言です。その後、`SECTIONS`内で各セクションの配置を定義しています。

「`*(.text .text.*)`」のような記述は、全ファイル中 (「`*`」) の`.text`と`.text.`で始まる名前のセクションをそこに配置するという意味になります。

「`.`」は「現在のアドレス」を表す変数のようなものです。`*(.text)` 等でデータが配置されるたびに自動的に加算されていきます。`. += 128 * 1024`は「現在のアドレスから128KB進める」という意味です。また、`ALIGN(4)`は「4バイト境界になるようにアドレスを調整する」という意味になります。

「`__bss = .`」のような記述は、`__bss`というシンボルに現在のアドレスを割り当てるという意味になります。 **「シンボル」** は関数や静的変数を表すもので、C言語では `extern char シンボル名` で定義したシンボルを参照できます。

> [!TIP]
>
> リンカスクリプトは特にカーネル開発において便利な機能がたくさんあります。GitHubなどで実際の例から学ぶのがおすすめです。

## 最小限のカーネル

まずは、最小限のカーネルを作成してみましょう。次のように`kernel.c`という名前のC言語のソースコードを作成しましょう。

```c [kernel.c]
typedef unsigned char uint8_t;
typedef unsigned int uint32_t;
typedef uint32_t size_t;

extern char __bss[], __bss_end[], __stack_top[];

void *memset(void *buf, char c, size_t n) {
    uint8_t *p = (uint8_t *) buf;
    while (n--)
        *p++ = c;
    return buf;
}

void kernel_main(void) {
    memset(__bss, 0, (size_t) __bss_end - (size_t) __bss);

    for (;;);
}

__attribute__((section(".text.boot")))
__attribute__((naked))
void boot(void) {
    __asm__ __volatile__(
        "mv sp, %[stack_top]\n"
        "j kernel_main\n"
        :
        : [stack_top] "r" (__stack_top)
    );
}
```

まず起動されるのが`boot`関数です。リンカスクリプトで用意したスタック領域の末尾アドレスをスタックポインタ (`sp`) に設定し、`kernel_main`関数へジャンプします。スタックはゼロに向かって伸びる (使われるにつれて減算されていく) ので、領域の末尾アドレスを設定するよう注意しましょう。

`__attribute__((naked))`は「関数の本文の前後に、余計なコード ([Wikipedia](https://en.wikipedia.org/wiki/Function_prologue_and_epilogue)) を生成しない」ことをコンパイラに指示するものです。これで、関数の中身はインラインアセンブリで書いたものが「そのまま」出力されるようになります。

また、OpenSBIからは実行イメージのベースアドレス (`0x80200000`) へジャンプするようになっているため、カーネルのエントリポイントは`0x80200000`に配置する必要があります。`__attribute__((section(".text.boot")))`という属性をつけて専用のセクションに配置し、リンカスクリプトで先頭に来るようにしています。

ファイルの冒頭では、リンカスクリプト内で定義されている各シンボルを`extern char`で宣言しておきます。ここではシンボルのアドレスだけが知りたいので、適当に`char`型にしています。

`extern char __bbs;`と宣言しても何ら問題ありませんが、`__bss`と書くと「`.bss`領域の先頭アドレス」ではなく「`.bss`領域の0バイト目の値」という意味になってしまいます。そこで、`[]`をつけて`__bss`がアドレスを返すようにすることで、ケアレスミスを防ぐのがおすすめです。

`kernel_main`関数では、まず`memset`関数を使って`.bss`領域をゼロで初期化します。ブートローダが`.bss`領域を認識してゼロクリアしてくれることもありますが、その確証がないため自らの手で初期化するのがおすすめです。最後に無限ループに入って終了です。

## 動かしてみよう

最後に`run.sh`へカーネルのコンパイルと、QEMUの起動オプション (`-kernel kernel.elf`) を次のように追加しましょう。

```bash [run.sh] {6-13,17}
#!/bin/bash
set -xue

QEMU=qemu-system-riscv32

# clangのパス (Ubuntuの場合は CC=clang)
CC=/opt/homebrew/opt/llvm/bin/clang

CFLAGS="-std=c11 -O2 -g3 -Wall -Wextra --target=riscv32-unknown-elf -fno-stack-protector -ffreestanding -nostdlib"

# カーネルをビルド
$CC $CFLAGS -Wl,-Tkernel.ld -Wl,-Map=kernel.map -o kernel.elf \
    kernel.c

# QEMUを起動
$QEMU -machine virt -bios default -nographic -serial mon:stdio --no-reboot \
    -kernel kernel.elf
```

> [!TIP]
>
> macOSのHomebrew版 clangのファイルパスは、次のコマンドで確認できます。
>
> ```
> $ ls $(brew --prefix)/opt/llvm/bin/clang
> /opt/homebrew/opt/llvm/bin/clang
> ```

clangに指定しているオプション (`CFLAGS`変数) は次のとおりです。

- `-std=c11`: C11を使用する。
- `-O2`: 最適化を有効にして、効率の良い機械語を生成する。
- `-g3`: デバッグ情報を最大限に出力する。
- `-Wall`: 主要な警告を有効にする。
- `-Wextra`: さらに追加の警告を有効にする。
- `--target=riscv32-unknown-elf`: 32ビットRISC-V用にコンパイルする。
- `-fno-stack-protector`: スタック保護機能を無効にする。[#31](https://github.com/nuta/operating-system-in-1000-lines/issues/31#issuecomment-2613219393)を参照。
- `-ffreestanding`: ホスト環境 (開発環境) の標準ライブラリを使用しない。
- `-nostdlib`: 標準ライブラリをリンクしない。
- `-Wl,-Tkernel.ld`: リンカスクリプトを指定する。
- `-Wl,-Map=kernel.map`: マップファイル (リンカーによる配置結果) を出力する。

`-Wl,`はCコンパイラではなく、リンカ (LLD) にオプションを渡すことを意味します。clangがコンパイラやリンカの呼び出しを、一括して行います。

## 最初のカーネルデバッグ

`run.sh`を実行しても、特にカーネルは無限ループを回っているだけなので、画面上の表示は変わりません。このように「ちゃんとOSが動いているのかよく分からない」という状況はOS開発で大変よくあることです。そこで登場するのがQEMUのデバッグ機能です。特に、今のような文字を出力する機能がない状況では大変便利です。

QEMUモニタを開いて、`info registers`コマンドを実行してみましょう。すると、次のように現在のレジスタの値が表示されます。

```
QEMU 8.0.2 monitor - type 'help' for more information
(qemu) info registers

CPU#0
 V      =   0
 pc       80200014  ← 実行する命令のアドレス (プログラムカウンタ)
 ...
 x0/zero  00000000 x1/ra    8000a084 x2/sp    80220018 x3/gp    00000000  ← 各汎用レジスタの値
 x4/tp    80033000 x5/t0    00000001 x6/t1    00000002 x7/t2    00000000
 x8/s0    80032f50 x9/s1    00000001 x10/a0   80220018 x11/a1   87e00000
 x12/a2   00000007 x13/a3   00000019 x14/a4   00000000 x15/a5   00000001
 x16/a6   00000001 x17/a7   00000005 x18/s2   80200000 x19/s3   00000000
 x20/s4   87e00000 x21/s5   00000000 x22/s6   80006800 x23/s7   8001c020
 x24/s8   00002000 x25/s9   8002b4e4 x26/s10  00000000 x27/s11  00000000
 x28/t3   616d6569 x29/t4   8001a5a1 x30/t5   000000b4 x31/t6   00000000
```

> [!TIP]
>
> clangやQEMUのバージョンなどによっては、細かい値が異なる場合があります。

`pc 80200014` が現在のプログラムカウンタ、実行される命令のアドレスを示しています。逆アセンブラ (`llvm-objdump`) を使って、CPUから見た`kernel.c`の内容を確認してみましょう。

```
$ llvm-objdump -d kernel.elf

kernel.elf:     file format elf32-littleriscv

Disassembly of section .text:

80200000 <boot>:  ← boot関数
80200000: 37 05 22 80   lui     a0, 524832
80200004: 13 05 85 01   addi    a0, a0, 24
80200008: 2a 81         mv      sp, a0
8020000a: 6f 00 60 00   j       0x80200010 <kernel_main>
8020000e: 00 00         unimp

80200010 <kernel_main>:  ← kernel_main関数
80200010: 73 00 50 10   wfi
80200014: f5 bf         j       0x80200010 <kernel_main>  ← プログラムカウンタがここにある
```

最初の列から順にアドレス、機械語の16進数ダンプ、逆アセンブルされた命令です。`pc 80200014` ということは、現在実行されている命令は `j 0x80200010` であることが分かります。つまり、QEMUは `kernel_main` 関数にきちんと到達していることが分かります。

もう1点、スタックポインタ (spレジスタ) に、リンカスクリプトで定義された `__stack_top` の値が設定されているかを確認してみましょう。レジスタダンプには `x2/sp 80220018` と表示されています。リンカが `__stack_top` をどこに配置したかは、`kernel.map`を見ると分かります。

```
     VMA      LMA     Size Align Out     In      Symbol
       0        0 80200000     1 . = 0x80200000
80200000 80200000       16     4 .text
...
80200016 80200016        2     1 . = ALIGN ( 4 )
80200018 80200018    20000     1 . += 128 * 1024
80220018 80220018        0     1 __stack_top = .
```

各関数や変数のアドレスは、`llvm-nm`で確認することもできます。

```
$ llvm-nm kernel.elf
80200010 t .LBB0_1
00000000 N .Lline_table_start0
80220018 T __stack_top
80200000 T boot
80200010 T kernel_main
```

最初が配置されるアドレス (VMA) です。`__stack_top`　が `0x80220018` に配置されていることが分かります。スタックポインタを`boot`関数で正しく設定できていることが分かります。

実行が進むごとに `info registers` の結果は変化します。いったん処理を停止させたい場合は `stop` コマンドで停止できます。

```
(qemu) stop             ← 処理が停止する
(qemu) info registers   ← 停止時の状態を観測できる
(qemu) cont             ← 処理が再開する
```
