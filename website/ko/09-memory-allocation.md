---
title: 메모리 할당
---

# 메모리 할당

이 장에서는 간단한 메모리 할당기를 구현합니다.

## 링커 스크립트(Linker Script) 다시 살펴보기

메모리 할당기를 구현하기 전에, 어떤 메모리 영역을 할당할 것인지 링커 스크립트에서 정의해둡시다:


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

여기서 새로 등장하는 심볼 `__free_ram`과 `__free_ram_end`는 스택 공간 다음에 위치하는 메모리 영역을 나타냅니다. 64MB라는 크기는 임의로 정한 값이고, `. = ALIGN(4096)`를 통해 이 영역을 4KB(페이지 크기) 경계로 맞추도록 했습니다.

하드코딩된 주소 대신 링커 스크립트에서 정의함으로써, 커널의 정적 데이터와 겹치지 않도록 링커가 위치를 자동으로 조정할 수 있습니다.

> [!TIP]
>
> 실제 x86-64 운영체제들은 부팅 시 하드웨어(UEFI 등)를 통해 메모리 맵 정보(GetMemoryMap)를 받아서 사용 가능한 메모리 영역을 동적으로 파악하는 방식을 사용합니다.


## 세상에서 가장 간단한 메모리 할당 알고리즘

메모리를 동적으로 할당하는 함수를 구현해봅시다. 여기서는 C의 `malloc`처럼 `“바이트 단위”`로 할당하는 대신, 더 큰 단위인 `“페이지(page)”` 단위로 할당합니다. 일반적으로 한 페이지는 4KB(4096바이트)입니다.


> [!TIP]
>
> 4KB는 4096이며, 16진수로는 0x1000입니다. 페이지 경계로 맞춰진 주소를 16진수로 보면 깔쌈하게 떨어지는 것을 확인할 수 있습니다.

다음 `alloc_pages` 함수는 `n`개의 페이지를 할당한 뒤, 그 시작 주소를 반환합니다:

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

여기서 `PAGE_SIZE`는 페이지 하나의 크기를 의미하며, `common.h`에 다음과 같이 정의해줍니다:


```c [common.h]
#define PAGE_SIZE 4096
```

주요 포인트

- `next_paddr`은 `static` 변수로 선언되었기 때문에, 로컬 변수와 달리 함수 호출이 끝나도 값을 계속 유지합니다(글로벌 변수처럼 동작). 
- `next_paddr`는 “새로 할당할 메모리 영역의 시작 주소”를 가리키고, 할당이 이루어지면 그만큼 주소를 증가시켜 다음 할당을 준비합니다.
- `next_paddr`의 초깃값은 __free_ram 주소입니다. 즉, 메모리는 `__free_ram` 이후부터 순차적으로 할당됩니다.
- 링커 스크립트에서 `ALIGN(4096)`로 맞춰주었으므로, `alloc_pages`는 항상 4KB 정렬된 주소를 반환합니다.
- 만약 `__free_ram_end`를 넘어가서 더 이상 할당할 메모리가 없다면, 커널 패닉을 일으킵니다.
- `memset`을 통해 할당된 메모리 영역을 0으로 초기화합니다. 미초기화된 메모리에서 생기는 디버깅 어려움을 예방하기 위함입니다.

간단하지 않나요? 이 알고리즘은 아주 단순하지만, **반환(메모리 해제)** 이 불가능하다는 문제가 있습니다. 그래도 우리의 취미용 OS에는 이 정도면 충분합니다.

> [!TIP]
>
> 이렇게 단순한 할당 방식을 흔히 **Bump Allocator** 또는 **Linear Allocator**라고 부르며, 해제가 필요 없는 상황에서 실제로도 쓰이는 유용한 알고리즘입니다. 구현이 매우 간단하고 빠릅니다.
>
> 메모리를 해제하는 기능까지 구현하려면, 비트맵(bitmap) 방식이나 버디 시스템(buddy system) 같은 좀 더 복잡한 알고리즘을 사용합니다.

## 메모리 할당 테스트하기

구현한 메모리 할당 함수를 테스트해봅시다. `kernel_main`에 다음 코드를 추가합니다:

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

이제 첫 번째 할당 주소(`paddr0`)가 `__free_ram` 주소와 같은지, 그리고 두 번째 주소(`paddr1`)가 `paddr0`로부터 8KB(2페이지 = 8KB) 뒤인지를 확인해봅시다:


```
$ ./run.sh
Hello World!
alloc_pages test: paddr0=80221000
alloc_pages test: paddr1=80223000
```

그리고 실제 심볼 주소를 확인해보면:

```
$ llvm-nm kernel.elf | grep __free_ram
80221000 R __free_ram
84221000 R __free_ram_end
```

`__free_ram`이 `80221000`에서 시작하고, `paddr0`도 `80221000이`므로 정상적으로 동작하는 것을 알 수 있습니다!