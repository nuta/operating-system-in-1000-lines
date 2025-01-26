---
title: 예외 (Exception)
---

# 예외 (Exception)

`Exception`은 CPU가 잘못된 메모리 접근(일명 페이지 폴트), 유효하지 않은 명령(Illegal Instructions), 그리고 시스템 콜 같은 다양한 이벤트가 발생했을 때 커널이 개입하도록 해주는 CPU 기능입니다.

비유하자면, C++나 Java의 `try-catch`와 유사한 하드웨어 지원 메커니즘입니다. CPU는 커널이 개입해야 할 상황이 발생하기 전까지는 계속 프로그램을 실행하다가, 문제가 발생하면 예외가 발생해 커널이 개입하게 됩니다. `try-catch`와 다른 점은, 예외가 발생한 지점에서 커널이 개입했다가, 처리 후에 다시 “아무 일 없었던 것처럼” 같은 지점부터 프로그램을 재개할 수 있다는 것입니다. 꽤 멋진 기능이죠? 

`Exception`은 커널 모드에서도 발생할 수 있으며, 대부분 치명적인 커널 버그로 이어집니다. QEMU가 갑자기 리셋되거나 커널이 이상 동작한다면, 예외가 발생했을 가능성이 큽니다. 예외 핸들러를 일찍 구현해두면, 이런 경우 커널 패닉을 발생시켜 좀 더 우아하게 디버깅할 수 있습니다. 웹 개발에서 자바스크립트의 “unhandled rejection” 핸들러를 제일 먼저 달아두는 것과 비슷한 개념입니다.



## 예외가 처리되는 과정

RISC-V에서 예외는 다음과 같은 단계를 거쳐 처리됩니다:

1. CPU는 `medeleg` 레지스터를 확인하여 어떤 모드(운영 모드)에서 예외를 처리할지 결정합니다. 여기서는 OpenSBI가 이미 U-Mode와 S-Mode 예외를 S-Mode 핸들러에서 처리하도록 설정해두었습니다.
2. CPU는 예외가 발생한 시점의 상태(각종 레지스터 값)를 여러 CSR(제어/상태 레지스터)들에 저장합니다(아래 표 참조).
3. `stvec` 레지스터에 저장된 값이 프로그램 카운터로 설정되면서, 커널의 예외 핸들러로 점프합니다.
4. 예외 핸들러는 일반 레지스터(프로그램 상태)를 별도로 저장한 뒤, 예외를 처리합니다.
5. 처리 후, 저장해둔 실행 상태를 복원하고 `sret` 명령어를 실행해 예외가 발생했던 지점으로 돌아가 프로그램을 재개합니다.
6. 2번 단계에서 업데이트되는 주요 CSR은 아래와 같습니다. 커널은 이 정보들을 기반으로 예외를 처리합니다:

| 레지스터     | 내용                                                   |
|----------|------------------------------------------------------|
| `scause` | 예외 유형. 커널은 이를 읽어 어떤 종류의 예외인지 판단합니다.                  |
| `stval`  | 예외에 대한 부가 정보(예: 문제를 일으킨 메모리 주소). 예외 종류에 따라 다르게 사용됩니다. |
| `sepc`   | 예외가 발생했을 때의 프로그램 카운터(PC) 값.                          |
| `sstatus` | 예외가 발생했을 때의 운영 모드(U-Mode/S-Mode 등).                  |

## 예외 핸들러 (Exception Handler) 구현

이제 첫 번째 예외 핸들러를 구현해봅시다! 아래 코드는 `stvec` 레지스터에 등록할 예외 핸들러 진입점(entry point) 예시입니다:

