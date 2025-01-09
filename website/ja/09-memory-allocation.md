---
title: メモリ割り当て
---

# メモリ割り当て

## リンカスクリプト

動的に割り当てるメモリ領域をリンカスクリプトに定義しましょう。

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

`__free_ram`から`__free_ram_end`までの領域を、動的割り当て可能なメモリ領域とします。64MBは筆者が適当に選んだ値です。`. = ALIGN(4096)`とすることで、4KB境界に配置されるようになります。

このように、アドレスを決め打ちで定義せずにリンカスクリプト上に定義することで、カーネルの静的データと被らないようにリンカが位置を決定してくれます。

実用的なOSでは、このようにデバイスごとにメモリサイズを決め打ちで定義する場合の他に、起動時にハードウェアから利用可能なメモリ領域の情報を取得して決定することもあります (例: UEFIの`GetMemoryMap`)。

## たぶん世界一シンプルなメモリ割り当てアルゴリズム

動的割り当て領域を定義したところで、実際に動的にメモリを割り当てる関数を実装しましょう。ただし、`malloc`関数のようなバイト単位で割り当てるのではなく「ページ」という、まとまった単位で割り当てます。1ページは一般的に4KB (4096バイト) です。

> [!TIP]
>
> 4KB = 4096 = 0x1000 です。16進数では`1000`であることを覚えておくと便利です。

次の`alloc_pages`関数は、`n`ページ分のメモリを動的に割り当てて、その先頭アドレスを返します。

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

新たに登場する `PAGE_SIZE` は、1ページのサイズを表します。`common.h`に定義しておきます。

```c [common.h]
#define PAGE_SIZE 4096
```

この関数からは、次のような特徴を読み取ることができます。

- `next_paddr`は`static`変数として定義されている。つまり、ローカル変数とは違い、関数呼び出し間で値が保持される (グローバル変数のような挙動を示す)。
- `next_paddr`が「次に割り当てられる領域 (空いている領域) の先頭アドレス」を指す。割り当て時には、確保するサイズ分だけ`next_paddr`を進める。
- `next_paddr`は`__free_ram`のアドレスを初期値として持つ。つまり、`__free_ram`から順にメモリを割り当てていく。
- `__free_ram`はリンカスクリプトの`ALIGN(4096)`により4KB境界に配置される。つまり、`alloc_pages`関数必ず4KBでアラインされたアドレスを返す。
- `__free_ram_end`を超えるアドレスに割り当てようとした場合は、カーネルパニックする。`malloc`関数が`NULL`を返すのと同じように`0`を返すのも手だが、返り値チェックし忘れのバグはデバッグが面倒なので、分かりやすさのためパニックさせる。
- `memset`関数によって、割り当てたメモリ領域が必ずゼロで初期化されている。初期化し忘れのバグはデバッグが面倒なので、ここで初期化しておく。

このメモリ割り当ての最大の特徴は個別のメモリページを解放できないことです。つまり、割り当てっぱなしです。ただ、自作OSを長時間動かし続けることはまずないでしょうから、今のところはメモリリークを許容しても差し支えないでしょう。

> [!TIP]
>
> ちなみに、この割り当てアルゴリズムのことは**Bumpアロケータ**または**Linearアロケータ**と呼ばれており、解放処理が必要ない場面で実際に使われています。数行に実装できて高速に動作する、魅力的な割り当てアルゴリズムです。
>
> 解放処理を実装する場合は、ビットマップで空き状況を管理したり、バディシステムというアルゴリズムを使ったりすることが多いです。

## メモリ割り当てのテスト

実装したメモリ割り当て関数をテストしてみましょう。

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

次のように、最初のアドレス (`paddr0`) が`__free_ram`のアドレスと一致し、次のアドレス (`paddr1`) が最初のアドレスから2 \* 4KB分進んだアドレス (16進数で`0x2000`足した数) と一致することを確認します。

```
$ ./run.sh
Hello World!
alloc_pages test: paddr0=80221000
alloc_pages test: paddr1=80223000
```

```
$ llvm-nm kernel.elf | grep __free_ram
80221000 R __free_ram
84221000 R __free_ram_end
```
