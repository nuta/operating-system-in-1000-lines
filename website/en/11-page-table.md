---
title: Page Table
layout: chapter
lang: en
---

# Memory management and virtual addressing

When a program accesses memory, the CPU performs a conversion from virtual addresses to physical addresses. The table that maps virtual addresses to physical addresses is called a **page table**. By switching page tables, the same virtual address can access different physical addresses. This allows isolation of memory spaces (virtual address spaces) for each process and separation of kernel and application memory areas, enhancing system security.

In this chapter, we'll implement the construction and switching of page tables to realize independent memory spaces for each process.

## Structure of virtual addresses

We'll use the Sv32 mode of RISC-V's paging mechanism, which uses a two-level page table. The 32-bit virtual address is divided into a first-level page table index (`VPN[1]`), a second-level index (`VPN[0]`), and a page offset.

This is easier to understand by looking at some example values:

| Virtual Address | `VPN[1]` (10 bits) | `VPN[0]` (10 bits) | Offset (12 bits) |
|-----------------|---------------------|---------------------|-------------------|
| 0x1000_0000     | 0x040               | 0x000               | 0x000             |
| 0x1000_0000     | 0x040               | 0x000               | 0x000             |
| 0x1000_1000     | 0x040               | 0x001               | 0x000             |
| 0x1000_f000     | 0x040               | 0x00f               | 0x000             |
| 0x2000_f0ab     | 0x080               | 0x00f               | 0x0ab             |
| 0x2000_f012     | 0x080               | 0x00f               | 0x012             |
| 0x2000_f034     | 0x080               | 0x00f               | 0x045             |

> [!TIP]
>
> Upon close observation, you may notice:
>
> - Changing the middle bits (`VPN[0]`) doesn't affect the first-level index. This means page table entries for nearby addresses are concentrated in the same second-level page table.
> - Changing the lower bits doesn't affect either `VPN[1]` or `VPN[0]`. This means addresses within the same 4KB page reference the same page table entry.

This structure utilizes the principle of locality of reference, allowing for smaller page table sizes and more effective use of the Translation Lookaside Buffer (TLB).

When accessing memory, the CPU uses `VPN[1]` and `VPN[0]` to identify the corresponding page table entry, then adds the physical address from that entry to the `offset` to calculate the final physical address to access.

## Constructing the page table

Let's construct a page table using the Sv32 method. First, we'll define some macros. `SATP_SV32` is a bit in the `satp` register that indicates "enable paging in Sv32 mode", and `PAGE_*` are bits to be set in page table entries.

```c:kernel.h
#define SATP_SV32 (1u << 31)
#define PAGE_V    (1 << 0)   // "Valid" bit (entry is enabled)
#define PAGE_R    (1 << 1)   // Readable
#define PAGE_W    (1 << 2)   // Writable
#define PAGE_X    (1 << 3)   // Executable
#define PAGE_U    (1 << 4)   // User (accessible in user mode)
```


## Mapping pages

The following `map_page` function takes the first-level page table (`table1`), the virtual address to be mapped (`vaddr`), the physical address to map to (`paddr`), and the flags to set in the page table entry (`flags`):

```c:kernel.c
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

The function simply prepares the second-level page table, and sets the physical page number and flags for the desired page table entry in the second level.

It divides `paddr` by `PAGE_SIZE` because the entry should contain the physical page number, not the physical address itself.

## カーネルページのマッピング

ページテーブルにはアプリケーション (ユーザー空間) のマッピングだけでなく、カーネルのそれも設定する必要があります。

本書では、カーネルのマッピングは、カーネルの仮想アドレスと物理アドレスが一致するように設定します。こうすることで、ページングを有効化しても同じコードを引き続き実行できるようになります。

まずはカーネルのリンカスクリプトの修正です。カーネルが利用するアドレスの先頭 (`__kernel_base`) を定義します。

```plain:kernel.ld {5}
ENTRY(boot)

