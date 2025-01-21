# Memory Allocation

In this chapter, we'll implement a simple memory allocator.

## Revisiting the linker script

Before implementing a memory allocator, let's define the memory regions to be managed by the allocator:

```ld [kernel.ld] {5-8}
    . = ALIGN(4);
    . += 128 * 1024; /* 128KB */
    __stack_top = .;

    . = ALIGN(4096);
    __free_ram = .;
    . += 64 * 1024 * 1024; /* 64MB */
    __free_ram_end = .;
}
```

This adds two new symbols: `__free_ram` and `__free_ram_end`. This defines a memory area after the stack space. The size of the space (64MB) is an arbitrary value and `. = ALIGN(4096)` ensures that it's aligned to a 4KB boundary.

By defining this in the linker script instead of hardcoding addresses, the linker can determine the position to avoid overlapping with the kernel's static data.

> [!TIP]
>
> Practical operating systems on x86-64 determine available memory regions by obtaining information from hardware at boot time (for example, UEFI's `GetMemoryMap`).

## The world's simplest memory allocation algorithm

Let's implement a function to allocate memory dynamically. Instead of allocating in bytes like `malloc`, it allocates in a larger unit called *"pages"*. 1 page is typically 4KB (4096 bytes).

> [!TIP]
>
> 4KB = 4096 = 0x1000 (hexadecimal). Thus, page-aligned addresses look nicely aligned in hexadecimal.

The following `alloc_pages` function dynamically allocates `n` pages of memory and returns the starting address:

```c [kernel.c]
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

`PAGE_SIZE` represents the size of one page. Define it in `common.h`:

```c [common.h]
#define PAGE_SIZE 4096
```

You will find the following key points:

- `next_paddr` is defined as a `static` variable. This means, unlike local variables, its value is retained between function calls. That is, it behaves like a global variable.
- `next_paddr` points to the start address of the "next area to be allocated" (free area). When allocating, `next_paddr` is advanced by the size being allocated.
- `next_paddr` initially holds the address of `__free_ram`. This means memory is allocated sequentially starting from `__free_ram`.
- `__free_ram` is placed on a 4KB boundary due to `ALIGN(4096)` in the linker script. Therefore, the `alloc_pages` function always returns an address aligned to 4KB.
- If it tries to allocate beyond `__free_ram_end`, in other words, if it runs out of memory, a kernel panic occurs.
- The `memset` function ensures that the allocated memory area is always filled with zeroes. This is to avoid hard-to-debug issues caused by uninitialized memory.

Isn't it simple? However, there is a big problem with this memory allocation algorithm: allocated memory cannot be freed! That said, it's good enough for our simple hobby OS.

> [!TIP]
>
> This algorithm is known as **Bump allocator** or **Linear allocator**, and it's actually used in scenarios where deallocation is not necessary. It's an attractive allocation algorithm that can be implemented in just a few lines and is very fast.
>
> When implementing deallocation, it's common to use a bitmap-based algorithm or use an algorithm called the buddy system.

## Let's try memory allocation

Let's test the memory allocation function we've implemented. Add some code to `kernel_main`:

```c [kernel.c] {4-7}
void kernel_main(void) {
    memset(__bss, 0, (size_t) __bss_end - (size_t) __bss);

    paddr_t paddr0 = alloc_pages(2);
    paddr_t paddr1 = alloc_pages(1);
    printf("alloc_pages test: paddr0=%x\n", paddr0);
    printf("alloc_pages test: paddr1=%x\n", paddr1);

    PANIC("booted!");
}
```

Verify that the first address (`paddr0`) matches the address of `__free_ram`, and that the next address (`paddr1`) matches an address 8KB after `paddr0`:

```
$ ./run.sh
Hello World!
alloc_pages test: paddr0=80221000
alloc_pages test: paddr1=80223000
```

```
$ llvm-nm kernel.elf | grep __free_ram
80221000 R __free_ram
84221000 R __free_ram_end
```
