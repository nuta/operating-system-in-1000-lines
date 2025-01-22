# User Mode

In this chapter, we'll run the application we created in the previous chapter.

## Extracting the executable file

In executable file formats like ELF, the load address is stored in its file header (program header in ELF). However, since our application's execution image is a raw binary, we need to prepare it with a fixed value like this:

```c [kernel.h]
// The base virtual address of an application image. This needs to match the
// starting address defined in `user.ld`.
#define USER_BASE 0x1000000
```

Next, define symbols to use the embedded raw binary in `shell.bin.o`:

```c [kernel.c]
extern char _binary_shell_bin_start[], _binary_shell_bin_size[];
```

Also, update the `create_process` function to start the application:

```c [kernel.c] {1-3,5,11,20-33}
void user_entry(void) {
    PANIC("not yet implemented");
}

struct process *create_process(const void *image, size_t image_size) {
    /* omitted */
    *--sp = 0;                      // s3
    *--sp = 0;                      // s2
    *--sp = 0;                      // s1
    *--sp = 0;                      // s0
    *--sp = (uint32_t) user_entry;  // ra (changed!)

    uint32_t *page_table = (uint32_t *) alloc_pages(1);

    // Map kernel pages.
    for (paddr_t paddr = (paddr_t) __kernel_base;
         paddr < (paddr_t) __free_ram_end; paddr += PAGE_SIZE)
        map_page(page_table, paddr, paddr, PAGE_R | PAGE_W | PAGE_X);

    // Map user pages.
    for (uint32_t off = 0; off < image_size; off += PAGE_SIZE) {
        paddr_t page = alloc_pages(1);

        // Handle the case where the data to be copied is smaller than the
        // page size.
        size_t remaining = image_size - off;
        size_t copy_size = PAGE_SIZE <= remaining ? PAGE_SIZE : remaining;

        // Fill and map the page.
        memcpy((void *) page, image + off, copy_size);
        map_page(page_table, USER_BASE + off, page,
                 PAGE_U | PAGE_R | PAGE_W | PAGE_X);
    }
```

We've modified `create_process` to take the pointer to the execution image (`image`) and the image size (`image_size`) as arguments. It copies the execution image page by page for the specified size and maps it to the process' page table. Also, it sets the jump destination for the first context switch to `user_entry`. For now, we'll keep this as an empty function.

> [!WARNING]
>
> If you map the execution image directly without copying it, processes of the same application would end up sharing the same physical pages. It ruins the memory isolation!

Lastly, modify the caller of the `create_process` function and make it create a user process:

```c [kernel.c] {8,12}
void kernel_main(void) {
    memset(__bss, 0, (size_t) __bss_end - (size_t) __bss);

    printf("\n\n");

    WRITE_CSR(stvec, (uint32_t) kernel_entry);

    idle_proc = create_process(NULL, 0); // updated!
    idle_proc->pid = -1; // idle
    current_proc = idle_proc;

    // new!
    create_process(_binary_shell_bin_start, (size_t) _binary_shell_bin_size);

    yield();
    PANIC("switched to idle process");
}
```

Let's try it and check with the QEMU monitor if the execution image is mapped as expected:

```
(qemu) info mem
vaddr    paddr            size     attr
-------- ---------------- -------- -------
01000000 0000000080265000 00001000 rwxu---
01001000 0000000080267000 00010000 rwxu---
```

We can see that the physical address `0x80265000` is mapped to the virtual address `0x1000000` (`USER_BASE`). Let's take a look at the contents of this physical address. To display the contents of physical memory, use `xp` command:

```
(qemu) xp /32b 0x80265000
0000000080265000: 0x37 0x05 0x01 0x01 0x13 0x05 0x05 0x26
0000000080265008: 0x2a 0x81 0x19 0x20 0x29 0x20 0x00 0x00
0000000080265010: 0x01 0xa0 0x00 0x00 0x82 0x80 0x01 0xa0
0000000080265018: 0x09 0xca 0xaa 0x86 0x7d 0x16 0x13 0x87
```

It seems some data is present. Check the contents of `shell.bin` to confirm that it indeed matches:

