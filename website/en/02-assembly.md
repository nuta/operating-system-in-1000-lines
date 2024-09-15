---
title: RISC-V 101
layout: chapter
lang: en
---

## RISC-V

Just as you consider "which OS to run on" when developing applications, you also need to think about which hardware (especially CPU) to run the OS on in the layer below. In this book, we chose RISC-V for the following reasons:

- The specification is simple and suitable for beginners.
- It's a CPU (Instruction Set Architecture) that has been frequently discussed in recent years.
- The explanations of "why this design was chosen" mentioned throughout the specification are interesting and educational.

Note that this book uses 32-bit RISC-V. While it can be implemented similarly in 64-bit, the wider bit width makes it more complex, and the longer addresses can be tedious to read, so 32-bit is recommended for beginners.

## Virt machine

Computers are composed of various devices, not just CPUs, including memory. For example, although iPhones and Raspberry Pis use the same Arm CPUs, it's natural to consider them as different devices.

In this book, we decided to support the QEMU virt machine ([documentation](https://www.qemu.org/docs/master/system/riscv/virt.html)) among RISC-V based options for the following reasons:

- Although it's a virtual computer that doesn't physically exist, as the name suggests, it provides an experience very close to supporting real hardware.
- It runs on QEMU, so you can try it immediately for free without buying physical hardware. Also, there's no risk of it becoming difficult to obtain due to discontinuation.
- If you encounter debugging issues, you can read QEMU's source code or connect a debugger to QEMU itself to investigate what's wrong.

## RISC-V assembly 101

A quick way to learn assembly is to "observe how C code translates into assembly." Then, you can trace the functions of each instruction used one by one. The following items form the basic knowledge. Although it might be too basic to write in an introductory book, you can ask ChatGPT about these:

- What are registers
- Arithmetic operations
- Memory access
- Branch instructions
- Function calls
- Stack mechanism

A useful tool for learning assembly is [Compiler Explorer](https://godbolt.org/), an online compiler. Here, you can input C code and see the disassembled result of the compiled code. It color-codes to show which machine code corresponds to which part of the C code.

Also, it's educational to specify optimization options like `-O0` (optimization off) or `-O2` (optimization level 2) in the compiler options and observe what kind of assembly the compiler outputs.

> [!WARNING]
>
> By default, x86-64 CPU assembly is output. Specify the compiler as `RISC-V rv32gc clang (trunk)` in the right pane to output 32-bit RISC-V assembly.

## CPU operation modes

In CPUs, the executable instructions and behavior differ depending on the operation mode. RISC-V has the following three operation modes:

| Mode | Overview |
| --- | --- |
| M-mode | Mode in which OpenSBI operates. |
| S-mode | Mode in which the kernel operates. |
| U-mode | Mode in which applications operate. |

## Privileged instructions

Among CPU instructions, there are types called privileged instructions that applications cannot execute. In this book, several privileged instructions that change CPU operation settings will appear. The following are the privileged instructions that appear in this book:

| Instruction and Operands | Overview | Pseudocode |
| --- | --- | --- |
| `csrr rd, csr` | Read from CSR | `rd = csr;` |
| `csrw csr, rs` | Write to CSR | `csr = rs;` |
| `csrrw rd, csr, rs` | Read from and write to CSR at once | `tmp = csr; csr = rs; rd = tmp;` |
| `sret` | Return from trap handler (restoring program counter, operation mode, etc.) | |
| `sfence.vma` | Clear Translation Lookaside Buffer (TLB) | |

CSR (Control and Status Register) mentioned here is a register that stores CPU operation settings. Explanations for each CSR are in the book, but if you want to see a list,

## Inline assembly

In the main text, you'll encounter special C language syntax like this:

```c
uint32_t value;
__asm__ __volatile__("csrr %0, sepc" : "=r"(value));
```

This is called "inline assembly," a syntax for embedding assembly within C code. While you can write assembly in a separate file (with a `.S` extension) and compile it, using inline assembly offers the following advantages:

- You can use C variables within the assembly. Also, you can assign the results of assembly to C variables.
- You can leave register allocation to the C compiler. You don't have to manually write the preservation and restoration of registers whose contents are changed in the assembly.

### How to write inline assembly

Inline assembly is written in the following format:

```c
__asm__ __volatile__("Assembly" : Output operands : Input operands : Clobbered registers);
```

- Assembly is written as a string literal. Each instruction has the structure `opcode operand1, operand2, ...`. The opcode is the assembly instruction name, and operands are the arguments for the instruction.
- Generally, one instruction is written per line in assembly. In terms of general programming languages, it's like a series of function calls. When writing multiple instructions, separate them with newline characters like `"addi x1, x2, 3\naddi x3, x4, 5"`.
- Output operands declare where to extract the results of the assembly. They are written in the format `"constraint" (C variable name)`. The constraint is usually `=r`, where `=` indicates it's modified by the assembly, and `r` indicates it uses any general-purpose register.
- Input operands declare values to be used in the assembly. They are written similarly to output operands, in the format `"constraint" (C expression)`. The constraint is usually `r`, indicating it sets a value in any general-purpose register.
- Finally, specify registers whose contents are destroyed in the assembly. If forgotten, the C compiler won't preserve and restore the contents of these registers, leading to bugs where local variables have unexpected values.
- Output and input operands can be accessed in the assembly using `%0`, `%1`, `%2`, etc., in order starting from the output operands.
- `__volatile__` instructs the compiler "do not optimize the assembly content (output as is)".

### Examples

```c
uint32_t value;
__asm__ __volatile__("csrr %0, sepc" : "=r"(value));
```

This inline assembly reads the value of the `sepc` register using the `csrr` instruction and assigns it to the `value` variable. `%0` corresponds to the `value` variable.

```c
__asm__ __volatile__("csrw sscratch, %0" : : "r"(123));
```

This inline assembly writes `123` to the `sscratch` register using the `csrw` instruction. `%0` corresponds to the register containing `123` (`r` constraint), and it expands as follows:

```
li    a0, 123        // Set 123 to a0 register
csrw  sscratch, a0   // Write the value of a0 register to sscratch register
```

Although only the `csrw` instruction is written in the inline assembly, the `li` instruction is automatically inserted by the compiler to satisfy the `"r"` constraint.

> [!TIP]
>
> Inline assembly is a compiler-specific extension not included in the C language specification. You can check detailed usage in the [GCC documentation](https://gcc.gnu.org/onlinedocs/gcc/Extended-Asm.html). However, it's a feature that takes time to understand as constraint writing differs depending on CPU architecture, and it has many complex functionalities.
>
> For beginners, it's recommended to encounter many practical examples. For instance, [HinaOS's inline assembly collection](https://github.com/nuta/microkernel-book/blob/52d66bd58cd95424f009e2df8bc1184f6ffd9395/kernel/riscv32/asm.h) can be a good reference.