---
title: 유저 모드
---

# 유저 모드

이전 장에서 작성한 애플리케이션을 실행해보겠습니다.


## 실행 파일 추출하기

ELF와 같은 실행 파일 형식에서는 로드 주소가 파일 헤더(ELF의 경우 프로그램 헤더)에 저장됩니다. 하지만, 우리의 애플리케이션 실행 이미지는 원시 바이너리(raw binary)이기 때문에, 다음과 같이 고정된 값으로 준비해주어야 합니다:


```c [kernel.h]
// 애플리케이션 이미지의 기본 가상 주소입니다. 이는 `user.ld`에 정의된 시작 주소와 일치해야 합니다.
#define USER_BASE 0x1000000
```

다음으로, `shell.bin.o`에 포함된 원시 바이너리를 사용하기 위해 심볼들을 정의합니다:

```c [kernel.c]
extern char _binary_shell_bin_start[], _binary_shell_bin_size[];
```

또한, 애플리케이션을 시작하기 위해 `create_process` 함수를 다음과 같이 수정합니다:


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

`create_process` 함수를 실행 이미지의 포인터(`image`)와 이미지 크기(`image_size`)를 인자로 받도록 수정했습니다. 이 함수는 지정된 크기만큼 실행 이미지를 페이지 단위로 복사하여 프로세스의 페이지 테이블에 매핑합니다. 또한, 첫 번째 컨텍스트 스위치 시 점프할 주소를 user_entry로 설정합니다. 현재는 이 함수를 빈 함수로 유지합니다.

> [!WARNING]
>
> 실행 이미지를 복사하지 않고 직접 매핑하면, 동일한 애플리케이션의 프로세스들이 동일한 물리 페이지를 공유하게 됩니다. 이는 메모리 격리를 파괴합니다!

마지막으로, `create_process` 함수를 호출하는 부분을 수정하여 사용자 프로세스를 생성하도록 합니다:


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

이제 QEMU 모니터를 통해 실행 이미지가 예상대로 매핑되었는지 확인해봅시다:


```
(qemu) info mem
vaddr    paddr            size     attr
-------- ---------------- -------- -------
01000000 0000000080265000 00001000 rwxu---
01001000 0000000080267000 00010000 rwxu---
```

물리 주소 `0x80265000`이 가상 주소 `0x1000000` (`USER_BASE`)에 매핑되어 있는 것을 확인할 수 있습니다. 이제 이 물리 주소의 내용을 살펴봅시다. 물리 메모리의 내용을 표시하려면 `xp` 명령어를 사용합니다:

```
(qemu) xp /32b 0x80265000
0000000080265000: 0x37 0x05 0x01 0x01 0x13 0x05 0x05 0x26
0000000080265008: 0x2a 0x81 0x19 0x20 0x29 0x20 0x00 0x00
0000000080265010: 0x01 0xa0 0x00 0x00 0x82 0x80 0x01 0xa0
0000000080265018: 0x09 0xca 0xaa 0x86 0x7d 0x16 0x13 0x87
```

일부 데이터가 있는 것 같습니다. `shell.bin`의 내용을 확인하여 실제로 일치하는지 검증해봅시다:


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

16진수로만 보면 이해하기 어렵습니다. 기계어를 역어셈블하여 예상한 명령어들과 일치하는지 확인해봅시다:

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

이 코드는 초기 스택 포인터 값을 계산/설정하고, 두 개의 다른 함수를 호출합니다. 이를 `shell.elf`의 역어셈블 결과와 비교하면 실제로 일치함을 확인할 수 있습니다:


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

## 사용자 모드로 전환하기

애플리케이션을 실행하기 위해, 우리는 **user mode** 또는 RISC-V에서는 **U-Mode** 라 불리는 CPU 모드를 사용합니다. U-Mode로 전환하는 것은 의외로 간단합니다. 방법은 다음과 같습니다:


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

S-Mode에서 U-Mode로의 전환은 `sret` 명령어를 사용하여 이루어집니다. 다만, 모드를 변경하기 전에 두 개의 CSR에 값을 기록합니다:


- `sepc` 레지스터: U-Mode로 전환 시 실행할 프로그램 카운터를 설정합니다. 즉, sret 명령어가 점프할 위치입니다.
- `sstatus` 레지스터의 `SPIE` 비트: 이 비트를 설정하면 U-Mode로 진입할 때 하드웨어 인터럽트가 활성화되며, `stvec` 레지스터에 설정된 핸들러가 호출됩니다.


> [!TIP]
>
> 이 책에서는 하드웨어 인터럽트를 사용하지 않고 폴링을 사용하기 때문에 SPIE 비트를 설정할 필요는 없습니다. 하지만 인터럽트를 묵살하기보다는 명확하게 설정하는 것이 좋습니다.


## 사용자 모드 실행하기

이제 실행해봅시다! 다만, `shell.c`가 단순히 무한 루프를 돌기 때문에 화면상에서 제대로 동작하는지 확인하기 어렵습니다. 대신, QEMU 모니터를 통해 확인해봅시다:

```
(qemu) info registers

CPU#0
 V      =   0
 pc       01000010
```

CPU가 계속해서 `0x1000010` 주소를 실행하고 있는 것 같습니다. 제대로 작동하는 것처럼 보이지만, 뭔가 아쉬운 점이 있습니다. 그래서 U-Mode에서만 나타나는 동작을 관찰해보기로 합시다. shell.c에 한 줄을 추가합니다:

```c [shell.c] {4}
#include "user.h"

void main(void) {
    *((volatile int *) 0x80200000) = 0x1234; // new!
    for (;;);
}
```

이 `0x80200000은` 커널에서 사용하는 메모리 영역으로, 페이지 테이블에 매핑되어 있습니다. 그러나 이 주소는 페이지 테이블 항목에서 `U` 비트가 설정되지 않은 커널 페이지이므로 예외(페이지 폴트)가 발생해야 하며, 커널이 패닉 상태에 빠져야 합니다. 한번 실행해봅시다:


```
$ ./run.sh

PANIC: kernel.c:71: unexpected trap scause=0000000f, stval=80200000, sepc=0100001a
```

15번째 예외(`scause = 0xf = 15`)는 "Store/AMO 페이지 폴트"에 해당합니다. 예상했던 예외가 발생한 것 같습니다! 또한, `sepc`에 저장된 프로그램 카운터는 우리가 shell.c에 추가한 라인을 가리키고 있습니다:

```
$ llvm-addr2line -e shell.elf 0x100001a
/Users/seiya/dev/os-from-scratch/shell.c:4
```

축하합니다! 첫 번째 애플리케이션을 성공적으로 실행했습니다! 사용자 모드를 구현하는 것이 이렇게 간단하다는 것이 놀랍지 않나요? 커널은 애플리케이션과 매우 유사하며, 단지 몇 가지 추가 권한만 가지고 있을 뿐입니다.
