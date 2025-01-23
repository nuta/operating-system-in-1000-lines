---
title: Hello World!
---

# Hello World!

이전 챕터에서는 간단한 커널을 부팅하는 데 성공했습니다. 레지스터 덤프를 확인하여 동작을 검증할 수 있었지만, 여전히 눈으로 직접 확인하기에는 조금 아쉬운 느낌이 있었습니다.

이번 챕터에서는 커널에서 문자열을 직접 출력해 보면서, 좀 더 명확하게 동작을 확인해보겠습니다.

## SBI에게 "hello"라고 말하기

이전 챕터에서 SBI가 "OS를 위한 API"라는 점을 배웠습니다. SBI 함수를 호출하려면 `ecall` 명령어를 사용하면 됩니다:

```c [kernel.c] {1, 5-26, 29-32}
#include "kernel.h"

extern char __bss[], __bss_end[], __stack_top[];

struct sbiret sbi_call(long arg0, long arg1, long arg2, long arg3, long arg4,
                       long arg5, long fid, long eid) {
    register long a0 __asm__("a0") = arg0;
    register long a1 __asm__("a1") = arg1;
    register long a2 __asm__("a2") = arg2;
    register long a3 __asm__("a3") = arg3;
    register long a4 __asm__("a4") = arg4;
    register long a5 __asm__("a5") = arg5;
    register long a6 __asm__("a6") = fid;
    register long a7 __asm__("a7") = eid;

    __asm__ __volatile__("ecall"
                         : "=r"(a0), "=r"(a1)
                         : "r"(a0), "r"(a1), "r"(a2), "r"(a3), "r"(a4), "r"(a5),
                           "r"(a6), "r"(a7)
                         : "memory");
    return (struct sbiret){.error = a0, .value = a1};
}

void putchar(char ch) {
    sbi_call(ch, 0, 0, 0, 0, 0, 0, 1 /* Console Putchar */);
}

void kernel_main(void) {
    const char *s = "\n\nHello World!\n";
    for (int i = 0; s[i] != '\0'; i++) {
        putchar(s[i]);
    }

    for (;;) {
        __asm__ __volatile__("wfi");
    }
}
```

그리고 새로운 kernel.h 파일을 만들어, 반환값 구조체를 정의해 줍니다:


```c [kernel.h]
#pragma once

struct sbiret {
    long error;
    long value;
};
```

여기서 새롭게 추가된 sbi_call 함수는 SBI 스펙에서 정의된 대로 OpenSBI를 호출하기 위해 설계되었습니다. 구체적인 호출 규약은 다음과 같습니다:

> **Chapter 3. Binary Encoding**
>
> All SBI functions share a single binary encoding, which facilitates the mixing of SBI extensions. The SBI specification follows the below calling convention.
>
> - An `ECALL` is used as the control transfer instruction between the supervisor and the SEE.
> - `a7` encodes the SBI extension ID (**EID**),
> - `a6` encodes the SBI function ID (**FID**) for a given extension ID encoded in `a7` for any SBI extension defined in or after SBI v0.2.
> - All registers except `a0` & `a1` must be preserved across an SBI call by the callee.
> - SBI functions must return a pair of values in `a0` and `a1`, with `a0` returning an error code. This is analogous to returning the C structure
>
> ```c
> struct sbiret {
>     long error;
>     long value;
> };
> ```
>
> -- "RISC-V Supervisor Binary Interface Specification" v2.0-rc1

> [!TIP]
>
> *"All registers except `a0` & `a1` must be preserved across an SBI call by the callee"* 라는 말은, OpenSBI 쪽(callee 측)에서 a0와 a1을 제외한 레지스터 값(a2 ~ a7)을 변경해서는 안 된다는 뜻입니다. 즉 커널 입장에서는 SBI 호출 후에도 a2~a7 값이 유지됨을 보장받을 수 있습니다.

