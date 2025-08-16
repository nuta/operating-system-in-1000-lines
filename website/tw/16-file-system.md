# 檔案系統（File System）

你到目前為止的表現非常棒！你已經實作了行程、shell、記憶體管理與磁碟驅動器。現在，讓我們來完成一個真正的作業系統的最後一塊拼圖：檔案系統。

## 使用 tar 作為檔案系統

在本書中，我們將採取一種有趣的方式來實作檔案系統：使用 tar 檔作為我們的檔案系統格式。

Tar 是一種可以包含多個檔案的壓縮封裝格式，它包含了檔案的內容、檔名、建立日期等資訊，這些正好就是一個檔案系統所需要管理的東西。與 FAT 或 ext2 這類常見檔案系統相比，tar 的資料結構要簡單得多。此外，我們還可以直接使用熟悉的 tar 指令來操作這個檔案系統映像。對於教育用途來說，這樣是不是很理想呢？

> [!TIP]
>
> 現在的 tar 常被當作 ZIP 的替代品來用，但其實它最初是為了磁帶儲存裝置（磁帶機）而設計的一種檔案系統格式。雖然我們在這一章中將它用作類似檔案系統的用途，但你會發現 tar 並不適合隨機存取（random access）。如果有興趣的話，建議閱讀 [FAT 檔案系統的設計](https://en.wikipedia.org/wiki/Design_of_the_FAT_file_system)。

## 建立磁碟映像檔（tar 檔）

我們首先需要準備好檔案系統的內容。請建立一個名為 `disk` 的資料夾，並在其中加入一些檔案。其中一個可以命名為 `hello.txt`。

```
$ mkdir disk
$ vim disk/hello.txt
$ vim disk/meow.txt
```

在建置腳本中加入一條指令用來建立一個 tar 檔案並將它作為磁碟映像傳遞給 QEMU：

```bash [run.sh] {1,5}
(cd disk && tar cf ../disk.tar --format=ustar *.txt)                          # new

$QEMU -machine virt -bios default -nographic -serial mon:stdio --no-reboot \
    -d unimp,guest_errors,int,cpu_reset -D qemu.log \
    -drive id=drive0,file=disk.tar,format=raw,if=none \                         # modified
    -device virtio-blk-device,drive=drive0,bus=virtio-mmio-bus.0 \
    -kernel kernel.elf
```

這裡使用的 `tar` 指令選項如下：

- `cf`：建立 tar 檔案。
- `--format=ustar`：以 ustar 格式建立。

> [!TIP]
>
> `(...)` 括號會建立一個子殼層（subshell），這樣 `cd` 指令的作用範圍只會在括號內，不會影響腳本其他部分的工作目錄。

## Tar 檔案結構

tar 檔案具有以下結構：

```
+----------------+
|   tar header   |
+----------------+
|   file data    |
+----------------+
|   tar header   |
+----------------+
|   file data    |
+----------------+
|      ...       |
```

總結來說，tar 檔基本上是一系列的「tar 標頭」與「檔案資料」配對，每個檔案對應一組這樣的配對。tar 格式有幾種類型，但我們將使用「ustar 格式」（參見[維基百科](<https://en.wikipedia.org/wiki/Tar_(computing)#UStar_format>)）。

我們將使用這種檔案結構作為我們檔案系統的資料結構。將這個與真實的檔案系統相比，會非常有趣且具有教育意義。

## 讀取檔案系統

首先，在 `kernel.h` 中定義與 tar 檔案系統相關的資料結構：

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
    char data[];      // Array pointing to the data area following the header
                      // (flexible array member)
} __attribute__((packed));

struct file {
    bool in_use;      // Indicates if this file entry is in use
    char name[100];   // File name
    char data[1024];  // File content
    size_t size;      // File size
};
```

在我們的檔案系統實作中，所有檔案會在開機時從磁碟讀入記憶體。`FILES_MAX` 定義了可載入的檔案最大數量，而 `DISK_MAX_SIZE` 指定了磁碟映像檔的最大大小。

接下來，我們來在 `kernel.c` 中將整個磁碟內容讀入記憶體：

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

在這個函式中，我們首先使用 `read_write_disk` 函式將磁碟映像檔載入到一個暫存緩衝區（`disk` 變數）中。`disk` 變數被宣告為 static 變數，而不是區域（stack）變數。這是因為堆疊的大小有限，盡量避免用來存放大型資料區塊。

在載入磁碟內容後，我們依序將資料複製到 `files` 陣列的每個項目中。請注意，**tar 標頭中的數字是八進位格式**。這非常容易混淆，因為它看起來像是十進位。`oct2int` 函式會將這些八進位字串值轉換成整數。

最後，請確認在 `kernel_main` 中初始化 virtio-blk 裝置（`virtio_blk_init`）之後，呼叫 `fs_init` 函式。

```c [kernel.c] {5}
void kernel_main(void) {
    memset(__bss, 0, (size_t) __bss_end - (size_t) __bss);
    WRITE_CSR(stvec, (uint32_t) kernel_entry);
    virtio_blk_init();
    fs_init();

    /* omitted */
}
```

## 測試檔案讀取功能

我們來試試看！它應該會列印出 `disk` 目錄中每個檔案的名稱與大小：

```
$ ./run.sh

virtio-blk: capacity is 2560 bytes
file: world.txt, size=0
file: hello.txt, size=22
```

## 寫入磁碟

可以透過將 `files` 變數的內容以 tar 檔案格式寫回磁碟，來實作檔案寫入功能。

```c [kernel.c]
void fs_flush(void) {
    // Copy all file contents into `disk` buffer.
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

        // Turn the file size into an octal string.
        int filesz = file->size;
        for (int i = sizeof(header->size); i > 0; i--) {
            header->size[i - 1] = (filesz % 8) + '0';
            filesz /= 8;
        }

        // Calculate the checksum.
        int checksum = ' ' * sizeof(header->checksum);
        for (unsigned i = 0; i < sizeof(struct tar_header); i++)
            checksum += (unsigned char) disk[off + i];

        for (int i = 5; i >= 0; i--) {
            header->checksum[i] = (checksum % 8) + '0';
            checksum /= 8;
        }

        // Copy file data.
        memcpy(header->data, file->data, file->size);
        off += align_up(sizeof(struct tar_header) + file->size, SECTOR_SIZE);
    }

    // Write `disk` buffer into the virtio-blk.
    for (unsigned sector = 0; sector < sizeof(disk) / SECTOR_SIZE; sector++)
        read_write_disk(&disk[sector * SECTOR_SIZE], sector, true);

    printf("wrote %d bytes to disk\n", sizeof(disk));
}
```

在這個函式中，會先在 `disk` 變數中建立 tar 檔案，然後透過 `read_write_disk` 函式將其寫入磁碟。是不是很簡單？

## 設計檔案讀寫的系統呼叫

既然我們已經實作了檔案系統的讀寫操作，接下來讓應用程式也能讀取與寫入檔案。我們會提供兩個系統呼叫：`readfile` 用於讀取檔案，`writefile` 用於寫入檔案。這兩個系統呼叫都會接受以下參數：檔案名稱、一段記憶體緩衝區（用於讀取或寫入），以及緩衝區的大小。

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
> 與其比較一般作業系統中系統呼叫的設計，了解這裡省略了哪些部分，是件很有趣的事。舉例來說，為什麼 Linux 中的 `read(2)` 和 `write(2)` 系統呼叫使用的是檔案描述符，而不是檔案名稱？

## 實作系統呼叫

讓我們來實作上一節中定義的系統呼叫。

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
        /* omitted */
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

檔案的讀取與寫入操作大致相同，因此它們被放在同一個地方一起處理。`fs_lookup` 函式會根據檔名在 `files` 變數中搜尋對應的項目。對於讀取操作，它會從檔案項目中讀出資料；而在寫入時，則會修改該項目的內容。最後，`fs_flush` 函式會將資料寫回磁碟中。

> [!WARNING]
>
> 為了簡化實作，我們直接引用了來自應用程式的指標（即「user pointers」），但這樣做會產生安全性問題。如果使用者可以任意指定記憶體位置，他們可能會透過系統呼叫讀取或修改核心的記憶體區域。

## 檔案讀寫指令

讓我們從 shell 讀寫檔案。由於 shell 尚未實作指令列參數解析，我們暫時以硬編碼的方式，在 `readfile` 和 `writefile` 指令中讀寫固定檔案 `hello.txt`：

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

這真的非常簡單！但它會導致一個頁錯（page fault）：

```
$ ./run.sh

