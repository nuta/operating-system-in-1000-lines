# RISC-V 101

Just like web browsers hide the differences between Windows/macOS/Linux, operating systems hide the differences between CPUs. In other words, operating system is a program which controls the CPU to provide an abstraction layer for applications.

In this book, I chose RISC-V as the target CPU because:

- [The specification](https://riscv.org/technical/specifications/) is simple and suitable for beginners.
- It's a trending ISA (Instruction Set Architecture) in recent years, along with x86 and Arm.
- The design decisions are well-documented throughout the spec and they are fun to read.

We will write an OS for **32-bit** RISC-V. Of course you can write for 64-bit RISC-V with only a few changes. However, the wider bit width makes it slightly more complex, and the longer addresses can be tedious to read.

## QEMU virt machine

Computers are composed of various devices: CPU, memory, network cards, hard disks, and so on. For example, although iPhones and Raspberry Pis use Arm CPUs, it's natural to consider them as different computers.

In this book, we support the QEMU `virt` machine ([documentation](https://www.qemu.org/docs/master/system/riscv/virt.html)) because:

- Even though it does not exist in the real world, it's simple and very similar to real devices.
- You can emulate it on QEMU for free. You don't need to buy a physical hardware.
- When you encounter debugging issues, you can read QEMU's source code, or attach a debugger to the QEMU process to investigate what's wrong.

## RISC-V assembly 101

RISC-V, or RISC-V ISA (Instruction Set Architecture), defines the instructions that the CPU can execute. It's similar to APIs or programming language specifications for programmers. When you write a C program, the compiler translates it into RISC-V assembly. Unfortunately, you need to write some assembly code to write an OS. But don't worry! Assembly is not as difficult as you might think.

> [!TIP]
>
> **Try Compiler Explorer!**
>
> A useful tool for learning assembly is [Compiler Explorer](https://godbolt.org/), an online compiler. As you type C code, it shows the corresponding assembly code.
>
> By default, Compiler Explorer uses x86-64 CPU assembly. Specify `RISC-V rv32gc clang (trunk)` in the right pane to output 32-bit RISC-V assembly.
>
> Also, it would be interesting to specify optimization options like `-O0` (optimization off) or `-O2` (optimization level 2) in the compiler options and see how the assembly changes.

### Assembly language basics

Assembly language is a (mostly) direct representation of machine code. Let's take a look at a simple example:

```asm
addi a0, a1, 123
```

Typically, each line of assembly code corresponds to a single instruction. The first column (`addi`) is the instruction name, also known as the *opcode*. The following columns (`a0, a1, 123`) are the *operands*, the arguments for the instruction. In this case, the `addi` instruction adds the value `123` to the value in register `a1`, and stores the result in register `a0`.

### Registers

Registers are like temporary variables in the CPU, and they are way faster than memory. CPU reads data from memory into registers, does arithmetic operations on registers, and writes the results back to memory/registers.

Here are some common registers in RISC-V:

| Register | ABI Name (alias) | Description |
|---| -------- | ----------- |
| `pc` | `pc`       | Program counter (where the next instruction is) |
| `x0` |`zero`     | Hardwired zero (always reads as zero) |
| `x1` |`ra`         | Return address |
| `x2` |`sp`         | Stack pointer |
| `x5` - `x7` | `t0` - `t2` | Temporary registers |
| `x8` | `fp`      | Stack frame pointer |
| `x10` - `x11` | `a0` - `a1`  | Function arguments/return values |
| `x12` - `x17` | `a2` - `a7`  | Function arguments |
| `x18` - `x27` | `s0` - `s11` | Temporary registers saved across calls |
| `x28` - `x31` | `t3` - `t6` | Temporary registers |

> [!TIP]
>
> **Calling convention:**
>
> Generally, you may use CPU registers as you like, but for the sake of interoperability with other software, how registers are used is well defined - this is called the *calling convention*.
>
> For example, `x10` - `x11` registers are used for function arguments and return values. For human readability, they are given aliases like `a0` - `a1` in the ABI. Check [the spec](https://riscv.org/wp-content/uploads/2015/01/riscv-calling.pdf) for more details.

### Memory access

Registers are really fast, but they are limited in number. Most of data are stored in memory, and programs reads/writes data from/to memory using the `lw` (load word) and `sw` (store word) instructions:

```asm
lw a0, (a1)  // Read a word (32-bits) from address in a1
             // and store it in a0. In C, this would be: a0 = *a1;
```

```asm
sw a0, (a1)  // Store a word in a0 to the address in a1.
             // In C, this would be: *a1 = a0;
```

You can consider `(...)` as a pointer dereference in C language. In this case, `a1` is a pointer to a 32-bits-wide value.

### Branch instructions

Branch instructions change the control flow of the program. They are used to implement `if`, `for`, and `while` statements, 


```asm
    bnez    a0, <label>   // Go to <label> if a0 is not zero
    // If a0 is zero, continue here

<label>:
    // If a0 is not zero, continue here
```

`bnez` stands for "branch if not equal to zero". Other common branch instructions include `beq` (branch if equal) and `blt` (branch if less than). They are similar to `goto` in C, but with conditions.

### Function calls

`jal` (jump and link) and `ret` (return) instructions are used for calling functions and returning from them:

```asm
    li  a0, 123      // Load 123 to a0 register (function argument)
    jal ra, <label>  // Jump to <label> and store the return address
                     // in the ra register.

    // After the function call, continue here...

// int func(int a) {
//   a += 1;
//   return a;
// }
<label>:
    addi a0, a0, 1    // Increment a0 (first argument) by 1

    ret               // Return to the address stored in ra.
                      // a0 register has the return value.
```

Function arguments are passed in `a0` - `a7` registers, and the return value is stored in `a0` register, as per the calling convention.

### Stack

Stack is a Last-In-First-Out (LIFO) memory space used for function calls and local variables. It grows downwards, and the stack pointer `sp` points to the top of the stack.

To save a value into the stack, decrement the stack pointer and store the value (aka. *push* operation):

```asm
    addi sp, sp, -4  // Move the stack pointer down by 4 bytes
                     // (i.e. stack allocation).

    sw   a0, (sp)    // Store a0 to the stack
```

To load a value from the stack, load the value and increment the stack pointer (aka. *pop* operation):

```asm
    lw   a0, (sp)    // Load a0 from the stack
    addi sp, sp, 4   // Move the stack pointer up by 4 bytes
                     // (i.e. stack deallocation).
```

> [!TIP]
>
> In C, stack operations are generated by the compiler, so you don't have to write them manually.

## CPU modes

CPU has multiple modes, each with different privileges. In RISC-V, there are three modes:

| Mode   | Overview                            |
| ------ | ----------------------------------- |
| M-mode | Mode in which OpenSBI (i.e. BIOS) operates.     |
| S-mode | Mode in which the kernel operates, aka. "kernel mode". |
| U-mode | Mode in which applications operate, aka. "user mode".  |

## Privileged instructions

Among CPU instructions, there are types called privileged instructions that applications (user mode) cannot execute. In this book, we use the following privileged instructions:

| Opcode and operands | Overview                                                                   | Pseudocode                       |
| ------------------------ | -------------------------------------------------------------------------- | -------------------------------- |
| `csrr rd, csr`           | Read from CSR                                                              | `rd = csr;`                      |
| `csrw csr, rs`           | Write to CSR                                                               | `csr = rs;`                      |
| `csrrw rd, csr, rs`      | Read from and write to CSR at once                                         | `tmp = csr; csr = rs; rd = tmp;` |
| `sret`                   | Return from trap handler (restoring program counter, operation mode, etc.) |                                  |
| `sfence.vma`             | Clear Translation Lookaside Buffer (TLB)                                   |                                  |

**CSR (Control and Status Register)** is a register that stores CPU settings. The list of CSRs can be found in [RISC-V Privileged Specification](https://riscv.org/specifications/privileged-isa/).

> [!TIP]
>
> Some instructions like `sret` do some somewhat complex operations. To understand what actually happens, reading RISC-V emulator source code might be helpful. Particularly, [rvemu](https://github.com/d0iasm/rvemu) is written in a intuitive and easy-to-understand way (e.g. [sret implementation](https://github.com/d0iasm/rvemu/blob/f55eb5b376f22a73c0cf2630848c03f8d5c93922/src/cpu.rs#L3357-L3400)).

## Inline assembly

In following chapters, you'll encounter special C language syntax like this:

```c
uint32_t value;
__asm__ __volatile__("csrr %0, sepc" : "=r"(value));
```

This is *"inline assembly"*, a syntax for embedding assembly into C code. While you can write assembly in a separate file (`.S` extension), using inline assembly is generally preferred because:

- You can use C variables within the assembly. Also, you can assign the results of assembly to C variables.
- You can leave register allocation to the C compiler. That is, you don't have to manually write the preservation and restoration of registers to be modified in the assembly.

### How to write inline assembly

Inline assembly is written in the following format:

```c
__asm__ __volatile__("assembly" : output operands : input operands : clobbered registers);
```

| Part               | Description                                                                 |
| ------------------ | --------------------------------------------------------------------------- |
| `__asm__`          | Indicates it's an inline assembly.                                           |
| `__volatile__`     | Tell the compiler not to optimize the `"assembly"` code.                          |
| `"assembly"`       | Assembly code written as a string literal.                                   |
| output operands  | C variables to store the results of the assembly.                           |
| input operands   | C expressions (e.g. `123`, `x`) to be used in the assembly.             |
| clobbered registers | Registers whose contents are destroyed in the assembly. If forgotten, the C compiler won't preserve the contents of these registers and would cause a bug. |

Output and input operands are comma-separated, and each operand is written in the format `constraint (C expression)`. Constraints are used to specify the type of operand, and usually `=r` (register) for output operands, and `r` for input operands.

Output and input operands can be accessed in the assembly using `%0`, `%1`, `%2`, etc., in order starting from the output operands.

### Examples

```c
uint32_t value;
__asm__ __volatile__("csrr %0, sepc" : "=r"(value));
```

This reads the value of the `sepc` CSR using the `csrr` instruction, and assigns it to the `value` variable. `%0` corresponds to the `value` variable.

```c
__asm__ __volatile__("csrw sscratch, %0" : : "r"(123));
```

This writes `123` to the `sscratch` CSR, using the `csrw` instruction. `%0` corresponds to the register containing `123` (`r` constraint), and it would actually look like:

```
li    a0, 123        // Set 123 to a0 register
csrw  sscratch, a0   // Write the value of a0 register to sscratch register
```

Although only the `csrw` instruction is written in the inline assembly, the `li` instruction is automatically inserted by the compiler to satisfy the `"r"` constraint (value in a register). It's super convenient!

> [!TIP]
>
> Inline assembly is a compiler-specific extension not included in the C language specification. You can check detailed usage in the [GCC documentation](https://gcc.gnu.org/onlinedocs/gcc/Extended-Asm.html). However, it takes time to understand because constraint syntax differs depending on CPU architecture, and it has many complex functionalities.
>
> For beginners, I recommend to search for real-world examples. For instance, [HinaOS](https://github.com/nuta/microkernel-book/blob/52d66bd58cd95424f009e2df8bc1184f6ffd9395/kernel/riscv32/asm.h) and [xv6-riscv](https://github.com/mit-pdos/xv6-riscv/blob/riscv/kernel/riscv.h) are good references.
