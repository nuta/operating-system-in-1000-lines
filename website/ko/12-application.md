---
title: 애플리케이션
---

# 애플리케이션

이 장에서는 우리가 만든 커널 위에서 동작할 첫 번째 애플리케이션 실행 파일을 준비해보겠습니다.

## 메모리 레이아웃

이전 장에서 페이징 메커니즘을 이용해 격리된 가상 주소 공간을 구현했습니다. 이제 애플리케이션을 주소 공간의 어느 위치에 배치할지 고민해 봅시다.

다음과 같이 애플리케이션용 링커 스크립트(`user.ld`)를 새로 만들어주세요:


```ld [user.ld]
ENTRY(start)

SECTIONS {
    . = 0x1000000;

    /* machine code */
    .text :{
        KEEP(*(.text.start));
        *(.text .text.*);
    }

    /* read-only data */
    .rodata : ALIGN(4) {
        *(.rodata .rodata.*);
    }

    /* data with initial values */
    .data : ALIGN(4) {
        *(.data .data.*);
    }

    /* data that should be zero-filled at startup */
    .bss : ALIGN(4) {
        *(.bss .bss.* .sbss .sbss.*);

        . = ALIGN(16);
        . += 64 * 1024; /* 64KB */
        __stack_top = .;

       ASSERT(. < 0x1800000, "too large executable");
    }
}
```

커널의 링커 스크립트와 상당히 비슷해 보이지만, 애플리케이션이 커널 주소 공간과 겹치지 않도록 베이스 주소(여기서는 `0x1000000`)가 다릅니다.

`ASSERT`는 첫 번째 인자로 주어진 조건이 충족되지 않을 경우 링커를 중단시키는 역할을 합니다. 여기서는 `.bss` 섹션 끝(즉, 애플리케이션 메모리 끝)이 `0x1800000`을 초과하지 않도록 제한하고 있습니다. 이를 통해 실행 파일이 너무 크게 만들어지는 실수를 방지합니다.


## 유저랜드 라이브러리

다음으로, 사용자 프로그램(유저랜드)에서 사용할 라이브러리를 만들어 봅시다. 여기서는 간단하게 애플리케이션 시작에 필요한 최소 기능만 구현합니다.

```c [user.c]
#include "user.h"

extern char __stack_top[];

__attribute__((noreturn)) void exit(void) {
    for (;;);
}putchar(char ch)

void putchar(char c) {
    /* TODO */
}

__attribute__((section(".text.start")))
__attribute__((naked))
void start(void) {
    __asm__ __volatile__(
        "mv sp, %[stack_top] \n"
        "call main           \n"
        "call exit           \n"
        :: [stack_top] "r" (__stack_top)
    );
}
```

애플리케이션의 실행은 `start` 함수부터 시작합니다. 커널 부트 과정과 비슷하게, 스택 포인터를 설정한 뒤 애플리케이션의 `main` 함수를 호출합니다.

`exit` 함수는 애플리케이션을 종료시킬 때 사용됩니다. 여기서는 단순히 무한 루프만 돌도록 구현했습니다.

또한, `putchar` 함수는 `common.c`의 `printf` 함수가 참조하고 있으므로, 여기에 정의만 해 두고 실제 구현은 나중에 진행합니다.

커널 초기화 과정과 달리 `.bss` 섹션을 0으로 초기화하는 코드를 넣지 않았습니다. 이는 이미 커널에서 `alloc_pages` 함수를 통해 .bss가 0으로 채워지도록 보장했기 때문입니다.


> [!TIP]
>
> 대부분의 운영체제에서도, 사용자 프로그램에 할당된 메모리는 이미 0으로 초기화된 상태입니다. 그렇지 않으면, 다른 프로세스에서 사용하던 민감 정보(예: 인증 정보)가 남아있을 수 있고, 이는 심각한 보안 문제가 될 수 있습니다.

마지막으로, 유저랜드 라이브러리용 헤더 파일(user.h)을 준비합니다:

```c [user.h]
#pragma once
#include "common.h"

__attribute__((noreturn)) void exit(void);
void putchar(char ch);
```

## 첫 번째 애플리케이션

이제 첫 번째 애플리케이션을 만들어 봅시다! 아직 문자를 화면에 표시하는 방법이 없으므로, "Hello, World!" 대신 단순 무한 루프를 도는 코드를 작성해 보겠습니다.

```c [shell.c]
#include "user.h"

void main(void) {
    for (;;);
}
```

## 애플리케이션 빌드하기

애플리케이션은 커널과 별도로 빌드합니다. 새로운 스크립트(`run.sh`)를 만들어 다음과 같이 작성해 봅시다:

```bash [run.sh] {1,3-6,10}
OBJCOPY=/opt/homebrew/opt/llvm/bin/llvm-objcopy

# Build the shell (application)
$CC $CFLAGS -Wl,-Tuser.ld -Wl,-Map=shell.map -o shell.elf shell.c user.c common.c
$OBJCOPY --set-section-flags .bss=alloc,contents -O binary shell.elf shell.bin
$OBJCOPY -Ibinary -Oelf32-littleriscv shell.bin shell.bin.o

# Build the kernel
$CC $CFLAGS -Wl,-Tkernel.ld -Wl,-Map=kernel.map -o kernel.elf \
    kernel.c common.c shell.bin.o
```

