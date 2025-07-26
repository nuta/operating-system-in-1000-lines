# 系統呼叫

在本章中，我們將實作系統呼叫（*system calls*），讓應用程式可以呼叫核心的功能。是時候讓使用者空間來印出 Hello World 了！

## 使用者函式庫

呼叫系統呼叫的方式，其實與我們之前看到的 [SBI 呼叫實作](/en/05-hello-world#say-hello-to-sbi)非常類似：

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

`syscall` 函式會將系統呼叫編號設在 `a3` 暫存器中，並將最多三個參數分別放入 `a0` 到 `a2`，然後執行 `ecall` 指令。`ecall` 是一個特殊指令，用來將控制權交給核心處理。當 `ecall` 被執行時，會觸發例外處理器（exception handler），進而將控制權轉移給核心。核心執行完後，回傳值會放在 `a0` 暫存器中。

我們要實作的第一個系統呼叫是 `putchar`，它會透過 system call 的方式輸出一個字元。它會將欲輸出的字元作為第一個參數傳入，而第二個與後續未使用的參數則設為 0：

```c [common.h]
#define SYS_PUTCHAR 1
```

```c [user.c] {2}
void putchar(char ch) {
    syscall(SYS_PUTCHAR, ch, 0, 0);
}
```

## 在核心中處理 `ecall` 指令

接下來，請更新核心的 trap handler，以處理 `ecall` 指令：

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

是否執行了 `ecall` 指令，可以透過檢查 `scause` 的值來判斷。除了呼叫 `handle_syscall` 函式外，我們還需要將 `sepc` 的值加上 4（即 `ecall` 指令的大小）。這是因為 `sepc` 指向的是觸發例外的那條指令的程式計數器，也就是那一條 `ecall` 指令本身。如果我們不更新它，核心回到使用者模式時會再次執行 `ecall`，導致陷入無限次的陷入（exception loop）。

## 系統呼叫處理函式

以下這個系統呼叫處理函式會從 trap handler 中被呼叫。它會接收一個結構，內容是例外發生當下所儲存的暫存器狀態：

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

它會透過檢查 `a3` 暫存器的值來判斷是哪一種系統呼叫。目前我們只實作了一個系統呼叫：`SYS_PUTCHAR`，它會輸出 `a0` 暫存器中所儲存的字元。

## 測試系統呼叫

你已經完成了系統呼叫的實作，現在來試跑看看吧！

還記得 `common.c` 裡的 `printf` 實作嗎？它是透過呼叫 `putchar` 來顯示字元的。

```c [shell.c] {2}
void main(void) {
    printf("Hello World from shell!\n");
}
```

你會在螢幕上看到這句迷人的訊息：

```
$ ./run.sh
Hello World from shell!
```

恭喜你！你已經成功實作了系統呼叫！但我們還沒結束，接下來讓我們繼續實作更多的系統呼叫功能吧！

## 從鍵盤接收字元（`getchar` 系統呼叫）

我們的下一個目標是實作一個 shell。而要做到這件事，我們需要有能力從鍵盤接收字元輸入。SBI 提供了一個介面，可以用來讀取「除錯主控台的輸入」。如果目前沒有輸入，這個介面會回傳 `-1`：

```c [kernel.c]
long getchar(void) {
    struct sbiret ret = sbi_call(0, 0, 0, 0, 0, 0, 0, 2);
    return ret.error;
}
```

`getchar` 系統呼叫的實作如下：

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
        /* omitted */
    }
}
```

`getchar` 系統呼叫的實作會反覆呼叫 SBI，直到有字元輸入為止。但如果只是單純重複這個操作，會阻塞其他行程的執行，因此我們會呼叫 `yield` 系統呼叫，主動讓出 CPU 給其他行程使用。

> [!NOTE]
>
> 嚴格來說，SBI 並不是從鍵盤讀取字元，而是從序列埠（serial port）讀取的。它之所以能運作，是因為鍵盤（或 QEMU 的標準輸入）被連接到了序列埠上。

## 撰寫一個 shell

讓我們撰寫一個簡易的 shell，並加入一個名為 `hello` 的指令，它會顯示出這句話：`Hello world from shell!`：

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

它會持續讀取字元直到遇到換行符號，然後檢查輸入的字串是否與指令名稱相符。

> [!WARNING]
>
> 請注意，在除錯主控台中，換行字元是 `'\r'`（Carriage Return）。

讓我們來試試看輸入 `hello` 指令：

```
$ ./run.sh

> hello
Hello world from shell!
```

你的作業系統現在已經開始看起來像個真正的作業系統了！你發展的速度真是令人驚艷！

## 行程終止（`exit` 系統呼叫）

最後，我們來實作 `exit` 系統呼叫，讓它能夠終止目前的行程：

```c [common.h]
#define SYS_EXIT    3
```

```c [user.c] {2-3}
__attribute__((noreturn)) void exit(void) {
    syscall(SYS_EXIT, 0, 0, 0);
    for (;;); // Just in case!
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
        /* omitted */
    }
}
```

這個系統呼叫會將行程的狀態設為 `PROC_EXITED`，並呼叫 `yield`，將 CPU 控制權讓給其他行程。排程器只會執行狀態為 `PROC_RUNNABLE` 的行程，因此它不會再回到這個已終止的行程。不過，還是加入了 `PANIC` 巨集以防萬一，如果這個行程意外地還被執行，就會觸發 panic。

> [!TIP]
>
> 為了簡化實作，我們目前只是標記行程為 `PROC_EXITED`。如果要開發一個實用的作業系統，就必須釋放該行程佔用的資源，例如頁表與記憶體區域等。

接著，在 shell 中加入 `exit` 指令：

```c [shell.c] {3-4}
        if (strcmp(cmdline, "hello") == 0)
            printf("Hello world from shell!\n");
        else if (strcmp(cmdline, "exit") == 0)
            exit();
        else
            printf("unknown command: %s\n", cmdline);
```

你已經完成了！現在來試著執行看看吧：

```
$ ./run.sh

> exit
process 2 exited
PANIC: kernel.c:333: switched to idle process
```
當執行 `exit` 指令時，shell 行程會透過系統呼叫終止。此時，由於系統中沒有其他可執行的行程，排程器將進入 idle 狀態，最終觸發 panic（核心錯誤）。