> readfile
PANIC: kernel.c:561: unexpected trap scause=0000000d, stval=01000423, sepc=8020128a
```

讓我們深入探討原因。根據 `llvm-objdump` 的分析，錯誤發生在 `strcmp` 函式中：

```
$ llvm-objdump -d kernel.elf
...

80201282 <strcmp>:
80201282: 03 46 05 00   lbu     a2, 0(a0)
80201286: 15 c2         beqz    a2, 0x802012aa <.LBB3_4>
80201288: 05 05         addi    a0, a0, 1

8020128a <.LBB3_2>:
8020128a: 83 c6 05 00   lbu     a3, 0(a1) ← page fault here (a1 has 2nd argument)
8020128e: 33 37 d0 00   snez    a4, a3
80201292: 93 77 f6 0f   andi    a5, a2, 255
80201296: bd 8e         xor     a3, a3, a5
80201298: 93 b6 16 00   seqz    a3, a3
```

在 QEMU monitor 中檢查分頁表內容後，位於 `0x1000423` 的分頁（對應虛擬位址 `0x01000000`）確實被映射為使用者頁面（`u`），且具有讀、寫、執行（`rwx`）權限。

```
QEMU 8.0.2 monitor - type 'help' for more information
(qemu) info mem
vaddr    paddr            size     attr
-------- ---------------- -------- -------
01000000 000000008026c000 00001000 rwxu-a-
```

讓我們使用 `x` 指令來轉儲（dump）該虛擬位址的記憶體內容：

```
(qemu) x /10c 0x1000423
01000423: 'h' 'e' 'l' 'l' 'o' '.' 't' 'x' 't' '\x00' 'r' 'e' 'a' 'd' 'f' 'i'
01000433: 'l' 'e' '\x00' 'h' 'e' 'l' 'l' 'o' '\x00' '%' 's' '\n' '\x00' 'e' 'x' 'i'
01000443: 't' '\x00' 'w' 'r' 'i' 't' 'e' 'f'
```

如果頁表設定錯誤，`x` 指令會顯示錯誤，或顯示其他頁面的內容。在這裡，我們可以確認頁表已正確配置，且指標確實指向字串 `"hello.txt"`。

那麼，頁錯（page fault）的原因是什麼呢？答案是：`sstatus` CSR 中的 `SUM` 位元未被設定。

## 存取使用者指標

在 RISC-V 中，S 模式（核心）的行為可以透過 `sstatus` CSR 來設定，其中包含「SUM（允許 Supervisor 存取 User 記憶體）位元」。當 SUM 未被設定時，S 模式程式（也就是核心）將無法存取 U 模式（使用者）頁面。

> [!TIP]
>
> 這是一項安全機制，用於防止核心程式無意中參考使用者記憶體區域。
> 順帶一提，Intel CPU 也有類似機制，稱為「SMAP（Supervisor Mode Access Prevention）」。

如下所示定義 `SUM` 位元的位置：

```c [kernel.h]
#define SSTATUS_SUM  (1 << 18)
```

我們只需要在進入使用者空間時設定 `SUM` 位元即可：

```c [kernel.c] {8}
__attribute__((naked)) void user_entry(void) {
    __asm__ __volatile__(
        "csrw sepc, %[sepc]\n"
        "csrw sstatus, %[sstatus]\n"
        "sret\n"
        :
        : [sepc] "r" (USER_BASE),
          [sstatus] "r" (SSTATUS_SPIE | SSTATUS_SUM) // updated
    );
}
```

> [!TIP]
>
> 我剛才解釋了「是 SUM 位元造成的問題」，但你可能會好奇：這要怎麼自己找出來？這確實很困難 ― 即使你知道有發生 page fault，要縮小範圍通常也很不容易。更糟的是，CPU 甚至不會提供詳細的錯誤代碼。我能找出這問題的原因，其實只是因為我剛好知道有 SUM 這個位元。
>
> 以下是當事情「看起來正常但實際上*不正常*」時，可以嘗試的一些除錯方法：
>
> - 仔細閱讀 RISC-V 規範，它有提到：「當 SUM 位元被設置時，S 模式才可以存取 U 模式頁面。」
> - 閱讀 QEMU 的原始碼。前面提到的 page fault 原因實作在[這段程式碼](https://github.com/qemu/qemu/blob/d1181d29370a4318a9f11ea92065bea6bb159f83/target/riscv/cpu_helper.c#L1008) 中。不過說真的，這可能跟看規格書一樣難，甚至更難。
> - 問問大型語言模型（LLM）。我不是在開玩笑，現在它們已經是你最好的程式夥伴了。
>
> 除錯是從零開始寫作業系統最花時間的部分之一，也是許多實作者放棄的主因。但每次你克服這些挑戰，你就會學到更多，然後……真的會超級開心！

## 測試檔案讀寫

我們再試一次讀寫檔案。`readfile` 指令應該會顯示出 `hello.txt` 的內容：

```
$ ./run.sh

> readfile
Can you see me? Ah, there you are! You've unlocked the achievement "Virtio Newbie!"
```

我們也來嘗試寫入檔案。寫入完成後，應該會顯示寫入的位元組（byte）數量。

```
> writefile
wrote 2560 bytes to disk
```

現在磁碟映像檔已經被新的內容更新了。離開 QEMU 並解壓 `disk.tar`。你應該會看到更新後的內容。

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

你已經實作了一個關鍵功能 ― 「檔案系統」！耶！