```
$ hexdump -C shell.bin | head
00000000  37 05 01 01 13 05 05 26  2a 81 19 20 29 20 00 00  |7......&*.. ) ..|
00000010  01 a0 00 00 82 80 01 a0  09 ca aa 86 7d 16 13 87  |............}...|
00000020  16 00 23 80 b6 00 ba 86  75 fa 82 80 01 ce aa 86  |..#.....u.......|
00000030  03 87 05 00 7d 16 85 05  93 87 16 00 23 80 e6 00  |....}.......#...|
00000040  be 86 7d f6 82 80 03 c6  05 00 aa 86 01 ce 85 05  |..}.............|
00000050  2a 87 23 00 c7 00 03 c6  05 00 93 06 17 00 85 05  |*.#.............|
00000060  36 87 65 fa 23 80 06 00  82 80 03 46 05 00 15 c2  |6.e.#......F....|
00000070  05 05 83 c6 05 00 33 37  d0 00 93 77 f6 0f bd 8e  |......37...w....|
00000080  93 b6 16 00 f9 8e 91 c6  03 46 05 00 85 05 05 05  |.........F......|
00000090  6d f2 03 c5 05 00 93 75  f6 0f 33 85 a5 40 82 80  |m......u..3..@..|
```

Hmm, it's hard to understand in hexadecimal. Let's disassemble the machine code to see if it matches the expected instructions:

```
(qemu) xp /8i 0x80265000
0x80265000:  01010537          lui                     a0,16842752
0x80265004:  26050513          addi                    a0,a0,608
0x80265008:  812a              mv                      sp,a0
0x8026500a:  2019              jal                     ra,6                    # 0x80265010
0x8026500c:  2029              jal                     ra,10                   # 0x80265016
0x8026500e:  0000              illegal
0x80265010:  a001              j                       0                       # 0x80265010
0x80265012:  0000              illegal
```

It calculates/fills the initial stack pointer value, and then calls two different functions. If we compare this with the disassembly results of `shell.elf`, we can confirm that it indeed matches:

```
$ llvm-objdump -d shell.elf | head -n20

shell.elf:      file format elf32-littleriscv

Disassembly of section .text:

01000000 <start>:
 1000000: 37 05 01 01   lui     a0, 4112
 1000004: 13 05 05 26   addi    a0, a0, 608
 1000008: 2a 81         mv      sp, a0
 100000a: 19 20         jal     0x1000010 <main>
 100000c: 29 20         jal     0x1000016 <exit>
 100000e: 00 00         unimp

01000010 <main>:
 1000010: 01 a0         j       0x1000010 <main>
 1000012: 00 00         unimp
```

## Transition to user mode

To run applications, we use a CPU mode called *user mode*, or in RISC-V terms, *U-Mode*. It's surprisingly simple to switch to U-Mode. Here's how:

```c [kernel.h]
#define SSTATUS_SPIE (1 << 5)
```

```c [kernel.c]
// ↓ __attribute__((naked)) is very important!
__attribute__((naked)) void user_entry(void) {
    __asm__ __volatile__(
        "csrw sepc, %[sepc]        \n"
        "csrw sstatus, %[sstatus]  \n"
        "sret                      \n"
        :
        : [sepc] "r" (USER_BASE),
          [sstatus] "r" (SSTATUS_SPIE)
    );
}
```

The switch from S-Mode to U-Mode is done with the `sret` instruction. However, before changing the operation mode, it does two writes to CSRs:

- Set the program counter for when transitioning to U-Mode in the `sepc` register. That is, where `sret` jumps to.
- Set the `SPIE` bit in the `sstatus` register. Setting this enables hardware interrupts when entering U-Mode, and the handler set in the `stvec` register will be called.

> [!TIP]
>
> In this book, we don't use hardware interrupts but use polling instead, so it's not necessary to set the `SPIE` bit. However, it's better to be clear rather than silently ignoring interrupts.

## Try user mode

Now let's try it! That said, because `shell.c` just loops infinitely, we can't tell if it's working properly on the screen. Instead, let's take a look with the QEMU monitor:

```
(qemu) info registers

CPU#0
 V      =   0
 pc       01000010
```

It seems CPU is continuously executing `0x1000010`. It appears to be working properly, but somehow it doesn't feel satisfying. So, let's see if we can observe behavior which is specific to U-Mode. Add one line to `shell.c`:

```c [shell.c] {4}
#include "user.h"

void main(void) {
    *((volatile int *) 0x80200000) = 0x1234; // new!
    for (;;);
}
```

This `0x80200000` is a memory area used by the kernel that is mapped on the page table. However, since it is a kernel page where the `U` bit in the page table entry is not set, an exception (page fault) should occur, and the kernel should panic. Let's try it:

```
$ ./run.sh

PANIC: kernel.c:71: unexpected trap scause=0000000f, stval=80200000, sepc=0100001a
```

The 15th exception (`scause = 0xf = 15`), it corresponds to "Store/AMO page fault". It seems the expected exception happened! Also, the program counter in `sepc` points to the line we added to `shell.c`:

```
$ llvm-addr2line -e shell.elf 0x100001a
/Users/seiya/dev/os-from-scratch/shell.c:4
```

Congrats! You've successfully executed your first application! Isn't it surprising how easy it is to implement user mode? The kernel is very similar to an application - it just has a few more privileges.
