---
title: Memory Allocation
layout: chapter
lang: en
---

In this chapter, we'll implement a simple memory allocation function in our kernel.

## Revisting the linker script

Let's define the memory regions to be dynamically allocated in the linker script:

```plain:kernel.ld {5-8}
    . = ALIGN(4);
    . += 128 * 1024; /* 128KB */
    __stack_top = .;

    . = ALIGN(4096);
    __free_ram = .;
    . += 64 * 1024 * 1024; /* 64MB */
    __free_ram_end = .;
}
```

We'll designate the area from `__free_ram` to `__free_ram_end` as the memory region available for dynamic allocation. The 64MB is an arbitrary value chosen by the author. Using `. = ALIGN(4096)` ensures that it's aligned to a 4KB boundary.

By defining this in the linker script instead of hardcoding addresses, the linker can determine the position to avoid overlapping with the kernel's static data.

In practical operating systems, besides defining memory sizes for each device like this, it's also common to determine available memory regions by obtaining information from hardware at boot time (for example, UEFI's `GetMemoryMap`).

## The world's simplest memory allocation algorithm, AFAIK ;)

Now that we've defined the dynamic allocation area, let's implement a function to actually allocate memory dynamically. However, instead of allocating by byte like the `malloc` function, we'll allocate in larger units called "pages". One page is typically 4KB (4096 bytes).

> [!TIP]
>
> 4KB = 4096 = 0x1000. It's useful to remember that in hexadecimal, this is represented as `1000`.

The following `alloc_pages` function dynamically allocates `n` pages of memory and returns the starting address.

```c:kernel.c
extern char __free_ram[], __free_ram_end[];

paddr_t alloc_pages(uint32_t n) {
    static paddr_t next_paddr = (paddr_t) __free_ram;
    paddr_t paddr = next_paddr;
    next_paddr += n * PAGE_SIZE;

    if (next_paddr > (paddr_t) __free_ram_end)
        PANIC("out of memory");

    memset((void *) paddr, 0, n * PAGE_SIZE);
    return paddr;
}
```

The newly introduced `PAGE_SIZE` represents the size of one page. We'll define it in `common.h`:

```c:common.h
#define PAGE_SIZE 4096
```

We can observe the following characteristics from this function:

- `next_paddr` is defined as a `static` variable. This means, unlike local variables, its value is retained between function calls (behaving similar to a global variable).
- `next_paddr` points to the start address of the "next area to be allocated" (free area). When allocating, `next_paddr` is advanced by the size being allocated.
- `next_paddr` initially holds the address of `__free_ram`. This means memory is allocated sequentially starting from `__free_ram`.
- `__free_ram` is placed on a 4KB boundary due to `ALIGN(4096)` in the linker script. Therefore, the `alloc_pages` function always returns an address aligned to 4KB.
- If an attempt is made to allocate beyond the `__free_ram_end` address, a kernel panic occurs. While returning `0` (like `malloc` returning `NULL`) is an option, we trigger a panic for easier debugging, as forgotten return value checks can be troublesome to debug.
- The `memset` function ensures that the allocated memory area is always initialized to zero. This is done to avoid hard-to-debug issues caused by uninitialized memory.

The main characteristic of this memory allocation is that individual memory pages cannot be freed. In other words, once allocated, memory is never released. However, since it's unlikely that you'll run your custom OS for extended periods, memory leaks can be tolerated for now.

> [!TIP]
>
> This allocation algorithm is known as a **Bump allocator** or **Linear allocator**, and it's actually used in scenarios where deallocation is not necessary. It's an attractive allocation algorithm that can be implemented in just a few lines and operates at high speed.
>
> When implementing deallocation, it's common to use a bitmap to manage free space or use an algorithm called the buddy system.

## Try memory allocation

Let's test the memory allocation function we've implemented.

```c:kernel.c {4-7}
void kernel_main(void) {
    memset(__bss, 0, (size_t) __bss_end - (size_t) __bss);

    paddr_t paddr0 = alloc_pages(2);
    paddr_t paddr1 = alloc_pages(1);
    printf("alloc_pages test: paddr0=%x\n", paddr0);
    printf("alloc_pages test: paddr1=%x\n", paddr1);

    PANIC("booted!");
}
```

We'll verify that the first address (`paddr0`) matches the address of `__free_ram`, and that the next address (`paddr1`) matches an address that's 2 \* 4KB (or 0x2000 in hexadecimal) ahead of the first address:

```plain
$ ./run.sh
Hello World!
alloc_pages test: paddr0=80221000
alloc_pages test: paddr1=80223000
```

```plain
$ llvm-nm kernel.elf | grep __free_ram
80221000 R __free_ram
84221000 R __free_ram_end
```
