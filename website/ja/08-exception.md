---
title: 例外処理
---

# 例外処理

例外は、不正なメモリアクセス (例: ヌルポインタ参照) のような「プログラムの実行が続行不能な状態」になったときに、あらかじめOSによって設定されるプログラム (例外ハンドラ) に処理を切り替える仕組みです。

例外の発生は、QEMUのデバッグログ (`-d`オプション) で確認できますが、長ったらしいログを読むのは面倒なのと、例外が発生してQEMUがリセットしても初学者にはそれが分かりづらいので、例外発生箇所を出力してカーネルパニックする例外ハンドラを序盤に実装しておくのがおすすめです。JavaScriptに親しみのある方なら「とりあえずUnhandled Rejectionのハンドラを追加する感じ」と言えばしっくりくるのではないでしょうか。

## 例外処理の流れ

例外が発生すると、RISC-Vでは次のような流れで処理が進みます。

1. CPUは`medeleg`レジスタを確認して、例外をどの動作モードで処理するかを決定する。本書では、主要な例外がU-Mode (ユーザーランド)、S-Mode (カーネル) のどちらかで発生した場合は、S-Modeで処理するようにOpenSBIによって設定されている。
2. 例外発生時のCPUの状態を各CSRに保存する。
3. `stvec` レジスタの値をプログラムカウンタにセットして、カーネルの例外処理プログラム (例外ハンドラ) にジャンプする。
4. 例外ハンドラは、カーネルが好きに使って良い `sscratch`レジスタを上手く使って、汎用レジスタの値 (つまり例外発生時の実行状態) を保存し、例外の種類に応じた処理を行う。
5. 例外処理を済ませると、保存していた実行状態を復元し、`sret`命令を呼び出して例外発生箇所から実行を再開する。

ステップ2で更新されるCSRは、主に次の通りです。カーネルの例外ハンドラはこれらの情報を元に、必要な処理を判断したり、例外発生時の状態を保存・復元したりします。

| レジスタ名 | 内容                                                                                      |
| ---------- | ----------------------------------------------------------------------------------------- |
| `scause`   | 例外の種類。これを読んでカーネルは例外の種類を判別する。                                  |
| `stval`    | 例外の付加情報 (例: 例外の原因となったメモリアドレス)。具体的な内容は、例外の種類による。 |
| `sepc`     | 例外発生箇所のプログラムカウンタ                                                          |
| `sstatus`  | 例外発生時の動作モード (U-Mode/S-Mode)                                                    |

カーネルの例外ハンドラの実装で一番気をつけなければいけないのは例外発生時の状態を正しく保存・復元することです。例えば、a0レジスタを間違って上書きしてしまうと「何もしていないのにローカル変数の値が変」といったデバッグの難しい問題に繋がってしまいます。

## 例外ハンドラの実装

準備が整ったところで、例外を受け取ってみましょう。まずは最初に実行される箇所です。`stvec`レジスタに、この`kernel_entry`関数の先頭アドレスを後ほどセットします。

```c [kernel.c]
__attribute__((naked))
__attribute__((aligned(4)))
void kernel_entry(void) {
    __asm__ __volatile__(
        "csrw sscratch, sp\n"
        "addi sp, sp, -4 * 31\n"
        "sw ra,  4 * 0(sp)\n"
        "sw gp,  4 * 1(sp)\n"
        "sw tp,  4 * 2(sp)\n"
        "sw t0,  4 * 3(sp)\n"
        "sw t1,  4 * 4(sp)\n"
        "sw t2,  4 * 5(sp)\n"
        "sw t3,  4 * 6(sp)\n"
        "sw t4,  4 * 7(sp)\n"
        "sw t5,  4 * 8(sp)\n"
        "sw t6,  4 * 9(sp)\n"
        "sw a0,  4 * 10(sp)\n"
        "sw a1,  4 * 11(sp)\n"
        "sw a2,  4 * 12(sp)\n"
        "sw a3,  4 * 13(sp)\n"
        "sw a4,  4 * 14(sp)\n"
        "sw a5,  4 * 15(sp)\n"
        "sw a6,  4 * 16(sp)\n"
        "sw a7,  4 * 17(sp)\n"
        "sw s0,  4 * 18(sp)\n"
        "sw s1,  4 * 19(sp)\n"
        "sw s2,  4 * 20(sp)\n"
        "sw s3,  4 * 21(sp)\n"
        "sw s4,  4 * 22(sp)\n"
        "sw s5,  4 * 23(sp)\n"
        "sw s6,  4 * 24(sp)\n"
        "sw s7,  4 * 25(sp)\n"
        "sw s8,  4 * 26(sp)\n"
        "sw s9,  4 * 27(sp)\n"
        "sw s10, 4 * 28(sp)\n"
        "sw s11, 4 * 29(sp)\n"

        "csrr a0, sscratch\n"
        "sw a0, 4 * 30(sp)\n"

        "mv a0, sp\n"
        "call handle_trap\n"

        "lw ra,  4 * 0(sp)\n"
        "lw gp,  4 * 1(sp)\n"
        "lw tp,  4 * 2(sp)\n"
        "lw t0,  4 * 3(sp)\n"
        "lw t1,  4 * 4(sp)\n"
        "lw t2,  4 * 5(sp)\n"
        "lw t3,  4 * 6(sp)\n"
        "lw t4,  4 * 7(sp)\n"
        "lw t5,  4 * 8(sp)\n"
        "lw t6,  4 * 9(sp)\n"
        "lw a0,  4 * 10(sp)\n"
        "lw a1,  4 * 11(sp)\n"
        "lw a2,  4 * 12(sp)\n"
        "lw a3,  4 * 13(sp)\n"
        "lw a4,  4 * 14(sp)\n"
        "lw a5,  4 * 15(sp)\n"
        "lw a6,  4 * 16(sp)\n"
        "lw a7,  4 * 17(sp)\n"
        "lw s0,  4 * 18(sp)\n"
        "lw s1,  4 * 19(sp)\n"
        "lw s2,  4 * 20(sp)\n"
        "lw s3,  4 * 21(sp)\n"
        "lw s4,  4 * 22(sp)\n"
        "lw s5,  4 * 23(sp)\n"
        "lw s6,  4 * 24(sp)\n"
        "lw s7,  4 * 25(sp)\n"
        "lw s8,  4 * 26(sp)\n"
        "lw s9,  4 * 27(sp)\n"
        "lw s10, 4 * 28(sp)\n"
        "lw s11, 4 * 29(sp)\n"
        "lw sp,  4 * 30(sp)\n"
        "sret\n"
    );
}
```

