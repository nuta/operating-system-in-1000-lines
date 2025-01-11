---
title: プロセス
---

# プロセス

プロセスは、アプリケーションのいわばインスタンスで、各プロセスが独立の実行コンテキストと、仮想アドレス空間といった資源を持ちます。OSによっては実行コンテキストを「スレッド」という別の概念で提供していることもありますが、本書では、簡単のため1プロセスにつき1スレッドとして一緒くたに扱います。

## プロセス管理構造体

プロセスの情報をまとめたのが次の`process`構造体です。この構造体のことを「プロセス管理構造体 (PCB: Process Control Block)」と呼びます。

```c [kernel.h]
#define PROCS_MAX 8       // 最大プロセス数

#define PROC_UNUSED   0   // 未使用のプロセス管理構造体
#define PROC_RUNNABLE 1   // 実行可能なプロセス

struct process {
    int pid;             // プロセスID
    int state;           // プロセスの状態: PROC_UNUSED または PROC_RUNNABLE
    vaddr_t sp;          // コンテキストスイッチ時のスタックポインタ
    uint8_t stack[8192]; // カーネルスタック
};
```

カーネルスタックにはコンテキストスイッチ時のCPUレジスタ、どこから呼ばれたのか (関数の戻り先)、各関数でのローカル変数などが入っています。カーネルスタックをプロセスごとに用意することで、別の実行コンテキストを持ち、コンテキストスイッチで状態の保存と復元ができるようになります。