```c [kernel.c]
__attribute__((naked))
__attribute__((aligned(4)))
void kernel_entry(void) {
    __asm__ __volatile__(
        "csrw sscratch, sp\n"
        "addi sp, sp, -4 * 31\n"
        "sw ra,  4 * 0(sp)\n"
        "sw gp,  4 * 1(sp)\n"
        "sw tp,  4 * 2(sp)\n"
        "sw t0,  4 * 3(sp)\n"
        "sw t1,  4 * 4(sp)\n"
        "sw t2,  4 * 5(sp)\n"
        "sw t3,  4 * 6(sp)\n"
        "sw t4,  4 * 7(sp)\n"
        "sw t5,  4 * 8(sp)\n"
        "sw t6,  4 * 9(sp)\n"
        "sw a0,  4 * 10(sp)\n"
        "sw a1,  4 * 11(sp)\n"
        "sw a2,  4 * 12(sp)\n"
        "sw a3,  4 * 13(sp)\n"
        "sw a4,  4 * 14(sp)\n"
        "sw a5,  4 * 15(sp)\n"
        "sw a6,  4 * 16(sp)\n"
        "sw a7,  4 * 17(sp)\n"
        "sw s0,  4 * 18(sp)\n"
        "sw s1,  4 * 19(sp)\n"
        "sw s2,  4 * 20(sp)\n"
        "sw s3,  4 * 21(sp)\n"
        "sw s4,  4 * 22(sp)\n"
        "sw s5,  4 * 23(sp)\n"
        "sw s6,  4 * 24(sp)\n"
        "sw s7,  4 * 25(sp)\n"
        "sw s8,  4 * 26(sp)\n"
        "sw s9,  4 * 27(sp)\n"
        "sw s10, 4 * 28(sp)\n"
        "sw s11, 4 * 29(sp)\n"

        "csrr a0, sscratch\n"
        "sw a0, 4 * 30(sp)\n"

        "mv a0, sp\n"
        "call handle_trap\n"

        "lw ra,  4 * 0(sp)\n"
        "lw gp,  4 * 1(sp)\n"
        "lw tp,  4 * 2(sp)\n"
        "lw t0,  4 * 3(sp)\n"
        "lw t1,  4 * 4(sp)\n"
        "lw t2,  4 * 5(sp)\n"
        "lw t3,  4 * 6(sp)\n"
        "lw t4,  4 * 7(sp)\n"
        "lw t5,  4 * 8(sp)\n"
        "lw t6,  4 * 9(sp)\n"
        "lw a0,  4 * 10(sp)\n"
        "lw a1,  4 * 11(sp)\n"
        "lw a2,  4 * 12(sp)\n"
        "lw a3,  4 * 13(sp)\n"
        "lw a4,  4 * 14(sp)\n"
        "lw a5,  4 * 15(sp)\n"
        "lw a6,  4 * 16(sp)\n"
        "lw a7,  4 * 17(sp)\n"
        "lw s0,  4 * 18(sp)\n"
        "lw s1,  4 * 19(sp)\n"
        "lw s2,  4 * 20(sp)\n"
        "lw s3,  4 * 21(sp)\n"
        "lw s4,  4 * 22(sp)\n"
        "lw s5,  4 * 23(sp)\n"
        "lw s6,  4 * 24(sp)\n"
        "lw s7,  4 * 25(sp)\n"
        "lw s8,  4 * 26(sp)\n"
        "lw s9,  4 * 27(sp)\n"
        "lw s10, 4 * 28(sp)\n"
        "lw s11, 4 * 29(sp)\n"
        "lw sp,  4 * 30(sp)\n"
        "sret\n"
    );
}
```

주요 포인트
- `sscratch` 레지스터를 임시 저장소로 이용해 예외 발생 시점의 스택 포인터를 저장해두고, 나중에 복구합니다.
- 커널에서는 부동소수점(FPU) 레지스터를 사용하지 않으므로(일반적으로 쓰레드 스위칭 시에만 저장/복원), 여기서는 저장하지 않았습니다.
- 스택 포인터(`sp`) 값을 `a0` 레지스터에 넘겨 `handle_trap` 함수를 C 코드로 호출합니다. 이때 sp가 가리키는 곳에는 조금 뒤에 소개할 `trap_frame` 구조체와 동일한 형태로 레지스터들이 저장되어 있습니다.
- `__attribute__((aligned(4)))`는 함수 시작 주소를 4바이트 경계에 맞추기 위함입니다. `stvec` 레지스터는 예외 핸들러 주소뿐 아니라 하위 2비트를 모드 정보 플래그로 사용하기 때문에, 핸들러 주소가 4바이트 정렬이 되어 있어야 합니다.

> [!NOTE]
>
> 예외 핸들러 진입점(entry point)은 커널에서 가장 까다롭고 실수하기 쉬운 부분 중 하나입니다. 코드를 자세히 보면, 원래의 일반 레지스터 값을 전부 스택에 저장하되, `sp`는 `sscratch`를 통해 우회적으로 저장하고 있음을 알 수 있습니다.
>
> 만약 `a0` 레지스터를 잘못 덮어써버리면, “지역 변수 값이 이유 없이 바뀐다” 같은 디버깅하기 어려운 문제들을 일으킬 수 있습니다. 금요일 밤을 야근에 쏟고 싶지 않다면, 프로그램 상태를 잘 저장해두세요!


