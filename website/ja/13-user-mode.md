---
title: ユーザーモード
---

# ユーザーモード

本章では、前章で作ったアプリケーションの実行イメージを動かしてみます。

## 実行ファイルの展開

まずは実行イメージの展開に必要な定義をいくつかしましょう。まずは、実行イメージの基点アドレス (`USER_BASE`) です。これは、`user.ld`で定義されている開始アドレスと合致する必要があります。

ELF形式のような一般的な実行可能ファイルであれば、そのファイルのヘッダ (ELFの場合プログラムヘッダ) にロード先のアドレスが書かれています。しかし、本書のアプリケーションの実行イメージは生バイナリなので、このように決め打ちで用意しておく必要があります。

```c [kernel.h]
#define USER_BASE 0x1000000
```

次に、`shell.bin.o`に入っている実行イメージへのポインタとイメージサイズのシンボルを定義しておきます。

```c [kernel.c]
extern char _binary_shell_bin_start[], _binary_shell_bin_size[];
```

次に、実行イメージをプロセスのアドレス空間にマップする処理を`create_process`関数に追加します。

```c [kernel.c] {1-3,5,11,20-35}
void user_entry(void) {
    PANIC("not yet implemented"); // 後で実装する
}

struct process *create_process(const void *image, size_t image_size) {
    /* 省略 */
    *--sp = 0;                      // s3
    *--sp = 0;                      // s2
    *--sp = 0;                      // s1
    *--sp = 0;                      // s0
    *--sp = (uint32_t) user_entry;  // ra

    uint32_t *page_table = (uint32_t *) alloc_pages(1);

    // カーネルのページをマッピングする
    for (paddr_t paddr = (paddr_t) __kernel_base;
         paddr < (paddr_t) __free_ram_end; paddr += PAGE_SIZE)
        map_page(page_table, paddr, paddr, PAGE_R | PAGE_W | PAGE_X);

    // ユーザーのページをマッピングする
    for (uint32_t off = 0; off < image_size; off += PAGE_SIZE) {
        paddr_t page = alloc_pages(1);

        // コピーするデータがページサイズより小さい場合を考慮
        // https://github.com/nuta/operating-system-in-1000-lines/pull/27
        size_t remaining = image_size - off;
        size_t copy_size = PAGE_SIZE <= remaining ? PAGE_SIZE : remaining;

        // 確保したページにデータをコピー
        memcpy((void *) page, image + off, copy_size);

        // ページテーブルにマッピング
        map_page(page_table, USER_BASE + off, page,
                 PAGE_U | PAGE_R | PAGE_W | PAGE_X);
    }
```

`create_process`関数は、実行イメージへのポインタ (`image`) とイメージサイズ (`image_size`) を引数に取るように変更しました。指定されたサイズ分、実行イメージをページ単位でコピーして、ユーザーモードのページにマッピングしています。また、初回のコンテキストスイッチ時のジャンプ先を`user_entry`に設定しています。今のところは空っぽの関数にしておきます。

> [!WARNING]
>
> このとき、実行イメージをコピーせずにそのままマッピングしてしまうと、同じアプリケーションのプロセスたちが同じ物理ページを共有することになります。

最後に `create_process` 関数の呼び出し側の修正と、ユーザープロセスを作成するようにします。

```c [kernel.c] {8,12}
void kernel_main(void) {
    memset(__bss, 0, (size_t) __bss_end - (size_t) __bss);

    printf("\n\n");

    WRITE_CSR(stvec, (uint32_t) kernel_entry);

    idle_proc = create_process(NULL, 0);
    idle_proc->pid = -1; // idle
    current_proc = idle_proc;

    create_process(_binary_shell_bin_start, (size_t) _binary_shell_bin_size);

    yield();
    PANIC("switched to idle process");
}
```

実際に動かしてみて、実行イメージが期待通りマッピングされているかQEMUモニタで確認してみましょう。

> [!TIP]
>
> まだこの時点ではユーザーモードへの移行処理がないので、アプリケーションは動きません。まずは、実行イメージが正しく展開されているかのみを確認します。

```
(qemu) info mem
vaddr    paddr            size     attr
-------- ---------------- -------- -------
01000000 0000000080265000 00001000 rwxu---
01001000 0000000080267000 00010000 rwxu---
```

仮想アドレス `0x1000000` (`USER_BASE`) に、物理アドレス `0x80265000` がマップされていることがわかります。この物理アドレスの中身を見てみましょう。物理メモリの内容を表示するには、`xp`コマンドを使います。

```
(qemu) xp /32b 0x80265000
0000000080265000: 0x37 0x05 0x01 0x01 0x13 0x05 0x05 0x26
0000000080265008: 0x2a 0x81 0x19 0x20 0x29 0x20 0x00 0x00
0000000080265010: 0x01 0xa0 0x00 0x00 0x82 0x80 0x01 0xa0
0000000080265018: 0x09 0xca 0xaa 0x86 0x7d 0x16 0x13 0x87
```

何かしらデータが入っているようです。`shell.bin`の中身を確認してみると、確かに合致しています。