SECTIONS {
    . = 0x80200000;
    __kernel_base = .;
```

> [!WARNING]
>
> `. = 0x80200000`の**後ろ**に定義するよう注意してください。順番が逆だと、`__kernel_base`の値がゼロになります。

次にプロセス管理構造体にページテーブルを追加します。1段目のページテーブルを指すポインタです。

```c:kernel.h {5}
struct process {
    int pid;
    int state;
    vaddr_t sp;
    uint32_t *page_table;
    uint8_t stack[8192];
};
```

最後に、`create_process`関数でカーネルのページをマッピングします。カーネルのページは、`__kernel_base`から`__free_ram_end`までの範囲です。こうすることで、静的に配置される領域 (`.text`など) と、`alloc_pages`関数で動的に割り当てられる領域の両方を、カーネルはいつでもアクセスできるようにしておきます。

```c:kernel.c {1,6-11,16}
extern char __kernel_base[];

struct process *create_process(uint32_t pc) {
    /* 省略 */

    uint32_t *page_table = (uint32_t *) alloc_pages(1);

    // カーネルのページをマッピングする
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

## ページテーブルの切り替え

最後に、プロセスの切り替え時にページテーブルを切り替えるようにします。

```c:kernel.c {5-7,10-11}
void yield(void) {
    /* 省略 */

    __asm__ __volatile__(
        "sfence.vma\n"
        "csrw satp, %[satp]\n"
        "sfence.vma\n"
        "csrw sscratch, %[sscratch]\n"
        :
        // 行末のカンマを忘れずに！
        : [satp] "r" (SATP_SV32 | ((uint32_t) next->page_table / PAGE_SIZE)),
          [sscratch] "r" ((uint32_t) &next->stack[sizeof(next->stack)])
    );

    switch_context(&prev->sp, &next->sp);
}
```

`satp`レジスタへの一段目のページテーブルを設定することで、ページテーブルを切り替えることができます。なお、物理ページ番号を指定するので、`PAGE_SIZE`で割っています。

ページテーブルの設定の前後に追加されている `sfence.vma` 命令は、「ページテーブルへの変更をきちんと完了させることを保証する (参考: メモリフェンス)」「ページテーブルエントリのキャッシュ (TLB) を消す」という意味合いがあります。

> [!TIP]
>
> ブート時には、ページングが無効化されています (`satp`レジスタが設定されていない)。そのため、仮想アドレスと物理アドレスが一致しているかような挙動になります。

## ページングのテスト

ページングを一通り実装したところで、実際に動かしてみましょう。

```plain
$ ./run.sh

starting process A
Astarting process B
BABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABABAB
```

表示内容は前章と全く同じです。ページングを有効化しても変化はありません。そこで、ページテーブルを上手く設定できているかをQEMUモニタを使って見てみましょう。

## ページテーブルの内容を確認する


仮想アドレス`0x80000000`周辺が、どうマップされているかを見てみましょう。正しく設定されていれば、`(仮想アドレス) == (物理アドレス)`になるようにマップされているはずです。

```plain
QEMU 8.0.2 monitor - type 'help' for more information
(qemu) stop
(qemu) info registers
 ...
 satp     80080253
 ...
```

まず、`satp`レジスタの値を見てみると`0x80080253`になっています。これを仕様書の通り解釈すると `0x80080253 & 0x3fffff) * 4096 = 0x80253000` がページテーブルの一段目の先頭物理アドレスです。

では、ページテーブルの中身を見てみましょう。QEMUにはメモリの内容 (メモリダンプ) を表示するコマンドを用意しています。`xp`コマンドを使うと指定した物理アドレスのメモリダンプを表示できます。`/x`は16進数で表示することを意味します。`/1024x`のように`x`の前に数字を書くと、その分表示してくれます。

> [!TIP]
>
> `xp`ではなく`x`コマンドを使うと、指定した**仮想**アドレスのメモリダンプを確認できます。後に設定するユーザー空間 (アプリケーション) では、カーネル空間とは違い仮想アドレスと物理アドレスが同一にならないため、ユーザー空間のメモリ内容を調べたいときに便利です。

ここでは仮想アドレス`0x80000000`に紐づく2段目のページテーブルが知りたいので、`0x80000000 >> 22 = 512`番目のエントリを見てみます。1エントリ4バイトなので、4をかけています。

```plain
(qemu) xp /x 0x80253000+512*4
0000000080253800: 0x20095001
```

1列目が物理アドレス、2列目以降がメモリの値です。ゼロではないので、なんらかの値が設定されていることがわかります。これを仕様書の通り解釈すると、`(0x20095000 >> 10) * 4096 = 0x80254000` に2段目のページテーブルがあることがわかります。2段目のテーブル全体 (1024エントリ) をみてみましょう。

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

最初の方はゼロで埋まっていますが、`(0x800バイト目) / 4 = 512エントリ目`から値が埋まっています。`512 << 12 = 0x200000`であるため、2段目は`0x200000`バイト目の部分からマップされていることがわかります。

ここまでメモリダンプを自力で読んでみましたが、QEMUには使用中のページテーブルの設定情報を読みやすい形で表示するコマンドがあります。正しくマップされているかを最終確認したい場合は`info mem`コマンドを使うとよいでしょう。

```plain
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

1列目から順に、仮想アドレス、物理アドレス (マップ先)、サイズ (16進バイト数)、属性を表しています。属性は`r`が読み込み可能、`w`が書き込み可能、`x`が実行可能を表します。また、`a`と`d`は、それぞれCPUが「ページにアクセスしたことがある」、「ページに書き込みしたことがある」ことを表しています。実際に使われているページをOSが把握するための補助的な情報です。

> [!TIP]
>
> ページテーブルの設定ミスは、初学者にとってはデバッグがかなり難しい部分です。もし上手く動かない場合は、「付録: ページングのデバッグ」を参照してください。

## ページングのデバッグ (おまけ)

ページテーブルの設定は難易度が少し高く、ミスに気づきにくいものです。そこで、本章ではよくあるページングのミスを題材に、どうデバッグできるかを見ていきます。

### モードの設定し忘れ

まずは`satp`レジスタにモードを設定し忘れた場合です。次のように抜いてみましょう。

```c:kernel.c {6}
    __asm__ __volatile__(
        "sfence.vma\n"
        "csrw satp, %[satp]\n"
        "sfence.vma\n"
        :
        : [satp] "r" (((uint32_t) next->page_table / PAGE_SIZE)) // SATP_SV32を忘れた
    );
```

実際に動かしてみると、きちんと動作しているように見えるはずです。これは、ページテーブルの場所が指定されてはいますがページングが無効化されているままだからです。この場合、QEMUモニタの`info mem`コマンドで確認すると、次のように表示されます。

```
(qemu) info mem
No translation or protection
```

### 物理ページ番号ではなく物理アドレスを指定している

次に物理ページ番号ではなく物理アドレスでページテーブルを指定してしまった時です。

```c:kernel.c {6}
    __asm__ __volatile__(
        "sfence.vma\n"
        "csrw satp, %[satp]\n"
        "sfence.vma\n"
        :
        : [satp] "r" (SATP_SV32 | ((uint32_t) next->page_table)) // シフトし忘れ
    );
```

OSを起動して`info mem`コマンドで確認すると、次のように空っぽになっているはずです。

```plain
$ ./run.sh

QEMU 8.0.2 monitor - type 'help' for more information
(qemu) stop
(qemu) info mem
vaddr    paddr            size     attr
-------- ---------------- -------- -------
```

レジスタダンプを見て、CPUが何をしているのかを確認しましょう。

```plain
(qemu) info registers

CPU#0
 V      =   0
 pc       80200188
 ...
 scause   0000000c
 ...
```

`80200188`を`llvm-addr2line`で確認すると例外ハンドラの先頭アドレス、例外の発生理由 (`scause`レジスタ) は仕様書によると「Instruction page fault」に該当します。ページテーブルを切り替えて次の命令を実行するとき、CPUはページテーブルからプログラムカウンタの仮想アドレスを物理アドレスに変換しようとします。しかし、ページテーブルのアドレス (`satp`レジスタ) が正しくないため、変換に失敗し、ページフォルトが発生しています。

より具体的に何が起きているのかをQEMUのログから見てみましょう。

```bash:run.sh {2}
$QEMU -machine virt -bios default -nographic -serial mon:stdio --no-reboot \
    -d unimp,guest_errors,int,cpu_reset -D qemu.log \
    -kernel kernel.elf
```

```plain
Invalid read at addr 0x253000800, size 4, region '(null)', reason: rejected
riscv_cpu_do_interrupt: hart:0, async:0, cause:0000000c, epc:0x80200580, tval:0x80200580, desc=exec_page_fault
Invalid read at addr 0x253000800, size 4, region '(null)', reason: rejected
riscv_cpu_do_interrupt: hart:0, async:0, cause:0000000c, epc:0x80200188, tval:0x80200188, desc=exec_page_fault
Invalid read at addr 0x253000800, size 4, region '(null)', reason: rejected
riscv_cpu_do_interrupt: hart:0, async:0, cause:0000000c, epc:0x80200188, tval:0x80200188, desc=exec_page_fault
```

- 最初のページフォルトの例外発生箇所 (`epc`レジスタ) の値は`0x80200580`で、`llvm-objdump`で確認すると`satp`レジスタを設定した直後の命令を指している。つまり、ページングを有効化した直後にページフォルトが発生している。
- 2番目以降のページフォルトはみな同じ値が続いていく。例外発生箇所は`0x80200188`で、`llvm-objdump`で確認すると例外ハンドラの先頭アドレスを指している。このログが無限に続いていくことから、例外ハンドラを実行しようとして再度例外 (ページフォルト) が発生していることがわかる。
- `info registers`コマンドを見ると、`satp`レジスタの値は`0x80253000`で、仕様書に従って物理アドレスを計算すると `(0x80253000 & 0x3fffff) * 4096 = 0x253000000` で、これは32ビットのアドレス空間に収まらない。このことから、異常な値が入っていることがわかる。

このように、QEMUのログとレジスタダンプ、メモリダンプを確認していきながら、どこがおかしいのかを探していくことができます。ただし、デバッグで最も大事なことは「仕様書をしっかり読む」ことです。「仕様書の記述を見落としていた・勘違いしていた」ということが大変よくあります。
