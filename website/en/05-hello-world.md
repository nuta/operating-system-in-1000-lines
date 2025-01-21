# Hello World! 

In the previous chapter, we successfully booted our first kernel. Although we could confirm it works by reading the register dump, it still felt somewhat unsatisfactory.

In this chapter, let's make it more obvious by outputting a string from the kernel.

## Say "hello" to SBI

In the previous chapter, we learned that SBI is an "API for OS". To call the SBI to use its function, we use the `ecall` instruction:

```c [kernel.c] {1, 5-26, 29-32}
#include "kernel.h"

extern char __bss[], __bss_end[], __stack_top[];

struct sbiret sbi_call(long arg0, long arg1, long arg2, long arg3, long arg4,
                       long arg5, long fid, long eid) {
    register long a0 __asm__("a0") = arg0;
    register long a1 __asm__("a1") = arg1;
    register long a2 __asm__("a2") = arg2;
    register long a3 __asm__("a3") = arg3;
    register long a4 __asm__("a4") = arg4;
    register long a5 __asm__("a5") = arg5;
    register long a6 __asm__("a6") = fid;
    register long a7 __asm__("a7") = eid;

    __asm__ __volatile__("ecall"
                         : "=r"(a0), "=r"(a1)
                         : "r"(a0), "r"(a1), "r"(a2), "r"(a3), "r"(a4), "r"(a5),
                           "r"(a6), "r"(a7)
                         : "memory");
    return (struct sbiret){.error = a0, .value = a1};
}

void putchar(char ch) {
    sbi_call(ch, 0, 0, 0, 0, 0, 0, 1 /* Console Putchar */);
}

void kernel_main(void) {
    const char *s = "\n\nHello World!\n";
    for (int i = 0; s[i] != '\0'; i++) {
        putchar(s[i]);
    }

    for (;;) {
        __asm__ __volatile__("wfi");
    }
}
```

Also, create a new `kernel.h` file and define the return value structure:

```c [kernel.h]
#pragma once

struct sbiret {
    long error;
    long value;
};
```

We've newly added the `sbi_call` function. This function is designed to call OpenSBI as specified in the SBI specification. The specific calling convention is as follows:

> **Chapter 3. Binary Encoding**
>
> All SBI functions share a single binary encoding, which facilitates the mixing of SBI extensions. The SBI specification follows the below calling convention.
>
> - An `ECALL` is used as the control transfer instruction between the supervisor and the SEE.
> - `a7` encodes the SBI extension ID (**EID**),
> - `a6` encodes the SBI function ID (**FID**) for a given extension ID encoded in `a7` for any SBI extension defined in or after SBI v0.2.
> - All registers except `a0` & `a1` must be preserved across an SBI call by the callee.
> - SBI functions must return a pair of values in `a0` and `a1`, with `a0` returning an error code. This is analogous to returning the C structure
>
> ```c
> struct sbiret {
>     long error;
>     long value;
> };
> ```
>
> -- "RISC-V Supervisor Binary Interface Specification" v2.0-rc1

> [!TIP]
>
> *"All registers except `a0` & `a1` must be preserved across an SBI call by the callee"* means that the callee (OpenSBI side) must not change the values of ***except*** `a0` and `a1`. In other words, from the kernel's perspective, it is guaranteed that the registers (`a2` to `a7`) will remain the same after the call.

