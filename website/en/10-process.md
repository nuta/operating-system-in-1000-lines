# Process

A process is an instance of an application. Each process has its own independent execution context and resources, such as a virtual address space.

> [!NOTE]
>
> Practical operating systems provide the execution context as a separate concept called a *"thread"*. For simplicity, in this book we'll treat each process as having a single thread.

## Process control block

The following `process` structure defines a process object. It's also known as  _"Process Control Block (PCB)"_.

```c [kernel.h]
#define PROCS_MAX 8       // Maximum number of processes

#define PROC_UNUSED   0   // Unused process control structure
#define PROC_RUNNABLE 1   // Runnable process

struct process {
    int pid;             // Process ID
    int state;           // Process state: PROC_UNUSED or PROC_RUNNABLE
    vaddr_t sp;          // Stack pointer
    uint8_t stack[8192]; // Kernel stack
};
```

The kernel stack contains saved CPU registers, return addresses (where it was called from), and local variables. By preparing a kernel stack for each process, we can implement context switching by saving and restoring CPU registers, and switching the stack pointer.

> [!TIP]
>
> There is another approach called *"single kernel stack"*. Instead of having a kernel stack for each process (or thread), there's only single stack per CPU. [seL4 adopts this method](https://trustworthy.systems/publications/theses_public/05/Warton%3Abe.abstract).
>
> This *"where to store the program's context"* issue is also a topic discussed in async runtimes of programming languages like Go and Rust. Try searching for *"stackless async"* if you're interested.

## Context switch

Switching the process execution context is called *"context switching"*. The following `switch_context` function is the implementation of context switching:

```c [kernel.c]
__attribute__((naked)) void switch_context(uint32_t *prev_sp,
                                           uint32_t *next_sp) {
    __asm__ __volatile__(
        // Save callee-saved registers onto the current process's stack.
        "addi sp, sp, -13 * 4\n" // Allocate stack space for 13 4-byte registers
        "sw ra,  0  * 4(sp)\n"   // Save callee-saved registers only
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

        // Switch the stack pointer.
        "sw sp, (a0)\n"         // *prev_sp = sp;
        "lw sp, (a1)\n"         // Switch stack pointer (sp) here

        // Restore callee-saved registers from the next process's stack.
        "lw ra,  0  * 4(sp)\n"  // Restore callee-saved registers only
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
        "addi sp, sp, 13 * 4\n"  // We've popped 13 4-byte registers from the stack
        "ret\n"
    );
}
```

`switch_context` saves the callee-saved registers onto the stack, switches the stack pointer, and then restores the callee-saved registers from the stack. In other words, the execution context is stored as temporary local variables on the stack. Alternatively, you could save the context in `struct process`, but this stack-based approach is beautifully simple, isn't it?

Callee-saved registers are registers that a called function must restore before returning. In RISC-V, `s0` to `s11` are callee-saved registers. Other registers like `a0` are caller-saved registers, and already saved on the stack by the caller. This is why `switch_context` handles only part of registers.

The `naked` attribute tells the compiler not to generate any other code than the inline assembly. It should work without this attribute, but it's a good practice to use it to avoid unintended behavior especially when you modify the stack pointer manually.

