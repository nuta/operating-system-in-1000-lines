---
title: 파일 시스템
---

# 파일 시스템

지금까지 정말 잘 해왔습니다! 프로세스, 셸, 메모리 관리, 디스크 드라이버를 구현했습니다. 이제 파일 시스템 구현으로 마무리해보겠습니다.

## tar를 파일 시스템으로 사용하기

이 책에서는 흥미로운 방식으로 파일 시스템을 구현할 것입니다. 바로 tar 파일을 파일 시스템으로 사용하는 것입니다.

tar는 여러 파일을 포함할 수 있는 아카이브 형식입니다. 파일의 내용, 파일 이름, 생성 날짜 등 파일 시스템에 필요한 정보들을 포함하고 있습니다. FAT나 ext2와 같은 일반적인 파일 시스템 형식에 비해 tar의 데이터 구조는 훨씬 간단합니다. 또한 익숙한 tar 명령어를 사용해 파일 시스템 이미지를 다룰 수 있으니 교육 목적으로 이상적인 파일 형식이라 할 수 있습니다.


> [!TIP]
>
> 요즘 tar는 ZIP의 대안으로도 사용되지만, 원래는 자기 테이프용 파일 시스템처럼 탄생했습니다. 이번 챕터처럼 tar 파일 시스템을 사용할 수 있지만, 무작위 접근에는 적합하지 않다는 점을 알아두세요. FAT 파일 시스템의 설계도 읽어보면 재미있을 것입니다.


## 디스크 이미지(즉, tar 파일) 생성하기

파일 시스템의 내용을 준비하기 위해 `disk`라는 디렉토리를 만들고 그 안에 몇 개의 파일을 추가합니다. 그중 하나의 이름은 `hello.txt`로 해봅니다:


```
$ mkdir disk
$ vim disk/hello.txt
$ vim disk/meow.txt
```

빌드 스크립트에 tar 파일을 생성하고 이를 QEMU에 디스크 이미지로 전달하는 명령을 추가합니다:

```bash [run.sh] {1,5}
(cd disk && tar cf ../disk.tar --format=ustar *.txt)                          # new

$QEMU -machine virt -bios default -nographic -serial mon:stdio --no-reboot \
    -d unimp,guest_errors,int,cpu_reset -D qemu.log \
    -drive id=drive0,file=disk.tar,format=raw,if=none \                         # modified
    -device virtio-blk-device,drive=drive0,bus=virtio-mmio-bus.0 \
    -kernel kernel.elf
```

위의 `tar` 명령에서 사용한 옵션은 다음과 같습니다:

- `cf`: tar 파일 생성 (create)
- `--format=ustar`: ustar 형식으로 생성

> [!TIP]
>
> 괄호 `(...)`는 서브셸을 생성하여 `cd` 명령이 스크립트의 다른 부분에 영향을 주지 않도록 합니다.

## tar 파일 구조

tar 파일은 아래와 같은 구조로 되어 있습니다:

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

