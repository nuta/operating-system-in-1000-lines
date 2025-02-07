---
title: 文件系统
---

# 文件系统

到目前为止，你做得很好！你已经实现了进程、shell、内存管理和磁盘驱动程序。让我们通过实现文件系统来完成最后的部分。

## 以 Tar 作为文件系统

在本书中，我们将采用一种有趣的方式来实现文件系统：使用 tar 文件作为我们的文件系统。

Tar 是一种可以包含多个文件的归档格式。它包含文件内容、文件名、创建日期和其他文件系统所需的信息。与常见的文件系统格式（如 FAT 或 ext2）相比，tar 具有更简单的数据结构。此外，你可以使用已经熟悉的 tar 命令来操作文件系统镜像。这不是一个理想的教育用途文件格式吗？

> [!TIP]
>
> 现在，tar 被用作 ZIP 的替代品，但最初它是作为磁带的一种文件系统而诞生的。我们可以像本章那样将其用作文件系统，但是你会发现它不适合随机访问。阅读 [FAT 文件系统的设计](https://en.wikipedia.org/wiki/Design_of_the_FAT_file_system) 会很有趣。

## 创建磁盘镜像（tar 文件）

让我们从准备文件系统的内容开始。创建一个名为 `disk` 的目录并向其中添加一些文件。将其中一个命名为 `hello.txt`：

```
$ mkdir disk
$ vim disk/hello.txt
$ vim disk/meow.txt
```

在构建脚本中添加一条命令来创建 tar 文件并将其作为磁盘镜像传递给 QEMU：

```bash [run.sh] {1,5}
(cd disk && tar cf ../disk.tar --format=ustar *.txt)                          # new

$QEMU -machine virt -bios default -nographic -serial mon:stdio --no-reboot \
    -d unimp,guest_errors,int,cpu_reset -D qemu.log \
    -drive id=drive0,file=disk.tar,format=raw,if=none \                         # modified
    -device virtio-blk-device,drive=drive0,bus=virtio-mmio-bus.0 \
    -kernel kernel.elf
```

这里使用的 `tar` 命令选项是：

- `cf`：创建 tar 文件。
- `--format=ustar`：以 ustar 格式创建。

> [!TIP]
>
> 括号 `(...)` 创建一个子 shell，这样 `cd` 不会影响脚本的其他部分。

## Tar 文件结构

tar 文件具有以下结构：

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

简而言之，tar 文件本质上是一系列"tar header"和"file data"对的组合，每个文件一对。有几种类型的 tar 格式，但我们将使用 **ustar 格式**（[Wikipedia](<https://en.wikipedia.org/wiki/Tar_(computing)#UStar_format>)）。

我们将这种文件结构用作文件系统的数据结构。将其与真实的文件系统进行比较将会非常有趣且具有教育意义。

## 读取文件系统

首先，在 `kernel.h` 中定义与 tar 文件系统相关的数据结构：

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
    char data[];      // 指向头部后面数据区域的数组
                      // (flexible array member)
} __attribute__((packed));

struct file {
    bool in_use;      // 表示此文件条目是否在使用中
    char name[100];   // 文件名
    char data[1024];  // 文件内容
    size_t size;      // 文件大小
};
```

在我们的文件系统实现中，所有文件都在启动时从磁盘读入内存。`FILES_MAX` 定义了可以加载的最大文件数，而 `DISK_MAX_SIZE` 指定了磁盘镜像的最大大小。

接下来，让我们在 `kernel.c` 中将整个磁盘读入内存：

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

在这个函数中，我们首先使用 `read_write_disk` 函数将磁盘镜像加载到临时缓冲区（`disk` 变量）中。`disk` 变量被声明为静态变量而不是局部（栈）变量。这是因为栈的大小有限，最好避免将其用于大型数据区域。

加载磁盘内容后，我们将它们按顺序复制到 `files` 变量条目中。需要注意的是，**tar header 中的数字是八进制格式**。这很容易让人困惑，因为它看起来像十进制数。`oct2int` 函数用于将这些八进制字符串值转换为整数。

最后，确保在初始化 virtio-blk 设备（`virtio_blk_init`）之后在 `kernel_main` 中调用 `fs_init` 函数：

```c [kernel.c] {5}
void kernel_main(void) {
    memset(__bss, 0, (size_t) __bss_end - (size_t) __bss);
    WRITE_CSR(stvec, (uint32_t) kernel_entry);
    virtio_blk_init();
    fs_init();

    /* omitted */
}
```

## 测试文件读取

让我们试一试！它应该打印出 `disk` 目录中的文件名及其大小：

```
$ ./run.sh

virtio-blk: capacity is 2560 bytes
file: world.txt, size=0
file: hello.txt, size=22
```

## 写入磁盘

通过以 tar 文件格式将 `files` 变量的内容写回磁盘，可以实现文件写入：

```c [kernel.c]
void fs_flush(void) {
    // 将所有文件内容复制到 `disk` 缓冲区
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

        // 将文件大小转换为八进制字符串
        int filesz = file->size;
        for (int i = sizeof(header->size); i > 0; i--) {
            header->size[i - 1] = (filesz % 8) + '0';
            filesz /= 8;
        }

        // 计算校验和
        int checksum = ' ' * sizeof(header->checksum);
        for (unsigned i = 0; i < sizeof(struct tar_header); i++)
            checksum += (unsigned char) disk[off + i];

        for (int i = 5; i >= 0; i--) {
            header->checksum[i] = (checksum % 8) + '0';
            checksum /= 8;
        }

        // 复制文件数据
        memcpy(header->data, file->data, file->size);
        off += align_up(sizeof(struct tar_header) + file->size, SECTOR_SIZE);
    }

    // 将 `disk` 缓冲区写入 virtio-blk
    for (unsigned sector = 0; sector < sizeof(disk) / SECTOR_SIZE; sector++)
        read_write_disk(&disk[sector * SECTOR_SIZE], sector, true);

    printf("wrote %d bytes to disk\n", sizeof(disk));
}
```

在这个函数中，tar 文件在 `disk` 变量中构建，然后使用 `read_write_disk` 函数写入磁盘。很简单，不是吗？

## 设计文件读写系统调用

现在我们已经实现了文件系统的读写操作，让我们使应用程序能够读写文件。我们将提供两个系统调用：用于读取文件的 `readfile` 和用于写入文件的 `writefile`。两者都将文件名、用于读写的内存缓冲区和缓冲区大小作为参数。

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
> 阅读通用操作系统中的系统调用设计并比较这里省略的内容会很有趣。例如，为什么 Linux 中的 `read(2)` 和 `write(2)` 系统调用以文件描述符而不是文件名作为参数？

## 实现系统调用

让我们实现上一节中定义的系统调用。

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

文件读写操作大体相同，所以它们被放在同一个地方。`fs_lookup` 函数根据文件名在 `files` 变量中搜索条目。对于读取，它从文件条目中读取数据，对于写入，它修改文件条目的内容。最后，`fs_flush` 函数写入磁盘。

> [!WARNING]
>
> 为了简单起见，我们直接引用从应用程序传递的指针（即 *user pointers*），但这会带来安全问题。如果用户可以指定任意内存区域，他们可以通过系统调用读写内核内存区域。

## 文件读写命令

让我们从 shell 中读写文件。由于 shell 没有实现命令行参数解析，我们将实现 `readfile` 和 `writefile` 命令，暂时只读写硬编码的 `hello.txt` 文件：

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

非常简单！但是，它会导致页面错误：

```
$ ./run.sh

