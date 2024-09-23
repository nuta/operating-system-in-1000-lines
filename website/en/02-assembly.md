---
title: RISC-V 101
layout: chapter
lang: en
---

> [!NOTE] Translation of this English version is in progress.

## RISC-V

Just like web browsers hide the differences between Windows, macOS, and Linux, OSes hide the differences between CPUs. In this book, we chose RISC-V as the target CPU for the following reasons:

- The specification is simple and suitable for beginners.
- It's a trending CPU (or *"Instruction Set Architecture"*) recent years.
- The explanations of design decitions mentioned throughout the specification are interesting and educational.

Note that this book uses **32-bit** RISC-V. While it can be implemented similarly in 64-bit, the wider bit width makes it more complex, and the longer addresses can be tedious to read, so 32-bit is recommended for beginners.

## `virt` machine

Computers are composed of various devices, not just CPUs, including memory, network cards, hard disks, and so on. For example, although iPhone and Raspberry Pi use Arm CPUs, it's natural to consider them as different computers.

In this book, we decided to support the QEMU `virt` machine ([documentation](https://www.qemu.org/docs/master/system/riscv/virt.html)) because:

- Although it does not exist in the real world, as the name suggests, it provides an experience very close to supporting real hardware.
- It runs on QEMU, so you can try it immediately for free without buying a physical hardware.
- If you encounter debugging issues, you can read QEMU's source code or connect a debugger to QEMU itself to investigate what's wrong.

## RISC-V assembly 101

A quick way to learn assembly is to observe how C code translates into assembly. 

> [!NOTE]
>
> The explanation of assembly in this English version is currently omitted. Ask ChatGPT for the following topics for now!
> 
> - What are registers
> - Arithmetic operations
> - Memory access
> - Branch instructions
> - Function calls
> - Stack/heap

A useful tool for learning assembly is [Compiler Explorer](https://godbolt.org/), an online compiler. Here, you can input C code and see the disassembled result of the compiled code. It color-codes to show which machine code corresponds to which part of the C code.

Also, it's educational to specify optimization options like `-O0` (optimization off) or `-O2` (optimization level 2) in the compiler options and observe what kind of assembly the compiler outputs.

> [!WARNING]
>
> By default, Compiler Explorer uses x86-64 CPU assembly. Specify `RISC-V rv32gc clang (trunk)` in the right pane to output 32-bit RISC-V assembly.

## CPU modes

In CPUs, the executable instructions and behavior differ depending on the mode. RISC-V has the following three modes:

| Mode   | Overview                            |
| ------ | ----------------------------------- |
| M-mode | Mode in which OpenSBI (i.e. BIOS) operates.     |
| S-mode | Mode in which the kernel operates.  |
| U-mode | Mode in which applications operate. |

## Privileged instructions

Among CPU instructions, there are types called privileged instructions that applications cannot execute. In this book, several privileged instructions that change CPU operation settings will appear:

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
> Some instructions, especially `sret` do some somewhat complex operations. To understand what actually happens, reading RISC-V emulator source code might be helpful. Particularly, [rvemu](https://github.com/d0iasm/rvemu) is written in a intuitive and easy-to-understand way (e.g. [sret implementation](https://github.com/d0iasm/rvemu/blob/f55eb5b376f22a73c0cf2630848c03f8d5c93922/src/cpu.rs#L3357-L3400)).


## Inline assembly

In following chapters, you'll encounter special C language syntax like this:

```c
uint32_t value;
__asm__ __volatile__("csrr %0, sepc" : "=r"(value));
```

This is called *"inline assembly"*, a syntax for embedding assembly into C code. While you can write assembly in a separate file (`.S` extension), using inline assembly are generally preferred:

- You may use C variables within the assembly. Also, you can assign the results of assembly to C variables.
- You can leave register allocation to the C compiler. That is, you don't have to manually write the preservation and restoration of registers to be modified in the assembly.

### How to write inline assembly

Inline assembly is written in the following format:

```c
__asm__ __volatile__("assembly" : output operands : input operands : clobbered registers);
```

- Assembly is written as a string literal. Each instruction has the structure `opcode operand1, operand2, ...`. The opcode is the assembly instruction name, and operands are the arguments for the instruction.
- Generally, one instruction is written per line in assembly. In terms of general programming languages, it's like a series of function calls. When writing multiple instructions, separate them with newline characters, e.g. `"addi x1, x2, 3\naddi x3, x4, 5"`.
- Output operands declare where to extract the results of the assembly. They are written in the format `"constraint" (C variable name)`. The constraint is usually `=r`, where `=` indicates it's modified by the assembly, and `r` indicates it uses any general-purpose register.
- Input operands declare values to be used in the assembly. They are written similarly to output operands, in the format `"constraint" (C expression)`. The constraint is usually `r`, indicating it sets a value in any general-purpose register.
- Lastly, specify registers whose contents are destroyed in the assembly. If forgotten, the C compiler won't preserve and restore the contents of these registers, leading to bugs where local variables have unexpected values.
- Output and input operands can be accessed in the assembly using `%0`, `%1`, `%2`, etc., in order starting from the output operands.
- `__volatile__` instructs the compiler *"do not optimize the assembly content (output as is)"*.

### Examples

```c
uint32_t value;
__asm__ __volatile__("csrr %0, sepc" : "=r"(value));
```

This inline assembly reads the value of the `sepc` CSR using the `csrr` instruction, and assigns it to the `value` variable. `%0` corresponds to the `value` variable.

```c
__asm__ __volatile__("csrw sscratch, %0" : : "r"(123));
```

This inline assembly writes `123` to the `sscratch` CSR, using the `csrw` instruction. `%0` corresponds to the register containing `123` (`r` constraint), and it expands as follows:

```
li    a0, 123        // Set 123 to a0 register
csrw  sscratch, a0   // Write the value of a0 register to sscratch register
```

Although only the `csrw` instruction is written in the inline assembly, the `li` instruction is automatically inserted by the compiler to satisfy the `"r"` constraint.

> [!TIP]
>
> Inline assembly is a compiler-specific extension not included in the C language specification. You can check detailed usage in the [GCC documentation](https://gcc.gnu.org/onlinedocs/gcc/Extended-Asm.html). However, it's a feature that takes time to understand as constraint syntax differs depending on CPU architecture, and it has many complex functionalities.
>
> For beginners, it's recommended to encounter many practical examples. For instance, [HinaOS's inline assembly collection](https://github.com/nuta/microkernel-book/blob/52d66bd58cd95424f009e2df8bc1184f6ffd9395/kernel/riscv32/asm.h) can be a good reference.
