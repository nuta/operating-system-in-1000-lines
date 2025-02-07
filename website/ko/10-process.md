---
title: 프로세스
---

# 프로세스

프로세스는 한 애플리케이션의 인스턴스입니다. 각 프로세스는 독립적인 실행 컨텍스트와 가상 주소 공간과 같은 리소스를 가지고 있습니다.

> [!NOTE]
>
> 실제 운영체제에서는 실행 컨텍스트를 “스레드(Thread)”라는 별도의 개념으로 분리해서 제공합니다. 이 책에서는 단순화를 위해, 각 프로세스가 하나의 스레드를 가진다고 가정합니다.


## 프로세스 제어 블록 (PCB, Process control block)

다음 `process` 구조체는 프로세스 객체를 정의합니다. 흔히 “프로세스 제어 블록(PCB, Process Control Block)”이라고 불립니다:

```c [kernel.c]
#define PROCS_MAX 8       // 최대 프로세스 개수

#define PROC_UNUSED   0   // 사용되지 않는 프로세스 구조체
#define PROC_RUNNABLE 1   // 실행 가능한(runnable) 프로세스

struct process {
    int pid;             // 프로세스 ID
    int state;           // 프로세스 상태: PROC_UNUSED 또는 PROC_RUNNABLE
    vaddr_t sp;          // 스택 포인터
    uint8_t stack[8192]; // 커널 스택
};
```

여기서 커널 스택(`stack`)은 저장된 CPU 레지스터, 함수 리턴 주소, 로컬 변수를 저장합니다. 프로세스마다 커널 스택을 별도로 준비해두면, CPU 레지스터를 저장/복원하고 스택 포인터를 바꾸는 방식으로 컨텍스트 스위칭을 구현할 수 있습니다.


