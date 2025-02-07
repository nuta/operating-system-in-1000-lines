---
title: ファイルシステム
---

# ファイルシステム

ディスクの読み書きができるようになったので、ファイルの読み書きを実装しましょう。

## tarファイルシステム

本書では、ちょっぴり面白いアプローチでファイルシステムを実装します。それは「tarファイルをファイルシステムとして使う」というものです。

tarファイルは、複数のファイルをまとめるアーカイブファイルです。tarファイルの中には、ファイルの内容とファイル名、作成日時などファイルシステムとして必要な情報が含まれています。FATやext2などの一般的なファイルシステム形式に比べ非常に簡素なデータ構造であるのと、馴染み深いであろうtarコマンドを使ってファイルシステムイメージを操作できるので、教育用にはもってこいのファイル形式なのです。

> [!TIP]
>
> 今でこそtarは「ディレクトリをZIPみたいにまとめるやつ」として使われていますが、元々は [磁気テープ](https://ja.wikipedia.org/wiki/%E7%A3%81%E6%B0%97%E3%83%86%E3%83%BC%E3%83%97) のための、ある意味でファイルシステムとして誕生しました。ただしFATなどとは異なり、ランダムアクセスには向かないデータ構造であることを実装していくにつれ理解できると思います。

## ディスクイメージの作成

まずはファイルシステムの内容を用意しましょう。`disk`ディレクトリを作成し、その中に適当なファイルを作成します。一つは`hello.txt`という名前にしておきます。

```
$ mkdir disk
$ vim disk/hello.txt
$ vim disk/meow.txt
```

ビルドスクリプトにtarファイルの作成コマンドを追加し、それをディスクイメージとしてQEMUに渡すようにします。

```bash [run.sh] {1,5}
(cd disk && tar cf ../disk.tar --format=ustar *.txt)

$QEMU -machine virt -bios default -nographic -serial mon:stdio --no-reboot \
    -d unimp,guest_errors,int,cpu_reset -D qemu.log \
    -drive id=drive0,file=disk.tar,format=raw,if=none \
    -device virtio-blk-device,drive=drive0,bus=virtio-mmio-bus.0 \
    -kernel kernel.elf
```

ここで使われている`tar`コマンドのオプションは次のとおりです:

- `cf`: tarファイルを作成する
- `--format=ustar`: ustar形式のtarファイルを作成する

なお、tarコマンドの行が丸括弧 `(...)` で囲われているのは、囲われたコマンドを独立したシェルで実行する **サブシェル** という機能です。これを使うことで、`cd` コマンドでのディレクトリ移動が括弧外に影響しないようにできます。

## tarファイルの構造

tarファイルは、次のような構造をしています。

```
+----------------+
|   tar ヘッダ    |
+----------------+
|  ファイルデータ  |
+----------------+
|   tar ヘッダ    |
+----------------+
|  ファイルデータ  |
+----------------+
|      ...       |
```

つまり、「tarヘッダ」と「ファイルデータ」のペアがファイルの数だけ続いたものがtarファイルです。tarにはいくつかの種類がありますが、本書では **ustar形式** ([Wikipedia](<https://en.wikipedia.org/wiki/Tar_(computing)#UStar_format>)) を使います。

今回は、このファイル構造をそのままファイルシステムのデータ構造として利用します。

## ファイルシステムの読み込み

まずはファイルシステム関連のデータ構造を定義します。`kernel.h`に次のように定義します。

```c [kernel.h]
#define FILES_MAX      2
#define DISK_MAX_SIZE  align_up(sizeof(struct file) * FILES_MAX, SECTOR_SIZE)

struct tar_header {
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char checksum[8];
    char type;
    char linkname[100];
    char magic[6];
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
    char padding[12];
    char data[];      // ヘッダに続くデータ領域を指す配列 (フレキシブル配列メンバ)
} __attribute__((packed));

struct file {
    bool in_use;      // このファイルエントリが使われているか
    char name[100];   // ファイル名
    char data[1024];  // ファイルの内容
    size_t size;      // ファイルサイズ
};
```

本書のファイルシステム実装では、全てのファイルを起動時にディスクからメモリへ読み込みます。各ファイルのtarヘッダ (`struct tar_header`) と、それに続くファイルの内容を`file`構造体へ読み込みます。`FILES_MAX`が読み込む最大ファイル数、`DISK_MAX_SIZE`がディスクイメージの最大サイズです。

実際にファイルを読み込む処理が、次の`fs_init`関数です。

```c [kernel.c]
struct file files[FILES_MAX];
uint8_t disk[DISK_MAX_SIZE];

int oct2int(char *oct, int len) {
    int dec = 0;
    for (int i = 0; i < len; i++) {
        if (oct[i] < '0' || oct[i] > '7')
            break;

        dec = dec * 8 + (oct[i] - '0');
    }
    return dec;
}

void fs_init(void) {
    for (unsigned sector = 0; sector < sizeof(disk) / SECTOR_SIZE; sector++)
        read_write_disk(&disk[sector * SECTOR_SIZE], sector, false);

    unsigned off = 0;
    for (int i = 0; i < FILES_MAX; i++) {
        struct tar_header *header = (struct tar_header *) &disk[off];
        if (header->name[0] == '\0')
            break;

        if (strcmp(header->magic, "ustar") != 0)
            PANIC("invalid tar header: magic=\"%s\"", header->magic);

        int filesz = oct2int(header->size, sizeof(header->size));
        struct file *file = &files[i];
        file->in_use = true;
        strcpy(file->name, header->name);
        memcpy(file->data, header->data, filesz);
        file->size = filesz;
        printf("file: %s, size=%d\n", file->name, file->size);

        off += align_up(sizeof(struct tar_header) + filesz, SECTOR_SIZE);
    }
}
```

この関数では、まず`read_write_disk`関数を使ってディスクイメージをメモリ上 (`disk`変数) に読み込みます。`disk`変数はローカル変数 (スタック上) ではなく、わざと静的変数 (`static`) として宣言しています。スタックの大きさには限りがあるので、このようなデータ領域はなるべくスタックの利用を避けることが望ましいです。

ディスクの内容を読み込んだあとは、それをtarファイルと同じように順番に`files`変数のエントリとしてコピーしていきます。注意点として **tarヘッダの数値は8進数表記** です。`oct2int`関数で、8進数表記の文字列を整数に変換しています。

最後に、`fs_init`関数を`kernel_main`関数から呼び出すようにして完了です。

```c [kernel.c] {5}
void kernel_main(void) {
    memset(__bss, 0, (size_t) __bss_end - (size_t) __bss);
    WRITE_CSR(stvec, (uint32_t) kernel_entry);
    virtio_blk_init();
    fs_init();

    /* 省略 */
}
```

## ファイルシステムの読み込みテスト

実際に動かしてみましょう。`disk`ディレクトリに用意したファイル名とその大きさが表示されれば成功です。

```
$ ./run.sh

virtio-blk: capacity is 2560 bytes
file: world.txt, size=0
file: hello.txt, size=22
```

## ディスクへの書き込み戻し

ファイルシステムを読み込めるようになったので、次はファイルの書き込みを実装しましょう。ファイルの書き込みは、`files`変数の内容を、tarファイルの形式でディスクに書き込むことで実現します。

```c [kernel.c]
void fs_flush(void) {
    // files変数の各ファイルの内容をdisk変数に書き込む
    memset(disk, 0, sizeof(disk));
    unsigned off = 0;
    for (int file_i = 0; file_i < FILES_MAX; file_i++) {
        struct file *file = &files[file_i];
        if (!file->in_use)
            continue;

        struct tar_header *header = (struct tar_header *) &disk[off];
        memset(header, 0, sizeof(*header));
        strcpy(header->name, file->name);
        strcpy(header->mode, "000644");
        strcpy(header->magic, "ustar");
        strcpy(header->version, "00");
        header->type = '0';

        // ファイルサイズを8進数文字列に変換
        int filesz = file->size;
        for (int i = sizeof(header->size); i > 0; i--) {
            header->size[i - 1] = (filesz % 8) + '0';
            filesz /= 8;
        }

        // チェックサムを計算
        int checksum = ' ' * sizeof(header->checksum);
        for (unsigned i = 0; i < sizeof(struct tar_header); i++)
            checksum += (unsigned char) disk[off + i];

        for (int i = 5; i >= 0; i--) {
            header->checksum[i] = (checksum % 8) + '0';
            checksum /= 8;
        }

        // ファイルデータをコピー
        memcpy(header->data, file->data, file->size);
        off += align_up(sizeof(struct tar_header) + file->size, SECTOR_SIZE);
    }

    // disk変数の内容をディスクに書き込む
    for (unsigned sector = 0; sector < sizeof(disk) / SECTOR_SIZE; sector++)
        read_write_disk(&disk[sector * SECTOR_SIZE], sector, true);

    printf("wrote %d bytes to disk\n", sizeof(disk));
}
```

この関数では、まず`files`変数の内容をtarファイル形式で`disk`変数に書き込み、その後`disk`変数の内容をディスクに書き込みます。tarヘッダの各フィールドの値は8進数の文字列であるため、`strcpy`関数など文字列を扱う処理がみられます。

## ファイルの読み書きAPI

ファイルシステムの読み書きを実装したところで、アプリケーションからファイルの読み書きを行えるようにしましょう。本書ではファイルの読み込みを行う`readfile`、ファイルの書き込みを行う`writefile`というシステムコールを用意します。どちらもファイル名、読み書きに使うメモリバッファ、そしてバッファのサイズを引数に取ります。

```c [common.h]
#define SYS_READFILE  4
#define SYS_WRITEFILE 5
```

```c [user.c]
int readfile(const char *filename, char *buf, int len) {
    return syscall(SYS_READFILE, (int) filename, (int) buf, len);
}

int writefile(const char *filename, const char *buf, int len) {
    return syscall(SYS_WRITEFILE, (int) filename, (int) buf, len);
}
```

```c [user.h]
int readfile(const char *filename, char *buf, int len);
int writefile(const char *filename, const char *buf, int len);
```

> [!TIP]
>
> 一般的なOSのシステムコールの設計を読んでみて、何が省略されているのかを比較すると面白いでしょう。

## システムコールの実装

前節で定義したシステムコールを実装しましょう。

```c [kernel.c] {1-9,14-39}
struct file *fs_lookup(const char *filename) {
    for (int i = 0; i < FILES_MAX; i++) {
        struct file *file = &files[i];
        if (!strcmp(file->name, filename))
            return file;
    }

    return NULL;
}

void handle_syscall(struct trap_frame *f) {
    switch (f->a3) {
        /* 省略 */
        case SYS_READFILE:
        case SYS_WRITEFILE: {
            const char *filename = (const char *) f->a0;
            char *buf = (char *) f->a1;
            int len = f->a2;
            struct file *file = fs_lookup(filename);
            if (!file) {
                printf("file not found: %s\n", filename);
                f->a0 = -1;
                break;
            }

            if (len > (int) sizeof(file->data))
                len = file->size;

            if (f->a3 == SYS_WRITEFILE) {
                memcpy(file->data, buf, len);
                file->size = len;
                fs_flush();
            } else {
                memcpy(buf, file->data, len);
            }

            f->a0 = len;
            break;
        }
        default:
            PANIC("unexpected syscall a3=%x\n", f->a3);
    }
}
```

ファイルの読み書き処理は共通する処理が多いので、同じところにまとめています。`fs_lookup`関数でファイル名から`files`変数のエントリを探し出し、読み込みであれば、ファイルエントリからデータを読み込み、書き込みであればファイルエントリの内容を書き換え、最後に`fs_flush`関数でディスクに書き込みます。

> [!WARNING]
>
> 簡単のため、アプリケーションから渡されたポインタ (ユーザーポインタ) をそのまま参照していますが、これはセキュリティ上の問題があります。ユーザーが任意のメモリ領域を指定できてしまうと、システムコール経由でカーネルのメモリ領域を読み書きできてしまいます。

## ファイルの読み書きコマンド

システムコールを実装したところで、シェルからファイルの読み書きを試してみましょう。シェルはコマンドライン引数のパースを実装していないので、とりあえず`hello.txt`を決めうちで読み書きする`readfile`と`writefile`コマンドを実装します。

```c [shell.c]
        else if (strcmp(cmdline, "readfile") == 0) {
            char buf[128];
            int len = readfile("hello.txt", buf, sizeof(buf));
            buf[len] = '\0';
            printf("%s\n", buf);
        }
        else if (strcmp(cmdline, "writefile") == 0)
            writefile("hello.txt", "Hello from shell!\n", 19);
```

実行してみると、次のようにページフォルトが発生してしまいます。

```
$ ./run.sh

> readfile
PANIC: kernel.c:561: unexpected trap scause=0000000d, stval=01000423, sepc=8020128a
```

`sepc`の値を`llvm-addr2line`で見てみると、`strcmp`関数でページフォルトが発生していることがわかります。

```
$ llvm-objdump -d kernel.elf
...

80201282 <strcmp>:
80201282: 03 46 05 00   lbu     a2, 0(a0)
80201286: 15 c2         beqz    a2, 0x802012aa <.LBB3_4>
80201288: 05 05         addi    a0, a0, 1

8020128a <.LBB3_2>:
8020128a: 83 c6 05 00   lbu     a3, 0(a1) ← ここでページフォルト: a1は第2引数
8020128e: 33 37 d0 00   snez    a4, a3
80201292: 93 77 f6 0f   andi    a5, a2, 255
80201296: bd 8e         xor     a3, a3, a5
80201298: 93 b6 16 00   seqz    a3, a3
```

ページテーブルの内容を確認してみると、`0x1000423`のページ (`vaddr = 01000000`) は確かに読み・書き・実行可能 (`rwx`) なユーザーページ (`u`) としてマップされています。

```
QEMU 8.0.2 monitor - type 'help' for more information
(qemu) info mem
vaddr    paddr            size     attr
-------- ---------------- -------- -------
01000000 000000008026c000 00001000 rwxu-a-
```

試しに仮想アドレスでメモリダンプ (`x`コマンド) をしてみましょう。

```
(qemu) x /10c 0x1000423
01000423: 'h' 'e' 'l' 'l' 'o' '.' 't' 'x' 't' '\x00' 'r' 'e' 'a' 'd' 'f' 'i'
01000433: 'l' 'e' '\x00' 'h' 'e' 'l' 'l' 'o' '\x00' '%' 's' '\n' '\x00' 'e' 'x' 'i'
01000443: 't' '\x00' 'w' 'r' 'i' 't' 'e' 'f'
```

ページテーブルの設定が正しくない場合、`x`コマンドはエラーを表示します。ここでは、ページテーブルが正しく設定されており、ポインタは確かに`hello.txt`の文字列を指していることがわかります。

答えを言ってしまうと「`sstatus`レジスタの`SUM`ビットがセットされていない」ことが原因です。

## ユーザーポインタへのアクセス

RISC-Vでは、`sstatus`レジスタによってS-Mode (カーネル) の振る舞いを変更できます。その中の一つが **SUM (permit Supervisor User Memory access) ビット** です。これがセットされていない場合、S-Modeのプログラム (カーネル) はU-Mode (ユーザー) のページにアクセスできません。

> [!TIP]
>
> 意図せずユーザーのメモリ領域を参照しないようにする、一種の安全策です。
> ちなみにIntelのCPUにも「SMAP (Supervisor Mode Access Prevention)」という名前で実装されています。

`SUM`ビットの位置を次のように定義します。

```c [kernel.h]
#define SSTATUS_SUM  (1 << 18)
```

あとはユーザー空間に入る時に`sstatus`レジスタにセットすれば修正完了です。

```c [kernel.c] {8}
__attribute__((naked)) void user_entry(void) {
    __asm__ __volatile__(
        "csrw sepc, %[sepc]\n"
        "csrw sstatus, %[sstatus]\n"
        "sret\n"
        :
        : [sepc] "r" (USER_BASE),
          [sstatus] "r" (SSTATUS_SPIE | SSTATUS_SUM)
    );
}
```

> [!TIP]
>
> ここでは「SUMビットが原因」とさらっと説明していますが「自分でこれを見つけられるか？」というのは難しい問題です。ページフォルトが起きていることは分かっても、その具体的な原因は分からないことがほとんどです。CPUは困ったことに細かいエラーコードすら出してくれないのです。筆者がなぜ気づいたかというと「SUMビットを知っていたから」です。
>
> このような「上手く動かない」場合のデバッグ方法は次のようなものがあります。
>
> - RISC-Vの仕様書をよく読む。「SUMビットが立っていると、S-ModeでもU-Mode用ページにアクセスできる」と一応書いてある。
> - QEMU本体の実装を読む。前述のページフォルトの原因は[ココで実装されている](https://github.com/qemu/qemu/blob/d1181d29370a4318a9f11ea92065bea6bb159f83/target/riscv/cpu_helper.c#L1008)。ただし仕様書をよく読むのと同等かそれ以上に大変。
> - ChatGPTとかに上手く聞き出す ([成功例](https://sdk.vercel.ai/r/H0gm2Ky))。
>
> これが「ゼロからOSを作る」のが時間泥棒で挫折しやすい大きな理由のひとつです。ただ、辛い分だけ解決した時の達成感は他のソフトウェア開発では味わえないものがあります。辛い思いをするのがゼロからのOS自作の醍醐味とも言えるでしょう。

## ファイルの読み書きテスト

`SUM`ビットをセットしたところで、ファイルの読み書きを試してみましょう。次のように`hello.txt`に書き込んでおいた文章が表示されたら成功です。

```
$ ./run.sh

> readfile
Can you see me? Ah, there you are! You've unlocked the achievement "Virtio Newbie!"
```

ファイルの書き込みも試してみましょう。書き込みが成功すると、次のように書き込んだバイト数が表示されます。

```
> writefile
wrote 2560 bytes to disk
```

QEMUを終了して、`disk.tar`を展開してみましょう。`disk.tar`を`virtio-blk`のディスクイメージとして指定しているので、ディスクへの書き込みがあり次第、QEMUがそのファイルを更新します。ファイルシステムとvirtio-blkを正しく実装できていれば、`writefile`システムコールで書き込んだ文章が表示されます。

```
$ mkdir tmp
$ cd tmp
$ tar xf ../disk.tar
$ ls -alh
total 4.0K
drwxr-xr-x  4 seiya staff 128 Jul 22 22:50 .
drwxr-xr-x 25 seiya staff 800 Jul 22 22:49 ..
-rw-r--r--  1 seiya staff  26 Jan  1  1970 hello.txt
-rw-r--r--  1 seiya staff   0 Jan  1  1970 meow.txt
$ cat hello.txt
Hello from shell!
```

これでOSの基本機能である「ファイルシステム」を手に入れました！
