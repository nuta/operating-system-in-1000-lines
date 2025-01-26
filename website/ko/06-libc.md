---
title: C 표준 라이브러리
---

# C 표준 라이브러리

이 장에서는 기본 타입과 메모리 조작, 문자열 조작 함수를 직접 구현해 봅시다. 학습 목적으로, C 표준 라이브러리를 사용하지 않고 직접 만들어 볼 예정입니다.


> [!TIP]
>
> 이 장에서 소개하는 개념은 C 프로그래밍에서 매우 흔하므로 ChatGPT가 좋은 답변을 제공할 것입니다. 구현이나 이해에 어려움을 겪는 부분이 있다면 질문해보거나 저에게 알려주세요.

## 기본 타입들

먼저 `common.h`에 기본 타입과 편리한 매크로를 정의해 봅시다:


```c [common.h] {1-15,21-24}
typedef int bool;
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;
typedef uint32_t size_t;
typedef uint32_t paddr_t;
typedef uint32_t vaddr_t;

#define true  1
#define false 0
#define NULL  ((void *) 0)
#define align_up(value, align)   __builtin_align_up(value, align)
#define is_aligned(value, align) __builtin_is_aligned(value, align)
#define offsetof(type, member)   __builtin_offsetof(type, member)
#define va_list  __builtin_va_list
#define va_start __builtin_va_start
#define va_end   __builtin_va_end
#define va_arg   __builtin_va_arg

void *memset(void *buf, char c, size_t n);
void *memcpy(void *dst, const void *src, size_t n);
char *strcpy(char *dst, const char *src);
int strcmp(const char *s1, const char *s2);
void printf(const char *fmt, ...);
```

대부분 C 표준 라이브러리에 이미 있는 내용이지만, 몇 가지 유용한 것들을 추가했습니다.



- `paddr_t`: 물리 메모리 주소를 나타내는 타입.
- `vaddr_t`: 가상 메모리 주소를 나타내는 타입. 표준 라이브러리의 `uintptr_t`에 해당합니다.
- `align_up`: `value`를 `align`의 배수로 맞춰 올림합니다. 여기서 `align`은 2의 거듭제곱이어야 합니다.
- `is_aligned`: `value`가 `align`의 배수인지 확인합니다. 마찬가지로 `align`은 2의 거듭제곱이어야 합니다.
- `offsetof`: 구조체 내에서 특정 멤버가 시작되는 위치(바이트 단위)를 반환합니다.


예를 들어 `align_up(0x1234, 0x1000)`은 `0x2000`이 되고, `is_aligned(0x2000, 0x1000)`은 true를 반환하지만 `is_aligned(0x2f00, 0x1000)`은 false를 반환합니다.

위 매크로들은 Clang의 확장 기능인 `__builtin_` 함수를 사용했습니다. (자세한 내용은 [Clang built-in functions and macros](https://clang.llvm.org/docs/LanguageExtensions.html)를 참고하세요.)

> [!TIP]
>
> 이 매크로들은 빌트인 함수를 사용하지 않고, 순수 C 코드로도 구현할 수 있습니다. 특히 `offsetof`의 전통적인 C 구현은 꽤 흥미롭습니다.


## 메모리 조작 함수

다음으로, 메모리 조작 함수들을 구현해 봅시다.

### memcpy

`memcpy` 함수는 `src`에서 `dst`로 `n` 바이트를 복사합니다:

```c [common.c]
void *memcpy(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t *) dst;
    const uint8_t *s = (const uint8_t *) src;
    while (n--)
        *d++ = *s++;
    return dst;
}
```

### memset

`memset`은 `buf`의 시작 위치부터 `n`바이트를 문자 `c`로 채웁니다. 예전(4장)에는 BSS 섹션 초기화 용도로 `kernel.c`에서 잠시 썼는데, 이제 `common.c`로 옮깁니다.

```c [common.c]
void *memset(void *buf, char c, size_t n) {
    uint8_t *p = (uint8_t *) buf;
    while (n--)
        *p++ = c;
    return buf;
}
```

> [!TIP]
>
> *p++ = c; 구문은 포인터 역참조와 포인터 이동이 한 줄에서 동시에 이루어집니다. 더 명확히 쓰면 다음과 같습니다:
>
> ```c
> *p = c;    // Dereference the pointer
> p = p + 1; // Advance the pointer after the assignment
> ```


## 문자열 조작 함수

### strcpy

이제 `strcpy` 함수도 살펴봅시다. 이 함수는 `src` 문자열을 `dst`로 복사합니다.

```c [common.c]
char *strcpy(char *dst, const char *src) {
    char *d = dst;
    while (*src)
        *d++ = *src++;
    *d = '\0';
    return dst;
}
```

> [!WARNING]
>
> `strcpy`는 `src`가 `dst`보다 길어도 무조건 계속 복사하기 때문에, 매우 위험할 수 있습니다. 버그나 보안 취약점의 원인이 되기 쉽죠. 실제로는 `strcpy` 대신 `strcpy_s` 같은 안전한 함수를 쓰는 것이 권장됩니다.
> 
> 여기서는 단순히 학습을 위해 strcpy를 사용합니다만, 실무에서는 strcpy는 피해야 합니다!


### strcmp

다음은 `strcmp` 함수입니다. s1과 s2를 비교하여,

- 같으면 0,
- s1이 더 크면 양수,
- s1이 더 작으면 음수를 반환합니다.

| Condition | Result |
| --------- |-------|
| `s1` == `s2` | 0     |
| `s1` > `s2` | 양수    |
| `s1` < `s2` | 음수    |

```c [common.c]
int strcmp(const char *s1, const char *s2) {
    while (*s1 && *s2) {
        if (*s1 != *s2)
            break;
        s1++;
        s2++;
    }

    return *(unsigned char *)s1 - *(unsigned char *)s2;
}
```

> [!TIP]
>
> `*(unsigned char *)s1`로 캐스팅하는 이유는  [POSIX 명세](https://www.man7.org/linux/man-pages/man3/strcmp.3.html#:~:text=both%20interpreted%20as%20type%20unsigned%20char)에 따르면, 문자를 비교할 때 `unsigned char`로 해석하도록 권장하기 때문입니다.

예를 들어, 아래처럼 두 문자열이 같은지 비교할 때 사용합니다. `strcmp(s1, s2)`가 `0`을 반환하면 두 문자열이 동일하다는 뜻입니다.


```c
if (!strcmp(s1, s2))
    printf("s1 == s2\n");
else
    printf("s1 != s2\n");
```
