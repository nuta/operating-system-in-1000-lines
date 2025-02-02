---
title: 부트(Boot)
---

# 커널 부팅 과정

컴퓨터 전원을 켜면 CPU가 초기화되고, 이후 운영체제(OS)가 시작됩니다. OS는 하드웨어를 초기화하고 응용 프로그램을 실행합니다. 이를 "부팅(booting)"이라고 부릅니다.

그렇다면 OS가 시작되기 전에는 무슨 일이 일어날까요? PC 환경에서는 BIOS(또는 최근 PC의 경우 UEFI)가 하드웨어를 초기화하고, 스플래시 화면을 띄운 뒤 디스크에서 OS를 불러옵니다. QEMU의 virt 머신 환경에서는 OpenSBI가 이 BIOS/UEFI 역할을 대신합니다.

## 슈퍼바이저 바이너리 인터페이스 (SBI, Supervisor Binary Interface)

슈퍼바이저 바이너리 인터페이스(SBI)는 OS 커널을 위한 API이며, 동시에 펌웨어(OpenSBI)가 OS에 제공하는 기능을 정의한 것입니다.

SBI 명세는 [GitHub](https://github.com/riscv-non-isa/riscv-sbi-doc/releases). 에 공개되어 있습니다. 디버그 콘솔(예: 시리얼 포트)에 문자열을 출력하거나(putchar 등), 재부팅/종료 요청 및 타이머 설정 등을 할 수 있게 정의합니다.

가장 널리 사용되는 SBI 구현체 중 하나는 [OpenSBI](https://github.com/riscv-software-src/opensbi)입니다. QEMU에서 virt 머신을 실행하면 기본적으로 OpenSBI가 먼저 구동되어 하드웨어 특화 초기화를 수행한 뒤, 커널을 실행합니다.


## OpenSBI 부팅해보기

먼저 OpenSBI가 어떤 식으로 시작되는지 확인해보겠습니다. 아래와 같이 `run.sh` 스크립트를 만들어 주세요.


```
$ touch run.sh
$ chmod +x run.sh
```

```bash [run.sh]
#!/bin/bash
set -xue

# QEMU 실행 파일 경로
QEMU=qemu-system-riscv32

# QEMU 실행
$QEMU -machine virt -bios default -nographic -serial mon:stdio --no-reboot
```

위 스크립트에서 사용한 QEMU 옵션은 다음과 같습니다:

- `-machine virt`: `virt` 머신을 시작합니다. `-machine '?'` 명령어로 다른 머신 종류를 확인할 수 있습니다.
- `-bios default`: QEMU가 제공하는 기본 펌웨어(OpenSBI)를 사용합니다.
- `-nographic`: GUI 없이 QEMU를 실행합니다.
- `-serial mon:stdio`: QEMU의 표준 입출력을 가상 머신의 시리얼 포트에 연결합니다. `mon:` 접두사를 붙여 <kbd>Ctrl</kbd>+<kbd>A</kbd> 이후 <kbd>C</kbd>를 눌러 QEMU 모니터로 전환할 수 있습니다.
- `--no-reboot`: 가상 머신이 크래시되면 재부팅하지 않고 종료합니다(디버깅 시에 편리합니다).

> [!TIP]
>
> macOS에서 Homebrew 버전의 QEMU 파일 경로는 다음 명령어로 확인할 수 있습니다:
>
> ```
> $ ls $(brew --prefix)/bin/qemu-system-riscv32
> /opt/homebrew/bin/qemu-system-riscv32
> ```

스크립트를 실행하면 다음과 같은 배너가 표시됩니다:

```
$ ./run.sh

OpenSBI v1.2
   ____                    _____ ____ _____
  / __ \                  / ____|  _ \_   _|
 | |  | |_ __   ___ _ __ | (___ | |_) || |
 | |  | | '_ \ / _ \ '_ \ \___ \|  _ < | |
 | |__| | |_) |  __/ | | |____) | |_) || |_
  \____/| .__/ \___|_| |_|_____/|____/_____|
        | |
        |_|

Platform Name             : riscv-virtio,qemu
Platform Features         : medeleg
Platform HART Count       : 1
Platform IPI Device       : aclint-mswi
Platform Timer Device     : aclint-mtimer @ 10000000Hz
...
```

OpenSBI가 버전 정보, 플랫폼 이름, 제공 기능, HART(코어) 수 등을 출력합니다.

이 시점에서 아무 키를 눌러도 반응이 없는 것은 자연스러운 현상입니다. 현재 표준 입출력이 QEMU의 시리얼 포트에 연결되어 있고, OpenSBI에는 입력을 처리하는 루틴이 없기 때문에 입력에 대한 동작이 없게 됩니다.

<kbd>Ctrl</kbd>+<kbd>A</kbd>를 누른 후 <kbd>C</kbd>를 눌러 QEMU 디버그 콘솔(QEMU 모니터)로 전환합니다. 모니터에서 `q` 명령으로 QEMU를 종료할 수 있습니다.

```
QEMU 8.0.2 monitor - type 'help' for more information
(qemu) q
```

> [!TIP]
>
> <kbd>Ctrl</kbd>+<kbd>A</kbd>에는 <kbd>C</kbd> 키를 누르면 QEMU 모니터로 전환하는 기능 외에도 여러 기능이 있습니다. 예를 들어 <kbd>X</kbd> 키를 누르면 QEMU를 즉시 종료합니다.
>
> ```
> C-a h    도움말 표시
> C-a x    에뮬레이터 종료
> C-a s    디스크 데이터를 파일에 저장(-snapshot 사용 시)
> C-a t    콘솔 타임스탬프 토글
> C-a b    break(매직 sysrq)
> C-a c    콘솔과 모니터 간 전환
> C-a C-a  C-a를 전송
> ```

## 링커 스크립트(Linker Script)

링커 스크립트는 실행 파일의 메모리 배치를 정의하는 파일입니다. 링커는 이 정보를 기반으로 함수와 변수가 배치될 메모리 주소를 결정합니다.

아래와 같이 `kernel.ld` 파일을 만들어 주세요:

```ld [kernel.ld]
ENTRY(boot)

SECTIONS {
    . = 0x80200000;

    .text :{
        KEEP(*(.text.boot));
        *(.text .text.*);
    }

    .rodata : ALIGN(4) {
        *(.rodata .rodata.*);
    }

    .data : ALIGN(4) {
        *(.data .data.*);
    }

    .bss : ALIGN(4) {
        __bss = .;
        *(.bss .bss.* .sbss .sbss.*);
        __bss_end = .;
    }

    . = ALIGN(4);
    . += 128 * 1024; /* 128KB */
    __stack_top = .;
}
```
주요 포인트는 다음과 같습니다:

- boot 함수를 엔트리 포인트로 지정합니다.
- 베이스 주소(base address)는 0x80200000으로 설정합니다.
- `.text.boot` 섹션을 가장 앞에 둡니다.
- 각 섹션을 `.text`, `.rodata`, `.data`, `.bss`. 순서대로 배치합니다.
- `.bss` 이후에 커널 스택을 배치하고, 크기는 128KB로 설정합니다.

`.text`, `.rodata`, `.data`, `.bss` 는 각각 다음과 같은 용도를 갖는 섹션입니다:

| 섹션       | 설명                                                                |
|----------|-------------------------------------------------------------------|
| `.text`  | 프로그램의 코드(함수 등)가 저장되는 영역입니다.|
| `.rodata` | 읽기 전용 상수 데이터가 저장되는 영역입니다. |
| `.data`  | 읽기/쓰기가 가능한 데이터가 저장되는 영역입니다.                           |
| `.bss`   | 초기값이 0인 읽기/쓰기가 가능한 데이터가 저장되는 영역입니다.|

링커 스크립트 구문을 좀 더 살펴보겠습니다.

`ENTRY(boot)`는 `boot` 함수를 프로그램의 진입점으로 지정합니다.
`*(.text .text.*)`와 같이 쓰면, `.text` 섹션과 `.text.`로 시작하는 모든 섹션을 해당 위치에 배치합니다.
`.`(점)은 현재 주소를 의미하고, 섹션이 배치되면서 자동으로 증가합니다.
`. += 128 * 1024`는 현재 주소를 128KB 늘린다는 의미입니다.
`ALIGN(4)`는 4바이트 경계로 주소를 맞춥니다.

`__bss = .`는 현재 주소를 `__bss`라는 심볼에 저장한다는 의미입니다. C 코드에서 `extern char __bss[];`처럼 선언해 두면, 이를 통해 `.bss`구간의 주소를 참조할 수 있습니다.

> [!TIP]
>
> 링커 스크립트는 커널 개발에 특히 유용한 기능을 많이 제공합니다. GitHub에서 실제 예제를 찾아보세요!


## 최소화된 커널

이번에는 실제 커널을 작성해보겠습니다. 가장 간단한 버전부터 시작하겠습니다. `kernel.c` 파일을 만들어 주세요:


```c [kernel.c]
typedef unsigned char uint8_t;
typedef unsigned int uint32_t;
typedef uint32_t size_t;

extern char __bss[], __bss_end[], __stack_top[];

void *memset(void *buf, char c, size_t n) {
    uint8_t *p = (uint8_t *) buf;
    while (n--)
        *p++ = c;
    return buf;
}

void kernel_main(void) {
    memset(__bss, 0, (size_t) __bss_end - (size_t) __bss);

    for (;;);
}

__attribute__((section(".text.boot")))
__attribute__((naked))
void boot(void) {
    __asm__ __volatile__(
        "mv sp, %[stack_top]\n" // Set the stack pointer
        "j kernel_main\n"       // Jump to the kernel main function
        :
        : [stack_top] "r" (__stack_top) // Pass the stack top address as %[stack_top]
    );
}
```

핵심 내용은 다음과 같습니다:

### 커널 진입점 (Kernel entry point)

실행은 `boot` 함수에서 시작됩니다. 이는 링커 스크립트에서 ENTRY(boot)로 지정했기 때문입니다. 이 함수에서는 링커 스크립트가 지정한 스택 영역의 끝 주소를 스택 포인터(`sp`)에 대입하고, `kernel_main`으로 점프합니다. RISC-V 아키텍처에서는 스택이 내려가는 방향으로 성장하므로, 스택의 최상위(끝) 주소를 설정해야 합니다.


### `boot` 함수 속성

`boot` 함수에는 두 가지 속성이 지정되어 있습니다.

- `__attribute__((naked))`: 함수 시작과 끝에서 컴파일러가 추가로 생성하는 코드(프롤로그, 에필로그 등)를 생략합니다.
- `__attribute__((section(".text.boot")))`: .text.boot 섹션에 이 함수를 배치하도록 합니다.

OpenSBI는 기본적으로 `0x80200000` 주소로 점프만 하기 때문에, 우리가 `boot` 함수를 해당 주소에 확실히 배치해야 합니다.

### 링커 스크립트 심볼 (`extern char`)

소스의 맨 윗부분에서 링커 스크립트에 정의한 심볼`(__bss, __bss_end, __stack_top)`을 `extern char __bss[], ...;`형태로 선언했습니다. 실제로는 "해당 심볼이 가리키는 주소"가 필요하기 때문에 `[]`를 사용하여 주소 형식으로 사용하는 것이 좋습니다.

### `.bss` 섹션 초기화

`kernel_main` 함수에서는 `.bss`를 0으로 초기화합니다. 일부 부트로더가 `.bss`를 클리어해주기도 하지만, 여러 환경에서 확실히 동작하게 하려면 이렇게 수동으로 초기화하는 것이 안전합니다. 이후 무한 루프에 진입하여 커널이 종료되지 않도록 합니다.

## 실행해보기

`run.sh` 스크립트에 커널 빌드 명령과 `-kernel kernel.elf` 옵션을 추가해 보겠습니다:

```bash [run.sh] {6-12,16}
#!/bin/bash
set -xue

QEMU=qemu-system-riscv32

# clang 경로와 컴파일 옵션
CC=/opt/homebrew/opt/llvm/bin/clang  # Ubuntu 등 환경에 따라 경로 조정: CC=clang
CFLAGS="-std=c11 -O2 -g3 -Wall -Wextra --target=riscv32-unknown-elf -fno-stack-protector -ffreestanding -nostdlib"

# 커널 빌드
$CC $CFLAGS -Wl,-Tkernel.ld -Wl,-Map=kernel.map -o kernel.elf \
    kernel.c

# QEMU 실행
$QEMU -machine virt -bios default -nographic -serial mon:stdio --no-reboot \
    -kernel kernel.elf
```

> [!TIP]
>
> macOS에서 Homebrew로 clang을 설치했다면 다음 명령어로 경로를 확인할 수 있습니다:
>
> ```
> $ ls $(brew --prefix)/opt/llvm/bin/clang
> /opt/homebrew/opt/llvm/bin/clang
> ```

`CFLAGS`에 지정한 옵션의 의미는 다음과 같습니다:

| 옵션 | 설명                                            |
| ------ |-----------------------------------------------|
| `-std=c11` | C11 표준 사용                                     |
| `-O2` | 최적화 레벨 2 설정                                   |
| `-g3` | 최대한의 디버그 정보 생성                                |
| `-Wall` | 핵심 경고 활성화                                     |                                                                             
| `-Wextra` | 추가 경고 활성화                                     |
| `--target=riscv32-unknown-elf` | 32비트 RISC-V 대상 아키텍처로 컴파일                      |
| `-fno-stack-protector` | 스택 보호 기능 비활성화 ([#31](https://github.com/nuta/operating-system-in-1000-lines/issues/31#issuecomment-2613219393) 참고) |
| `-ffreestanding` | 호스트(개발 환경) 표준 라이브러리를 사용하지 않음                  |
| `-nostdlib` | 표준 라이브러리를 링크하지 않음                             |
| `-Wl,-Tkernel.ld` | 링커 스크립트(`kernel.ld`) 지정                         |
| `-Wl,-Map=kernel.map` | 맵 파일(`kernel.map`) 생성 (링킹 결과와 섹션 배치를 확인할 수 있음) |

`-Wl,`는 링커 옵션을 직접 전달하는 방법입니다. `clang`은 내부적으로 링커를 실행하므로 이렇게 지정해 줍니다.

## 첫 번째 커널 디버깅

`run.sh`를 실행하면, 작성한 커널이 `kernel_main`에서 무한 루프에 들어갑니다. 화면상으로는 별다른 변화가 없어 보일 수 있지만, 이는 매우 흔한 상황입니다. 이런 때는 QEMU의 디버그 기능을 사용해 실제로 코드가 어디까지 실행되었는지를 확인해볼 수 있습니다.

QEMU 모니터에서 `info registers` 명령어를 실행하면 CPU 레지스터 정보를 확인할 수 있습니다:

```
QEMU 8.0.2 monitor - type 'help' for more information
(qemu) info registers

CPU#0
 V      =   0
 pc       80200014  ← Address of the instruction to be executed (Program Counter)
 ...
 x0/zero  00000000 x1/ra    8000a084 x2/sp    80220018 x3/gp    00000000  ← Values of each register
 x4/tp    80033000 x5/t0    00000001 x6/t1    00000002 x7/t2    00000000
 x8/s0    80032f50 x9/s1    00000001 x10/a0   80220018 x11/a1   87e00000
 x12/a2   00000007 x13/a3   00000019 x14/a4   00000000 x15/a5   00000001
 x16/a6   00000001 x17/a7   00000005 x18/s2   80200000 x19/s3   00000000
 x20/s4   87e00000 x21/s5   00000000 x22/s6   80006800 x23/s7   8001c020
 x24/s8   00002000 x25/s9   8002b4e4 x26/s10  00000000 x27/s11  00000000
 x28/t3   616d6569 x29/t4   8001a5a1 x30/t5   000000b4 x31/t6   00000000
```

> [!TIP]
>
> clang과 QEMU 버전에 따라 레지스터 값은 다를 수 있습니다.

`pc 80200014` 는 현재 0x80200014 주소의 명령어가 실행되고 있음을 의미합니다. 이제 `llvm-objdump`로 어떤 명령어가 있는지 확인해보겠습니다:

```
$ llvm-objdump -d kernel.elf

kernel.elf:     file format elf32-littleriscv

Disassembly of section .text:

80200000 <boot>:  ← boot function
80200000: 37 05 22 80   lui     a0, 524832
80200004: 13 05 85 01   addi    a0, a0, 24
80200008: 2a 81         mv      sp, a0
8020000a: 6f 00 60 00   j       0x80200010 <kernel_main>
8020000e: 00 00         unimp

80200010 <kernel_main>:  ← kernel_main function
80200010: 73 00 50 10   wfi
80200014: f5 bf         j       0x80200010 <kernel_main>  ← pc is here
```

각 줄은 다음 정보를 보여줍니다:

- 명령어가 배치된 주소
- 기계어(16진수)
- 역어셈블된 명령어

`pc 80200014` 부분을 보면 `j 0x80200010` 명령어가 확인됩니다. 이는 곧 `kernel_main`의 무한 루프에 진입했음을 의미합니다.

또한 스택 포인터(`sp`)가 정말로 링커 스크립트에서 정의한 `__stack_top` 주소로 설정되었는지도 확인해볼 수 있습니다. 레지스터 정보에서 `x2/sp 80220018`로 나와 있는데, `kernel.map`을 보면 다음과 같은 배치를 확인할 수 있습니다:

```
     VMA      LMA     Size Align Out     In      Symbol
       0        0 80200000     1 . = 0x80200000
80200000 80200000       16     4 .text
...
80200016 80200016        2     1 . = ALIGN ( 4 )
80200018 80200018    20000     1 . += 128 * 1024
80220018 80220018        0     1 __stack_top = .
```

혹은 `llvm-nm` 명령어로도 확인할 수 있습니다:

```
$ llvm-nm kernel.elf
80200010 t .LBB0_1
00000000 N .Lline_table_start0
80220018 T __stack_top
80200000 T boot
80200010 T kernel_main
```

첫 열이 각 심볼의 배치된 가상 메모리 주소(`VMA`)이며, `__stack_top`이 `0x80220018` 주소로 배치된 것을 볼 수 있습니다. `QEMU`에서 본 스택 포인터 값과 동일하므로, 설정이 제대로 이루어졌음을 알 수 있습니다.

만약 실행을 일시 정지하고 싶다면, `QEMU` 모니터에서 `stop` 명령어로 정지한 뒤 `info registers`를 통해 상태를 확인하고, `cont` 명령어로 재개할 수 있습니다:


```
(qemu) stop             ← The process stops
(qemu) info registers   ← You can observe the state at the stop
(qemu) cont             ← The process resumes
```

이제 첫 번째 커널을 성공적으로 작성했습니다! 여기까지 오느라 고생 많으셨습니다.