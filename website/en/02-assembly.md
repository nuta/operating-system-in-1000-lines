---
title: RISC-V 101
layout: chapter
lang: en
---

## RISC-V

Just like web browsers hide the differences between Windows, macOS, and Linux, operating systems hide the differences between CPUs. In other words, operating system is a program which controls the CPU to provides an abstraction layer for applications.

In this book, we write an OS for RISC-V CPU for the following reasons:

- [The specification](https://riscv.org/technical/specifications/) is simple and suitable for beginners.
- It's a trending CPU ("Instruction Set Architecture") recent years.
- The explanations of design decitions mentioned throughout the specification are interesting and educational.

Note that this book uses **32-bit** RISC-V. While it can be implemented similarly in 64-bit, the wider bit width makes it more complex, and the longer addresses can be tedious to read, so 32-bit is recommended for beginners.

## `virt` machine

Computers are composed of various devices: CPU, memory, network cards, hard disks, and so on. For example, although iPhone and Raspberry Pi use Arm CPUs, it's natural to consider them as different computers.

In this book, we support the QEMU `virt` machine ([documentation](https://www.qemu.org/docs/master/system/riscv/virt.html)) because:

- Although it's a virtual device which does not exist in the real world, it's simple and is very similar to real devices.
- It use it on QEMU emulator for free. You don't need to buy a physical hardware.
- When you encounter debugging issues, you can read QEMU's source code, or connect a debugger to QEMU itself to investigate what's wrong.

## RISC-V assembly 101

A quick way to learn assembly is to observe how C code translates into assembly. 

<!--
- Assembly is written as a string literal. Each instruction has the structure `opcode operand1, operand2, ...`. The opcode is the assembly instruction name, and operands are the arguments for the instruction.
- Generally, one instruction is written per line in assembly. In terms of general programming languages, it's like a series of function calls. When writing multiple instructions, separate them with newline characters, e.g. `"addi x1, x2, 3\naddi x3, x4, 5"`.
-->

> [!NOTE]
>
> **TODO:** The explanation of assembly in this English version is currently omitted. Ask ChatGPT for the following topics for now.
> 
> - What are registers
> - Arithmetic operations
> - Memory access
> - Branch instructions
> - Function calls
> - Stack/heap

### Try Compiler Explorer

A useful tool for learning assembly is [Compiler Explorer](https://godbolt.org/), an online compiler. As you type C code, it shows the corresponding assembly code.

Also, it would be interesting to specify optimization options like `-O0` (optimization off) or `-O2` (optimization level 2) in the compiler options and see how the assembly changes.

> [!WARNING]
>
> By default, Compiler Explorer uses x86-64 CPU assembly. Specify `RISC-V rv32gc clang (trunk)` in the right pane to output 32-bit RISC-V assembly.

## CPU modes

CPU has multiple modes, each with different privileges. In RISC-V, there are three modes:

| Mode   | Overview                            |
| ------ | ----------------------------------- |
| M-mode | Mode in which OpenSBI (i.e. BIOS) operates.     |
| S-mode | Mode in which the kernel operates, aka. "kernel mode". |
| U-mode | Mode in which applications operate, aka. "user mode".  |

## Privileged instructions

Among CPU instructions, there are types called privileged instructions that applications cannot execute. In this book, we use the following privileged instructions:

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
> Some instructions, especially `sret` does some somewhat complex operations. To understand what actually happens, reading RISC-V emulator source code might be helpful. Particularly, [rvemu](https://github.com/d0iasm/rvemu) is written in a intuitive and easy-to-understand way (e.g. [sret implementation](https://github.com/d0iasm/rvemu/blob/f55eb5b376f22a73c0cf2630848c03f8d5c93922/src/cpu.rs#L3357-L3400)).

## Inline assembly

In following chapters, you'll encounter special C language syntax like this:

```c
uint32_t value;
__asm__ __volatile__("csrr %0, sepc" : "=r"(value));
```

This is *"inline assembly"*, a syntax for embedding assembly into C code. While you can write assembly in a separate file (`.S` extension), using inline assembly are generally preferred because:

- You can use C variables within the assembly. Also, you can assign the results of assembly to C variables.
- You can leave register allocation to the C compiler. That is, you don't have to manually write the preservation and restoration of registers to be modified in the assembly.

### How to write inline assembly

Inline assembly is written in the following format:

```c
__asm__ __volatile__("assembly" : output operands : input operands : clobbered registers);
```

| Part               | Description                                                                 |
| ------------------ | --------------------------------------------------------------------------- |
| `"assembly"`       | Assembly code written as a string literal.                                   |
| `output operands`  | C variables to store the results of the assembly.                           |
| `input operands`   | C expressions (e.g. `123`, `x`) to be used in the assembly.             |
| `__volatile__`     | Tell the compiler not optimize the `assembly` code.                          |
| `clobbered registers` | Registers whose contents are destroyed in the assembly. If forgotten, the C compiler won't preserve and restore the contents of these registers. |

Output and input operands are comma-separated, and each operand is written in the format `constraint (C expression)`. Constraints are used to specify the type of operand, and usually `=r` (register) for output operands and `r` for input operands.

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
> Inline assembly is a compiler-specific extension not included in the C language specification. You can check detailed usage in the [GCC documentation](https://gcc.gnu.org/onlinedocs/gcc/Extended-Asm.html). However, it's a feature that takes time to understand as constraint syntax differs depending on CPU architecture, and it has many complex functionalities.
>
> For beginners, it's recommended to search for real-world examples. For instance, [HinaOS](https://github.com/nuta/microkernel-book/blob/52d66bd58cd95424f009e2df8bc1184f6ffd9395/kernel/riscv32/asm.h) and [xv6-riscv](https://github.com/mit-pdos/xv6-riscv/blob/riscv/kernel/riscv.h) are good references.
