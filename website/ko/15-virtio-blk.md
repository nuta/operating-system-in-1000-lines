---
title: 디스크 I/O
---

# 디스크 I/O

이 장에서는, 가상 디스크 장치인 virtio-blk를 위한 디바이스 드라이버를 구현합니다. virtio-blk은 실제 하드웨어에서는 존재하지 않지만, 실제 디바이스와 동일한 인터페이스를 사용합니다.


## Virtio

Virtio는 가상 장치(virtio devices)를 위한 디바이스 인터페이스 표준입니다. 즉, 디바이스 드라이버가 장치를 제어하기 위한 API 중 하나입니다. 웹 서버에 접근하기 위해 HTTP를 사용하는 것처럼, virtio를 사용하여 virtio 장치에 접근합니다. Virtio는 QEMU, Firecracker와 같은 가상화 환경에서 널리 사용됩니다.

> [!NOTE]
>
> [최신 Virtio 사양](https://docs.oasis-open.org/virtio/virtio/v1.3/csd01/virtio-v1.3-csd01.html)에서는 Legacy와 Modern 두 가지 인터페이스를 정의합니다. 이 구현에서는 **Legacy 인터페이스**를 사용합니다. 약간 더 단순하고 Modern과 크게 다르지 않기 때문입니다.
>
> [Legacy PDF](https://ozlabs.org/~rusty/virtio-spec/virtio-0.9.5.pdf)를 참조하거나, [최신 HTML](https://docs.oasis-open.org/virtio/virtio/v1.3/csd01/virtio-v1.3-csd01.html)에서 *Legacy Interface:*로 시작하는 섹션을 검색하세요.

### Virtqueue

Virtio 장치는 virtqueue라는 구조체를 가지고 있습니다. 이름에서 알 수 있듯이, 이는 드라이버와 장치가 공유하는 큐입니다. 간단히 말하면:

Virtqueue는 다음 세 영역으로 구성됩니다:

| 이름              | 주체  | 내용                                 | 세부 내용                      |
|-----------------|-----|------------------------------------|----------------------------|
| Descriptor Table | 드라이버 | 요청(request)의 주소와 크기를 기록한 디스크립터 테이블	 | 메모리 주소, 길이, 다음 디스크립터의 인덱스 등 |
| Available Ring  | 드라이버 | 장치에 처리할 요청들을 등록함	                  | 디스크립터 체인의 헤드 인덱스           |
| Used Ring       | 드라이버 | 장치가 처리한 요청들을 기록함                   | 디스크립터 체인의 헤드 인덱스           |

![virtqueue diagram](../images/virtio.svg)

각 요청(예: 디스크에 쓰기)은 여러 개의 디스크립터로 구성된 디스크립터 체인(descriptor chain) 으로 이루어집니다. 여러 개의 디스크립터로 나누면, 메모리 상에 흩어져 있는 데이터를 지정하거나(Scatter-Gather I/O), 디스크립터마다 다른 속성(장치가 쓸 수 있는지 여부 등)을 줄 수 있습니다.

예를 들어, 디스크에 쓰기 요청을 할 때 virtqueue는 다음과 같이 사용됩니다:

1. 드라이버는 Descriptor Table에 읽기/쓰기 요청을 작성합니다. 
2. 드라이버는 디스크립터 체인의 헤드 디스크립터 인덱스를 Available Ring에 추가합니다. 
3. 드라이버는 장치에 새 요청이 있음을 알립니다. 
4. 장치는 Available Ring에서 요청을 읽어 처리합니다. 
5. 장치는 디스크립터 인덱스를 Used Ring에 기록한 후, 드라이버에게 완료되었음을 알립니다.

자세한 내용은 [virtio specification](https://docs.oasis-open.org/virtio/virtio/v1.1/csprd01/virtio-v1.1-csprd01.html)을 참고하세요. 이번 구현에서는 virtio-blk 장치에 집중합니다.

## virtio 장치 활성화

디바이스 드라이버를 작성하기 전에 테스트용 파일을 준비합니다. 다음과 같이 `lorem.txt` 파일을 생성하고 임의의 텍스트를 채워 넣습니다:

```
$ echo "Lorem ipsum dolor sit amet, consectetur adipiscing elit. In ut magna consequat, cursus velit aliquam, scelerisque odio. Ut lorem eros, feugiat quis bibendum vitae, malesuada ac orci. Praesent eget quam non nunc fringilla cursus imperdiet non tellus. Aenean dictum lobortis turpis, non interdum leo rhoncus sed. Cras in tellus auctor, faucibus tortor ut, maximus metus. Praesent placerat ut magna non tristique. Pellentesque at nunc quis dui tempor vulputate. Vestibulum vitae massa orci. Mauris et tellus quis risus sagittis placerat. Integer lorem leo, feugiat sed molestie non, viverra a tellus." > lorem.txt
```

또한, QEMU에 virtio-blk 장치를 연결합니다:


```bash [run.sh] {3-4}
$QEMU -machine virt -bios default -nographic -serial mon:stdio --no-reboot \
    -d unimp,guest_errors,int,cpu_reset -D qemu.log \
    -drive id=drive0,file=lorem.txt,format=raw,if=none \            # new
    -device virtio-blk-device,drive=drive0,bus=virtio-mmio-bus.0 \  # new
    -kernel kernel.elf
```

새로 추가된 옵션은 다음과 같습니다:

- `-drive id=drive0`: `drive0`이라는 이름의 디스크를 정의합니다. `lorem.txt`를 디스크 이미지로 사용하며, 이미지 형식은 raw (파일 내용을 그대로 디스크 데이터로 취급)입니다.
- `-device virtio-blk-device`: `drive0` 디스크를 사용하는 `virtio-blk` 장치를 추가합니다. `bus=virtio-mmio-bus.0` 옵션을 통해 해당 장치를 virtio-mmio 버스(메모리 맵 I/O를 통한 virtio)에 매핑합니다.


## C 매크로 및 구조체 정의

먼저, `kernel.h`에 virtio 관련 정의를 추가합니다:

```c [kernel.h]
#define SECTOR_SIZE       512
#define VIRTQ_ENTRY_NUM   16
#define VIRTIO_DEVICE_BLK 2
#define VIRTIO_BLK_PADDR  0x10001000
#define VIRTIO_REG_MAGIC         0x00
#define VIRTIO_REG_VERSION       0x04
#define VIRTIO_REG_DEVICE_ID     0x08
#define VIRTIO_REG_PAGE_SIZE     0x28
#define VIRTIO_REG_QUEUE_SEL     0x30
#define VIRTIO_REG_QUEUE_NUM_MAX 0x34
#define VIRTIO_REG_QUEUE_NUM     0x38
#define VIRTIO_REG_QUEUE_PFN     0x40
#define VIRTIO_REG_QUEUE_READY   0x44
#define VIRTIO_REG_QUEUE_NOTIFY  0x50
#define VIRTIO_REG_DEVICE_STATUS 0x70
#define VIRTIO_REG_DEVICE_CONFIG 0x100
#define VIRTIO_STATUS_ACK       1
#define VIRTIO_STATUS_DRIVER    2
#define VIRTIO_STATUS_DRIVER_OK 4
#define VIRTQ_DESC_F_NEXT          1
#define VIRTQ_DESC_F_WRITE         2
#define VIRTQ_AVAIL_F_NO_INTERRUPT 1
#define VIRTIO_BLK_T_IN  0
#define VIRTIO_BLK_T_OUT 1

// Virtqueue Descriptor Table entry.
struct virtq_desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} __attribute__((packed));

// Virtqueue Available Ring.
struct virtq_avail {
    uint16_t flags;
    uint16_t index;
    uint16_t ring[VIRTQ_ENTRY_NUM];
} __attribute__((packed));

// Virtqueue Used Ring entry.
struct virtq_used_elem {
    uint32_t id;
    uint32_t len;
} __attribute__((packed));

// Virtqueue Used Ring.
struct virtq_used {
    uint16_t flags;
    uint16_t index;
    struct virtq_used_elem ring[VIRTQ_ENTRY_NUM];
} __attribute__((packed));

// Virtqueue.
struct virtio_virtq {
    struct virtq_desc descs[VIRTQ_ENTRY_NUM];
    struct virtq_avail avail;
    struct virtq_used used __attribute__((aligned(PAGE_SIZE)));
    int queue_index;
    volatile uint16_t *used_index;
    uint16_t last_used_index;
} __attribute__((packed));

// Virtio-blk request.
struct virtio_blk_req {
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;
    uint8_t data[512];
    uint8_t status;
} __attribute__((packed));
```

> [!NOTE]
>
> `__attribute__((packed))`는 컴파일러 확장 기능으로, 컴파일러가 구조체 멤버 사이에 **패딩**을 추가하지 않고 꽉 채워서 배치하도록 합니다. 패딩이 추가되면 드라이버와 장치가 서로 다른 값을 보게 될 수 있습니다.


다음으로, MMIO 레지스터에 접근하기 위한 유틸리티 함수를 `kernel.c`에 추가합니다:

```c [kernel.c]
uint32_t virtio_reg_read32(unsigned offset) {
    return *((volatile uint32_t *) (VIRTIO_BLK_PADDR + offset));
}

uint64_t virtio_reg_read64(unsigned offset) {
    return *((volatile uint64_t *) (VIRTIO_BLK_PADDR + offset));
}

void virtio_reg_write32(unsigned offset, uint32_t value) {
    *((volatile uint32_t *) (VIRTIO_BLK_PADDR + offset)) = value;
}

void virtio_reg_fetch_and_or32(unsigned offset, uint32_t value) {
    virtio_reg_write32(offset, virtio_reg_read32(offset) | value);
}
```

> [!WARNING]
>
> MMIO 레지스터에 접근할 때는 일반 메모리 접근과 다릅니다. volatile 키워드를 사용하여 컴파일러가 읽기/쓰기 작업을 최적화하지 않도록 해야 합니다. MMIO에서는 메모리 접근이 부수 효과(예: 장치에 명령 전송)를 일으킬 수 있습니다.

## MMIO 영역 매핑

먼저, 커널이 MMIO 레지스터에 접근할 수 있도록 `virtio-blk`의 MMIO 영역을 페이지 테이블에 매핑합니다. 매우 간단한 구현입니다:

```c [kernel.c] {8}
struct process *create_process(const void *image, size_t image_size) {
    /* omitted */

    for (paddr_t paddr = (paddr_t) __kernel_base;
         paddr < (paddr_t) __free_ram_end; paddr += PAGE_SIZE)
        map_page(page_table, paddr, paddr, PAGE_R | PAGE_W | PAGE_X);

    map_page(page_table, VIRTIO_BLK_PADDR, VIRTIO_BLK_PADDR, PAGE_R | PAGE_W); // new
```

## Virtio 장치 초기화

초기화 과정은 사양에 다음과 같이 설명되어 있습니다:

> 1. Reset the device. This is not required on initial start up.
> 2. The ACKNOWLEDGE status bit is set: we have noticed the device.
> 3. The DRIVER status bit is set: we know how to drive the device.
> 4. Device-specific setup, including reading the Device Feature Bits, discovery of virtqueues for the device, optional MSI-X setup, and reading and possibly writing the virtio configuration space.
> 5. The subset of Device Feature Bits understood by the driver is written to the device.
> 6. The DRIVER_OK status bit is set.
>
> [Virtio 0.9.5 Specification (PDF)](https://ozlabs.org/~rusty/virtio-spec/virtio-0.9.5.pdf)

길게 느껴질 수 있지만, 걱정하지 마세요. 단순한 구현은 매우 간단합니다:

```c [kernel.c]
struct virtio_virtq *blk_request_vq;
struct virtio_blk_req *blk_req;
paddr_t blk_req_paddr;
uint64_t blk_capacity;

void virtio_blk_init(void) {
    if (virtio_reg_read32(VIRTIO_REG_MAGIC) != 0x74726976)
        PANIC("virtio: invalid magic value");
    if (virtio_reg_read32(VIRTIO_REG_VERSION) != 1)
        PANIC("virtio: invalid version");
    if (virtio_reg_read32(VIRTIO_REG_DEVICE_ID) != VIRTIO_DEVICE_BLK)
        PANIC("virtio: invalid device id");

    // 1. 장치를 리셋
    virtio_reg_write32(VIRTIO_REG_DEVICE_STATUS, 0);
    // 2. ACKNOWLEDGE 상태 비트 설정: 장치를 발견함
    virtio_reg_fetch_and_or32(VIRTIO_REG_DEVICE_STATUS, VIRTIO_STATUS_ACK);
    // 3. DRIVER 상태 비트 설정: 장치 사용 방법을 알고 있음
    virtio_reg_fetch_and_or32(VIRTIO_REG_DEVICE_STATUS, VIRTIO_STATUS_DRIVER);
    // 페이지 크기 설정: 4KB 페이지 사용. PFN (페이지 프레임 번호) 계산에 사용됨
    virtio_reg_write32(VIRTIO_REG_PAGE_SIZE, PAGE_SIZE);
    // 디스크 읽기/쓰기 요청용 큐 초기화
    blk_request_vq = virtq_init(0);
    // 6. DRIVER_OK 상태 비트 설정: 이제 장치를 사용할 수 있음
    virtio_reg_write32(VIRTIO_REG_DEVICE_STATUS, VIRTIO_STATUS_DRIVER_OK);

    // 디스크 용량을 가져옵니다.
    blk_capacity = virtio_reg_read64(VIRTIO_REG_DEVICE_CONFIG + 0) * SECTOR_SIZE;
    printf("virtio-blk: capacity is %d bytes\n", (int)blk_capacity);

    // 장치에 요청(request)을 저장할 영역을 할당합니다.
    blk_req_paddr = alloc_pages(align_up(sizeof(*blk_req), PAGE_SIZE) / PAGE_SIZE);
    blk_req = (struct virtio_blk_req *) blk_req_paddr;
}
```

그리고 `kernel_main` 함수에 `virtio-blk` 초기화를 추가합니다:

```c [kernel.c] {5}
void kernel_main(void) {
    memset(__bss, 0, (size_t) __bss_end - (size_t) __bss);
    WRITE_CSR(stvec, (uint32_t) kernel_entry);

    virtio_blk_init(); // new
```

이것은 디바이스 드라이버의 일반적인 초기화 패턴입니다. 장치를 리셋하고, 파라미터를 설정한 다음, 장치를 활성화합니다. OS는 장치 내부에서 실제로 어떤 일이 일어나는지 신경 쓸 필요가 없습니다. 위와 같이 몇 가지 메모리 읽기/쓰기 작업만 수행하면 됩니다.

## Virtqueue 초기화

Virtqueue는 다음과 같이 초기화해야 합니다:

> 1. Write the virtqueue index (first queue is 0) to the Queue Select field.
> 2. Read the virtqueue size from the Queue Size field, which is always a power of 2. This controls how big the virtqueue is (see below). If this field is 0, the virtqueue does not exist.
> 3. Allocate and zero virtqueue in contiguous physical memory, on a 4096 byte alignment. Write the physical address, divided by 4096 to the Queue Address field.
>
> [Virtio 0.9.5 Specification (PDF)](https://ozlabs.org/~rusty/virtio-spec/virtio-0.9.5.pdf)

간단한 구현 예제는 다음과 같습니다:

```c [kernel.c]
struct virtio_virtq *virtq_init(unsigned index) {
    paddr_t virtq_paddr = alloc_pages(align_up(sizeof(struct virtio_virtq), PAGE_SIZE) / PAGE_SIZE);
    struct virtio_virtq *vq = (struct virtio_virtq *) virtq_paddr;
    vq->queue_index = index;
    vq->used_index = (volatile uint16_t *) &vq->used.index;
    // 큐 선택: virtqueue 인덱스를 기록 (첫 번째 큐는 0)
    virtio_reg_write32(VIRTIO_REG_QUEUE_SEL, index);
    // 큐 크기 지정: 사용할 디스크립터 개수를 기록
    virtio_reg_write32(VIRTIO_REG_QUEUE_NUM, VIRTQ_ENTRY_NUM);
    // 큐의 페이지 프레임 번호 (물리 주소가 아님!) 를 기록
    virtio_reg_write32(VIRTIO_REG_QUEUE_PFN, virtq_paddr / PAGE_SIZE);
    return vq;
}
```

이 함수는 virtqueue를 위한 메모리 영역을 할당하고, 그 페이지 프레임 번호(물리적 주소가 아님!)를 장치에 알려줍니다. 장치는 이 메모리 영역을 사용하여 요청을 읽거나 씁니다.

> [!TIP]
>
> 드라이버 초기화 과정에서 하는 일은 장치 기능/능력을 확인하고, OS 자원(예: 메모리 영역)을 할당하며, 파라미터를 설정하는 것입니다. 이는 네트워크 프로토콜의 핸드쉐이크와 비슷한 역할을 합니다.


## I/O 요청 보내기

이제 초기화된 virtio-blk 장치를 이용해 I/O 요청을 디스크에 전송해보겠습니다. 디스크에 I/O 요청을 보내는 방식은 "virtqueue에 처리 요청을 추가하는 것" 입니다:

```c [kernel.c]
// desc_index는 새로운 요청의 디스크립터 체인의 헤드 디스크립터 인덱스입니다.
// 장치에 새로운 요청이 있음을 알립니다.
void virtq_kick(struct virtio_virtq *vq, int desc_index) {
    vq->avail.ring[vq->avail.index % VIRTQ_ENTRY_NUM] = desc_index;
    vq->avail.index++;
    __sync_synchronize();
    virtio_reg_write32(VIRTIO_REG_QUEUE_NOTIFY, vq->queue_index);
    vq->last_used_index++;
}

// 장치가 요청을 처리 중인지 확인합니다.
bool virtq_is_busy(struct virtio_virtq *vq) {
    return vq->last_used_index != *vq->used_index;
}

// virtio-blk 장치로부터 읽기/쓰기를 수행합니다.
void read_write_disk(void *buf, unsigned sector, int is_write) {
    if (sector >= blk_capacity / SECTOR_SIZE) {
        printf("virtio: tried to read/write sector=%d, but capacity is %d\n",
              sector, blk_capacity / SECTOR_SIZE);
        return;
    }

    // virtio-blk 사양에 따라 요청을 구성합니다.
    blk_req->sector = sector;
    blk_req->type = is_write ? VIRTIO_BLK_T_OUT : VIRTIO_BLK_T_IN;
    if (is_write)
        memcpy(blk_req->data, buf, SECTOR_SIZE);

    // virtqueue 디스크립터를 구성합니다 (3개의 디스크립터 사용).
    struct virtio_virtq *vq = blk_request_vq;
    vq->descs[0].addr = blk_req_paddr;
    vq->descs[0].len = sizeof(uint32_t) * 2 + sizeof(uint64_t);
    vq->descs[0].flags = VIRTQ_DESC_F_NEXT;
    vq->descs[0].next = 1;

    vq->descs[1].addr = blk_req_paddr + offsetof(struct virtio_blk_req, data);
    vq->descs[1].len = SECTOR_SIZE;
    vq->descs[1].flags = VIRTQ_DESC_F_NEXT | (is_write ? 0 : VIRTQ_DESC_F_WRITE);
    vq->descs[1].next = 2;

    vq->descs[2].addr = blk_req_paddr + offsetof(struct virtio_blk_req, status);
    vq->descs[2].len = sizeof(uint8_t);
    vq->descs[2].flags = VIRTQ_DESC_F_WRITE;

    // 장치에 새로운 요청이 있음을 알림.
    virtq_kick(vq, 0);

    // 장치가 요청 처리를 마칠 때까지 대기(바쁜 대기; busy-wait).
    while (virtq_is_busy(vq))
        ;

    // virtio-blk: 0이 아닌 값이 반환되면 에러입니다.
    if (blk_req->status != 0) {
        printf("virtio: warn: failed to read/write sector=%d status=%d\n",
               sector, blk_req->status);
        return;
    }

    // 읽기 작업의 경우, 데이터를 버퍼에 복사합니다.
    if (!is_write)
        memcpy(buf, blk_req->data, SECTOR_SIZE);
}
```

요청은 다음 단계로 전송됩니다:

1. `blk_req`에 요청 내용을 구성합니다. 접근할 섹터 번호와 읽기/쓰기 유형을 지정합니다. 
2. `blk_req`의 각 영역을 가리키는 디스크립터 체인을 구성합니다. 
3. 디스크립터 체인의 헤드 인덱스를 Available Ring에 추가합니다. 
4. 장치에 새로운 요청이 있음을 알립니다. 
5. 장치가 요청 처리를 완료할 때까지 대기합니다. 
6. 장치의 응답(status)을 확인합니다.

여기서는 3개의 디스크립터로 구성된 체인을 사용합니다. 디스크립터마다 다른 속성(flags)을 설정해야 하는데, 이는 다음과 같습니다:


```c
struct virtio_blk_req {
    // 첫 번째 디스크립터: 장치에서 읽어올 데이터를 위한 영역 (읽기 전용)
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;

    // 두 번째 디스크립터: 읽기 작업 시, 장치가 데이터를 쓸 수 있도록 허용(VIRTQ_DESC_F_WRITE)
    uint8_t data[512];

    // 세 번째 디스크립터: 장치가 쓸 수 있는 상태 정보 영역 (VIRTQ_DESC_F_WRITE)
    uint8_t status;
} __attribute__((packed));
```

여기서는 매번 바쁜 대기(busy-wait)를 통해 요청 처리가 완료될 때까지 기다리므로, 단순히 링의 처음 3개의 디스크립터를 사용합니다. 실제 환경에서는 동시에 여러 요청을 처리할 수 있도록 자유 디스크립터를 추적해야 합니다.

## 직접 실행해보기

마지막으로, 디스크 I/O를 시험해봅니다. `kernel.c`에 다음 코드를 추가합니다:

```c [kernel.c] {3-8}
    virtio_blk_init();

    char buf[SECTOR_SIZE];
    read_write_disk(buf, 0, false /* read from the disk */);
    printf("first sector: %s\n", buf);

    strcpy(buf, "hello from kernel!!!\n");
    read_write_disk(buf, 0, true /* write to the disk */);
```

여기서 디스크 이미지로 `lorem.txt` 파일을 지정했기 때문에, 원본 내용이 그대로 출력됩니다:

```
$ ./run.sh

virtio-blk: capacity is 1024 bytes
first sector: Lorem ipsum dolor sit amet, consectetur adipiscing elit ...
```

또한, 첫 번째 섹터는 "hello from kernel!!!" 문자열로 덮어써집니다:


```
$ head lorem.txt
hello from kernel!!!
amet, consectetur adipiscing elit ...
```

축하합니다! 디스크 I/O 드라이버 구현에 성공하였습니다.


> [!TIP]
> 보시다시피, 디바이스 드라이버는 OS와 장치 사이의 "접착제(Glue)" 역할을 합니다. 드라이버는 장치에게 직접 하드웨어를 제어하도록 하지 않고, 장치의 내부 소프트웨어(예: 펌웨어)와 통신하며 나머지 무거운 작업(예: 디스크 읽기/쓰기 헤드 이동 등)을 장치에 맡깁니다.
