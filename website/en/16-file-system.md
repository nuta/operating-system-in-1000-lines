# File System

You've done a great job so far! You've implemented a process, a shell, memory management, and a disk driver. Let's finish up by implementing a file system.

## Tar as file system

In this book, we'll take an interesting approach to implement a file system: using a tar file as our file system.

Tar is an archive format that can contain multiple files. It contains file contents, filenames, creation dates, and other information necessary for a file system. Compared to common file system formats like FAT or ext2, tar has a much simpler data structure. Additionally, you can manipulate the file system image using the  tar command which you are already familiar with. Isn't it an ideal file format for educational purposes?

> [!TIP]
>
> Nowadays, tar is used as a ZIP alternative, but originally it was born as sort of file system for magnetic tape. We can use it as a file system as we do in this chapter, however, you'll notice that it is not suitable for random access. [The design of FAT file system](https://en.wikipedia.org/wiki/Design_of_the_FAT_file_system) would be fun to read.

## Create a disk image (tar file)

Let's start by preparing the contents of our file system. Create a directory called `disk` and add some files to it. Name one of them `hello.txt`:

```
$ mkdir disk
$ vim disk/hello.txt
$ vim disk/meow.txt
```

Add a command to the build script to create a tar file and pass it as a disk image to QEMU:

```bash [run.sh] {1,5}
(cd disk && tar cf ../disk.tar --format=ustar *.txt)                          # new

$QEMU -machine virt -bios default -nographic -serial mon:stdio --no-reboot \
    -d unimp,guest_errors,int,cpu_reset -D qemu.log \
    -drive id=drive0,file=disk.tar,format=raw,if=none \                         # modified
    -device virtio-blk-device,drive=drive0,bus=virtio-mmio-bus.0 \
    -kernel kernel.elf
```

The `tar` command options used here are:

- `cf`: Create tar file.
- `--format=ustar`: Create in ustar format.

> [!TIP]
>
> The parentheses `(...)` create a subshell so that `cd` doesn't affect in other parts of the script.

## Tar file structure

A tar file has the following structure:

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

