---
title: システムコール
---

# システムコール

前章では、ページフォルトをわざと起こすことでユーザーモードへの移行を確認しました。本章では、ユーザーモードで実行されているアプリケーションからカーネルの機能を呼び出す **「システムコール」** を実装します。

## システムコール呼び出し関数 (ユーザーランド側)

まずはシステムコールを呼び出すユーザーランド側の実装から始めましょう。手始めに、文字を出力する `putchar` 関数をシステムコールとして実装してみます。システムコールを識別するための番号 (`SYS_PUTCHAR`) を`common.h`に定義します。

```c [common.h]
#define SYS_PUTCHAR 1
```

次にシステムコールを実際に呼び出す関数です。大体は [SBIの呼び出し](/ja/05-hello-world#初めてのsbi) の実装と同じです。

```c [user.c]
int syscall(int sysno, int arg0, int arg1, int arg2) {
    register int a0 __asm__("a0") = arg0;
    register int a1 __asm__("a1") = arg1;
    register int a2 __asm__("a2") = arg2;
    register int a3 __asm__("a3") = sysno;

    __asm__ __volatile__("ecall"
                         : "=r"(a0)
                         : "r"(a0), "r"(a1), "r"(a2), "r"(a3)
                         : "memory");

    return a0;
}
```

`syscall`関数は、`a3`にシステムコール番号、`a0`〜`a2`レジスタにシステムコールの引数を設定して `ecall` 命令を実行します。`ecall` 命令は、カーネルに処理を委譲するための特殊な命令です。`ecall` 命令を実行すると、例外ハンドラが呼び出され、カーネルに処理が移ります。カーネルからの戻り値は`a0`レジスタに設定されます。

最後に、次のように `putchar` 関数で `putchar`システムコールを呼び出しましょう。このシステムコールでは、第1引数として文字を渡します。第2引数以降は、未使用なので0を渡すことにします。

```c [user.c] {2}
void putchar(char ch) {
    syscall(SYS_PUTCHAR, ch, 0, 0);
}
```

## 例外ハンドラの更新

次に、`ecall` 命令を実行したときに呼び出される例外ハンドラを更新します。

```c [kernel.h]
#define SCAUSE_ECALL 8
```

```c [kernel.c] {5-7,12}
void handle_trap(struct trap_frame *f) {
    uint32_t scause = READ_CSR(scause);
    uint32_t stval = READ_CSR(stval);
    uint32_t user_pc = READ_CSR(sepc);
    if (scause == SCAUSE_ECALL) {
        handle_syscall(f);
        user_pc += 4;
    } else {
        PANIC("unexpected trap scause=%x, stval=%x, sepc=%x\n", scause, stval, user_pc);
    }

    WRITE_CSR(sepc, user_pc);
}
```

`ecall` 命令が呼ばれたのかどうかは、`scause` の値を確認することで判定できます。`handle_syscall`関数を呼び出す以外にも、`sepc`の値に4を加えています。これは、`sepc`は例外を引き起こしたプログラムカウンタ、つまり`ecall`命令を指しています。変えないままだと、`ecall`命令を無限に繰り返し実行してしまうので、命令のサイズ分 (4バイト) だけ加算することで、ユーザーモードに戻る際に次の命令から実行を再開するようにしています。

## システムコールハンドラ

例外ハンドラから呼ばれるのが次のシステムコールハンドラです。引数には、例外ハンドラで保存した「例外発生時のレジスタ」の構造体を受け取ります。

```c [kernel.c]
void handle_syscall(struct trap_frame *f) {
    switch (f->a3) {
        case SYS_PUTCHAR:
            putchar(f->a0);
            break;
        default:
            PANIC("unexpected syscall a3=%x\n", f->a3);
    }
}
```

システムコールの種類に応じて処理を分岐します。今回は、`SYS_PUTCHAR` に対応する処理を実装します。単に`a0`レジスタに入っている文字を出力するだけです。

## システムコールのテスト

システムコールを一通り実装したので試してみましょう。`common.c`にある`printf`関数の実装を覚えているでしょうか。この関数は文字を表示する際に`putchar`関数を呼び出しています。たった今ユーザーランド上のライブラリで`putchar`を実装したのでそのまま使えます。

```c [shell.c] {2}
void main(void) {
    printf("Hello World from shell!\n");
}
```

次のようにメッセージが表示されれば成功です。

```
$ ./run.sh
Hello World from shell!
```

## 文字入力システムコール (`getchar`)

次に、文字入力を行うシステムコールを実装しましょう。SBIには「デバッグコンソールへの入力」を読む機能があります。空の場合は-1を返します。

```c [kernel.c]
long getchar(void) {
    struct sbiret ret = sbi_call(0, 0, 0, 0, 0, 0, 0, 2);
    return ret.error;
}
```

あとは次の通り`getchar`システムコールを実装します。

```c [common.h]
#define SYS_GETCHAR 2
```

```c [user.c]
int getchar(void) {
    return syscall(SYS_GETCHAR, 0, 0, 0);
}
```

```c [user.h]
int getchar(void);
```

```c [kernel.c] {3-13}
void handle_syscall(struct trap_frame *f) {
    switch (f->a3) {
        case SYS_GETCHAR:
            while (1) {
                long ch = getchar();
                if (ch >= 0) {
                    f->a0 = ch;
                    break;
                }

                yield();
            }
            break;
        /* 省略 */
    }
}
```

`getchar`システムコールの実装は、文字が入力されるまでSBIを繰り返し呼び出します。ただし、単純に繰り返すとCPUを占有してしまうので、`yield`システムコールを呼び出してCPUを他のプロセスに譲るようにしています。

## シェルを書こう

文字入力ができるようになったので、シェルを書いてみましょう。手始めに、`Hello world from shell!`と表示する`hello`コマンドを実装します。

```c [shell.c]
void main(void) {
    while (1) {
prompt:
        printf("> ");
        char cmdline[128];
        for (int i = 0;; i++) {
            char ch = getchar();
            putchar(ch);
            if (i == sizeof(cmdline) - 1) {
                printf("command line too long\n");
                goto prompt;
            } else if (ch == '\r') {
                printf("\n");
                cmdline[i] = '\0';
                break;
            } else {
                cmdline[i] = ch;
            }
        }

        if (strcmp(cmdline, "hello") == 0)
            printf("Hello world from shell!\n");
        else
            printf("unknown command: %s\n", cmdline);
    }
}
```

改行が来るまで文字を読み込んでいき、入力された文字列がコマンド名に完全一致するかをチェックする、非常に単純な実装です。デバッグコンソール上では改行が (`'\r'`) でやってくるので注意してください。

実際に動かしてみて、文字が入力されるか、そして`hello`コマンドが動くか確認してみましょう。

```
$ ./run.sh

> hello
Hello world from shell!
```

## プロセスの終了 (`exit`システムコール)

最後に、プロセスを終了する`exit`システムコールを実装します。

```c [common.h]
#define SYS_EXIT    3
```

```c [user.c] {2-3}
__attribute__((noreturn)) void exit(void) {
    syscall(SYS_EXIT, 0, 0, 0);
    for (;;); // 念のため
}
```

```c [kernel.h]
#define PROC_EXITED   2
```

```c [kernel.c] {3-7}
void handle_syscall(struct trap_frame *f) {
    switch (f->a3) {
        case SYS_EXIT:
            printf("process %d exited\n", current_proc->pid);
            current_proc->state = PROC_EXITED;
            yield();
            PANIC("unreachable");
        /* 省略 */
    }
}
```

まず、プロセスの状態を`PROC_EXITED`に変更し、`yield`システムコールを呼び出してCPUを他のプロセスに譲ります。スケジューラは`PROC_RUNNABLE`のプロセスしか実行しないため、このプロセスに戻ってくることはありません。ただし念の為、`PANIC`マクロで万が一戻ってきた場合はパニックを起こします。

> [!TIP]
>
> 分かりやすさのためにプロセスの状態を変えているだけで、プロセス管理構造体を開放していません。実用的なOSを目指したい時には、ページテーブルや割り当てられたメモリ領域など、プロセスが持つ資源を開放する必要があります。

最後に、シェルに`exit`コマンドを追加します。

```c [shell.c] {3-4}
        if (strcmp(cmdline, "hello") == 0)
            printf("Hello world from shell!\n");
        else if (strcmp(cmdline, "exit") == 0)
            exit();
        else
            printf("unknown command: %s\n", cmdline);
```

実際に動かしてみましょう。

```
$ ./run.sh

> exit
process 2 exited
PANIC: kernel.c:333: switched to idle process
```

`exit`コマンドを実行するとシェルプロセスが終了し、他に実行可能なプロセスがなくなります。そのため、スケジューラがアイドルプロセスを選ぶという流れになります。
