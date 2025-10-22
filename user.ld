ENTRY(start)

SECTIONS {
    . = 0x1000000;

    .text :{
        KEEP(*(.text.start));
        *(.text .text.*);
    }

    .rodata : ALIGN(4) {
        *(.rodata .rodata.*);
    }

    .data : ALIGN(4) {
        *(.data .data.*);
    }

    .bss : ALIGN(4) {
        *(.bss .bss.* .sbss .sbss.*);

        /*
         * RISC-V ABI requires 16-byte alignment for the stack.
         *
         * https://github.com/nuta/operating-system-in-1000-lines/pull/23
         */
        . = ALIGN(16);
        . += 64 * 1024; /* 64KB stack space */
        __stack_top = .;

       ASSERT(. < 0x1800000, "too large executable");
    }
}