위 진입점 코드에서는 `handle_trap` 함수를 호출해, 예외 처리를 C 언어로 진행합니다:


```c [kernel.c]
void handle_trap(struct trap_frame *f) {
    uint32_t scause = READ_CSR(scause);
    uint32_t stval = READ_CSR(stval);
    uint32_t user_pc = READ_CSR(sepc);

    PANIC("unexpected trap scause=%x, stval=%x, sepc=%x\n", scause, stval, user_pc);
}
```

`scause`가 어떤 이유로 예외가 발생했는지 알려주고, `stval`은 예외 부가정보(예: 잘못된 메모리 주소 등), `sepc`는 예외가 일어난 시점의 PC를 알려줍니다. 여기서는 디버깅을 위해 커널 패닉을 발생시킵니다.

사용된 매크로들은 `kernel.h`에서 다음과 같이 정의합니다:

```c [kernel.h]
#include "common.h"

struct trap_frame {
    uint32_t ra;
    uint32_t gp;
    uint32_t tp;
    uint32_t t0;
    uint32_t t1;
    uint32_t t2;
    uint32_t t3;
    uint32_t t4;
    uint32_t t5;
    uint32_t t6;
    uint32_t a0;
    uint32_t a1;
    uint32_t a2;
    uint32_t a3;
    uint32_t a4;
    uint32_t a5;
    uint32_t a6;
    uint32_t a7;
    uint32_t s0;
    uint32_t s1;
    uint32_t s2;
    uint32_t s3;
    uint32_t s4;
    uint32_t s5;
    uint32_t s6;
    uint32_t s7;
    uint32_t s8;
    uint32_t s9;
    uint32_t s10;
    uint32_t s11;
    uint32_t sp;
} __attribute__((packed));

#define READ_CSR(reg)                                                          \
    ({                                                                         \
        unsigned long __tmp;                                                   \
        __asm__ __volatile__("csrr %0, " #reg : "=r"(__tmp));                  \
        __tmp;                                                                 \
    })

#define WRITE_CSR(reg, value)                                                  \
    do {                                                                       \
        uint32_t __tmp = (value);                                              \
        __asm__ __volatile__("csrw " #reg ", %0" ::"r"(__tmp));                \
    } while (0)
```

`trap_frame` 구조체는 `kernel_entry`에서 저장한 프로그램 상태를 나타냅니다. `READ_CSR`와 `WRITE_CSR` 매크로는 CSR 레지스터를 읽고 쓰는 편리한 매크로입니다.

마지막으로 CPU에게 예외 핸들러 위치를 알려주려면, `kernel_main` 함수에서 `stvec` 레지스터를 설정하면 됩니다:


```c [kernel.c] {4-5}
void kernel_main(void) {
    memset(__bss, 0, (size_t) __bss_end - (size_t) __bss);

    WRITE_CSR(stvec, (uint32_t) kernel_entry); // new
    __asm__ __volatile__("unimp"); // new
```

`stvec`를 설정한 뒤, `unimp` 명령어(illegal instruction 로 간주됨)를 실행해 일부러 예외를 일으키는 코드입니다.


> [!NOTE]
>
> **`unimp` 는 “의사(pseudo) 명령어”**.
>
> [RISC-V Assembly Programmer's Manual](https://github.com/riscv-non-isa/riscv-asm-manual/blob/main/src/asm-manual.adoc#instruction-aliases), 에 따르면, 어셈블러는 `unimp`를 다음과 같은 명령어로 변환합니다:
>
> ```
> csrrw x0, cycle, x0
> ```
>
> `cycle` 레지스터는 읽기 전용(read-only) 레지스터이므로, 이를 쓰기 시도(`csrrw`)하는 것은 illegal instruction 예외로 이어집니다.



## 실행 해보기

이제 실행해보고, 예외 핸들러가 호출되는지 확인해봅시다:

```
$ ./run.sh
Hello World!
PANIC: kernel.c:47: unexpected trap scause=00000002, stval=ffffff84, sepc=8020015e
```

`scause`가 2면 “Illegal instruction” 예외에 해당합니다. 이는 우리가 `unimp`로 의도했던 동작과 일치합니다!

또한 `sepc`가 어디를 가리키는지 확인해봅시다. `unimp` 명령어가 호출된 라인 번호에 해당한다면, 예외가 정상적으로 동작하고 있는 것입니다:

```
$ llvm-addr2line -e kernel.elf 8020015e
/Users/seiya/os-from-scratch/kernel.c:129
```

해당 주소가 kernel.c에서 unimp를 실행한 줄을 가리키면 성공입니다!