> [!TIP]
>
> 커널 스택을 프로세스별(또는 스레드별)로 두지 않고, CPU별 하나씩만 두는 방법(“single kernel stack”)도 있습니다. 예를 들어, 마이크로커널인 [seL4](https://trustworthy.systems/publications/theses_public/05/Warton%3Abe.abstract)가 이 방식을 채택합니다.
>
> 이런 “프로그램의 컨텍스트를 어디에 저장할지” 문제는 Go나 Rust 같은 언어의 비동기 런타임(‘stackless async’)에서도 다뤄지는 주제입니다.

## 컨텍스트 스위칭

프로세스 실행 컨텍스트를 바꾸는 것을 컨텍스트 스위칭이라고 합니다. 아래 `switch_context` 함수가 그 예시 구현입니다:

```c [kernel.c]
__attribute__((naked)) void switch_context(uint32_t *prev_sp,
                                           uint32_t *next_sp) {
    __asm__ __volatile__(
        // 현재 프로세스의 스택에 callee-saved 레지스터를 저장
        "addi sp, sp, -13 * 4\n" // 13개(4바이트씩) 레지스터 공간 확보
        "sw ra,  0  * 4(sp)\n"   // callee-saved 레지스터만 저장
        "sw s0,  1  * 4(sp)\n"
        "sw s1,  2  * 4(sp)\n"
        "sw s2,  3  * 4(sp)\n"
        "sw s3,  4  * 4(sp)\n"
        "sw s4,  5  * 4(sp)\n"
        "sw s5,  6  * 4(sp)\n"
        "sw s6,  7  * 4(sp)\n"
        "sw s7,  8  * 4(sp)\n"
        "sw s8,  9  * 4(sp)\n"
        "sw s9,  10 * 4(sp)\n"
        "sw s10, 11 * 4(sp)\n"
        "sw s11, 12 * 4(sp)\n"

        // 스택 포인터 교체
        "sw sp, (a0)\n"         // *prev_sp = sp
        "lw sp, (a1)\n"         // sp를 다음 프로세스의 값으로 변경

        // 다음 프로세스 스택에서 callee-saved 레지스터 복원
        "lw ra,  0  * 4(sp)\n"  
        "lw s0,  1  * 4(sp)\n"
        "lw s1,  2  * 4(sp)\n"
        "lw s2,  3  * 4(sp)\n"
        "lw s3,  4  * 4(sp)\n"
        "lw s4,  5  * 4(sp)\n"
        "lw s5,  6  * 4(sp)\n"
        "lw s6,  7  * 4(sp)\n"
        "lw s7,  8  * 4(sp)\n"
        "lw s8,  9  * 4(sp)\n"
        "lw s9,  10 * 4(sp)\n"
        "lw s10, 11 * 4(sp)\n"
        "lw s11, 12 * 4(sp)\n"
        "addi sp, sp, 13 * 4\n" 
        "ret\n"
    );
}
```

위 코드는 callee-saved 레지스터들을 스택에 저장하고 스택 포인터를 교체한 다음, 새 스택에서 callee-saved 레지스터를 복원합니다. 즉, 실행 컨텍스트를 임시 로컬 변수처럼 스택에 저장하는 셈입니다. 직접 struct process에 레지스터를 저장해도 되지만, 스택 방식을 쓰면 오히려 단순하고 깔끔하게 구현할 수 있습니다.


“callee-saved 레지스터”란, 함수가 호출되었을 때 함수를 호출한 쪽에서 복원해야 하는 레지스터를 말합니다. RISC-V에서 `s0 ~ s11`가 여기에 해당합니다. `a0` 같은 “caller-saved” 레지스터는 이미 호출자가 따로 스택에 저장해둔 뒤 함수를 호출하기 때문에, `switch_context`에서는 건드리지 않습니다.


`__attribute__((naked))`는 컴파일러가 이 함수에서 인라인 어셈블리 외의 다른 코드를 생성하지 않도록 해줍니다. 스택 포인터를 수동으로 수정할 때, 예기치 않은 동작을 막아주므로 안전상 권장됩니다.



> [!TIP]
>
> 어떤 레지스터가 callee-saved이고, 어떤 레지스터가 caller-saved인지는 [Calling Convention](https://riscv.org/wp-content/uploads/2015/01/riscv-calling.pdf)에서 정의됩니다. 컴파일러들은 이 규약에 맞춰 코드 생성을 합니다.


### 프로세스 생성 함수

다음은 프로세스 초기화 함수 `create_process`입니다. 프로세스의 엔트리 포인터(시작 함수 주소)를 매개변수로 받아, 생성된 프로세스 구조체의 포인터를 반환합니다:

```c
struct process procs[PROCS_MAX]; // 모든 프로세스 제어 구조체 배열

struct process *create_process(uint32_t pc) {
    // 미사용(UNUSED) 상태의 프로세스 구조체 찾기
    struct process *proc = NULL;
    int i;
    for (i = 0; i < PROCS_MAX; i++) {
        if (procs[i].state == PROC_UNUSED) {
            proc = &procs[i];
            break;
        }
    }

    if (!proc)
        PANIC("no free process slots");

    // 커널 스택에 callee-saved 레지스터 공간을 미리 준비
    // 첫 컨텍스트 스위치 시, switch_context에서 이 값들을 복원함
    uint32_t *sp = (uint32_t *) &proc->stack[sizeof(proc->stack)];
    *--sp = 0;                      // s11
    *--sp = 0;                      // s10
    *--sp = 0;                      // s9
    *--sp = 0;                      // s8
    *--sp = 0;                      // s7
    *--sp = 0;                      // s6
    *--sp = 0;                      // s5
    *--sp = 0;                      // s4
    *--sp = 0;                      // s3
    *--sp = 0;                      // s2
    *--sp = 0;                      // s1
    *--sp = 0;                      // s0
    *--sp = (uint32_t) pc;          // ra (처음 실행 시 점프할 주소)

    // 구조체 필드 초기화
    proc->pid = i + 1;
    proc->state = PROC_RUNNABLE;
    proc->sp = (uint32_t) sp;
    return proc;
}
```

여기서는 stack 배열의 가장 끝(고주소)부터 레지스터 값들을 역순으로 쌓습니다. 실제 실행이 시작될 때 `switch_context`가 이들을 스택에서 꺼내 복원하게 됩니다.


## 컨텍스트 스위칭 테스트하기

이제 프로세스의 핵심 기능인 “멀티프로세스” 형태의 동시 실행을 체험해 봅시다. 두 개의 프로세스를 만들어 테스트해보겠습니다:

```c [kernel.c] {1-25,32-34}
void delay(void) {
    for (int i = 0; i < 30000000; i++)
        __asm__ __volatile__("nop"); // do nothing
}

struct process *proc_a;
struct process *proc_b;

void proc_a_entry(void) {
    printf("starting process A\n");
    while (1) {
        putchar('A');
        switch_context(&proc_a->sp, &proc_b->sp);
        delay();
    }
}

void proc_b_entry(void) {
    printf("starting process B\n");
    while (1) {
        putchar('B');
        switch_context(&proc_b->sp, &proc_a->sp);
        delay();
    }
}

void kernel_main(void) {
    memset(__bss, 0, (size_t) __bss_end - (size_t) __bss);

    WRITE_CSR(stvec, (uint32_t) kernel_entry);

    proc_a = create_process((uint32_t) proc_a_entry);
    proc_b = create_process((uint32_t) proc_b_entry);
    proc_a_entry();

    PANIC("unreachable here!");
}
```

`proc_a_entry`와 `proc_b_entry`는 각각 프로세스 A, 프로세스 B의 시작 함수를 정의합니다.`putchar`로 문자를 하나 출력한 뒤, `switch_contex`t로 상대 프로세스로 넘어가고, `delay`로 조금 쉰 뒤 다시 반복합니다.

`delay` 함수는 단순 바쁜 대기(“busy wait”)로, 문자가 지나치게 빠르게 출력되어 터미널이 먹통이 되는 것을 방지합니다. `nop(no operation)` 명령어는 실제로 아무 일도 하지 않으며, 컴파일러가 루프를 최적화해 제거하는 것을 막기 위해 삽입했습니다.

이제 실행해보면, 처음 각 프로세스의 시작 메시지가 한 번씩 출력된 뒤, “ABABAB…” 문자열이 무한정 반복됩니다.

```
$ ./run.sh

starting process A
Astarting process B
BABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABAQE
```

## 스케쥴러

위 예제에서는 `switch_context`를 직접 호출하여 “다음에 실행할 프로세스”를 지정했습니다. 그러나 프로세스가 많아지면, 어느 프로세스가 다음에 실행될지 매번 결정하는 것이 복잡해집니다. 이를 해결하기 위해 “스케줄러(scheduler)”라는 커널 코드를 작성해, 실행 순서를 결정하도록 합니다.

다음 `yield` 함수가 스케줄러의 예시 구현입니다:

> [!TIP]
>
> "yield"라는 단어는 CPU를 다른 프로세스에게 자발적으로 양보하는 API의 이름으로 자주 사용됩니다.

```c [kernel.c]
struct process *current_proc; // 현재 실행 중인 프로세스
struct process *idle_proc;    // Idle 프로세스

void yield(void) {
    // 실행 가능한 프로세스를 탐색
    struct process *next = idle_proc;
    for (int i = 0; i < PROCS_MAX; i++) {
        struct process *proc = &procs[(current_proc->pid + i) % PROCS_MAX];
        if (proc->state == PROC_RUNNABLE && proc->pid > 0) {
            next = proc;
            break;
        }
    }

    // 현재 프로세스 말고는 실행 가능한 프로세스가 없으면, 그냥 리턴
    if (next == current_proc)
        return;

    // 컨텍스트 스위칭
    struct process *prev = current_proc;
    current_proc = next;
    switch_context(&prev->sp, &next->sp);
}
```

여기서 `current_proc`는 현재 실행 중인 프로세스를 가리키고, `idle_proc`는 “실행할 프로세스가 없을 때 대신 실행할 프로세스”를 가리키는 전역 변수입니다. 부팅할 때 아이들 프로세스를 만들고, 이 프로세스의 pid를 `-1`로 설정해둡니다:

```c [kernel.c] {8-10,15-16}
void kernel_main(void) {
    memset(__bss, 0, (size_t) __bss_end - (size_t) __bss);

    printf("\n\n");

    WRITE_CSR(stvec, (uint32_t) kernel_entry);

    idle_proc = create_process((uint32_t) NULL);
    idle_proc->pid = -1; // idle
    current_proc = idle_proc;

    proc_a = create_process((uint32_t) proc_a_entry);
    proc_b = create_process((uint32_t) proc_b_entry);

    yield();
    PANIC("switched to idle process");
}
```


여기서 핵심은 `current_proc = idle_proc`로 초기화한다는 것입니다. 그 결과, `yield`를 처음 호출할 때는 idle 프로세스에서 A 프로세스로 넘어가고, 다시 idle 프로세스로 돌아올 때는 마치 이 yield 함수가 리턴되는 것처럼 동작하게 됩니다(즉 idle 프로세스의 문맥이 살아있음).

마지막으로, `proc_a_entry`와 `proc_b_entry`를 다음과 같이 수정해 `switch_context` 대신 `yield` 함수를 호출하도록 합니다:

```c [kernel.c] {5,13}
void proc_a_entry(void) {
    printf("starting process A\n");
    while (1) {
        putchar('A');
        yield();
    }
}

void proc_b_entry(void) {
    printf("starting process B\n");
    while (1) {
        putchar('B');
        yield();
    }
}
```

A와 B가 이전처럼 번갈아가며 출력된다면, 스케줄러가 잘 동작하는 것입니다!

## 예외 처리기 수정

예외 핸들러가 예외 발생 시점의 실행 상태를 스택에 저장하는데, 이제 프로세스마다 별도의 커널 스택을 사용하므로 약간의 수정을 해야 합니다.

먼저, 스위칭 시 `sscratch`에 커널 스택 초기값을 설정 합니다. 아래와 같이 스케줄러(`yield`)에서 프로세스 전환 직전에 `sscratch` 레지스터를 적절히 설정합니다:

```c [kernel.c] {4-8}
void yield(void) {
    /* omitted */

    __asm__ __volatile__(
        "csrw sscratch, %[sscratch]\n"
        :
        : [sscratch] "r" ((uint32_t) &next->stack[sizeof(next->stack)])
    );

    // Context switch
    struct process *prev = current_proc;
    current_proc = next;
    switch_context(&prev->sp, &next->sp);
}
```

스택 포인터가 낮은 주소 방향으로 확장되므로, `&next->stack[sizeof(next->stack)]` 을 커널 스택의 초기값으로 설정합니다.


다음으로 예외 핸들러(`kernel_entry`)에서 수정합니다.

```c [kernel.c] {3-4,38-44}
void kernel_entry(void) {
    __asm__ __volatile__(
        // Retrieve the kernel stack of the running process from sscratch.
        "csrrw sp, sscratch, sp\n"

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

        // Retrieve and save the sp at the time of exception.
        "csrr a0, sscratch\n"
        "sw a0,  4 * 30(sp)\n"

        // Reset the kernel stack.
        "addi a0, sp, 4 * 31\n"
        "csrw sscratch, a0\n"

        "mv a0, sp\n"
        "call handle_trap\n"
```

csrrw sp, sscratch, sp는 사실상 스왑(swap) 연산으로 보면 됩니다:

```
tmp = sp;
sp = sscratch;
sscratch = tmp;
```

결과적으로 sp는 “현재 실행 중인 프로세스의 커널 스택”이 됩니다.

`sscratch`에는 예외 발생 시점(유저 스택이었을 수도 있음)의 `sp` 값이 들어갑니다.

이후 레지스터를 저장하고 나서, 다시 sscratch를 이용해 원래 sp를 스택에 보관하고, sscratch를 새로 계산해(“리셋”) 저장합니다.

중요한 점은, 프로세스마다 독립된 커널 스택이 있다는 것입니다. 컨텍스트 스위칭 시 sscratch 내용을 바꾸어 주면, 예외가 발생했을 때도 해당 프로세스의 커널 스택으로 자연스럽게 돌아가서 “아무 일도 없었던 것처럼” 이어서 실행할 수 있습니다.

> [!TIP]
>
> 여기까지는 “커널” 스택의 컨텍스트 스위칭만 구현했습니다. 실제 애플리케이션(유저) 스택은 따로 메모리를 할당해 사용합니다. 유저 스택은 이후 챕터에서 다룹니다.



## 부록: 왜 스택 포인터를 매번 리셋해야 할까?

위에서 `sscratch`를 통해 커널 스택으로 옮겨타는 로직을 작성하는데, “굳이 왜 이렇게 복잡하게 할까?”라고 의문이 생길 수 있습니다.

이는 “예외가 발생했을 때의 스택 포인터를 신뢰할 수 없기” 때문입니다. 예외 핸들러는 다음 세 가지 상황을 고려해야 합니다:

1. 커널 모드에서 예외가 발생한 경우
2. 커널 모드에서 예외를 처리하는 도중(중첩 예외) 다시 예외가 발생한 경우
3. 유저 모드에서 예외가 발생한 경우

(1)에서는 스택 포인터를 그대로 써도 큰 문제가 없고, (2)에서는 중첩 예외 시에 저장 영역이 겹칠 수 있지만, 보통 중첩 예외가 발생하면 커널 패닉으로 처리하므로 문제는 없습니다.

문제는 (3)입니다. 유저 모드에서 예외가 발생하면, `sp`는 유저(애플리케이션)의 스택을 가리킬 수 있습니다. 이 스택 포인터를 그대로 커널이 써버리면, 매핑되지 않은 주소에 접근하거나 악의적으로 조작된 주소에 접근해 커널이 오동작(또는 보안 취약점)이 생길 수 있습니다.

실제로 이 책의 17장까지 구현을 마친 뒤, 예시로 아래와 같은 애플리케이션을 실행해보면:


```c
// 사용자 애플리케이션 예시
#include "user.h"

void main(void) {
    __asm__ __volatile__(
        "li sp, 0xdeadbeef\n"  // 잘못된 주소를 sp에 설정
        "unimp"                // illegal instruction 예외 발생
    );
}
```

예외 처리 시 `sscratch`를 통해 커널 스택으로 변경하지 않으면, 커널이 영문도 없이 멈춥니다. QEMU 로그를 보면:


```
epc:0x0100004e, tval:0x00000000, desc=illegal_instruction <- unimp triggers the trap handler
epc:0x802009dc, tval:0xdeadbe73, desc=store_page_fault <- an aborted write to the stack  (0xdeadbeef)
epc:0x802009dc, tval:0xdeadbdf7, desc=store_page_fault <- an aborted write to the stack  (0xdeadbeef) (2)
epc:0x802009dc, tval:0xdeadbd7b, desc=store_page_fault <- an aborted write to the stack  (0xdeadbeef) (3)
epc:0x802009dc, tval:0xdeadbcff, desc=store_page_fault <- an aborted write to the stack  (0xdeadbeef) (4)
...
```

이처럼 잘못된 주소 `0xdeadbeef`에 스택을 쓰려 하다가 예외가 연달아 발생해 무한 루프에 빠집니다. 그래서 신뢰할 수 있는 커널 스택을 사용해야 합니다.

다른 해결책으로, 예외 핸들러를 아예 분리하는 방안도 있습니다. 예: RISC-V용 xv6(교육용 유닉스 커널)은 (1), (2)를 처리하는 kernelvec와 (3)을 처리하는 uservec를 따로 둡니다. 커널 모드 예외는 현재 스택을 계속 쓰고, 유저 모드 예외는 별도의 커널 스택으로 전환합니다.

> [!TIP]
>
> Google이 개발한 Fuchsia OS에서도, 유저가 임의의 PC를 설정 가능하게 하는 API가 보안 [취약점](https://blog.quarkslab.com/playing-around-with-the-fuchsia-operating-system.html) 이 된 적이 있습니다. 커널을 짜면서 “유저(애플리케이션)가 제공하는 값은 절대 믿지 말라”는 습관이 매우 중요합니다.

## Next Steps

이제 여러 프로세스를 번갈아 실행할 수 있어, 멀티태스킹 OS의 기본 골격을 갖췄습니다.

다만 현재 상태에서는 프로세스가 커널 메모리를 마음대로 읽고 쓸 수 있습니다. 보안상 문제가 크겠죠! 다음 장에서는 애플리케이션을 안전하게 실행하기 위해, 커널과 애플리케이션을 어떻게 격리(isolation)할지를 살펴볼 것입니다.