```
$ hexdump -C shell.bin | head
00000000  37 05 01 01 13 05 05 26  2a 81 19 20 29 20 00 00  |7......&*.. ) ..|
00000010  01 a0 00 00 82 80 01 a0  09 ca aa 86 7d 16 13 87  |............}...|
00000020  16 00 23 80 b6 00 ba 86  75 fa 82 80 01 ce aa 86  |..#.....u.......|
00000030  03 87 05 00 7d 16 85 05  93 87 16 00 23 80 e6 00  |....}.......#...|
00000040  be 86 7d f6 82 80 03 c6  05 00 aa 86 01 ce 85 05  |..}.............|
00000050  2a 87 23 00 c7 00 03 c6  05 00 93 06 17 00 85 05  |*.#.............|
00000060  36 87 65 fa 23 80 06 00  82 80 03 46 05 00 15 c2  |6.e.#......F....|
00000070  05 05 83 c6 05 00 33 37  d0 00 93 77 f6 0f bd 8e  |......37...w....|
00000080  93 b6 16 00 f9 8e 91 c6  03 46 05 00 85 05 05 05  |.........F......|
00000090  6d f2 03 c5 05 00 93 75  f6 0f 33 85 a5 40 82 80  |m......u..3..@..|
```

16進数だと分かりづらいので、`xp`コマンドを使ってメモリ上の機械語を逆アセンブルしてみましょう。

```
(qemu) xp /8i 0x80265000
0x80265000:  01010537          lui                     a0,16842752
0x80265004:  26050513          addi                    a0,a0,608
0x80265008:  812a              mv                      sp,a0
0x8026500a:  2019              jal                     ra,6                    # 0x80265010
0x8026500c:  2029              jal                     ra,10                   # 0x80265016
0x8026500e:  0000              illegal
0x80265010:  a001              j                       0                       # 0x80265010
0x80265012:  0000              illegal
```

何か計算した結果をスタックポインタに設定し、2回関数を呼び出しています。`shell.elf`の逆アセンブル結果と比較してみると、確かに合致しています。上手く展開できているようです。

```
$ llvm-objdump -d shell.elf | head -n20

shell.elf:      file format elf32-littleriscv

Disassembly of section .text:

01000000 <start>:
 1000000: 37 05 01 01   lui     a0, 4112
 1000004: 13 05 05 26   addi    a0, a0, 608
 1000008: 2a 81         mv      sp, a0
 100000a: 19 20         jal     0x1000010 <main>
 100000c: 29 20         jal     0x1000016 <exit>
 100000e: 00 00         unimp

01000010 <main>:
 1000010: 01 a0         j       0x1000010 <main>
 1000012: 00 00         unimp
```

## ユーザーモードへの移行

実行イメージを展開できたので、最後の処理を実装しましょう。それは「CPUの動作モードの切り替え」です。カーネルはS-Modeと呼ばれる特権モードで動作していますが、ユーザープログラムはU-Modeと呼ばれる非特権モードで動作します。以下がその実装です。

```c [kernel.h]
#define SSTATUS_SPIE (1 << 5)
```

```c [kernel.c]
// ↓ __attribute__((naked)) が追加されていることに注意
__attribute__((naked)) void user_entry(void) {
    __asm__ __volatile__(
        "csrw sepc, %[sepc]\n"
        "csrw sstatus, %[sstatus]\n"
        "sret\n"
        :
        : [sepc] "r" (USER_BASE),
          [sstatus] "r" (SSTATUS_SPIE)
    );
}
```

S-ModeからU-Modeへの切り替えは、`sret`命令で行います。ただし、動作モードを切り替える前に2つ下準備をしています。

- `sepc`レジスタにU-Modeに移行した際のプログラムカウンタを設定する。
- `sstatus`レジスタの`SPIE`ビットを立てる。これを設定しておくと、U-Modeに入った際に割り込みが有効化され、例外と同じように`stvec`レジスタに設定しているハンドラが呼ばれるようになる。

> [!TIP]
>
> 本書では割り込みを使わず代わりにポーリングを使うので、`SPIE`ビットを立てる必要はありません。しかし、有効化していても損はないので立てておきます。黙って割り込みを無視されるよりは分かりやすくて良いでしょう。

## 動作テスト

では実際に動かしてみてみましょう。といっても、`shell.c`は無限ループするだけなので画面上では上手く動いているのか分かりません。代わりにQEMUモニタで覗いてみましょう。

```
(qemu) info registers

CPU#0
 V      =   0
 pc       01000010
```

レジスタダンプを見てみると、`0x1000010`をずっと実行しているようです。上手く動いている気がしますが、なんだか納得がいきません。そこで、U-Mode特有の挙動が現れるかを見てみましょう。`shell.c`に一行追加してみます。

```c [shell.c] {4}
#include "user.h"

void main(void) {
    *((volatile int *) 0x80200000) = 0x1234;
    for (;;);
}
```

この`0x80200000`は、ページテーブル上でマップされているカーネルが利用するメモリ領域です。しかし、ページテーブルエントリの`U`ビットが立っていない「カーネル用ページ」であるため、例外 (ページフォルト) が発生するはずです。

実行してみると、期待通り例外が発生しました。

```
$ ./run.sh

PANIC: kernel.c:71: unexpected trap scause=0000000f, stval=80200000, sepc=0100001a
```

`0xf = 15`番目の例外を仕様書で確認してみると「Store/AMO page fault」に対応します。期待通りの例外が発生しているようです。また、`sepc`レジスタの例外発生時のプログラムカウンタを見てみると、確かに`shell.c`に追加している行を指しています。

```
$ llvm-addr2line -e shell.elf 0x100001a
/Users/seiya/dev/os-from-scratch/shell.c:4
```

初めてのアプリケーションを実行できました！
