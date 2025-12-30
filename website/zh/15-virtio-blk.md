---
title: 磁盘 I/O
---

# 磁盘 I/O

在本章中，我们将实现一个 virtio-blk 虚拟磁盘设备的驱动程序。虽然 virtio-blk 在真实硬件中并不存在，但它与真实硬件共享相同的接口。

## Virtio

Virtio 是一个用于虚拟设备(virtio devices)的设备接口标准。换句话说，它是设备驱动程序控制设备的 API 之一。就像使用 HTTP 访问 web 服务器一样，你使用 virtio 来访问 virtio 设备。Virtio 在 QEMU 和 Firecracker 等虚拟化环境中被广泛使用。

> [!NOTE]
>
> [最新的 Virtio 规范](https://docs.oasis-open.org/virtio/virtio/v1.3/csd01/virtio-v1.3-csd01.html)定义了两种接口：Legacy 和 Modern。在本实现中，**我们使用 Legacy 接口**，因为它稍微简单一些，与 Modern 版本差异不大。
>
> 请参考[旧版 PDF](https://ozlabs.org/~rusty/virtio-spec/virtio-0.9.5.pdf)，或在[最新的 HTML 版本](https://docs.oasis-open.org/virtio/virtio/v1.3/csd01/virtio-v1.3-csd01.html)中搜索以 *Legacy Interface:* 开头的章节。

### Virtqueue

Virtio 设备具有一个称为 virtqueue 的结构。顾名思义，它是驱动程序和设备之间共享的队列。简而言之：

Virtqueue 由以下三个区域组成：

| 名称             | 写入者    | 内容                                                           | 具体内容                                   |
| ---------------- | --------- | -------------------------------------------------------------- | ------------------------------------------ |
| Descriptor Table | 驱动程序  | 描述符表：请求的地址和大小                                     | 内存地址、长度、下一个描述符的索引        |
| Available Ring   | 驱动程序  | 向设备发送处理请求                                             | 描述符链的头索引                           |
| Used Ring        | 设备      | 设备处理完成的请求                                             | 描述符链的头索引                           |

![virtqueue diagram](../images/virtio.svg)

每个请求(例如，写入磁盘)由多个描述符组成，称为描述符链。通过拆分为多个描述符，你可以指定分散的内存数据(所谓的 Scatter-Gather IO)或给予不同的描述符属性(是否允许设备写入)。

例如，在写入磁盘时，virtqueue 将按以下方式使用：

1. 驱动程序在 Descriptor Table 写入读/写请求。
2. 驱动程序将头描述符的索引添加到 Available Ring。
3. 驱动程序通知设备有新的请求。
4. 设备从 Available Ring 读取请求并处理它。
3. 设备将描述符索引写入 Used Ring，并通知驱动程序完成。

详细信息请参阅 [virtio specification](https://docs.oasis-open.org/virtio/virtio/v1.1/csprd01/virtio-v1.1-csprd01.html)。在此实现中，我们将重点关注名为 virtio-blk 的设备。

## 启用 virtio 设备

在编写设备驱动程序之前，让我们准备一个测试文件。创建一个名为 `lorem.txt` 的文件并填入一些随机文本，如下所示：

```
$ echo "Lorem ipsum dolor sit amet, consectetur adipiscing elit. In ut magna consequat, cursus velit aliquam, scelerisque odio. Ut lorem eros, feugiat quis bibendum vitae, malesuada ac orci. Praesent eget quam non nunc fringilla cursus imperdiet non tellus. Aenean dictum lobortis turpis, non interdum leo rhoncus sed. Cras in tellus auctor, faucibus tortor ut, maximus metus. Praesent placerat ut magna non tristique. Pellentesque at nunc quis dui tempor vulputate. Vestibulum vitae massa orci. Mauris et tellus quis risus sagittis placerat. Integer lorem leo, feugiat sed molestie non, viverra a tellus." > lorem.txt
```

同时，将 virtio-blk 设备添加到 QEMU：

```bash [run.sh] {3-4}
$QEMU -machine virt -bios default -nographic -serial mon:stdio --no-reboot \
    -d unimp,guest_errors,int,cpu_reset -D qemu.log \
    -drive id=drive0,file=lorem.txt,format=raw,if=none \            # new
    -device virtio-blk-device,drive=drive0,bus=virtio-mmio-bus.0 \  # new
    -kernel kernel.elf
```

新添加的选项解释如下：

- `-drive id=drive0`：定义一个名为 `drive0` 的磁盘，使用 `lorem.txt` 作为磁盘镜像。磁盘镜像格式为 `raw`(将文件内容按原样作为磁盘数据处理)。
- `-device virtio-blk-device`：添加一个带有 `drive0` 磁盘的 virtio-blk 设备。`bus=virtio-mmio-bus.0` 将设备映射到 virtio-mmio 总线(通过内存映射 I/O 的 virtio)。

## 定义 C 宏/结构体 

首先，让我们在 `kernel.h` 中添加一些 virtio 相关的定义：

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

// Virtqueue Available Ring。
struct virtq_avail {
    uint16_t flags;
    uint16_t index;
    uint16_t ring[VIRTQ_ENTRY_NUM];
} __attribute__((packed));

// Virtqueue Used Ring 条目。
struct virtq_used_elem {
    uint32_t id;
    uint32_t len;
} __attribute__((packed));

// Virtqueue Used Ring。
struct virtq_used {
    uint16_t flags;
    uint16_t index;
    struct virtq_used_elem ring[VIRTQ_ENTRY_NUM];
} __attribute__((packed));

// Virtqueue。
struct virtio_virtq {
    struct virtq_desc descs[VIRTQ_ENTRY_NUM];
    struct virtq_avail avail;
    struct virtq_used used __attribute__((aligned(PAGE_SIZE)));
    int queue_index;
    volatile uint16_t *used_index;
    uint16_t last_used_index;
} __attribute__((packed));

// Virtio-blk 请求。
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
> `__attribute__((packed))` 是一个编译器扩展，它告诉编译器在不添加*填充*的情况下打包结构体成员。否则，编译器可能会添加隐藏的填充字节，导致驱动程序/设备看到不同的值。

接下来，在 `kernel.c` 中添加用于访问 MMIO 寄存器的工具函数：

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
> 访问 MMIO 寄存器与访问普通内存不同。你应该使用 `volatile` 关键字来防止编译器优化掉读/写操作。在 MMIO 中，内存访问可能会触发副作用(例如，向设备发送命令)。

## 映射 MMIO 区域

首先，将 `virtio-blk` MMIO 区域映射到页表中，以便内核可以访问 MMIO 寄存器。这非常简单：

```c [kernel.c] {8}
struct process *create_process(const void *image, size_t image_size) {
    /* omitted */

    for (paddr_t paddr = (paddr_t) __kernel_base;
         paddr < (paddr_t) __free_ram_end; paddr += PAGE_SIZE)
        map_page(page_table, paddr, paddr, PAGE_R | PAGE_W | PAGE_X);

    map_page(page_table, VIRTIO_BLK_PADDR, VIRTIO_BLK_PADDR, PAGE_R | PAGE_W); // new
```

## Virtio 设备初始化

初始化过程在规范中描述如下：

> 1. Reset the device. This is not required on initial start up.
> 2. The ACKNOWLEDGE status bit is set: we have noticed the device.
> 3. The DRIVER status bit is set: we know how to drive the device.
> 4. Device-specific setup, including reading the Device Feature Bits, discovery of virtqueues for the device, optional MSI-X setup, and reading and possibly writing the virtio configuration space.
> 5. The subset of Device Feature Bits understood by the driver is written to the device.
> 6. The DRIVER_OK status bit is set.
>
> [Virtio 0.9.5 Specification (PDF)](https://ozlabs.org/~rusty/virtio-spec/virtio-0.9.5.pdf)

你可能会被冗长的步骤吓到，但别担心。一个简单的实现非常简单：

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

    // 1. 重置设备
    virtio_reg_write32(VIRTIO_REG_DEVICE_STATUS, 0);
    // 2. 设置 ACKNOWLEDGE 状态位：已发现设备
    virtio_reg_fetch_and_or32(VIRTIO_REG_DEVICE_STATUS, VIRTIO_STATUS_ACK);
    // 3. 设置 DRIVER 状态位：知道如何使用此设备
    virtio_reg_fetch_and_or32(VIRTIO_REG_DEVICE_STATUS, VIRTIO_STATUS_DRIVER);
    // 设置页面大小：使用 4KB 页面。这用于 PFN（页框编号）的计算
    virtio_reg_write32(VIRTIO_REG_PAGE_SIZE, PAGE_SIZE);
    // 初始化磁盘读写请求用的队列
    blk_request_vq = virtq_init(0);
    // 6. 设置 DRIVER_OK 状态位：现在可以使用设备了
    virtio_reg_write32(VIRTIO_REG_DEVICE_STATUS, VIRTIO_STATUS_DRIVER_OK);

    // 获取磁盘容量。
    blk_capacity = virtio_reg_read64(VIRTIO_REG_DEVICE_CONFIG + 0) * SECTOR_SIZE;
    printf("virtio-blk: capacity is %d bytes\n", (int)blk_capacity);

    // 分配一个区域来存储对设备的请求。
    blk_req_paddr = alloc_pages(align_up(sizeof(*blk_req), PAGE_SIZE) / PAGE_SIZE);
    blk_req = (struct virtio_blk_req *) blk_req_paddr;
}
```

```c [kernel.c] {5}
void kernel_main(void) {
    memset(__bss, 0, (size_t) __bss_end - (size_t) __bss);
    WRITE_CSR(stvec, (uint32_t) kernel_entry);

    virtio_blk_init(); // new
```

这是设备驱动程序的典型初始化模式。重置设备、设置参数，然后启用设备。作为操作系统，我们不需要关心设备内部发生了什么。只需像上面那样执行一些内存读写操作即可。

## Virtqueue 初始化

Virtqueue 应按以下方式初始化：

> 1. Write the virtqueue index (first queue is 0) to the Queue Select field.
> 2. Read the virtqueue size from the Queue Size field, which is always a power of 2. This controls how big the virtqueue is (see below). If this field is 0, the virtqueue does not exist.
> 3. Allocate and zero virtqueue in contiguous physical memory, on a 4096 byte alignment. Write the physical address, divided by 4096 to the Queue Address field.
>
> [Virtio 0.9.5 Specification (PDF)](https://ozlabs.org/~rusty/virtio-spec/virtio-0.9.5.pdf)

下面是一个简单的实现：

```c [kernel.c]
struct virtio_virtq *virtq_init(unsigned index) {
    paddr_t virtq_paddr = alloc_pages(align_up(sizeof(struct virtio_virtq), PAGE_SIZE) / PAGE_SIZE);
    struct virtio_virtq *vq = (struct virtio_virtq *) virtq_paddr;
    vq->queue_index = index;
    vq->used_index = (volatile uint16_t *) &vq->used.index;
    // 选择队列：写入 virtqueue 索引（第一个队列为 0）
    virtio_reg_write32(VIRTIO_REG_QUEUE_SEL, index);
    // 指定队列大小：写入要使用的描述符数量
    virtio_reg_write32(VIRTIO_REG_QUEUE_NUM, VIRTQ_ENTRY_NUM);
    // 写入队列的页框编号（不是物理地址！）
    virtio_reg_write32(VIRTIO_REG_QUEUE_PFN, virtq_paddr / PAGE_SIZE);
    return vq;
}
```

这个函数为 virtqueue 分配一个内存区域，并告诉设备其页框编号（不是物理地址！）。设备将使用这个内存区域来读写请求。

> [!TIP]
>
> 你可能已经注意到，设备驱动程序只是操作系统和设备之间的“粘合剂”。设备将完成所有繁重的工作，如移动磁盘读/写头。驱动程序与运行在设备上的另一个软件(如固件)通信，而不是直接控制硬件。

## 发送 I/O 请求

现在我们有了一个已初始化的 virtio-blk 设备。让我们向磁盘发送一个 I/O 请求。向磁盘发送 I/O 请求是通过_"向 virtqueue 添加处理请求"_实现的，如下所示：

```c [kernel.c]
// 通知设备有新的请求。`desc_index` 是新请求头描述符的索引。
void virtq_kick(struct virtio_virtq *vq, int desc_index) {
    vq->avail.ring[vq->avail.index % VIRTQ_ENTRY_NUM] = desc_index;
    vq->avail.index++;
    __sync_synchronize();
    virtio_reg_write32(VIRTIO_REG_QUEUE_NOTIFY, vq->queue_index);
    vq->last_used_index++;
}

// 返回是否有请求正在被设备处理。
bool virtq_is_busy(struct virtio_virtq *vq) {
    return vq->last_used_index != *vq->used_index;
}

// 从 virtio-blk 设备读取/写入。
void read_write_disk(void *buf, unsigned sector, int is_write) {
    if (sector >= blk_capacity / SECTOR_SIZE) {
        printf("virtio: tried to read/write sector=%d, but capacity is %d\n",
              sector, blk_capacity / SECTOR_SIZE);
        return;
    }

    // 根据 virtio-blk 规范构造请求。
    blk_req->sector = sector;
    blk_req->type = is_write ? VIRTIO_BLK_T_OUT : VIRTIO_BLK_T_IN;
    if (is_write)
        memcpy(blk_req->data, buf, SECTOR_SIZE);

    // 构造 virtqueue 描述符(使用 3 个描述符)。
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

    // 通知设备有新的请求。
    virtq_kick(vq, 0);

    // 等待设备完成处理。
    while (virtq_is_busy(vq))
        ;

    // virtio-blk：如果返回非零值，则表示错误。
    if (blk_req->status != 0) {
        printf("virtio: warn: failed to read/write sector=%d status=%d\n",
               sector, blk_req->status);
        return;
    }

    // 对于读操作，将数据复制到缓冲区。
    if (!is_write)
        memcpy(buf, blk_req->data, SECTOR_SIZE);
}
```

发送请求按以下步骤进行：

1. 在 `blk_req` 中构造请求。指定要访问的扇区号和读/写类型。
2. 构造指向 `blk_req` 各个区域的描述符链(见下文)。
3. 将描述符链的头描述符索引添加到 Available Ring。
4. 通知设备有新的待处理请求。
5. 等待设备完成处理(又称*忙等待*或*轮询*)。
6. 检查设备的响应。

在这里，我们构造了一个由 3 个描述符组成的描述符链。我们需要 3 个描述符，因为每个描述符具有不同的属性(`flags`)，如下所示：

```c
struct virtio_blk_req {
    // 第一个描述符：设备只读
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;

    // 第二个描述符：如果是读操作则设备可写(VIRTQ_DESC_F_WRITE)
    uint8_t data[512];

    // 第三个描述符：设备可写(VIRTQ_DESC_F_WRITE)
    uint8_t status;
} __attribute__((packed));
```

因为我们每次都忙等待直到处理完成，所以我们可以简单地使用环中的*前三个*描述符。但是在实践中，你需要跟踪空闲/已使用的描述符以同时处理多个请求。

## 试一试

最后，让我们尝试磁盘 I/O。在 `kernel.c` 中添加以下代码：

```c [kernel.c] {3-8}
    virtio_blk_init();

    char buf[SECTOR_SIZE];
    read_write_disk(buf, 0, false /* 从磁盘读取 */);
    printf("first sector: %s\n", buf);

    strcpy(buf, "hello from kernel!!!\n");
    read_write_disk(buf, 0, true /* 写入磁盘 */);
```

由于我们指定 `lorem.txt` 作为(原始)磁盘镜像，其内容应该按原样显示：

```
$ ./run.sh

virtio-blk: capacity is 1024 bytes
first sector: Lorem ipsum dolor sit amet, consectetur adipiscing elit ...
```

同时，第一个扇区被覆盖为字符串 "hello from kernel!!!"：

```
$ head lorem.txt
hello from kernel!!!
amet, consectetur adipiscing elit ...
```

恭喜！你已经成功实现了一个磁盘 I/O 驱动程序！

> [!TIP]
> 正如你所注意到的，设备驱动程序只是操作系统和设备之间的“粘合剂”。设备将完成所有繁重的工作，如移动磁盘读/写头。驱动程序与运行在设备上的另一个软件(如固件)通信，而不是直接控制硬件。