In summary, a tar file is essentially a series of "tar header" and "file data" pair, one pair for each file. There are several types of tar formats, but we will use the **ustar format** ([Wikipedia](<https://en.wikipedia.org/wiki/Tar_(computing)#UStar_format>)).

We use this file structure as the data structure for our file system. Comparing this to a real file system would be very interesting and educational.

## Reading the file system

First, define the data structures related to tar file system in `kernel.h`:

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

In our file system implementation, all files are read from the disk into memory at boot. `FILES_MAX` defines the maximum number of files that can be loaded, and `DISK_MAX_SIZE` specifies the maximum size of the disk image.

Next, let's read the whole disk into memory in `kernel.c`:

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

In this function, we first use the `read_write_disk` function to load the disk image into a temporary buffer (`disk` variable). The `disk` variable is declared as a static variable instead of a local (stack) variable. This is because the stack has limited size, and it's preferable to avoid using it for large data areas.

After loading the disk contents, we sequentially copy them into the `files` variable entries. Note that **the numbers in the tar header are in octal format**. It's very confusing because it looks like decimals. The `oct2int` function is used to convert these octal string values to integers.

Lastly, make sure to call the `fs_init` function after initializing the virtio-blk device (`virtio_blk_init`) in `kernel_main`:

```c [kernel.c] {5}
void kernel_main(void) {
    memset(__bss, 0, (size_t) __bss_end - (size_t) __bss);
    WRITE_CSR(stvec, (uint32_t) kernel_entry);
    virtio_blk_init();
    fs_init();

    /* omitted */
}
```

## Test file reads

Let's try! It should print the file names and their sizes in `disk` directory:

```
$ ./run.sh

virtio-blk: capacity is 2560 bytes
file: world.txt, size=0
file: hello.txt, size=22
```

## Writing to the disk

Writing files can be implemented by writing the contents of the `files` variable back to the disk in tar file format:

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

In this function, a tar file is built in the `disk` variable, then written to the disk using the `read_write_disk` function. Isn't it simple?

## Design file read/write system calls

Now that we have implemented file system read and write operations, let's make it possible for applications to read and write files. We'll provide two system calls: `readfile` for reading files and `writefile` for writing files. Both take as arguments the filename, a memory buffer for reading or writing, and the size of the buffer.

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
> It would be interesting to read the design of system calls in general operating systems and compare what has been omitted here. For example, why do `read(2)` and `write(2)` system calls in Linux take file descriptors as arguments, not filenames?

## Implement system calls

Let's implement the system calls we defined in the previous section.

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

File read and write operations are mostly the same, so they are grouped together in the same place. The `fs_lookup` function searches for an entry in the `files` variable based on the filename. For reading, it reads data from the file entry, and for writing, it modifies the contents of the file entry. Lastly, the `fs_flush` function writes to the disk.

> [!WARNING]
>
> For simplicity, we are directly referencing pointers passed from applications (aka. *user pointers*), but this poses security issues. If users can specify arbitrary memory areas, they could read and write kernel memory areas through system calls.

## File read/write commands

Let's read and write files from the shell. Since the shell doesn't implement command-line argument parsing, we'll implement `readfile` and `writefile` commands that read and write a hardcoded `hello.txt` file for now:

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

It's easy peasy! However, it causes a page fault:

```
$ ./run.sh

> readfile
PANIC: kernel.c:561: unexpected trap scause=0000000d, stval=01000423, sepc=8020128a
```

Let's dig into the cause. According the `llvm-objdump`, it happens in `strcmp` function:

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

Upon checking the page table contents in QEMU monitor, the page at `0x1000423` (with `vaddr = 01000000`) is indeed mapped as a user page (`u`) with read, write, and execute (`rwx`) permissions:

```
QEMU 8.0.2 monitor - type 'help' for more information
(qemu) info mem
vaddr    paddr            size     attr
-------- ---------------- -------- -------
01000000 000000008026c000 00001000 rwxu-a-
```

Let's dump the memory at the virtual address (`x` command):

```
(qemu) x /10c 0x1000423
01000423: 'h' 'e' 'l' 'l' 'o' '.' 't' 'x' 't' '\x00' 'r' 'e' 'a' 'd' 'f' 'i'
01000433: 'l' 'e' '\x00' 'h' 'e' 'l' 'l' 'o' '\x00' '%' 's' '\n' '\x00' 'e' 'x' 'i'
01000443: 't' '\x00' 'w' 'r' 'i' 't' 'e' 'f'
```

If the page table settings are incorrect, the `x` command will display an error or contents in other pages. Here, we can see that the page table is correctly configured, and the pointer is indeed pointing to the string `"hello.txt"`.

In that case, what could be the cause of the page fault? The answer is:  `SUM` bit in `sstatus` CSR is not set.

## Accessing user pointers

In RISC-V, the behavior of S-Mode (kernel) can be configured through  `sstatus` CSR, including **SUM (permit Supervisor User Memory access) bit**. When SUM is not set, S-Mode programs (i.e. kernel) cannot access U-Mode (user) pages.

> [!TIP]
>
> This is a safety measure to prevent unintended references to user memory areas.
> Incidentally, Intel CPUs also have the same feature named "SMAP (Supervisor Mode Access Prevention)".

Define the position of the `SUM` bit as follows:

```c [kernel.h]
#define SSTATUS_SUM  (1 << 18)
```

All we need to do is to set the `SUM` bit when entering user space:

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
> I explained that _"the SUM bit was the cause"_, but you may wonder how you could find this on your own. It is indeed tough - even if you are aware that a page fault is occurring, it's often hard to narrow down. Unfortunately, CPUs don't even provide detailed error codes. The reason I noticed was, simply because I knew about the SUM bit.
>
> Here are some debugging methods for when things don't work *"properly"*:
>
> - Read the RISC-V specification carefully. It does mention that *"when the SUM bit is set, S-Mode can access U-Mode pages."*
> - Read QEMU's source code. The aforementioned page fault cause is [implemented here](https://github.com/qemu/qemu/blob/d1181d29370a4318a9f11ea92065bea6bb159f83/target/riscv/cpu_helper.c#L1008). However, this can be as challenging or more so than reading the specification thoroughly.
> - Ask LLMs. Not joking. It's becoming your best pair programmer.
>
> Troubleshooting is one of the major reasons why building an OS from scratch is a time sink and OS implementers are prone to giving up. However, more you overcome these challenges, the more you'll learn and ... be super happy!

## Testing file reads/writes

Let's try reading and writing files again. `readfile` should display the contents of `hello.txt`:

```
$ ./run.sh

> readfile
Can you see me? Ah, there you are! You've unlocked the achievement "Virtio Newbie!"
```

Let's also try writing to the file. Once it's done the number of bytes written should be displayed:

```
> writefile
wrote 2560 bytes to disk
```

Now the disk image has been updated with the new contents. Exit QEMU and extract `disk.tar`. You should see the updated contents:

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

You've implemented a key feature _"file system"_! Yay!
