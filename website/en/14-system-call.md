---
title: System Call
layout: chapter
lang: en
---

# Implementing system calls

In the previous chapter, we confirmed the transition to user mode by intentionally causing a page fault. In this chapter, we will implement "system calls" that allow applications running in user mode to invoke kernel functions.

## System call library (userland)

Let's start with the user-land implementation for invoking system calls. As a first step, we'll implement the `putchar` function, which outputs a character, as a system call. We'll define a number (`SYS_PUTCHAR`) in `common.h` to identify the system call:

```c:common.h
#define SYS_PUTCHAR 1
```

# System call handler (kernel)

Next, let's look at the function that actually invokes the system call. The implementation is quite similar to [the SBI call implementation](/en/05-hello-world#say-hello-to-sbi) we've seen before:

```c:user.c
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

# Implementing the syscall function

The `syscall` function sets the system call number in the `a3` register and the system call arguments in the `a0` to `a2` registers, then executes the `ecall` instruction. The `ecall` instruction is a special instruction used to delegate processing to the kernel. When the `ecall` instruction is executed, an exception handler is called, and control is transferred to the kernel. The return value from the kernel is set in the `a0` register.

Finally, let's invoke the `putchar` system call in the `putchar` function as follows. In this system call, we pass the character as the first argument. For the second and subsequent unused arguments are set to 0:

```c:user.c {2}
void putchar(char ch) {
    syscall(SYS_PUTCHAR, ch, 0, 0);
}
```

# Updating the exception handler

Next, we'll update the exception handler that is called when the `ecall` instruction is executed:

```c:kernel.c {5-7,12}
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

Whether the `ecall` instruction was called can be determined by checking the value of `scause`. Besides calling the `handle_syscall` function, we also add 4 to the value of `sepc`. This is because `sepc` points to the program counter that caused the exception, which in this case is the `ecall` instruction. If we don't change it, the `ecall` instruction would be executed repeatedly in an infinite loop. By adding the size of the instruction (4 bytes), we ensure that execution resumes from the next instruction when returning to user mode.

Lastly, `SCAUSE_ECALL` is defined as 8:

```c:kernel.h
#define SCAUSE_ECALL 8
```

# System call handler

The following system call handler is called from the exception handler. It receives a structure of "registers at the time of exception" that was saved in the exception handler as an argument:

```c:kernel.c
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

We branch the processing according to the type of system call. This time, we will implement the processing corresponding to `SYS_PUTCHAR`, which simply outputs the character stored in the `a0` register.

## Test the system call

Now that we have implemented the system call, let's test it. Do you remember the implementation of the `printf` function in `common.c`? This function calls the `putchar` function to display characters. Since we have just implemented `putchar` in the userland library, we can use it as is:

```c:shell.c {2}
void main(void) {
    printf("Hello World from shell!\n");
}
```

You'll see the following output when it works perfectly:

```plain
$ ./run.sh
Hello World from shell!
```

## Receive characters from keyboard (`getchar` system call)

Let's implement a system call for character input. The SBI has a function to read "input to the debug console". If there is no input, it returns `-1`:

```c:kernel.c
long getchar(void) {
    struct sbiret ret = sbi_call(0, 0, 0, 0, 0, 0, 0, 2);
    return ret.error;
}
```

The `getchar` system call is implemented as follows:

```c:common.h
#define SYS_GETCHAR 2
```

```c:user.c
int getchar(void) {
    return syscall(SYS_GETCHAR, 0, 0, 0);
}
```

```c:user.h
int getchar(void);
```

```c:kernel.c {3-13}
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

The implementation of the `getchar` system call repeatedly calls the SBI until a character is input. However, simply repeating this would monopolize the CPU, so we call the `yield` system call to yield the CPU to other processes.

## Write a shell

Now that we can input characters, let's write a shell. To begin with, we will implement a `hello` command that displays `Hello world from shell!`:

```c:shell.c
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

We will read characters until a newline is encountered and check if the entered string completely matches the command name. Note that on the debug console, the newline character is (`'\r'`).

Let's run it and verify if characters can be input and if the `hello` command works:

```plain
$ ./run.sh

> hello
Hello world from shell!
```

## Process termination (`exit` system call)

Lastly, let's implement `exit` system call, which terminates the process:

```c:common.h
#define SYS_EXIT    3
```

```c:user.c {2-3}
__attribute__((noreturn)) void exit(void) {
    syscall(SYS_EXIT, 0, 0, 0);
    for (;;); // Just in case!
}
```

```c:kernel.h
#define PROC_EXITED   2
```

```c:kernel.c {3-7}
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

First, change the process state to `PROC_EXITED` and call the `yield` system call to give up the CPU to other processes. The scheduler will only execute processes in the `PROC_RUNNABLE` state, so it will never return to this process. However, as a precaution, we use the `PANIC` macro to cause a panic in case it does return.

> [!TIP]
>
> For simplicity, we only mark the process as exited (`PROC_EXITED`) and not free the process management structure. If you are building a practical OS, it is necessary to free resources held by the process, such as page tables and allocated memory regions.

Add the `exit` command to the shell:

```c:shell.c {3-4}
        if (strcmp(cmdline, "hello") == 0)
            printf("Hello world from shell!\n");
        else if (strcmp(cmdline, "exit") == 0)
            exit();
        else
            printf("unknown command: %s\n", cmdline);
```

You're done! Let's try running it:

```plain
$ ./run.sh

> exit
process 2 exited
PANIC: kernel.c:333: switched to idle process
```

When the `exit` command is executed, the shell process terminates, and there are no other runnable processes remaining. As a result, the scheduler will select the idle process and cause a panic.