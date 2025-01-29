---
title: 페이지 테이블
---

# 페이지 테이블

## 메모리 관리와 가상 주소

프로그램이 메모리에 접근할 때, CPU는 지정된 가상 주소를 물리 주소로 변환합니다. 이때 **가상 주소**와 물리 주소의 매핑 정보를 저장하는 테이블을 페이지 테이블(page table) 이라고 합니다. 페이지 테이블을 교체함으로써 같은 가상 주소가 서로 다른 물리 주소를 가리키게 설정할 수도 있습니다. 이를 통해 메모리 공간(가상 주소 공간)을 분리하고, 커널 영역과 애플리케이션 영역을 분리하여 시스템 보안을 높일 수 있습니다.

이 장에서는 하드웨어 기반 메모리 격리 메커니즘을 직접 구현해 봅니다.

## 가상 주소 구조 (Virtual Address Structure)

이 책에서는 RISC-V의 페이징 메커니즘 중 하나인 Sv32를 사용합니다. Sv32는 2단계(page table)로 구성된 페이지 테이블 방식을 사용합니다. 32비트 가상 주소는 1단계 페이지 테이블 인덱스(`VPN[1]`), 2단계 인덱스(`VPN[0]`), 그리고 페이지 오프셋 (`offset`)으로 나뉩니다.