요약하자면, tar 파일은 각 파일마다 한 쌍의 "tar header"와 "file data"로 구성되어 있습니다. 여러 종류의 tar 포맷이 존재하지만, 여기서는 **ustar 포맷**을 사용합니다. ([Wikipedia](<https://en.wikipedia.org/wiki/Tar_(computing)#UStar_format>))

이러한 파일 구조를 파일 시스템의 데이터 구조로 활용할 수 있습니다. 실제 파일 시스템과 비교해보면 매우 흥미롭고 교육적인 접근 방식입니다.

## 파일 시스템 읽기

먼저, `kernel.h`에 tar 파일 시스템과 관련된 데이터 구조를 정의합니다:

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

이 파일 시스템 구현에서는 부팅 시 디스크의 모든 파일을 메모리로 읽어옵니다. `FILES_MAX`는 로드할 수 있는 파일의 최대 개수를 정의하며, `DISK_MAX_SIZE`는 디스크 이미지의 최대 크기를 지정합니다.

다음으로, `kernel.c`에서 전체 디스크 이미지를 메모리로 읽어오는 코드를 추가합니다:


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

위 함수에서는 먼저 `read_write_disk` 함수를 이용해 디스크 이미지를 임시 버퍼인 `disk` 변수에 로드합니다. `disk` 변수는 스택 대신 정적 변수로 선언한 이유는 스택 크기가 제한적이기 때문입니다.


디스크 내용을 로드한 후, 순차적으로 `files` 배열에 복사합니다. 여기서 주의할 점은 tar 헤더에 저장된 숫자들은 8진수 문자열이라는 것입니다. 숫자가 10진수처럼 보이지만, 실제로는 8진수 문자열이므로 oct2int 함수를 사용해 이를 정수로 변환합니다.


마지막으로, `kernel_main` 함수에서 `virtio-blk` 장치 초기화(`virtio_blk_init`) 이후에 fs_init을 호출하도록 합니다:

```c [kernel.c] {5}
void kernel_main(void) {
    memset(__bss, 0, (size_t) __bss_end - (size_t) __bss);
    WRITE_CSR(stvec, (uint32_t) kernel_entry);
    virtio_blk_init();
    fs_init();

    /* omitted */
}
```

## 파일 읽기 테스트

실행해보세요! `disk` 디렉토리에 있는 파일 이름과 크기가 출력됩니다:

```
$ ./run.sh

virtio-blk: capacity is 2560 bytes
file: world.txt, size=0
file: hello.txt, size=22
```

## 파일 시스템 디스크에 쓰기

파일을 쓰는 작업은 메모리 상의 `files` 변수에 있는 파일 내용을 tar 파일 형식으로 다시 디스크에 기록하는 방식으로 구현할 수 있습니다:

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

이 함수에서는 먼저 `disk` 버퍼에 tar 파일 형식으로 파일들을 구성한 뒤, `read_write_disk` 함수를 통해 실제 디스크에 기록합니다. 정말 간단하죠?

## 파일 읽기/쓰기를 위한 시스템 호출 설계

이제 파일 시스템의 읽기 및 쓰기 기능을 구현했으니, 애플리케이션에서 파일을 읽고 쓸 수 있도록 시스템 호출을 제공해보겠습니다.

두 개의 시스템 호출을 제공할 것입니다:

`readfile`: 파일을 읽기 위한 호출
`writefile`: 파일을 쓰기 위한 호출
두 호출 모두 파일 이름, 읽기/쓰기를 위한 메모리 버퍼, 버퍼 크기를 인자로 받습니다.

먼저, common.h에 시스템 호출 번호를 정의합니다:


```c [common.h]
#define SYS_READFILE  4
#define SYS_WRITEFILE 5
```

그리고 사용자 라이브러리 코드는 다음과 같이 작성합니다:

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
> 일반 운영체제의 시스템 호출 설계와 비교해보면 흥미로운 점들이 있습니다. 예를 들어, 왜 리눅스의 read(2)와 write(2) 시스템 호출은 파일 이름 대신 파일 디스크립터를 인자로 받는지 생각해보세요.

## 시스템 호출 구현

이제 앞에서 정의한 시스템 호출을 실제로 구현해봅니다.

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

위 코드에서, 파일 읽기와 쓰기 작업은 거의 동일하므로 하나의 케이스에서 처리합니다.
`fs_lookup` 함수는 주어진 파일 이름으로 `files` 배열에서 해당 파일 항목을 찾습니다.
읽기 작업의 경우 파일의 데이터를 버퍼에 복사하고, 쓰기 작업은 버퍼의 내용을 파일 항목에 기록한 후 `fs_flush`를 호출하여 디스크에 저장합니다.


> [!WARNING]
>
> 여기서는 단순함을 위해 애플리케이션에서 전달받은 포인터(즉, user pointer)를 직접 참조하고 있습니다. 만약 사용자가 임의의 메모리 영역을 지정할 수 있다면, 시스템 호출을 통해 커널 메모리에 접근할 위험이 있으므로 보안에 문제가 될 수 있습니다.

## 파일 읽기/쓰기 명령어

이제 셸에서 파일 읽기와 쓰기를 시도해봅니다. 셸에서는 명령줄 인자 파싱을 구현하지 않았으므로, 간단하게 `readfile`과 `writefile` 명령어를 구현하여 하드코딩된 `hello.txt` 파일을 대상으로 합니다:

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

간단해 보이지만, 실행 시 페이지 폴트(page fault)가 발생합니다:

```
$ ./run.sh

> readfile
PANIC: kernel.c:561: unexpected trap scause=0000000d, stval=01000423, sepc=8020128a
```

`llvm-objdump` 결과를 확인해보면, 페이지 폴트는 `strcmp` 함수에서 발생합니다:

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

QEMU 모니터에서 페이지 테이블 내용을 확인해보면, 가상 주소 `0x1000423` (`vaddr = 01000000`)가 사용자 페이지(`u`)로 매핑되어 있으며 읽기, 쓰기, 실행(`rwx`) 권한을 가지고 있는 것을 확인할 수 있습니다:


```
QEMU 8.0.2 monitor - type 'help' for more information
(qemu) info mem
vaddr    paddr            size     attr
-------- ---------------- -------- -------
01000000 000000008026c000 00001000 rwxu-a-
```

또한, QEMU 모니터의 `x` 명령을 사용해 해당 가상 주소의 메모리 내용을 덤프하면 "hello.txt" 문자열이 존재함을 확인할 수 있습니다:

```
(qemu) x /10c 0x1000423
01000423: 'h' 'e' 'l' 'l' 'o' '.' 't' 'x' 't' '\x00' 'r' 'e' 'a' 'd' 'f' 'i'
01000433: 'l' 'e' '\x00' 'h' 'e' 'l' 'l' 'o' '\x00' '%' 's' '\n' '\x00' 'e' 'x' 'i'
01000443: 't' '\x00' 'w' 'r' 'i' 't' 'e' 'f'
```

그렇다면 페이지 테이블 설정은 올바른데 왜 페이지 폴트가 발생할까요? 그 원인은 바로 SUM (Supervisor User Memory access) 비트가 설정되어 있지 않기 때문입니다.

## 사용자 포인터 접근

RISC-V에서는 S-모드(커널)가 U-모드(사용자) 페이지에 접근할 수 있는지 여부를 `sstatus` **CSR의 SUM 비트**를 통해 제어할 수 있습니다. SUM 비트가 설정되어 있지 않으면, S-모드(커널)에서는 U-모드(사용자) 페이지에 접근할 수 없습니다.

> [!TIP]
>
> 이는 커널이 사용자 메모리 영역을 의도치 않게 참조하는 것을 방지하기 위한 안전장치입니다.
> 참고로, 인텔 CPU에도 "SMAP (Supervisor Mode Access Prevention)"이라는 유사한 기능이 있습니다.

`kernel.h`에 `SUM` 비트의 위치를 다음과 같이 정의합니다:

```c [kernel.h]
#define SSTATUS_SUM  (1 << 18)
```

사용자 공간으로 진입할 때 sstatus CSR의 `SUM` 비트를 설정하면 됩니다:

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
> "SUM 비트가 원인이었다"라고 설명했지만, 여러분이 직접 이런 문제를 해결하려면 어떻게 찾아내야 할까요? 페이지 폴트가 발생하더라도 원인을 좁혀내기란 쉽지 않습니다. CPU는 상세한 오류 코드를 제공하지 않기 때문입니다.
>
> 다음과 같은 디버깅 방법들이 있습니다:
>
> - RISC-V 공식문서를 꼼꼼히 읽어보세요. "SUM 비트가 설정되어 있으면 S-모드에서 U-모드 페이지에 접근할 수 있다"라는 내용을 찾을 수 있습니다. 
> - QEMU의 소스 코드를 읽어보세요. 위 페이지 폴트 원인이 [여기](https://github.com/qemu/qemu/blob/d1181d29370a4318a9f11ea92065bea6bb159f83/target/riscv/cpu_helper.c#L1008)에서 구현된 것을 확인할 수 있습니다. 
> - LLM(대형 언어 모델)에게 물어보세요. 정말 유용한 페어 프로그래머가 되어줄 것입니다.

>
> OS를 처음부터 만드는 것은 어려운 작업이며 중도 포기의 원인이 되기도 합니다. 하지만 이러한 도전을 극복할수록 더 많은 것을 배우고, 성취감도 커지게 됩니다!


## 파일 읽기/쓰기 테스트


이제 파일 읽기와 쓰기를 다시 시도해봅니다. `readfile` 명령어를 실행하면 `hello.txt`의 내용이 출력되어야 합니다:

```
$ ./run.sh

> readfile
Can you see me? Ah, there you are! You've unlocked the achievement "Virtio Newbie!"
```

또한, writefile 명령어를 실행하면 파일에 쓰기가 완료된 후, 기록한 바이트 수가 출력됩니다:

```
> writefile
wrote 2560 bytes to disk
```

이제 디스크 이미지가 업데이트 되었으며, QEMU를 종료한 후 `disk.tar` 파일을 추출해보면 업데이트된 내용을 확인할 수 있습니다:

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

축하합니다! 이제 파일 시스템이라는 핵심 기능을 구현했습니다. 무야호!