実装のキーポイントは次のとおりです。

- `sscratch`レジスタに、例外発生時のスタックポインタを保存しておき、あとで復元している。このように、一時退避用として`sscratch`レジスタを使うことができる。
- 浮動小数点レジスタはカーネル内で使われないので、ここでは保存する必要がない。一般的にスレッドの切り替え時に保存・退避が行われる。
- `a0`レジスタにスタックポインタをセットして、`handle_trap`関数を呼び出している。このとき、スタックポインタが指し示すアドレスには、後述する`trap_frame`構造体と同じ構造でレジスタの値が保存されている。
- `__attribute__((aligned(4)))` をつけることで、関数の先頭アドレスを4バイト境界にアラインする。これは、`stvec`レジスタは例外ハンドラのアドレスだけでなく、下位2ビットにはモードを表すフラグを持っているため。

この関数で呼ばれているのが、次の`handle_trap`関数です。

```c [kernel.c]
void handle_trap(struct trap_frame *f) {
    uint32_t scause = READ_CSR(scause);
    uint32_t stval = READ_CSR(stval);
    uint32_t user_pc = READ_CSR(sepc);

    PANIC("unexpected trap scause=%x, stval=%x, sepc=%x\n", scause, stval, user_pc);
}
```

例外の発生事由 (`scause`) と例外発生時のプログラムカウンタ (`sepc`) を取得し、デバッグ用にカーネルパニックを発生させています。ここで使われている各種マクロは、`kernel.h`で次のように定義しましょう。

```c [kernel.h]
#include "common.h"

struct trap_frame {
    uint32_t ra;
    uint32_t gp;
    uint32_t tp;
    uint32_t t0;
    uint32_t t1;
    uint32_t t2;
    uint32_t t3;
    uint32_t t4;
    uint32_t t5;
    uint32_t t6;
    uint32_t a0;
    uint32_t a1;
    uint32_t a2;
    uint32_t a3;
    uint32_t a4;
    uint32_t a5;
    uint32_t a6;
    uint32_t a7;
    uint32_t s0;
    uint32_t s1;
    uint32_t s2;
    uint32_t s3;
    uint32_t s4;
    uint32_t s5;
    uint32_t s6;
    uint32_t s7;
    uint32_t s8;
    uint32_t s9;
    uint32_t s10;
    uint32_t s11;
    uint32_t sp;
} __attribute__((packed));

#define READ_CSR(reg)                                                          \
    ({                                                                         \
        unsigned long __tmp;                                                   \
        __asm__ __volatile__("csrr %0, " #reg : "=r"(__tmp));                  \
        __tmp;                                                                 \
    })

#define WRITE_CSR(reg, value)                                                  \
    do {                                                                       \
        uint32_t __tmp = (value);                                              \
        __asm__ __volatile__("csrw " #reg ", %0" ::"r"(__tmp));                \
    } while (0)
```

`trap_frame`構造体はスタックに積まれている元の実行状態の構造を表しています。`READ_CSR`マクロと`WRITE_CSR`マクロは、CSRレジスタの読み書きを行うための便利なマクロです。

最後に必要なのは例外ハンドラがどこにあるのかをCPUに教えてあげることです。次のように`main`関数で`stvec`レジスタに例外ハンドラのアドレスを書き込みましょう。

```c [kernel.c] {4-5}
void kernel_main(void) {
    memset(__bss, 0, (size_t) __bss_end - (size_t) __bss);

    WRITE_CSR(stvec, (uint32_t) kernel_entry);
    __asm__ __volatile__("unimp"); // 無効な命令
```

`stvec`レジスタの設定に加えて、`unimp`命令を実行します。この命令はRISC-Vの命令セットには存在しない架空の命令で、CPUが無効な機械語であると判断するようなバイナリ列を出力してくれる、少し便利なコンパイラの機能です (参考: [具体的なunimp命令の実装](https://github.com/llvm/llvm-project/commit/26403def69f72c7938889c1902d62121095b93d7#diff-1d077b8beaff531e8d78ba5bb21c368714f270f1b13ba47bb23d5ad2a5d1f01bR410-R414))。

実行してみて、例外ハンドラが呼ばれることを確認しましょう。

```
$ ./run.sh
Hello World!
PANIC: kernel.c:47: unexpected trap scause=00000002, stval=ffffff84, sepc=8020015e
```

仕様書によると、`scause`の値が2の場合は「Illegal instruction」つまり無効な命令の実行を試みたことが分かります。まさしく、`unimp`命令の期待通りの動作です。

また、`sepc`の値がどこを指しているかも確認してみましょう。`unimp`命令を呼び出している行であれば上手くいっています。

```
$ llvm-addr2line -e kernel.elf 8020015e
/Users/seiya/os-from-scratch/kernel.c:129
```
