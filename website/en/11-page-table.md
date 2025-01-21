# Page Table

## Memory management and virtual addressing

When a program accesses memory, CPU translates the specified address (*virtual* address) into a physical address. The table that maps virtual addresses to physical addresses is called a *page table*. By switching page tables, the same virtual address can point to different physical addresses. This allows isolation of memory spaces (virtual address spaces) and separation of kernel and application memory areas, enhancing system security.

In this chapter, we'll implement the hardware-based memory isolation mechanism.

## Structure of virtual address

In this book, we use one of RISC-V's paging mechanisms called Sv32, which uses a two-level page table. The 32-bit virtual address is divided into a first-level page table index (`VPN[1]`), a second-level index (`VPN[0]`), and a page offset.

Try **[RISC-V Sv-32 Virtual Address Breakdown](https://riscv-sv32-virtual-address.vercel.app/)** to see how virtual addresses are broken down into page table indices and offsets.

Here are some examples:

| Virtual Address | `VPN[1]` (10 bits) | `VPN[0]` (10 bits) | Offset (12 bits) |
| --------------- | ------------------ | ------------------ | ---------------- |
| 0x1000_0000     | 0x040              | 0x000              | 0x000            |
| 0x1000_0000     | 0x040              | 0x000              | 0x000            |
| 0x1000_1000     | 0x040              | 0x001              | 0x000            |
| 0x1000_f000     | 0x040              | 0x00f              | 0x000            |
| 0x2000_f0ab     | 0x080              | 0x00f              | 0x0ab            |
| 0x2000_f012     | 0x080              | 0x00f              | 0x012            |
| 0x2000_f034     | 0x080              | 0x00f              | 0x045            |

> [!TIP]
>
> From the examples above, we can see the following characteristics of the indices:
>
> - Changing the middle bits (`VPN[0]`) doesn't affect the first-level index. This means page table entries for nearby addresses are concentrated in the same first-level page table.
> - Changing the lower bits doesn't affect either `VPN[1]` or `VPN[0]`. This means addresses within the same 4KB page are in the same page table entry.
>
> This structure utilizes [the principle of locality](https://en.wikipedia.org/wiki/Locality_of_reference), allowing for smaller page table sizes and more effective use of the Translation Lookaside Buffer (TLB).

When accessing memory, CPU calculates `VPN[1]` and `VPN[0]` to identify the corresponding page table entry, reads the mapped base physical address, and adds `offset` to get the final physical address.

## Constructing the page table

Let's construct a page table in Sv32. First, we'll define some macros. `SATP_SV32` is a single bit in the `satp` register which indicates "enable paging in Sv32 mode", and `PAGE_*` are flags to be set in page table entries.

```c [kernel.h]
#define SATP_SV32 (1u << 31)
#define PAGE_V    (1 << 0)   // "Valid" bit (entry is enabled)
#define PAGE_R    (1 << 1)   // Readable
#define PAGE_W    (1 << 2)   // Writable
#define PAGE_X    (1 << 3)   // Executable
#define PAGE_U    (1 << 4)   // User (accessible in user mode)
```

## Mapping pages

The following `map_page` function takes the first-level page table (`table1`), the virtual address (`vaddr`), the physical address (`paddr`), and page table entry flags (`flags`):

```c [kernel.c]
void map_page(uint32_t *table1, uint32_t vaddr, paddr_t paddr, uint32_t flags) {
    if (!is_aligned(vaddr, PAGE_SIZE))
        PANIC("unaligned vaddr %x", vaddr);

    if (!is_aligned(paddr, PAGE_SIZE))
        PANIC("unaligned paddr %x", paddr);

    uint32_t vpn1 = (vaddr >> 22) & 0x3ff;
    if ((table1[vpn1] & PAGE_V) == 0) {
        // Create the non-existent 2nd level page table.
        uint32_t pt_paddr = alloc_pages(1);
        table1[vpn1] = ((pt_paddr / PAGE_SIZE) << 10) | PAGE_V;
    }

    // Set the 2nd level page table entry to map the physical page.
    uint32_t vpn0 = (vaddr >> 12) & 0x3ff;
    uint32_t *table0 = (uint32_t *) ((table1[vpn1] >> 10) * PAGE_SIZE);
    table0[vpn0] = ((paddr / PAGE_SIZE) << 10) | flags | PAGE_V;
}
```

This function prepares the second-level page table, and fills the page table entry in the second level.

It divides `paddr` by `PAGE_SIZE` because the entry should contain the physical page number, not the physical address itself. Don't confuse the two!

## Mapping kernel memory area

The page table must be configured not only for applications (user space), but also for the kernel.

In this book, the kernel memory mapping is configured so that the kernel's virtual addresses match the physical addresses (i.e. `vaddr == paddr`). This allows the same code to continue running even after enabling paging.

First, let's modify the kernel's linker script to define the starting address used by the kernel (`__kernel_base`):

```ld [kernel.ld] {5}
ENTRY(boot)

SECTIONS {
    . = 0x80200000;
    __kernel_base = .;
```

> [!WARNING]
>
> Define `__kernel_base` **after** the line `. = 0x80200000`. If the order is reversed, the value of `__kernel_base` will be zero.

Next, add the page table to the process struct. This will be a pointer to the first-level page table.

```c [kernel.h] {5}
struct process {
    int pid;
    int state;
    vaddr_t sp;
    uint32_t *page_table;
    uint8_t stack[8192];
};
```

Lastly, map the kernel pages in the `create_process` function. The kernel pages span from `__kernel_base` to `__free_ram_end`. This approach ensures that the kernel can always access both statically allocated areas (like `.text`), and dynamically allocated areas managed by `alloc_pages`:

```c [kernel.c] {1,6-11,15}
extern char __kernel_base[];

struct process *create_process(uint32_t pc) {
    /* omitted */

    // Map kernel pages.
    uint32_t *page_table = (uint32_t *) alloc_pages(1);
    for (paddr_t paddr = (paddr_t) __kernel_base;
         paddr < (paddr_t) __free_ram_end; paddr += PAGE_SIZE)
        map_page(page_table, paddr, paddr, PAGE_R | PAGE_W | PAGE_X);

    proc->pid = i + 1;
    proc->state = PROC_RUNNABLE;
    proc->sp = (uint32_t) sp;
    proc->page_table = page_table;
    return proc;
}
```

## Switching page tables

Let's switch the process's page table when context switching:

```c [kernel.c] {5-7,10-11}
void yield(void) {
    /* omitted */

    __asm__ __volatile__(
        "sfence.vma\n"
        "csrw satp, %[satp]\n"
        "sfence.vma\n"
        "csrw sscratch, %[sscratch]\n"
        :
        // Don't forget the trailing comma!
        : [satp] "r" (SATP_SV32 | ((uint32_t) next->page_table / PAGE_SIZE)),
          [sscratch] "r" ((uint32_t) &next->stack[sizeof(next->stack)])
    );

    switch_context(&prev->sp, &next->sp);
}
```

We can switch page tables by specifying the first-level page table in `satp`. Note that we divide by `PAGE_SIZE` because it's the physical page number.

`sfence.vma` instructions added before and after setting the page table serve two purposes:

1. To ensure that changes to the page table are properly completed (similar to a memory fence).
2. To clear the cache of page table entries (TLB).

> [!TIP]
>
> When the kernel starts, paging is disabled by default (the `satp` register is not set). Virtual addresses behave as if they match physical addresses.

## Testing paging

let's try it and see how it works!

```
$ ./run.sh

starting process A
Astarting process B
BABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABAB
```

The output is exactly the same as in the previous chapter (context switching). There's no visible change even after enabling paging. To check if we've set up the page tables correctly, let's inspect it with QEMU monitor!

## Examining page table contents

Let's look at how the virtual addresses around `0x80000000` are mapped. If set up correctly, they should be mapped so that `(virtual address) == (physical address)`.

```
QEMU 8.0.2 monitor - type 'help' for more information
(qemu) stop
(qemu) info registers
 ...
 satp     80080253
 ...
```

You can see that `satp` is `0x80080253`. According to the specification (RISC-V Sv32 mode), interpreting this value gives us the first-level page table's starting physical address: `(0x80080253 & 0x3fffff) * 4096 = 0x80253000`.

Next, let's inspect the contents of the first-level page table. We want to know the second-level page table corresponding to the virtual address `0x80000000`. QEMU provides commands to display memory contents (memory dump). `xp` command dumps memory at the specified physical address. Dump the 512th entry because `0x80000000 >> 22 = 512`. Since each entry is 4 bytes, we multiply by 4:

```
(qemu) xp /x 0x80253000+512*4
0000000080253800: 0x20095001
```

The first column shows the physical address, and the subsequent columns show the memory values. We can see that some non-zero values are set. The `/x` option specifies hexadecimal display. Adding a number before `x` (e.g., `/1024x`) specifies the number of entries to display.

> [!TIP]
>
> Using the `x` command instead of `xp` allows you to view the memory dump for a specified **virtual** address. This is useful when examining user space (application) memory, where virtual addresses do not match physical addresses, unlike in our kernel space.

According to the specification, the second-level page table is located at `(0x20095000 >> 10) * 4096 = 0x80254000`. Let's dump the entire second-level table (1024 entries):

```
(qemu) xp /1024x 0x80254000
0000000080254000: 0x00000000 0x00000000 0x00000000 0x00000000
0000000080254010: 0x00000000 0x00000000 0x00000000 0x00000000
0000000080254020: 0x00000000 0x00000000 0x00000000 0x00000000
0000000080254030: 0x00000000 0x00000000 0x00000000 0x00000000
...
00000000802547f0: 0x00000000 0x00000000 0x00000000 0x00000000
0000000080254800: 0x2008004f 0x2008040f 0x2008080f 0x20080c0f
0000000080254810: 0x2008100f 0x2008140f 0x2008180f 0x20081c0f
0000000080254820: 0x2008200f 0x2008240f 0x2008280f 0x20082c0f
0000000080254830: 0x2008300f 0x2008340f 0x2008380f 0x20083c0f
0000000080254840: 0x200840cf 0x2008440f 0x2008484f 0x20084c0f
0000000080254850: 0x200850cf 0x2008540f 0x200858cf 0x20085c0f
0000000080254860: 0x2008600f 0x2008640f 0x2008680f 0x20086c0f
0000000080254870: 0x2008700f 0x2008740f 0x2008780f 0x20087c0f
0000000080254880: 0x200880cf 0x2008840f 0x2008880f 0x20088c0f
...
```

The initial entries are filled with zeros, but values start appearing from the 512th entry (`254800`). This is because `__kernel_base` is `0x80200000`, and `VPN[1]` is `0x200`.

We've manually read memory dumps up, but QEMU actually provides a command that displays the current page table mappings in human-readable format. If you want to do a final check on whether the mapping is correct, you can use the `info mem` command:

```
(qemu) info mem
vaddr    paddr            size     attr
-------- ---------------- -------- -------
80200000 0000000080200000 00001000 rwx--a-
80201000 0000000080201000 0000f000 rwx----
80210000 0000000080210000 00001000 rwx--ad
80211000 0000000080211000 00001000 rwx----
80212000 0000000080212000 00001000 rwx--a-
80213000 0000000080213000 00001000 rwx----
80214000 0000000080214000 00001000 rwx--ad
80215000 0000000080215000 00001000 rwx----
80216000 0000000080216000 00001000 rwx--ad
80217000 0000000080217000 00009000 rwx----
80220000 0000000080220000 00001000 rwx--ad
80221000 0000000080221000 0001f000 rwx----
80240000 0000000080240000 00001000 rwx--ad
80241000 0000000080241000 001bf000 rwx----
80400000 0000000080400000 00400000 rwx----
80800000 0000000080800000 00400000 rwx----
80c00000 0000000080c00000 00400000 rwx----
81000000 0000000081000000 00400000 rwx----
81400000 0000000081400000 00400000 rwx----
81800000 0000000081800000 00400000 rwx----
81c00000 0000000081c00000 00400000 rwx----
82000000 0000000082000000 00400000 rwx----
82400000 0000000082400000 00400000 rwx----
82800000 0000000082800000 00400000 rwx----
82c00000 0000000082c00000 00400000 rwx----
83000000 0000000083000000 00400000 rwx----
83400000 0000000083400000 00400000 rwx----
83800000 0000000083800000 00400000 rwx----
83c00000 0000000083c00000 00400000 rwx----
84000000 0000000084000000 00241000 rwx----
```

The columns represent, in order: virtual address, physical address, size (in hexadecimal bytes), and attributes.

Attributes are represented by a combination of `r` (readable), `w` (writable), `x` (executable), `a` (accessed), and `d` (written), where `a` and `d` indicate that the CPU has "accessed the page" and "written to the page" respectively. They are auxiliary information for the OS to keep track of which pages are actually being used/modified.

> [!TIP]
>
> For beginners, debugging page table can be quite challenging. If things aren't working as expected, refer to the "Appendix: Debugging paging" section.

## Appendix: Debugging paging

Setting up page tables can be tricky, and mistakes can be hard to notice. In this appendix, we'll look at some common paging errors and how to debug them.

### Forgetting to set the paging mode

Let's say we forget to set the mode in the `satp` register:

```c [kernel.c] {6}
    __asm__ __volatile__(
        "sfence.vma\n"
        "csrw satp, %[satp]\n"
        "sfence.vma\n"
        :
        : [satp] "r" (((uint32_t) next->page_table / PAGE_SIZE)) // Missing SATP_SV32!
    );
```

However, when you run the OS, you'll see that it works as usual. This is because paging remains disabled and 
memory addresses are treated as physical addresses as before.

To debug this case, try `info mem` command in the QEMU monitor. You'll see something like this:

```
(qemu) info mem
No translation or protection
```

### Specifying physical address instead of physical page number

Let's say we mistakenly specify the page table using a physical *address* instead of a physical *page number*:

```c [kernel.c] {6}
    __asm__ __volatile__(
        "sfence.vma\n"
        "csrw satp, %[satp]\n"
        "sfence.vma\n"
        :
        : [satp] "r" (SATP_SV32 | ((uint32_t) next->page_table)) // Forgot to shift!
    );
```

In this case, `info mem` will print no mappings:

```
$ ./run.sh

QEMU 8.0.2 monitor - type 'help' for more information
(qemu) stop
(qemu) info mem
vaddr    paddr            size     attr
-------- ---------------- -------- -------
```

To debug this, dump registers to see what the CPU is doing:

```
(qemu) info registers

CPU#0
 V      =   0
 pc       80200188
 ...
 scause   0000000c
 ...
```

According to `llvm-addr2line`, `80200188` is the starting address of the exception handler. The exception reason in `scause` corresponds to "Instruction page fault". 

Let's take a closer look at what's specifically happening by examining the QEMU logs:

```bash [run.sh] {2}
$QEMU -machine virt -bios default -nographic -serial mon:stdio --no-reboot \
    -d unimp,guest_errors,int,cpu_reset -D qemu.log \  # new!
    -kernel kernel.elf
```

```
Invalid read at addr 0x253000800, size 4, region '(null)', reason: rejected
riscv_cpu_do_interrupt: hart:0, async:0, cause:0000000c, epc:0x80200580, tval:0x80200580, desc=exec_page_fault
Invalid read at addr 0x253000800, size 4, region '(null)', reason: rejected
riscv_cpu_do_interrupt: hart:0, async:0, cause:0000000c, epc:0x80200188, tval:0x80200188, desc=exec_page_fault
Invalid read at addr 0x253000800, size 4, region '(null)', reason: rejected
riscv_cpu_do_interrupt: hart:0, async:0, cause:0000000c, epc:0x80200188, tval:0x80200188, desc=exec_page_fault
```

Here are what you can infer from the logs:

- `epc`, which indicates the location of the page fault exception, is `0x80200580`. `llvm-objdump` shows that it points to the instruction immediately after setting the `satp` register. This means that a page fault occurs right after enabling paging.

- All subsequent page faults show the same value. The exceptions occurred at `0x80200188`, points to the starting address of the exception handler. Because this log continues indefinitely, the exceptions (page fault) occurs when trying to execute the exception handler.

- Looking at the `info registers` in QEMU monitor, `satp` is `0x80253000`. Calculating the physical address according to the specification: `(0x80253000 & 0x3fffff) * 4096 = 0x253000000`, which does not fit within a 32-bit address space. This indicates that an abnormal value has been set.

To summarize, you can investigate what's wrong by checking QEMU logs, register dumps, and memory dumps. However, the most important thing is to _"read the specification carefully."_ It's very common to overlook or misinterpret it.
