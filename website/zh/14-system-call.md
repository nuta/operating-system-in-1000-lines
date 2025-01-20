---
title: 系统调用
---

# 系统调用

在本章中，我们将实现允许应用程序调用内核函数的*“系统调用”*。是时候在用户空间打印 Hello World 了！ 

## 用户库

调用系统调用与我们之前看到的 [SBI 调用实现](/zh/05-hello-world) 非常相似：

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

`syscall` 函数在 `a3` 寄存器中设置系统调用号，在 `a0` 到 `a2` 寄存器中设置系统调用参数，然后执行 `ecall` 指令。`ecall` 指令是用于将处理委托给内核的特殊指令。当执行 `ecall` 指令时，会调用异常处理程序，控制权转移到内核。内核的返回值设置在 `a0` 寄存器中。

我们要实现的第一个系统调用是 `putchar`，它通过系统调用输出一个字符。它将一个字符作为第一个参数。第二个及后续的未使用参数设置为 0：

```c [common.h]
#define SYS_PUTCHAR 1
```

```c [user.c] {2}
void putchar(char ch) {
    syscall(SYS_PUTCHAR, ch, 0, 0);
}
```

## 在内核中处理 `ecall` 指令

接下来，更新陷阱处理程序以处理 `ecall` 指令：

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

是否调用了 `ecall` 指令可以通过检查 `scause` 的值来确定。除了调用 `handle_syscall` 函数外，我们还将 4（`ecall` 指令的大小）加到 `sepc` 的值上。这是因为 `sepc` 指向导致异常的程序计数器，它指向 `ecall` 指令。如果我们不改变它，内核会返回到同一个位置，并且 `ecall` 指令会被重复执行。

## 系统调用处理程序

以下系统调用处理程序从陷阱处理程序调用。它接收在陷阱处理程序中保存的“异常时的寄存器”结构：

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

它通过检查 `a3` 寄存器的值来确定系统调用的类型。现在我们只有一个系统调用，`SYS_PUTCHAR`，它只是输出存储在 `a0` 寄存器中的字符。

## 测试系统调用

你已经实现了系统调用。让我们来试试看！

还记得 `common.c` 中 `printf` 函数的实现吗？它调用 `putchar` 函数来显示字符。由于我们刚刚在用户空间库中实现了 `putchar`，我们可以直接使用它：

```c [shell.c] {2}
void main(void) {
    printf("Hello World from shell!\n");
}
```

你会在屏幕上看到这条迷人的消息：

```
$ ./run.sh
Hello World from shell!
```

恭喜！你已经成功实现了系统调用！但我们还没完成。让我们实现更多的系统调用！

## 从键盘接收字符（`getchar` 系统调用）

我们的下一个目标是实现 shell。为此，我们需要能够从键盘接收字符。

SBI 提供了一个接口来读取“调试控制台的输入”。如果没有输入，它返回 `-1`：

```c [kernel.c]
long getchar(void) {
    struct sbiret ret = sbi_call(0, 0, 0, 0, 0, 0, 0, 2);
    return ret.error;
}
```

`getchar` 系统调用的实现如下：

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

`getchar` 系统调用的实现重复调用 SBI 直到输入一个字符。但是，简单地重复这个操作会阻止其他进程运行，所以我们调用 `yield` 系统调用来将 CPU 让给其他进程。

> [!NOTE]
>
> 严格来说，SBI 不是从键盘读取字符，而是从串口读取。它之所以能工作，是因为键盘（或 QEMU 的标准输入）连接到串口。

## 编写 shell

让我们用一个简单的命令 `hello` 编写 shell，它会显示 `Hello world from shell!`：

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

它读取字符直到出现换行符，然后检查输入的字符串是否匹配命令名称。

> [!WARNING]
>
> 注意在调试控制台上，换行符是（`'\r'`）。

让我们试着输入 `hello` 命令：

```
$ ./run.sh

> hello
Hello world from shell!
```

你的操作系统开始看起来像一个真正的操作系统了！你走到这一步的速度真快！

## 进程终止（`exit` 系统调用）

最后，让我们实现 `exit` 系统调用，它用于终止进程：

```c [common.h]
#define SYS_EXIT    3
```

```c [user.c] {2-3}
__attribute__((noreturn)) void exit(void) {
    syscall(SYS_EXIT, 0, 0, 0);
    for (;;); // 以防万一！
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

系统调用将进程状态改为 `PROC_EXITED`，并调用 `yield` 将 CPU 让给其他进程。调度器只会执行处于 `PROC_RUNNABLE` 状态的进程，所以它永远不会返回到这个进程。然而，添加了 `PANIC` 宏以防万一确实返回时导致 panic。

> [!TIP]
> 
> 为了简单起见，我们只是将进程标记为已退出（`PROC_EXITED`）。如果你想构建一个实用的操作系统，需要释放进程持有的资源，比如页表和分配的内存区域。

将 `exit` 命令添加到 shell：

```c [shell.c] {3-4}
        if (strcmp(cmdline, "hello") == 0)
            printf("Hello world from shell!\n");
        else if (strcmp(cmdline, "exit") == 0)
            exit();
        else
            printf("unknown command: %s\n", cmdline);
```

你完成了！让我们试着运行一下：

```
$ ./run.sh

> exit
process 2 exited
PANIC: kernel.c:333: switched to idle process
```

当执行 `exit` 命令时，shell 进程通过系统调用终止，并且没有其他可运行的进程剩余。因此，调度器将选择空闲进程并导致 panic。