[RISC-V Sv-32 Virtual Address Breakdown](https://riscv-sv32-virtual-address.vercel.app/) 를 사용해 가상 주소가 어떻게 구성되는지 확인해볼 수 있습니다.

아래는 몇 가지 예시입니다:

| Virtual Address | `VPN[1]` (10 bits) | `VPN[0]` (10 bits) | Offset (12 bits) |
| --------------- | ------------------ | ------------------ | ---------------- |
| 0x1000_0000     | 0x040              | 0x000              | 0x000            |
| 0x1000_0000     | 0x040              | 0x000              | 0x000            |
| 0x1000_1000     | 0x040              | 0x001              | 0x000            |
| 0x1000_f000     | 0x040              | 0x00f              | 0x000            |
| 0x2000_f0ab     | 0x080              | 0x00f              | 0x0ab            |
| 0x2000_f012     | 0x080              | 0x00f              | 0x012            |
| 0x2000_f034     | 0x080              | 0x00f              | 0x045            |

> [!TIP]
>
> 위 예시에서 다음과 같은 특징을 볼 수 있습니다:
>
> - 가운데 비트(`VPN[0]`)가 바뀌어도 첫 번째 인덱스(`VPN[1]`)에는 영향을 주지 않습니다. 즉, 주소가 서로 가깝다면 같은 1단계 페이지 테이블 안에 페이지 테이블 엔트리가 모여 있게 됩니다.
> - 가장 낮은 비트는 `offset`이므로 `VPN[1]`이나 `VPN[0]`에 영향을 주지 않습니다. 즉, 4KB 페이지 내부 주소는 같은 페이지 테이블 엔트리를 공유합니다.
>
> 이러한 구조는 [참조 지역성의 원리](https://en.wikipedia.org/wiki/Locality_of_reference)(locality of reference)를 활용해, 페이지 테이블을 작게 유지하고 TLB(Translation Lookaside Buffer)를 더욱 효율적으로 사용할 수 있게 합니다.

메모리에 접근할 때, CPU는 VPN[1]과 VPN[0]을 계산하여 해당하는 페이지 테이블 엔트리를 찾은 뒤, 거기에 저장된 물리 기본 주소(base physical address)에 offset을 더해 최종 물리 주소를 얻습니다.

## 페이지 테이블 구성하기

Sv32 방식의 페이지 테이블을 구성해 봅시다. 먼저 매크로들을 정의합니다. `SATP_SV32`는 `satp` 레지스터에서 "Sv32 모드 페이징 활성화"를 나타내는 비트이며, `PAGE_*`들은 페이지 테이블 엔트리에 설정할 플래그를 의미합니다.

```c [kernel.h]
#define SATP_SV32 (1u << 31)
#define PAGE_V    (1 << 0)   // "Valid" 비트 (엔트리가 유효함을 의미)
#define PAGE_R    (1 << 1)   // 읽기 가능
#define PAGE_W    (1 << 2)   // 쓰기 가능
#define PAGE_X    (1 << 3)   // 실행 가능
#define PAGE_U    (1 << 4)   // 사용자 모드 접근 가능
```

## 페이지 매핑하기

다음 `map_page` 함수는 1단계 페이지 테이블(`table1`), 가상 주소(`vaddr`), 물리 주소(`paddr`), 그리고 페이지 테이블 엔트리 플래그(`flags`)를 인자로 받습니다:


```c [kernel.c]
void map_page(uint32_t *table1, uint32_t vaddr, paddr_t paddr, uint32_t flags) {
    if (!is_aligned(vaddr, PAGE_SIZE))
        PANIC("unaligned vaddr %x", vaddr);

    if (!is_aligned(paddr, PAGE_SIZE))
        PANIC("unaligned paddr %x", paddr);

    uint32_t vpn1 = (vaddr >> 22) & 0x3ff;
    if ((table1[vpn1] & PAGE_V) == 0) {
        // Create the non-existent 2nd level page table.
        uint32_t pt_paddr = alloc_pages(1);
        table1[vpn1] = ((pt_paddr / PAGE_SIZE) << 10) | PAGE_V;
    }

    // Set the 2nd level page table entry to map the physical page.
    uint32_t vpn0 = (vaddr >> 12) & 0x3ff;
    uint32_t *table0 = (uint32_t *) ((table1[vpn1] >> 10) * PAGE_SIZE);
    table0[vpn0] = ((paddr / PAGE_SIZE) << 10) | flags | PAGE_V;
}
```

이 함수는 2단계 페이지 테이블이 없으면 할당한 뒤, 2단계 페이지 테이블의 페이지 테이블 엔트리를 채웁니다.

`paddr`를 `PAGE_SIZE`로 나누는 이유는 엔트리에 물리 주소 자체가 아니라 "물리 페이지 번호(physical page number)"를 저장해야 하기 때문입니다. 헷갈리지 않도록 주의합시다!


## 커널 메모리 영역 매핑

페이지 테이블은 사용자 공간(애플리케이션)을 위해서만이 아니라, 커널을 위해서도 설정해주어야 합니다.

이 책에서는 커널 메모리 매핑을`"커널의 가상 주소 == 물리 주소"`로 동일하게 설정합니다. 이렇게 하면 페이징을 활성화해도 동일한 코드가 문제없이 동작할 수 있습니다.

먼저, 커널 링커 스크립트를 수정해 커널이 사용하는 시작 주소(`__kernel_base`)를 정의합니다:


```ld [kernel.ld] {5}
ENTRY(boot)

SECTIONS {
    . = 0x80200000;
    __kernel_base = .;
```

> [!WARNING]
>
> __kernel_base 는 `. = 0x80200000` 줄 `뒤에` 정의해야 합니다. 순서가 바뀌면 `__kernel_base` 값이 0이 되어 버립니다.

다음으로, 프로세스 구조체에 페이지 테이블을 가리키는 포인터를 추가합니다. 이 포인터는 1단계 페이지 테이블을 가리키게 됩니다.

```c [kernel.h] {5}
struct process {
    int pid;
    int state;
    vaddr_t sp;
    uint32_t *page_table;
    uint8_t stack[8192];
};
```

마지막으로, `create_process` 함수에서 커널 페이지들을 매핑해 줍니다. 커널 페이지는 `__kernel_base` 부터 `__free_ram_end` 까지를 커버합니다. 이렇게 하면 커널이 `.text` 같은 정적 할당 영역과 `alloc_pages`로 관리되는 동적 할당 영역도 모두 접근할 수 있게 됩니다.

```c [kernel.c] {1,6-11,15}
extern char __kernel_base[];

struct process *create_process(uint32_t pc) {
    /* omitted */

    // Map kernel pages.
    uint32_t *page_table = (uint32_t *) alloc_pages(1);
    for (paddr_t paddr = (paddr_t) __kernel_base;
         paddr < (paddr_t) __free_ram_end; paddr += PAGE_SIZE)
        map_page(page_table, paddr, paddr, PAGE_R | PAGE_W | PAGE_X);

    proc->pid = i + 1;
    proc->state = PROC_RUNNABLE;
    proc->sp = (uint32_t) sp;
    proc->page_table = page_table;
    return proc;
}
```

## 페이지 테이블 전환(switching)

컨텍스트 스위칭(context switching) 시에 프로세스의 페이지 테이블을 스위칭해야 합니다:

```c [kernel.c] {5-7,10-11}
void yield(void) {
    /* 생략 */

    __asm__ __volatile__(
        "sfence.vma\n"
        "csrw satp, %[satp]\n"
        "sfence.vma\n"
        "csrw sscratch, %[sscratch]\n"
        :
        // 끝에 꼭 콤마가 있어야 함!
        : [satp] "r" (SATP_SV32 | ((uint32_t) next->page_table / PAGE_SIZE)),
          [sscratch] "r" ((uint32_t) &next->stack[sizeof(next->stack)])
    );

    switch_context(&prev->sp, &next->sp);
}
```

`satp` 레지스터에 1단계 페이지 테이블 주소를 지정하면 페이지 테이블을 전환할 수 있습니다. 이때도 "물리 주소"가 아닌 "물리 페이지 번호"를 지정해야 하므로 `PAGE_SIZ`E로 나누어줍니다.

`sfence.vma` 명령어들은 페이징 구조가 바뀌었을 때, CPU가 내부적으로 캐싱해둔 페이지 테이블(TLB)을 무효화하고, 페이지 테이블 변경이 올바르게 완료되었음을 보장하는 역할을 합니다.

> [!TIP]
>
> 커널이 시작할 때는 `satp` 레지스터가 설정되어 있지 않으므로 페이징이 비활성화된 상태입니다. 이때 가상 주소는 물리 주소와 동일하게 동작합니다.

## 페이징 테스트하기

이제 실제로 페이징을 설정해보고 테스트해봅시다!

```
$ ./run.sh

starting process A
Astarting process B
BABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABAB
```

출력 결과는 이전 장(컨텍스트 스위칭만 했을 때)과 완전히 동일합니다. 페이징을 켰음에도 겉으로 보기에 달라진 것은 없습니다.
페이지 테이블이 제대로 구성되었는지 확인하려면, QEMU 모니터를 통해 확인해볼 수 있습니다!

## 페이지 테이블 내용 확인하기

우선 `0x80000000` 주변 가상 주소들이 어떻게 매핑되는지 살펴봅시다. 잘 설정됐다면 `(가상 주소) == (물리 주소)` 형태로 매핑되어 있어야 합니다.

```
QEMU 8.0.2 monitor - type 'help' for more information
(qemu) stop
(qemu) info registers
 ...
 satp     80080253
 ...
```

`satp` 레지스터 값이 `0x80080253` 으로 설정된 것을 볼 수 있습니다. RISC-V의 Sv32 사양에 따르면, `(0x80080253 & 0x3fffff) * 4096 = 0x80253000` 이 값이 1단계 페이지 테이블의 물리 시작 주소임을 의미합니다.

이제 1단계 페이지 테이블 내용을 확인해봅시다. `0x80000000` 가상 주소에 해당하는 1단계 인덱스는 `0x80000000 >> 22 = 512`이므로, 1단계 테이블의 512번째 엔트리를 보고 싶습니다. 각 엔트리는 4바이트이므로, `512 * 4`를 더한 위치를 살펴봅니다:

```
(qemu) xp /x 0x80253000+512*4
0000000080253800: 0x20095001
```

첫 번째 열은 물리 주소를 표시하고, 이후로 16진수 값이 나옵니다. (`/x` 옵션은 16진수 표시를 의미). `/1024x` 같은 식으로 개수를 지정하면 더 많은 엔트리를 덤프할 수 있습니다.


> [!TIP]
>
> `x` 명령어(예: x /x 0x...)는 가상 주소 기준으로 메모리를 확인하는 명령어이고, `xp` 명령어는 물리 주소 기준으로 확인하는 명령어입니다. 커널 공간은 가상 주소와 물리 주소가 동일하지만, 사용자 공간처럼 가상 주소와 물리 주소가 다른 곳을 볼 땐 `xp`를 사용해야 올바르게 확인할 수 있습니다.

스펙에 따르면, 2단계 페이지 테이블은 `(0x20095000 >> 10) * 4096 = 0x80254000` 에 위치합니다. 2단계 테이블(1024개 엔트리)을 전부 덤프해봅시다:

```
(qemu) xp /1024x 0x80254000
0000000080254000: 0x00000000 0x00000000 0x00000000 0x00000000
0000000080254010: 0x00000000 0x00000000 0x00000000 0x00000000
0000000080254020: 0x00000000 0x00000000 0x00000000 0x00000000
0000000080254030: 0x00000000 0x00000000 0x00000000 0x00000000
...
00000000802547f0: 0x00000000 0x00000000 0x00000000 0x00000000
0000000080254800: 0x2008004f 0x2008040f 0x2008080f 0x20080c0f
0000000080254810: 0x2008100f 0x2008140f 0x2008180f 0x20081c0f
0000000080254820: 0x2008200f 0x2008240f 0x2008280f 0x20082c0f
0000000080254830: 0x2008300f 0x2008340f 0x2008380f 0x20083c0f
0000000080254840: 0x200840cf 0x2008440f 0x2008484f 0x20084c0f
0000000080254850: 0x200850cf 0x2008540f 0x200858cf 0x20085c0f
0000000080254860: 0x2008600f 0x2008640f 0x2008680f 0x20086c0f
0000000080254870: 0x2008700f 0x2008740f 0x2008780f 0x20087c0f
0000000080254880: 0x200880cf 0x2008840f 0x2008880f 0x20088c0f
...
```

처음 부분은 전부 0으로 채워져 있고, 512번째 엔트리(`254800` 부근)부터 값이 채워져 있음을 볼 수 있습니다. 이는 `__kernel_base`가 `0x80200000` 이고, `VPN[1]`이 `0x200` 부근이기 때문입니다.

직접 메모리 덤프를 보는 대신, QEMU에는 현재 페이지 테이블 매핑 정보를 사람이 보기 좋게 출력해주는 `info mem` 명령어가 있습니다. 매핑이 제대로 되었는지 최종적으로 확인하고 싶다면 이 명령어를 이용하면 됩니다.

```
(qemu) info mem
vaddr    paddr            size     attr
-------- ---------------- -------- -------
80200000 0000000080200000 00001000 rwx--a-
80201000 0000000080201000 0000f000 rwx----
80210000 0000000080210000 00001000 rwx--ad
80211000 0000000080211000 00001000 rwx----
80212000 0000000080212000 00001000 rwx--a-
80213000 0000000080213000 00001000 rwx----
80214000 0000000080214000 00001000 rwx--ad
80215000 0000000080215000 00001000 rwx----
80216000 0000000080216000 00001000 rwx--ad
80217000 0000000080217000 00009000 rwx----
80220000 0000000080220000 00001000 rwx--ad
80221000 0000000080221000 0001f000 rwx----
80240000 0000000080240000 00001000 rwx--ad
80241000 0000000080241000 001bf000 rwx----
80400000 0000000080400000 00400000 rwx----
80800000 0000000080800000 00400000 rwx----
80c00000 0000000080c00000 00400000 rwx----
81000000 0000000081000000 00400000 rwx----
81400000 0000000081400000 00400000 rwx----
81800000 0000000081800000 00400000 rwx----
81c00000 0000000081c00000 00400000 rwx----
82000000 0000000082000000 00400000 rwx----
82400000 0000000082400000 00400000 rwx----
82800000 0000000082800000 00400000 rwx----
82c00000 0000000082c00000 00400000 rwx----
83000000 0000000083000000 00400000 rwx----
83400000 0000000083400000 00400000 rwx----
83800000 0000000083800000 00400000 rwx----
83c00000 0000000083c00000 00400000 rwx----
84000000 0000000084000000 00241000 rwx----
```

표의 각 열은 가상 주소, 물리 주소, 크기(16진수), 그리고 속성 정보를 나타냅니다.

속성은 `r`(읽기 가능), `w`(쓰기 가능), `x`(실행 가능), `a`(accessed: CPU가 페이지를 읽거나 실행함), `d`(dirty: CPU가 페이지에 기록함)로 구성됩니다. `a`와 `d`는 OS가 “이 페이지가 실제로 사용되었는지(읽혔는지), 쓰여졌는지” 추적할 때 유용합니다.

> [!TIP]
>
> 처음 접하는 사람에게는 페이지 테이블 디버깅이 쉽지 않습니다. 원하는 대로 동작하지 않으면, 아래 "부록: 페이징 디버깅" 섹션을 참고해보세요.

## 부록: 페이징 디버깅

페이지 테이블 설정은 실수하기 쉽고, 에러가 잘 드러나지 않을 때가 많습니다. 여기서는 흔히 발생하는 페이징 오류와, 이를 디버깅하는 방법을 살펴봅니다.

### 페이징 모드 비트 설정을 빼먹었을 때

예를 들어, `satp` 레지스터에 모드를 설정하는 것을 깜빡했다고 합시다:

```c [kernel.c] {6}
    __asm__ __volatile__(
        "sfence.vma\n"
        "csrw satp, %[satp]\n"
        "sfence.vma\n"
        :
        : [satp] "r" (((uint32_t) next->page_table / PAGE_SIZE)) // Missing SATP_SV32!
    );
```

이 경우, OS를 실행해도 전과 달라진 점 없이 정상 작동하는 것처럼 보입니다. 실제로는 페이징이 꺼져 있어서 모든 주소가 여전히 물리 주소처럼 동작하기 때문입니다.

이를 디버깅하기 위해 QEMU 모니터에서 `info mem` 명령어를 써보면:

```
(qemu) info mem
No translation or protection
```

처럼 전혀 페이지 테이블 매핑 정보가 나오지 않습니다.

### 물리 페이지 번호 대신 물리 주소를 지정했을 때

이번엔 다음처럼 페이지 테이블을 설정할 때 "물리 주소"를 그대로 써버려야 하는데, 실수로 "물리 페이지 번호" 대신 물리 주소를 그대로 써버렸다고 가정합시다:

```c [kernel.c] {6}
    __asm__ __volatile__(
        "sfence.vma\n"
        "csrw satp, %[satp]\n"
        "sfence.vma\n"
        :
        : [satp] "r" (SATP_SV32 | ((uint32_t) next->page_table)) // Forgot to shift!
    );
```

이 경우 `info mem` 명령어를 써보면 매핑이 전혀 잡히지 않은 것을 볼 수 있습니다:

```
$ ./run.sh

QEMU 8.0.2 monitor - type 'help' for more information
(qemu) stop
(qemu) info mem
vaddr    paddr            size     attr
-------- ---------------- -------- -------
```

디버깅을 위해 레지스터도 살펴봅시다:


```
(qemu) info registers

CPU#0
 V      =   0
 pc       80200188
 ...
 scause   0000000c
 ...
```

`pc`가 `0x80200188`이고, `scause`가 `0000000c` (Instruction page fault)이라는 것을 확인할 수 있습니다. `llvm-addr2line`으로 `0x80200188`의 위치를 확인해 보면 예외 핸들러의 시작점임을 알 수 있습니다.

QEMU 로그(-d unimp,guest_errors,int,cpu_reset -D qemu.log)를 열어보면:

```bash [run.sh] {2}
$QEMU -machine virt -bios default -nographic -serial mon:stdio --no-reboot \
    -d unimp,guest_errors,int,cpu_reset -D qemu.log \  # new!
    -kernel kernel.elf
```

```
Invalid read at addr 0x253000800, size 4, region '(null)', reason: rejected
riscv_cpu_do_interrupt: hart:0, async:0, cause:0000000c, epc:0x80200580, tval:0x80200580, desc=exec_page_fault
Invalid read at addr 0x253000800, size 4, region '(null)', reason: rejected
riscv_cpu_do_interrupt: hart:0, async:0, cause:0000000c, epc:0x80200188, tval:0x80200188, desc=exec_page_fault
Invalid read at addr 0x253000800, size 4, region '(null)', reason: rejected
riscv_cpu_do_interrupt: hart:0, async:0, cause:0000000c, epc:0x80200188, tval:0x80200188, desc=exec_page_fault
```

이 로그로부터 몇 가지를 알 수 있습니다:

- `epc`(예외 발생 PC)가 `0x80200580`인 점을 보면, `satp`를 설정하자마자 곧바로 페이지 폴트가 발생했다는 것을 의미합니다.

- 이후 예외(page fault)가 `0x80200188`에서 반복적으로 일어나는데, 이 위치는 예외 핸들러의 시작점입니다. 결국 예외를 처리하려고 해도, 예외 핸들러 코드조차 페이지 폴트가 발생하는 악순환이 이어집니다.

- info registers에서 `satp`가 `0x80253000` 같은 값으로 설정되었고, Sv32에 따르면 `(0x80253000 & 0x3fffff) * 4096 = 0x253000000`처럼 주소가 32비트 범위를 초과해버리는 비정상적인 값이 되어 매핑 자체가 엉켜 버렸습니다.

정리하자면, QEMU 로그나 레지스터 덤프, 메모리 덤프 등을 통해 무엇이 문제인지 추적할 수 있지만, 결국 가장 중요한 것은 "스펙을 꼼꼼히 읽고 정확히 구현하는 것" 입니다. 페이징에서는 사소한 실수로도 큰 오류가 발생하기 쉽습니다.