> [!TIP]
>
> Callee/Caller saved registers are defined in [Calling Convention](https://riscv.org/wp-content/uploads/2015/01/riscv-calling.pdf). Compilers generate code based on this convention.

Next, let's implement the process initialization function, `create_process`. It takes the entry point as a parameter, and returns a pointer to the created `process` struct:

```c
struct process procs[PROCS_MAX]; // All process control structures.

struct process *create_process(uint32_t pc) {
    // Find an unused process control structure.
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

    // Stack callee-saved registers. These register values will be restored in
    // the first context switch in switch_context.
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

    // Initialize fields.
    proc->pid = i + 1;
    proc->state = PROC_RUNNABLE;
    proc->sp = (uint32_t) sp;
    return proc;
}
```

## Testing context switch

We have implemented the most basic function of processes - concurrent execution of multiple programs. Let's create two processes:

```c [kernel.c] {1-25,32-34}
void delay(void) {
    for (int i = 0; i < 30000000; i++)
        __asm__ __volatile__("nop"); // do nothing
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

The `proc_a_entry` function and `proc_b_entry` function are the entry points for Process A and Process B respectively. After displaying a single character using the `putchar` function, they switch context to the other process using the `switch_context` function.

`delay` function implements a busy wait to prevent the character output from becoming too fast, which would make your terminal unresponsive. `nop` instruction is a "do nothing" instruction. It is added to prevent compiler optimization from removing the loop.

Now, let's try! The startup messages will be displayed once each, and then "ABABAB..." lasts forever:

```
$ ./run.sh

starting process A
Astarting process B
BABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABAQE
```

## Scheduler

In the previous experiment, we directly called the `switch_context` function to specify the "next process to execute". However, this method becomes complicated when determining which process to switch to next as the number of processes increases. To solve the issue, let's implement a *"scheduler"*, a kernel program which decides the next process.

The following `yield` function is the implementation of the scheduler:

> [!TIP]
>
> The word "yield" is often used as the name for an API which allows giving up the CPU to another process voluntarily.

```c [kernel.c]
struct process *current_proc; // Currently running process
struct process *idle_proc;    // Idle process

void yield(void) {
    // Search for a runnable process
    struct process *next = idle_proc;
    for (int i = 0; i < PROCS_MAX; i++) {
        struct process *proc = &procs[(current_proc->pid + i) % PROCS_MAX];
        if (proc->state == PROC_RUNNABLE && proc->pid > 0) {
            next = proc;
            break;
        }
    }

    // If there's no runnable process other than the current one, return and continue processing
    if (next == current_proc)
        return;

    // Context switch
    struct process *prev = current_proc;
    current_proc = next;
    switch_context(&prev->sp, &next->sp);
}
```

Here, we introduce two global variables. `current_proc` points to the currently running process. `idle_proc` refers to the idle process, which is "the process to run when there are no runnable processes". The `idle_proc` is created at startup as a process with process ID `-1`, as shown below:

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

The key point of this initialization process is `current_proc = idle_proc`. This ensures that the execution context of the boot process is saved and restored as that of the idle process. During the first call to the `yield` function, it switches from the idle process to process A, and when switching back to the idle process, it behaves as if returning from this `yield` function call.

Lastly, modify `proc_a_entry` and `proc_b_entry` as follows to call the `yield` function instead of directly calling the `switch_context` function:

```c [kernel.c] {5,13}
void proc_a_entry(void) {
    printf("starting process A\n");
    while (1) {
        putchar('A');
        yield();
    }
}

void proc_b_entry(void) {
    printf("starting process B\n");
    while (1) {
        putchar('B');
        yield();
    }
}
```

If "A" and "B" are printed as before, it works perfectly!

## Changes in the exception handler

In the exception handler, it saves the execution state onto the stack. However, since we now use separate kernel stacks for each process, we need to update it slightly.

First, set the initial value of the kernel stack for the currently executing process in the `sscratch` register during process switching.

```c [kernel.c] {4-8}
void yield(void) {
    /* omitted */

    __asm__ __volatile__(
        "csrw sscratch, %[sscratch]\n"
        :
        : [sscratch] "r" ((uint32_t) &next->stack[sizeof(next->stack)])
    );

    // Context switch
    struct process *prev = current_proc;
    current_proc = next;
    switch_context(&prev->sp, &next->sp);
}
```

Since the stack pointer extends towards lower addresses, we set the address at the `sizeof(next->stack)`th byte as the initial value of the kernel stack.

The modifications to the exception handler are as follows:

```c [kernel.c] {3-4,38-44}
void kernel_entry(void) {
    __asm__ __volatile__(
        // Retrieve the kernel stack of the running process from sscratch.
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

        // Retrieve and save the sp at the time of exception.
        "csrr a0, sscratch\n"
        "sw a0,  4 * 30(sp)\n"

        // Reset the kernel stack.
        "addi a0, sp, 4 * 31\n"
        "csrw sscratch, a0\n"

        "mv a0, sp\n"
        "call handle_trap\n"
```

The first `csrrw` instruction is a swap operation in short:

```
tmp = sp;
sp = sscratch;
sscratch = tmp;
```

Thus, `sp` now points to the *kernel* (not *user*) stack of the currently running process. Also, `sscratch` now holds the original value of `sp` (user stack) at the time of the exception.

After saving other registers onto the kernel stack, We restore the original `sp` value from `sscratch` and save it onto the kernel stack. Then, calculate the initial value of `sscratch` and restore it.

The key point here is that each process has its own independent kernel stack. By switching the contents of `sscratch` during context switching, we can resume the execution of the process from the point where it was interrupted, as if nothing had happened.

> [!TIP]
>
> We've implemented the context switching mechanism for the "kernel" stack. The stack used by applications (so-called *user stack*) will be allocated separately from the kernel stack. This will be implemented in later chapters.

## Appendix: Why do we reset the stack pointer?

In the previous section, you might have wondered why we need to switch to the kernel stack by tweaking `sscratch`.

This is because we must not trust the stack pointer at the time of exception. In the exception handler, we need to consider the following three patterns:

1. An exception occurred in kernel mode.
2. An exception occurred in kernel mode, when handling another exception (nested exception).
3. An exception occurred in user mode.

In case (1), there's generally no problem even if we don't reset the stack pointer. In case (2), we would overwrite the saved area, but our implementation triggers a kernel panic on nested exceptions, so it's OK.

The problem is with case (3). In this case, `sp` points to the "user (application) stack area". If we implement it to use (trust) `sp` as is, it could lead to a vulnerability that crashes the kernel.

Let's experiment with this by running the following application after completing all the implementations up to Chapter 17 in this book:

```c
// An example of applications
#include "user.h"

void main(void) {
    __asm__ __volatile__(
        "li sp, 0xdeadbeef\n"  // Set an invalid address to sp
        "unimp"                // Trigger an exception
    );
}
```

If we run this without applying the modifications from this chapter (i.e. restoring the kernel stack from `sscratch`), the kernel hangs without displaying anything, and you'll see the following output in QEMU's log:

```
epc:0x0100004e, tval:0x00000000, desc=illegal_instruction <- unimp triggers the trap handler
epc:0x802009dc, tval:0xdeadbe73, desc=store_page_fault <- an aborted write to the stack  (0xdeadbeef)
epc:0x802009dc, tval:0xdeadbdf7, desc=store_page_fault <- an aborted write to the stack  (0xdeadbeef) (2)
epc:0x802009dc, tval:0xdeadbd7b, desc=store_page_fault <- an aborted write to the stack  (0xdeadbeef) (3)
epc:0x802009dc, tval:0xdeadbcff, desc=store_page_fault <- an aborted write to the stack  (0xdeadbeef) (4)
...
```

First, an invalid instruction exception occurs with the `unimp` pseudo-instruction, transitioning to the kernel's trap handler. However, because the stack pointer points to an unmapped address (`0xdeadbeef`), an exception occurs when trying to save registers, jumping back to the beginning of the trap handler. This becomes an infinite loop, causing the kernel to hang. To prevent this, we need to retrieve a trusted stack area from `sscratch`.

Another solution is to have multiple exception handlers. In the RISC-V version of xv6 (a famous educational UNIX-like OS), there are separate exception handlers for cases (1) and (2) ([`kernelvec`](https://github.com/mit-pdos/xv6-riscv/blob/f5b93ef12f7159f74f80f94729ee4faabe42c360/kernel/kernelvec.S#L13-L14)) and for case (3) ([`uservec`](https://github.com/mit-pdos/xv6-riscv/blob/f5b93ef12f7159f74f80f94729ee4faabe42c360/kernel/trampoline.S#L74-L75)). In the former case, it inherits the stack pointer at the time of the exception, and in the latter case, it retrieves a separate kernel stack. The trap handler is [switched](https://github.com/mit-pdos/xv6-riscv/blob/f5b93ef12f7159f74f80f94729ee4faabe42c360/kernel/trap.c#L44-L46) when entering and exiting the kernel.

> [!TIP]
>
> In Fuchsia, an OS developed by Google, there was a case where an API allowing arbitrary program counter values to be set from the user became [a vulnerability](https://blog.quarkslab.com/playing-around-with-the-fuchsia-operating-system.html). Not trusting input from users (applications) is an extremely important habit in kernel development.

## Next Steps

We have now achieved the ability to run multiple processes concurrently, realizing a multi-tasking OS.

However, as it stands, processes can freely read and write to the kernel's memory space. It's super insecure! In the coming chapters, we'll look at how to safely run applications, in other words, how to isolate the kernel and applications.