The `register` and `__asm__("register name")` used in each local variable declaration asks the compiler to place values in the specified registers. This is a common idiom in system call invocations (e.g., [Linux system call invocation process](https://git.musl-libc.org/cgit/musl/tree/arch/riscv64/syscall_arch.h)).

After preparing the arguments, the `ecall` instruction is executed in inline assembly. When this is called, the CPU's execution mode switches from kernel mode (S-Mode) to OpenSBI mode (M-Mode), and OpenSBI's processing handler is invoked. Once it's done, it switches back to kernel mode, and execution resumes after the `ecall` instruction. 

The `ecall` instruction is also used when applications call the kernel (system calls). This instruction behaves like a function call to the more privileged CPU mode.

To display characters, we can use `Console Putchar` function:

> 5.2. Extension: Console Putchar (EID #0x01)
>
> ```c
>   long sbi_console_putchar(int ch)
> ```
>
> Write data present in ch to debug console.
>
> Unlike sbi_console_getchar(), this SBI call will block if there remain any pending characters to be transmitted or if the receiving terminal is not yet ready to receive the byte. However, if the console doesn’t exist at all, then the character is thrown away.
>
> This SBI call returns 0 upon success or an implementation specific negative error code.
>
> -- "RISC-V Supervisor Binary Interface Specification" v2.0-rc1

`Console Putchar` is a function that outputs the character passed as an argument to the debug console.

### Try it out

Let's try your implementation. You should see `Hello World!` if it works:

```
$ ./run.sh
...

Hello World!
```

> [!TIP]
>
> **Life of Hello World:**
>
> When SBI is called, characters will be displayed as follows:
>
> 1. The kernel executes `ecall` instruction. The CPU jumps to the M-mode trap handler (`mtvec` register), which is set by OpenSBI during startup.
> 2. After saving registers, the [trap handler written in C](https://github.com/riscv-software-src/opensbi/blob/0ad866067d7853683d88c10ea9269ae6001bcf6f/lib/sbi/sbi_trap.c#L263) is called.
> 3. Based on the `eid`, the [corresponding SBI processing function is called](https://github.com/riscv-software-src/opensbi/blob/0ad866067d7853683d88c10ea9269ae6001bcf6f/lib/sbi/sbi_ecall_legacy.c#L63C2-L65).
> 4. The [device driver](https://github.com/riscv-software-src/opensbi/blob/0ad866067d7853683d88c10ea9269ae6001bcf6f/lib/utils/serial/uart8250.c#L77) for the 8250 UART ([Wikipedia](https://en.wikipedia.org/wiki/8250_UART)) sends the character to QEMU.
> 5. QEMU's 8250 UART emulation implementation receives the character and sends it to the standard output.
> 6. The terminal emulator displays the character.
>
> That is, by calling `Console Putchar` function is not a magic at all - it just uses the device driver implemented in OpenSBI!

## `printf` function

We've successfully printed some characters. The next item is implementing `printf` function.

`printf` function takes a format string, and the values to be embedded in the output. For example, `printf("1 + 2 = %d", 1 + 2)` will display `1 + 2 = 3`.

While `printf` bundled in the C standard library has a very rich set of features, let's start with a minimal version. Specifically, we'll implement a `printf` that supports three format specifiers: `%d` (decimal), `%x` (hexadecimal), and `%s` (string).

Since we'll use `printf` in applications too, let's create a new file `common.c` for code shared between the kernel and userland.

Here's the implementation of the `printf` function:

```c [common.c]
#include "common.h"

void putchar(char ch);

void printf(const char *fmt, ...) {
    va_list vargs;
    va_start(vargs, fmt);

    while (*fmt) {
        if (*fmt == '%') {
            fmt++; // Skip '%'
            switch (*fmt) { // Read the next character
                case '\0': // '%' at the end of the format string
                    putchar('%');
                    goto end;
                case '%': // Print '%'
                    putchar('%');
                    break;
                case 's': { // Print a NULL-terminated string.
                    const char *s = va_arg(vargs, const char *);
                    while (*s) {
                        putchar(*s);
                        s++;
                    }
                    break;
                }
                case 'd': { // Print an integer in decimal.
                    int value = va_arg(vargs, int);
                    if (value < 0) {
                        putchar('-');
                        value = -value;
                    }

                    int divisor = 1;
                    while (value / divisor > 9)
                        divisor *= 10;

                    while (divisor > 0) {
                        putchar('0' + value / divisor);
                        value %= divisor;
                        divisor /= 10;
                    }

                    break;
                }
                case 'x': { // Print an integer in hexadecimal.
                    int value = va_arg(vargs, int);
                    for (int i = 7; i >= 0; i--) {
                        int nibble = (value >> (i * 4)) & 0xf;
                        putchar("0123456789abcdef"[nibble]);
                    }
                }
            }
        } else {
            putchar(*fmt);
        }

        fmt++;
    }

end:
    va_end(vargs);
}
```

It's surprisingly concise, isn't it? It goes through the format string character by character, and if we encounter a `%`, we look at the next character and perform the corresponding formatting operation. Characters other than `%` are printed as is.

For decimal numbers, if `value` is negative, we first output a `-` and then get its absolute value. We then calculate the divisor to get the most significant digit and output the digits one by one.

For hexadecimal numbers, we output from the most significant *nibble* (a hexadecimal digit, 4 bits) to the least significant. Here, `nibble` is an integer from 0 to 15, so we use it as the index in string `"0123456789abcdef"` to get the corresponding character.

`va_list` and related macros are defined in the C standard library's `<stdarg.h>`. In this book, we use compiler builtins directly without relying on the standard library. Specifically, we'll define them in `common.h` as follows:

```c [common.h]
#pragma once

#define va_list  __builtin_va_list
#define va_start __builtin_va_start
#define va_end   __builtin_va_end
#define va_arg   __builtin_va_arg

void printf(const char *fmt, ...);
```

We're simply defining these as aliases for the versions with `__builtin_` prefixed. They are builtin features provided by the compiler (clang) itself ([Reference: clang documentation](https://clang.llvm.org/docs/LanguageExtensions.html#variadic-function-builtins)). The compiler will handle the rest appropriately, so we don't need to worry about it.

Now we've implemented `printf`. Let's add a "Hello World" from the kernel:

```c [kernel.c] {2,5-6}
#include "kernel.h"
#include "common.h"

void kernel_main(void) {
    printf("\n\nHello %s\n", "World!");
    printf("1 + 2 = %d, %x\n", 1 + 2, 0x1234abcd);

    for (;;) {
        __asm__ __volatile__("wfi");
    }
}
```

Also, Add `common.c` to the compilation targets:

```bash [run.sh] {2}
$CC $CFLAGS -Wl,-Tkernel.ld -Wl,-Map=kernel.map -o kernel.elf \
    kernel.c common.c
```

Now, let's try! You will see `Hello World!` and `1 + 2 = 3, 1234abcd` as shown below:

```
$ ./run.sh

Hello World!
1 + 2 = 3, 1234abcd
```

The powerful ally "printf debugging" has joined your OS!