각 로컬 변수 선언에 사용된 `register`와 `__asm__("레지스터 이름")`은, 특정 레지스터에 값을 배치하도록 컴파일러에 지시하는 역할을 합니다. 이는 시스템 콜을 호출할 때 자주 쓰는 관용적 방식이며, 예를 들어 [시스템 콜](https://git.musl-libc.org/cgit/musl/tree/arch/riscv64/syscall_arch.h)에서도 유사하게 이용합니다.

인자를 준비한 뒤 인라인 어셈블리에서 `ecall` 명령어를 실행하면, CPU 실행 모드가 커널 모드(S-Mode)에서 OpenSBI 모드(M-Mode)로 전환되어 OpenSBI 처리 루틴이 동작합니다. 처리가 끝나면 다시 S-Mode로 돌아와 ecall 이후의 명령어부터 실행이 계속됩니다.

애플리케이션이 커널에 시스템 콜을 호출할 때도 `ecall`이 사용됩니다. 이 명령어는 상위 권한 레벨로의 함수를 호출하는 것과 비슷한 역할을 합니다.

문자를 출력하기 위해서는 `Console Putchar` 함수를 사용할 수 있습니다:

> 5.2. Extension: Console Putchar (EID #0x01)
>
> ```c
>   long sbi_console_putchar(int ch)
> ```
>
> Write data present in ch to debug console.
>
> Unlike sbi_console_getchar(), this SBI call will block if there remain any pending characters to be transmitted or if the receiving terminal is not yet ready to receive the byte. However, if the console doesn’t exist at all, then the character is thrown away.
>
> This SBI call returns 0 upon success or an implementation specific negative error code.
>
> -- "RISC-V Supervisor Binary Interface Specification" v2.0-rc1

`Console Putchar`는 인자로 받은 문자를 디버그 콘솔에 출력해주는 함수입니다.

### 직접 실행해보기

이제 위 코드를 시도해보면, 커널에서 `Hello World!`라는 메시지를 볼 수 있을 것입니다:

```
$ ./run.sh
...

Hello World!
```

> [!TIP]
>
> **Hello World** 메시지가 화면에 출력되는 과정은 다음과 같습니다:
>
> SBI 호출 시, 문자는 다음과 같이 표시됩니다:
>
> 1. 커널에서 ecall 명령어를 실행합니다. CPU는 OpenSBI가 부팅 시점에 설정해둔 M-Mode 트랩 핸들러(mtvec 레지스터)로 점프합니다.
> 2. 레지스터를 저장한 뒤, [C로 작성된 트랩 핸들러](https://github.com/riscv-software-src/opensbi/blob/0ad866067d7853683d88c10ea9269ae6001bcf6f/lib/sbi/sbi_trap.c#L263)가 호출됩니다.
> 3. eid에 따라, 해당 [SBI 기능을 처리하는 함수](https://github.com/riscv-software-src/opensbi/blob/0ad866067d7853683d88c10ea9269ae6001bcf6f/lib/sbi/sbi_ecall_legacy.c#L63C2-L65)가 실행됩니다. 
> 4. 8250 UART용 [디바이스 드라이버](https://github.com/riscv-software-src/opensbi/blob/0ad866067d7853683d88c10ea9269ae6001bcf6f/lib/utils/serial/uart8250.c#L77)가 문자를 QEMU에 전송합니다. 
> 5. QEMU의 8250 UART 에뮬레이션이 이 문자를 받아서 표준 출력으로 보냅니다. 
> 6. 터미널 에뮬레이터가 문자를 화면에 표시합니다.
>
> 즉 `Console Putchar` 함수를 부르는 것은 어떤 마법이 아니라, 단지 OpenSBI에 구현된 디바이스 드라이버를 호출하는 것일 뿐입니다!



## `printf` 함수

이제 단순히 문자를 출력하는 데 성공했습니다. 다음 단계로는 `printf` 함수를 만들어보겠습니다.

`printf` 함수는 포맷 문자열과, 그 안에 들어갈 값을 인자로 받아 출력합니다. 예를 들어 `printf("1 + 2 = %d", 1 + 2)`와 같이 부르면 `1 + 2 = 3`이 출력됩니다.

표준 C 라이브러리에 포함된 `printf`는 아주 많은 기능을 제공하지만, 여기서는 간단한 기능부터 시작하겠습니다. 구체적으로, `%d(10진수), %x(16진수), %s(문자열)`만 지원하는 작은 버전을 만들어 봅시다.

또한, 이 `printf`는 추후 유저 모드 프로그램에서도 쓸 예정이므로, 커널과 유저랜드에서 공유할 `common.c`라는 파일에 작성하겠습니다.

다음은 printf 함수의 구현 예시입니다:

```c [common.c]
#include "common.h"

void putchar(char ch);

void printf(const char *fmt, ...) {
    va_list vargs;
    va_start(vargs, fmt);

    while (*fmt) {
        if (*fmt == '%') {
            fmt++; // Skip '%'
            switch (*fmt) { // Read the next character
                case '\0': // '%' at the end of the format string
                    putchar('%');
                    goto end;
                case '%': // Print '%'
                    putchar('%');
                    break;
                case 's': { // Print a NULL-terminated string.
                    const char *s = va_arg(vargs, const char *);
                    while (*s) {
                        putchar(*s);
                        s++;
                    }
                    break;
                }
                case 'd': { // Print an integer in decimal.
                    int value = va_arg(vargs, int);
                    if (value < 0) {
                        putchar('-');
                        value = -value;
                    }

                    int divisor = 1;
                    while (value / divisor > 9)
                        divisor *= 10;

                    while (divisor > 0) {
                        putchar('0' + value / divisor);
                        value %= divisor;
                        divisor /= 10;
                    }

                    break;
                }
                case 'x': { // Print an integer in hexadecimal.
                    int value = va_arg(vargs, int);
                    for (int i = 7; i >= 0; i--) {
                        int nibble = (value >> (i * 4)) & 0xf;
                        putchar("0123456789abcdef"[nibble]);
                    }
                }
            }
        } else {
            putchar(*fmt);
        }

        fmt++;
    }

end:
    va_end(vargs);
}
```

생각보다 간단하지 않나요? 포맷 문자열을 한 글자씩 확인하면서 `%`를 만나면 그 다음 글자를 보고 적절한 처리를 합니다. `%` 이외의 문자는 그대로 출력합니다.

`10진수(%d)`의 경우, value가 음수라면 먼저 `-`를 출력하고, `value`를 양수로 만들도록 처리했습니다. 그 뒤 가장 높은 자리수를 구하기 위해 divisor를 계산하고, 각 자리수를 순서대로 출력합니다.

`16진수(%x)`의 경우, 가장 상위 *nibble*(4비트)부터 하위 nibble까지 차례대로 출력합니다. nibble은 0~15 범위의 정수이므로 `0123456789abcdef` 문자열에서 인덱스로 사용하여 해당 문자를 얻습니다.

`va_list` 및 관련 매크로들은 원래 `<stdarg.h>` 헤더에 정의되어 있습니다. 여기서는 표준 라이브러리에 의존하지 않고 컴파일러 빌트인 기능을 직접 활용합니다. 구체적으로 `common.h`에 다음과 같이 선언합니다:

```c [common.h]
#pragma once

#define va_list  __builtin_va_list
#define va_start __builtin_va_start
#define va_end   __builtin_va_end
#define va_arg   __builtin_va_arg

void printf(const char *fmt, ...);
```

`__builtin_`으로 시작하는 식별자들은 컴파일러(예: clang)에서 제공하는 빌트인 기능입니다(참고: [Clang 문서](https://clang.llvm.org/docs/LanguageExtensions.html#variadic-function-builtins)). 내부 처리는 컴파일러가 알아서 해주므로, 우리는 단순히 이러한 매크로를 정의만 해두면 됩니다.

이제 `printf`를 구현했으니, 커널에서 한번 사용해봅시다:

```c [kernel.c] {2,5-6}
#include "kernel.h"
#include "common.h"

void kernel_main(void) {
    printf("\n\nHello %s\n", "World!");
    printf("1 + 2 = %d, %x\n", 1 + 2, 0x1234abcd);

    for (;;) {
        __asm__ __volatile__("wfi");
    }
}
```

그리고 `common.c`도 빌드 대상에 추가해줘야 합니다:

```bash [run.sh] {2}
$CC $CFLAGS -Wl,-Tkernel.ld -Wl,-Map=kernel.map -o kernel.elf \
    kernel.c common.c
```

이제 다시 실행해보면, 아래와 같이 `Hello World!`와` 1 + 2 = 3, 1234abcd`가 출력되는 것을 볼 수 있을 것입니다:

```
$ ./run.sh

Hello World!
1 + 2 = 3, 1234abcd
```

"printf 디버깅"이라는 강력한 동료가 드디어 OS에 합류했습니다!