처음 `$CC` 명령은 커널 빌드와 비슷합니다. C 파일들을 컴파일하고, `user.ld` 링커 스크립트를 사용해 링킹합니다.

첫 번째 `$OBJCOPY` 명령은 `ELF` 형식의 실행 파일(`shell.elf`)을 실제 메모리 내용만 포함하는 바이너리(`shell.bin`)로 변환합니다. 우리는 단순히 이 바이너리 파일을 메모리에 로드해 애플리케이션을 실행할 예정입니다. 일반적인 OS에서는 ELF 같은 형식을 사용해, 메모리 매핑 정보와 실제 메모리 내용을 분리해서 다루지만, 여기서는 단순화를 위해 바이너리만 사용합니다.

두 번째 `$OBJCOPY` 명령은 이 바이너리(`shell.bin`)를 C 언어에 임베드할 수 있는 오브젝트(`shell.bin.o`)로 변환합니다. 이 파일 안에 어떤 심볼이 들어있는지 `llvm-nm` 명령으로 확인해 봅시다:

```
$ llvm-nm shell.bin.o
00000000 D _binary_shell_bin_start
00010260 D _binary_shell_bin_end
00010260 A _binary_shell_bin_size
```

`_binary_`라는 접두사 뒤에 파일 이름이 오고, 그 다음에 `start`, `end`, `size`가 붙습니다. 이 심볼들은 각각 바이너리 내용의 시작, 끝, 크기를 의미합니다. 보통은 다음과 같이 사용할 수 있습니다:

```c
extern char _binary_shell_bin_start[];
extern char _binary_shell_bin_size[];

void main(void) {
    uint8_t *shell_bin = (uint8_t *) _binary_shell_bin_start;
    printf("shell_bin size = %d\n", (int) _binary_shell_bin_size);
    printf("shell_bin[0] = %x (%d bytes)\n", shell_bin[0]);
}
```

이 프로그램은 `shell.bin`의 파일 크기와 파일 내용의 첫 바이트를 출력합니다. 다시 말해, `_binary_shell_bin_start` 변수를 파일 내용이 들어 있는 배열처럼 간주할 수 있습니다.

예를 들면, 다음과 같이 생각할 수 있습니다:


```c
char _binary_shell_bin_start[] = "<shell.bin contents here>";
```

그리고 `_binary_shell_bin_size`에는 파일 크기가 들어 있습니다. 다만, 조금 독특한 방식으로 처리됩니다. 다시 `llvm-nm`을 확인해 봅시다.

```
$ llvm-nm shell.bin.o | grep _binary_shell_bin_size
00010454 A _binary_shell_bin_size

$ ls -al shell.bin   ← note: do not confuse with shell.bin.o!
-rwxr-xr-x 1 seiya staff 66644 Oct 24 13:35 shell.bin

$ python3 -c 'print(0x10454)'
66644
```

`llvm-nm` 출력의 첫 번째 열은 심볼의 주소를 나타냅니다. 여기서 `10454`(16진수)는 실제 파일 크기와 일치합니다. A(두 번째 열)는 이 심볼이 링커에 의해 주소가 재배치되지 않는 '절대(Absolute)' 심볼이라는 뜻입니다. 즉, 파일 크기를 '주소' 형태로 박아놓은 것입니다.

`char _binary_shell_bin_size[]` 같은 식으로 정의하면, 일반 포인터처럼 보일 수 있지만 실제로는 그 값이 '파일 크기'를 담은 주소로 간주되어, 캐스팅하면 파일 크기를 얻게 됩니다.

마지막으로, 커널 컴파일 시 `shell.bin.o`를 함께 링크하면, 첫 번째 애플리케이션의 실행 파일이 커널 이미지 내부에 임베드됩니다.


## 실행 파일 디스어셈블

디스어셈블을 해보면 `.text.start` 섹션이 실행 파일의 맨 앞에 배치되어 있고, `start` 함수가 실제 주소 `0x1000000`에 있는 것을 볼 수 있습니다:

```
$ llvm-objdump -d shell.elf

shell.elf:	file format elf32-littleriscv

Disassembly of section .text:

01000000 <start>:
 1000000: 37 05 01 01  	lui	a0, 4112
 1000004: 13 05 05 26  	addi	a0, a0, 608
 1000008: 2a 81        	mv	sp, a0
 100000a: 19 20        	jal	0x1000010 <main>
 100000c: 29 20        	jal	0x1000016 <exit>
 100000e: 00 00        	unimp

01000010 <main>:
 1000010: 01 a0        	j	0x1000010 <main>
 1000012: 00 00        	unimp

01000016 <exit>:
 1000016: 01 a0        	j	0x1000016 <exit>
```