> [!TIP]
>
> ちなみに、カーネルスタックをプロセス (スレッド) ごとではなく、CPUごとに1つだけ使う「シングルカーネルスタック」というカーネル実装手法もあります。seL4がこの方式を採用しています ([参考](https://trustworthy.systems/publications/theses_public/05/Warton%3Abe.abstract))。
>
> この「プログラムのコンテキスト、つまり文脈をどこに保存しておくか」という問題は、GoやRustといったプログラム言語の非同期処理ランタイムでも議論されるテーマです。「stackless/stackful async」で検索してみましょう。

## コンテキストスイッチ

コンテキストスイッチは [エナガ本](https://www.hanmoto.com/bd/isbn/9784798068718) での解説と同じ実装です。スタックに呼び出し先保存レジスタを保存し、スタックポインタの保存・復元、そして呼び出し先保存レジスタを復元します。

```c [kernel.c]
struct process procs[PROCS_MAX];

__attribute__((naked)) void switch_context(uint32_t *prev_sp,
                                           uint32_t *next_sp) {
    __asm__ __volatile__(
        // 実行中プロセスのスタックへレジスタを保存
        "addi sp, sp, -13 * 4\n"
        "sw ra,  0  * 4(sp)\n"
        "sw s0,  1  * 4(sp)\n"
        "sw s1,  2  * 4(sp)\n"
        "sw s2,  3  * 4(sp)\n"
        "sw s3,  4  * 4(sp)\n"
        "sw s4,  5  * 4(sp)\n"
        "sw s5,  6  * 4(sp)\n"
        "sw s6,  7  * 4(sp)\n"
        "sw s7,  8  * 4(sp)\n"
        "sw s8,  9  * 4(sp)\n"
        "sw s9,  10 * 4(sp)\n"
        "sw s10, 11 * 4(sp)\n"
        "sw s11, 12 * 4(sp)\n"

        // スタックポインタの切り替え
        "sw sp, (a0)\n"
        "lw sp, (a1)\n"

        // 次のプロセスのスタックからレジスタを復元
        "lw ra,  0  * 4(sp)\n"
        "lw s0,  1  * 4(sp)\n"
        "lw s1,  2  * 4(sp)\n"
        "lw s2,  3  * 4(sp)\n"
        "lw s3,  4  * 4(sp)\n"
        "lw s4,  5  * 4(sp)\n"
        "lw s5,  6  * 4(sp)\n"
        "lw s6,  7  * 4(sp)\n"
        "lw s7,  8  * 4(sp)\n"
        "lw s8,  9  * 4(sp)\n"
        "lw s9,  10 * 4(sp)\n"
        "lw s10, 11 * 4(sp)\n"
        "lw s11, 12 * 4(sp)\n"
        "addi sp, sp, 13 * 4\n"
        "ret\n"
    );
}
```

プロセスの初期化処理が次の`create_process`関数です。この関数は実行開始アドレス (`pc`) を受け取り、プロセス管理構造体を初期化して返します。

```c [kernel.c]
struct process *create_process(uint32_t pc) {
    // 空いているプロセス管理構造体を探す
    struct process *proc = NULL;
    int i;
    for (i = 0; i < PROCS_MAX; i++) {
        if (procs[i].state == PROC_UNUSED) {
            proc = &procs[i];
            break;
        }
    }

    if (!proc)
        PANIC("no free process slots");

    // switch_context() で復帰できるように、スタックに呼び出し先保存レジスタを積む
    uint32_t *sp = (uint32_t *) &proc->stack[sizeof(proc->stack)];
    *--sp = 0;                      // s11
    *--sp = 0;                      // s10
    *--sp = 0;                      // s9
    *--sp = 0;                      // s8
    *--sp = 0;                      // s7
    *--sp = 0;                      // s6
    *--sp = 0;                      // s5
    *--sp = 0;                      // s4
    *--sp = 0;                      // s3
    *--sp = 0;                      // s2
    *--sp = 0;                      // s1
    *--sp = 0;                      // s0
    *--sp = (uint32_t) pc;          // ra

    // 各フィールドを初期化
    proc->pid = i + 1;
    proc->state = PROC_RUNNABLE;
    proc->sp = (uint32_t) sp;
    return proc;
}
```

## コンテキストスイッチのテスト

これでプロセスの最も基本的な機能である「複数のプログラムの並行実行」が実装できました。早速、2つのプロセスを作成してみましょう。

```c [kernel.c] {1-25,32-34}
void delay(void) {
    for (int i = 0; i < 30000000; i++)
        __asm__ __volatile__("nop"); // 何もしない命令
}

struct process *proc_a;
struct process *proc_b;

void proc_a_entry(void) {
    printf("starting process A\n");
    while (1) {
        putchar('A');
        switch_context(&proc_a->sp, &proc_b->sp);
        delay();
    }
}

void proc_b_entry(void) {
    printf("starting process B\n");
    while (1) {
        putchar('B');
        switch_context(&proc_b->sp, &proc_a->sp);
        delay();
    }
}

void kernel_main(void) {
    memset(__bss, 0, (size_t) __bss_end - (size_t) __bss);

    WRITE_CSR(stvec, (uint32_t) kernel_entry);

    proc_a = create_process((uint32_t) proc_a_entry);
    proc_b = create_process((uint32_t) proc_b_entry);
    proc_a_entry();

    PANIC("unreachable here!");
}
```

`proc_a_entry`関数と`proc_b_entry`関数がそれぞれプロセスA、プロセスBのエントリポイントです。`putchar`関数で1文字表示したら、`switch_context`関数で他方のプロセスにコンテキストスイッチします。

`delay`関数で呼び出されているnop命令は「何もしない」命令です。これをしばらく繰り返すループを入れることで、文字の出力が速すぎてターミナルを操作できなくなるのを防ぎます。

では、実際に動かしてみましょう。次のように起動時のメッセージが1回ずつ表示され、その後は「ABABAB...」と交互に表示されます。

```
$ ./run.sh

starting process A
Astarting process B
BABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABAQE
```

## スケジューラ

上記の実験では、`switch_context`関数を直接呼び出して「次に実行するプロセス」を指定していました。ただこの手法では、プロセスの数が増えると「次にどのプロセスに切り替えるべきか」を決めるのが大変です。そこで次のプロセスを決定する「スケジューラ」を実装しましょう。

次の`yield`関数がスケジューラの実装です。

> [!TIP]
>
> 「yield」は「譲る」という意味の英単語です。「CPU時間という資源を譲る」という意味合いで、プロセスが自発的に呼び出すAPIの名前としてよく使われます。

```c [kernel.c]
struct process *current_proc; // 現在実行中のプロセス
struct process *idle_proc;    // アイドルプロセス

void yield(void) {
    // 実行可能なプロセスを探す
    struct process *next = idle_proc;
    for (int i = 0; i < PROCS_MAX; i++) {
        struct process *proc = &procs[(current_proc->pid + i) % PROCS_MAX];
        if (proc->state == PROC_RUNNABLE && proc->pid > 0) {
            next = proc;
            break;
        }
    }

    // 現在実行中のプロセス以外に、実行可能なプロセスがない。戻って処理を続行する
    if (next == current_proc)
        return;

    // コンテキストスイッチ
    struct process *prev = current_proc;
    current_proc = next;
    switch_context(&prev->sp, &next->sp);
}
```

ここで、2つのグローバル変数を導入しています。`current_proc`は現在実行中のプロセスを指します。`idle_proc`はアイドル (idle) プロセスという「実行可能なプロセスがないときに実行するプロセス」です。`idle_proc`はプロセスIDが`-1`のプロセスとして、次のように起動時に作成しておきます。

```c [kernel.c] {8-10,15-16}
void kernel_main(void) {
    memset(__bss, 0, (size_t) __bss_end - (size_t) __bss);

    printf("\n\n");

    WRITE_CSR(stvec, (uint32_t) kernel_entry);

    idle_proc = create_process((uint32_t) NULL);
    idle_proc->pid = -1; // idle
    current_proc = idle_proc;

    proc_a = create_process((uint32_t) proc_a_entry);
    proc_b = create_process((uint32_t) proc_b_entry);

    yield();
    PANIC("switched to idle process");
}
```

この初期化処理のキーポイントが `current_proc = idle_proc` です。こうすることで、ブート処理の実行コンテキストがアイドルプロセスのそれとして保存・復元されます。最初の`yield`関数の呼び出しではアイドルプロセス→プロセスAに切り替わり、アイドルプロセスに切り替わるときには、この`yield`関数の呼び出しから戻ってきたかのように動きます。

ブート時のスタックがアイドルプロセスのスタックとして使われる (`switch_context`関数で保存・復元される) ため、`process`構造体に割り当ててある`stack`フィールドは使われません。

最後に、`proc_a_entry`と`proc_b_entry`関数を次のように変更して、`switch_context`関数を直接呼び出す代わりに、`yield`関数を呼び出すようにします。

```c [kernel.c] {5,14}
void proc_a_entry(void) {
    printf("starting process A\n");
    while (1) {
        putchar('A');
        yield();
        delay();
    }
}

void proc_b_entry(void) {
    printf("starting process B\n");
    while (1) {
        putchar('B');
        yield();
        delay();
    }
}
```

同じように「A」「B」が交互に表示されたら成功です。

## 例外ハンドラの修正

例外ハンドラではスタックに実行状態を保存していましたが、プロセスごとに別のカーネルスタックを使うようになったので少し修正が必要です。

まずはプロセス切り替え時に`sscratch`レジスタへ、実行中プロセスのカーネルスタックの初期値を設定するようにします。

```c [kernel.c] {4-8}
void yield(void) {
    /* 省略 */

    __asm__ __volatile__(
        "csrw sscratch, %[sscratch]\n"
        :
        : [sscratch] "r" ((uint32_t) &next->stack[sizeof(next->stack)])
    );

    // コンテキストスイッチ
    struct process *prev = current_proc;
    current_proc = next;
    switch_context(&prev->sp, &next->sp);
}
```

スタックポインタは下位アドレスの方向に伸びる (スタック領域の末尾から使われていく) ため、`sizeof(next->stack)`バイト目のアドレスをカーネルスタックの初期値として設定します。

例外ハンドラの修正は次のとおりです。

```c [kernel.c] {3-5,39-45}
void kernel_entry(void) {
    __asm__ __volatile__(
        // 実行中プロセスのカーネルスタックをsscratchから取り出す
        // tmp = sp; sp = sscratch; sscratch = tmp;
        "csrrw sp, sscratch, sp\n"

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

        // 例外発生時のspを取り出して保存
        "csrr a0, sscratch\n"
        "sw a0,  4 * 30(sp)\n"

        // カーネルスタックを設定し直す
        "addi a0, sp, 4 * 31\n"
        "csrw sscratch, a0\n"

        "mv a0, sp\n"
        "call handle_trap\n"
```

まず、`sscratch`レジスタにある実行中プロセスのカーネルスタックのアドレスを`sp`レジスタに設定し、同時に例外発生時の`sp`を`sscratch`レジスタに保存します。`csrrw`命令はいわばswap操作です。例外発生時の状態を保存したら、`sscratch`レジスタに退避しておいた例外発生時の`sp`レジスタの値を取り出して保存します。最後に`sscratch`レジスタへカーネルスタックの初期値を設定し直して終わりです。

ここでのキーポイントは「プロセスごと独立したカーネルスタックを持っている」点です。コンテキストスイッチ時に`sscratch`の内容も切り替えておくことで、プロセスごとのトラップ発生時の状態の保存・復元をトラップハンドラができるようになっています。

> [!TIP]
>
> ここでは「カーネル」スタックの切り替え処理を実装しています。アプリケーションが使うスタックは、カーネルスタックとは別に割り当てられる予定です。後々の章で実装します。

## なぜスタックポインタを設定し直すのか

上記の修正は「例外発生時のスタックポインタを信頼しない」ためのものです。そもそも、なぜ信頼すべきではないのか考えてみましょう。例外ハンドラでは、次の3つのパターンを考慮する必要があります。

1. カーネルモードで例外が発生した。
2. 例外処理中にカーネルモードで例外が発生した (ネストされた例外)。
3. ユーザーモードで例外が発生した。

(1) の場合は、スタックポインタを設定し直さなくても基本的に問題ありません。(2) の場合は退避領域を上書きしてしまいますが、本実装ではネストされた例外からの復帰を想定せずカーネルパニックして停止するため問題ありません。

問題は (3) の場合です。このとき、`sp`は「ユーザー (アプリケーション) のスタック領域」を指しています。`sp`をそのまま利用する (信頼する) 実装の場合では、次のような不正な値をセットして例外を発生させると、カーネルをクラッシュさせる脆弱性に繋がります。17章まで本書の一通りの実装が終わった状態で、以下のようなアプリケーションを実行して実験してみます。

```c
// 後の章で登場する「アプリケーション」の例
#include "user.h"

void main(void) {
    __asm__ __volatile__(
        "li sp, 0xdeadbeef\n"
        "unimp"
    );
}
```

本章の修正 (`sscratch`からカーネルスタックを復元する) を適用せずにこれを実行してみると、カーネルが何も表示せずハングアップし、QEMUのログには以下のような出力が残ります。

```
epc:0x0100004e, tval:0x00000000, desc=illegal_instruction <- unimpでトラップハンドラに遷移
epc:0x802009dc, tval:0xdeadbe73, desc=store_page_fault <- スタック領域 (0xdeadbeef) への書き込み失敗例外
epc:0x802009dc, tval:0xdeadbdf7, desc=store_page_fault <- スタック領域 (0xdeadbeef) への書き込み失敗例外 (2)
epc:0x802009dc, tval:0xdeadbd7b, desc=store_page_fault <- スタック領域 (0xdeadbeef) への書き込み失敗例外 (3)
epc:0x802009dc, tval:0xdeadbcff, desc=store_page_fault <- スタック領域 (0xdeadbeef) への書き込み失敗例外 (4)
...
```

最初に`unimp`擬似命令で無効命令例外が発生し、カーネルのトラップハンドラに遷移しています。しかし、スタックポインタがマップされていないアドレス (`0xdeadbeef`) を指しているため、レジスタを保存する際に例外発生し、再びトラップハンドラの先頭へジャンプしています。これが無限ループとなり、カーネルがハングアップします。これを防ぐために、信頼できるスタック領域を`sscratch`から取り出すようにしています。

xv6 (有名な教育用UNIX風OS) のRISC-V版実装では、(1)と(2)の時用の例外ハンドラ ([`kernelvec`](https://github.com/mit-pdos/xv6-riscv/blob/f5b93ef12f7159f74f80f94729ee4faabe42c360/kernel/kernelvec.S#L13-L14)) と、(3)の時用の例外ハンドラ ([`uservec`](https://github.com/mit-pdos/xv6-riscv/blob/f5b93ef12f7159f74f80f94729ee4faabe42c360/kernel/trampoline.S#L74-L75)) がそれぞれ別になっています。前者の場合は、例外発生時のスタックポインタを引き継ぎ、後者の場合はカーネルスタックを別途取り出す実装になっており、カーネルを出入りするときに [ハンドラの設定を切り替える](https://github.com/mit-pdos/xv6-riscv/blob/f5b93ef12f7159f74f80f94729ee4faabe42c360/kernel/trap.c#L44-L46) ようになっています。xv6の実装と解説 ([日本語訳](https://www.sugawara-lab.jp/lecture.html) の「4.2 カーネル空間からのトラップ」あたり) が参考になるでしょう。

> [!TIP]
>
> 余談ですが、Googleが開発しているOSのFuchsiaには、ユーザーから任意のプログラムカウンタの値を設定できる実装が[脆弱性になっていたケース](https://blog.quarkslab.com/playing-around-with-the-fuchsia-operating-system.html)がありました。「ユーザー (アプリケーション) からの入力を信頼しない」というのは、堅牢なカーネルを実装する上で非常に重要な習慣です。

## 次のステップ

これで複数のプロセスを並列に動作できるようになり、マルチタスクOSを実現できました。あとはアプリケーションのマルチスレッドプログラミングと同じ要領でOSの機能を実装していくことができます。

ただし、このままではプロセスがカーネルのメモリ空間を自由に読み書きできてしまいます。次章からは、アプリケーションをどう安全に動かすか、つまりカーネルとアプリケーションをどう隔離するのかをするのかをみていきます。
