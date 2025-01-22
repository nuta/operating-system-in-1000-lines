# System Call

In this chapter, we will implement *"system calls"* that allow applications to invoke kernel functions. Time to Hello World from the userland!

## User library

Invoking system call is quite similar to [the SBI call implementation](/en/05-hello-world#say-hello-to-sbi) we've seen before:

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

The `syscall` function sets the system call number in the `a3` register and the system call arguments in the `a0` to `a2` registers, then executes the `ecall` instruction. The `ecall` instruction is a special instruction used to delegate processing to the kernel. When the `ecall` instruction is executed, an exception handler is called, and control is transferred to the kernel. The return value from the kernel is set in the `a0` register.

The first system call we will implement is `putchar`, which outputs a character, via system call. It takes a character as the first argument. For the second and subsequent unused arguments are set to 0:

```c [common.h]
#define SYS_PUTCHAR 1
```

```c [user.c] {2}
void putchar(char ch) {
    syscall(SYS_PUTCHAR, ch, 0, 0);
}
```

## Handle `ecall` instruction in the kernel

Next, update the trap handler to handle `ecall` instruction:

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

Whether the `ecall` instruction was called can be determined by checking the value of `scause`. Besides calling the `handle_syscall` function, we also add 4 (the size of `ecall` instruction) to the value of `sepc`. This is because `sepc` points to the program counter that caused the exception, which points to the `ecall` instruction. If we don't change it, the kernel goes back to the same place, and the `ecall` instruction is executed repeatedly.

## System call handler

The following system call handler is called from the trap handler. It receives a structure of "registers at the time of exception" that was saved in the trap handler:

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

It determines the type of system call by checking the value of the `a3` register. Now we only have one system call, `SYS_PUTCHAR`, which simply outputs the character stored in the `a0` register.

## Test the system call

You've implemented the system call. Let's try it out!

Do you remember the implementation of the `printf` function in `common.c`? It calls the `putchar` function to display characters. Since we have just implemented `putchar` in the userland library, we can use it as is:

```c [shell.c] {2}
void main(void) {
    printf("Hello World from shell!\n");
}
```

You'll see the charming message on the screen:

```
$ ./run.sh
Hello World from shell!
```

Congratulations! You've successfully implemented the system call! But we're not done yet. Let's implement more system calls!

## Receive characters from keyboard (`getchar` system call)

Our next goal is to implement a shell. To do that, we need to be able to receive characters from the keyboard.

SBI provides an interface to read "input to the debug console". If there is no input, it returns `-1`:

```c [kernel.c]
long getchar(void) {
    struct sbiret ret = sbi_call(0, 0, 0, 0, 0, 0, 0, 2);
    return ret.error;
}
```

The `getchar` system call is implemented as follows:

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

The implementation of the `getchar` system call repeatedly calls the SBI until a character is input. However, simply repeating this prevents other processes from running, so we call the `yield` system call to yield the CPU to other processes.

> [!NOTE]
>
> Strictly speaking, SBI does not read characters from keyboard, but from the serial port. It works because the keyboard (or QEMU's standard input) is connected to the serial port.

## Write a shell

Let's write a shell with a simple command `hello`, which displays `Hello world from shell!`:

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

It reads characters until a newline comes, and checks if the entered string matches the command name.

> [!WARNING]
>
> Note that on the debug console, the newline character is (`'\r'`).

Let's try typing `hello` command:

```
$ ./run.sh

> hello
Hello world from shell!
```

Your OS is starting to look like a real OS! How fast you've come this far!

## Process termination (`exit` system call)

Lastly, let's implement `exit` system call, which terminates the process:

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

The system call changes the process state to `PROC_EXITED`, and calls `yield` to give up the CPU to other processes. The scheduler will only execute processes in `PROC_RUNNABLE` state, so it will never return to this process. However, `PANIC` macro is added to cause a panic in case it does return.

> [!TIP]
>
> For simplicity, we only mark the process as exited (`PROC_EXITED`). If you want to build a practical OS, it is necessary to free resources held by the process, such as page tables and allocated memory regions.

Add the `exit` command to the shell:

```c [shell.c] {3-4}
        if (strcmp(cmdline, "hello") == 0)
            printf("Hello world from shell!\n");
        else if (strcmp(cmdline, "exit") == 0)
            exit();
        else
            printf("unknown command: %s\n", cmdline);
```

You're done! Let's try running it:

```
$ ./run.sh

> exit
process 2 exited
PANIC: kernel.c:333: switched to idle process
```

When the `exit` command is executed, the shell process terminates via system call, and there are no other runnable processes remaining. As a result, the scheduler will select the idle process and cause a panic.
