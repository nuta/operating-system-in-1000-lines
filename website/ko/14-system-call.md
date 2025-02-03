---
title: 시스템 콜
---

# 시스템 콜

이 장에서는 어플리케이션이 커널 기능을 호출할 수 있도록 하는 **"시스템 콜"** 을 구현해 보겠습니다. 사용자 영역에서 Hello World를 출력해볼 시간입니다!

## 사용자 라이브러리 (User library)

시스템 콜을 호출하는 방식은 우리가 이전에 본 [SBI 콜](/ko/05-hello-world#say-hello-to-sbi) 구현과 매우 유사합니다:


```c [user.c]
int syscall(int sysno, int arg0, int arg1, int arg2) {
    register int a0 __asm__("a0") = arg0;
    register int a1 __asm__("a1") = arg1;
    register int a2 __asm__("a2") = arg2;
    register int a3 __asm__("a3") = sysno;

    __asm__ __volatile__("ecall"
                         : "=r"(a0)
                         : "r"(a0), "r"(a1), "r"(a2), "r"(a3)
                         : "memory");

    return a0;
}
```

`syscall` 함수는 `a3` 레지스터에 시스템 콜 번호를, `a0`부터 `a2` 레지스터에는 시스템 콜 인자를 저장한 후 ecall 명령어를 실행합니다.
`ecall` 명령어는 커널로 처리를 위임하기 위해 사용되는 특별한 명령어입니다.
`ecall` 명령어가 실행되면 예외 핸들러가 호출되어 제어권이 커널로 넘어갑니다.
커널에서 반환하는 값은 `a0` 레지스터에 설정됩니다.

우리가 처음 구현할 시스템 콜은 문자 출력 함수 `putchar`입니다. 이 시스템 콜은 첫 번째 인자로 문자를 받고, 두 번째 이후의 사용하지 않는 인자들은 0으로 설정됩니다:

```c [common.h]
#define SYS_PUTCHAR 1
```

```c [user.c] {2}
void putchar(char ch) {
    syscall(SYS_PUTCHAR, ch, 0, 0);
}
```

## 커널에서 ecall 명령어 처리

이제 예외 트랩 핸들러를 업데이트하여 `ecall` 명령어를 처리하도록 합시다:

```c [kernel.h]
#define SCAUSE_ECALL 8
```

```c [kernel.c] {5-7,12}
void handle_trap(struct trap_frame *f) {
    uint32_t scause = READ_CSR(scause);
    uint32_t stval = READ_CSR(stval);
    uint32_t user_pc = READ_CSR(sepc);
    if (scause == SCAUSE_ECALL) {
        handle_syscall(f);
        user_pc += 4;
    } else {
        PANIC("unexpected trap scause=%x, stval=%x, sepc=%x\n", scause, stval, user_pc);
    }

    WRITE_CSR(sepc, user_pc);
}
```

`scause`의 값을 확인하면 `ecall` 명령어가 호출되었는지 판단할 수 있습니다. 또한, `handle_syscall` 함수를 호출한 후 `sepc`에 `4`(즉, `ecall` 명령어의 크기)를 더해줍니다. 이는 `sepc가` 예외를 발생시킨 명령어(즉, `ecall`)를 가리키기 때문인데, 만약 변경하지 않으면 커널이 동일한 위치로 돌아가 `ecall` 명령어를 반복 실행하게 됩니다.

## 시스템 콜 핸들러


아래의 시스템 콜 핸들러는 트랩 핸들러에서 호출됩니다. 이 함수는 예외가 발생했을 때 저장된 "레지스터 값들을 담은 구조체"를 인자로 받습니다:

```c [kernel.c]
void handle_syscall(struct trap_frame *f) {
    switch (f->a3) {
        case SYS_PUTCHAR:
            putchar(f->a0);
            break;
        default:
            PANIC("unexpected syscall a3=%x\n", f->a3);
    }
}
```

핸들러는 `a3` 레지스터의 값을 확인하여 시스템 콜의 종류를 판별합니다. 현재는 오직 하나의 시스템 콜, `SYS_PUTCHAR만` 구현되어 있으며, 이는 `a0` 레지스터에 저장된 문자를 출력합니다.

## 시스템 콜 테스트

이제 시스템 콜을 구현했으니 테스트해 보겠습니다! `common.c`에 구현된 `printf` 함수를 기억하시나요? 이 함수는 내부적으로 `putchar` 함수를 호출하여 문자를 출력합니다. 이미 사용자 라이브러리에서 `putchar`를 구현했으므로 그대로 사용할 수 있습니다:

```c [shell.c] {2}
void main(void) {
    printf("Hello World from shell!\n");
}
```

실행 결과는 다음과 같이 화면에 출력됩니다:

```
$ ./run.sh
Hello World from shell!
```

축하합니다! 시스템 콜 구현에 성공했습니다. 하지만 여기서 멈추지 않고 다른 시스템 콜도 구현해보겠습니다!

## 키보드 입력 받기 (getchar 시스템 콜)

이제 셸(shell)을 구현하려면 키보드로부터 문자를 입력받을 수 있어야 합니다.

SBI는 "디버그 콘솔의 입력"을 읽어들이는 인터페이스를 제공합니다. 입력이 없을 경우 `-1`을 반환합니다:


```c [kernel.c]
long getchar(void) {
    struct sbiret ret = sbi_call(0, 0, 0, 0, 0, 0, 0, 2);
    return ret.error;
}
```

`getchar` 시스템 콜은 다음과 같이 구현됩니다:

```c [common.h]
#define SYS_GETCHAR 2
```

```c [user.c]
int getchar(void) {
    return syscall(SYS_GETCHAR, 0, 0, 0);
}
```

```c [user.h]
int getchar(void);
```

```c [kernel.c] {3-13}
void handle_syscall(struct trap_frame *f) {
    switch (f->a3) {
        case SYS_GETCHAR:
            while (1) {
                long ch = getchar();
                if (ch >= 0) {
                    f->a0 = ch;
                    break;
                }

                yield();
            }
            break;
        /* omitted */
    }
}
```

이 시스템 콜의 구현에서는 SBI를 반복 호출하여 문자가 입력될 때까지 대기합니다. 하지만 단순히 반복 호출하게 되면 다른 프로세스들이 실행될 수 없으므로, `yield` 시스템 콜을 호출하여 CPU를 다른 프로세스에 양보합니다.

> [!NOTE]
>
> 엄밀히 말하면, SBI는 키보드의 문자를 읽는 것이 아니라 시리얼 포트의 문자를 읽습니다. 키보드(QEMU의 표준 입력)가 시리얼 포트에 연결되어 있기 때문에 동작하는 것입니다.



## 셸 구현하기

이제 `hello` 명령어를 지원하는 간단한 셸을 작성해보겠습니다. 이 명령어를 입력하면 `Hello world from shell!`이 출력됩니다:

```c [shell.c]
void main(void) {
    while (1) {
prompt:
        printf("> ");
        char cmdline[128];
        for (int i = 0;; i++) {
            char ch = getchar();
            putchar(ch);
            if (i == sizeof(cmdline) - 1) {
                printf("command line too long\n");
                goto prompt;
            } else if (ch == '\r') {
                printf("\n");
                cmdline[i] = '\0';
                break;
            } else {
                cmdline[i] = ch;
            }
        }

        if (strcmp(cmdline, "hello") == 0)
            printf("Hello world from shell!\n");
        else
            printf("unknown command: %s\n", cmdline);
    }
}
```

셸은 줄바꿈 문자가 입력될 때까지 문자를 읽어들이고, 입력된 문자열이 명령어와 일치하는지 확인합니다.

> [!WARNING]
>
> 디버그 콘솔에서는 줄바꿈 문자가 `'\r'`입니다.

예를 들어, `hello` 명령어를 입력해보면:

```
$ ./run.sh

> hello
Hello world from shell!
```

이렇게 나오게 됩니다. 이제 여러분의 OS는 점점 실제 OS처럼 보이기 시작합니다. 정말 빠르게 진도를 나가고 있네요!

## 프로세스 종료 (exit 시스템 콜)

마지막으로, 프로세스를 종료시키는 `exit` 시스템 콜을 구현해 보겠습니다:

```c [common.h]
#define SYS_EXIT    3
```

```c [user.c] {2-3}
__attribute__((noreturn)) void exit(void) {
    syscall(SYS_EXIT, 0, 0, 0);
    for (;;); // Just in case!
}
```

```c [kernel.h]
#define PROC_EXITED   2
```

```c [kernel.c] {3-7}
void handle_syscall(struct trap_frame *f) {
    switch (f->a3) {
        case SYS_EXIT:
            printf("process %d exited\n", current_proc->pid);
            current_proc->state = PROC_EXITED;
            yield();
            PANIC("unreachable");
        /* omitted */
    }
}
```

해당 시스템 콜은 프로세스의 상태를 `PROC_EXITED`로 변경한 후, `yield`를 호출하여 CPU를 다른 프로세스에 양보합니다. 스케줄러는 `PROC_RUNNABLE` 상태의 프로세스만 실행하기 때문에, 이 프로세스는 다시 실행되지 않습니다. 하지만 혹시라도 돌아올 경우를 대비하여 `PANIC` 매크로를 추가해 두었습니다.

> [!TIP]
>
> 단순화를 위해, 여기서는 프로세스를 종료할 때 단순히 상태만 `PROC_EXITED`로 변경합니다. 실제 OS를 구축하려면, 페이지 테이블이나 할당된 메모리 영역과 같이 프로세스가 점유한 자원을 해제해야 합니다.

셸에 `exit` 명령어를 추가해보겠습니다:

```c [shell.c] {3-4}
        if (strcmp(cmdline, "hello") == 0)
            printf("Hello world from shell!\n");
        else if (strcmp(cmdline, "exit") == 0)
            exit();
        else
            printf("unknown command: %s\n", cmdline);
```

이제 완료되었습니다! 실행해봅시다:


```
$ ./run.sh

> exit
process 2 exited
PANIC: kernel.c:333: switched to idle process
```

`exit` 명령어를 실행하면 셸 프로세스가 시스템 콜을 통해 종료되고, 실행 가능한 다른 프로세스가 없으므로 스케줄러가 `idle` 프로세스를 선택하여 `PANIC`이 발생합니다.