> readfile
PANIC: kernel.c:561: unexpected trap scause=0000000d, stval=01000423, sepc=8020128a
```

让我们深入研究原因。根据 `llvm-objdump`，它发生在 `strcmp` 函数中：

```
$ llvm-objdump -d kernel.elf
...

80201282 <strcmp>:
80201282: 03 46 05 00   lbu     a2, 0(a0)
80201286: 15 c2         beqz    a2, 0x802012aa <.LBB3_4>
80201288: 05 05         addi    a0, a0, 1

8020128a <.LBB3_2>:
8020128a: 83 c6 05 00   lbu     a3, 0(a1) ← 页面错误发生在这里 (a1 是第二个参数)
8020128e: 33 37 d0 00   snez    a4, a3
80201292: 93 77 f6 0f   andi    a5, a2, 255
80201296: bd 8e         xor     a3, a3, a5
80201298: 93 b6 16 00   seqz    a3, a3
```

在 QEMU 监视器中检查页表内容时，地址 `0x1000423`（`vaddr = 01000000`）的页确实被映射为用户页（`u`），具有读、写和执行（`rwx`）权限：

```
QEMU 8.0.2 monitor - type 'help' for more information
(qemu) info mem
vaddr    paddr            size     attr
-------- ---------------- -------- -------
01000000 000000008026c000 00001000 rwxu-a-
```

让我们转储虚拟地址处的内存（`x` 命令）：

```
(qemu) x /10c 0x1000423
01000423: 'h' 'e' 'l' 'l' 'o' '.' 't' 'x' 't' '\x00' 'r' 'e' 'a' 'd' 'f' 'i'
01000433: 'l' 'e' '\x00' 'h' 'e' 'l' 'l' 'o' '\x00' '%' 's' '\n' '\x00' 'e' 'x' 'i'
01000443: 't' '\x00' 'w' 'r' 'i' 't' 'e' 'f'
```

如果页表设置不正确，`x` 命令将显示错误或其他页面的内容。这里，我们可以看到页表配置正确，并且指针确实指向字符串 `"hello.txt"`。

那么，页面错误的原因是什么？答案是：`sstatus` CSR 中的 `SUM` 位未设置。

## 访问用户指针

在 RISC-V 中，S-Mode（内核）的行为可以通过 `sstatus` CSR 进行配置，包括 **SUM（permit Supervisor User Memory access）位**。当 SUM 未设置时，S-Mode 程序（即内核）无法访问 U-Mode（用户）页面。

> [!TIP]
>
> 这是一项防止意外引用用户内存区域的安全措施。
> 顺便说一下，Intel CPU 也有同样的功能，名为 "SMAP (Supervisor Mode Access Prevention)"。

将 `SUM` 位的位置定义如下：

```c [kernel.h]
#define SSTATUS_SUM  (1 << 18)
```

我们需要做的就是在进入用户空间时设置 `SUM` 位：

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
> 我解释说 _"SUM 位是原因"_，但你可能会想知道如何自己发现这一点。确实很难——即使你知道发生了页面错误，通常也很难缩小范围。不幸的是，CPU 甚至不提供详细的错误代码。我注意到的原因很简单，因为我知道 SUM 位。
>
> 以下是一些当东西不能 *“正常”* 工作时的调试方法：
>
> - 仔细阅读 RISC-V 规范。它确实提到了 *“当设置 SUM 位时，S-Mode 可以访问 U-Mode 页面”*。
> - 阅读 QEMU 的源代码。上述页面错误原因[在这里实现](https://github.com/qemu/qemu/blob/d1181d29370a4318a9f11ea92065bea6bb159f83/target/riscv/cpu_helper.c#L1008)。但是，这可能和仔细阅读规范一样具有挑战性，甚至更具挑战性。
> - 询问 LLMs。不是开玩笑。它正在成为你最好的配对编程伙伴。
>
> 这是从头开始构建操作系统为什么会耗时且容易放弃的主要原因之一。但是，你克服这些挑战越多，就会学到越多，并且...会非常开心！

## 测试文件读写

让我们再次尝试读写文件。`readfile` 应该显示 `hello.txt` 的内容：

```
$ ./run.sh

> readfile
Can you see me? Ah, there you are! You've unlocked the achievement "Virtio Newbie!"
```

让我们也尝试写入文件。完成后应显示写入的字节数：

```
> writefile
wrote 2560 bytes to disk
```

现在磁盘镜像已经用新内容更新。退出 QEMU 并提取 `disk.tar`。你应该看到更新后的内容：

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

你已经实现了一个关键功能 _"文件系统"_！耶！
