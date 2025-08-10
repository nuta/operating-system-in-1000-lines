# 磁碟輸入/輸出（Disk I/O）

在本章中，我們將實作一個虛擬磁碟裝置的驅動程式：virtio-blk。儘管 virtio-blk 並不存在於實體硬體中，但它使用的介面與真實的磁碟裝置幾乎完全相同。

## Virtio

Virtio 是一種用於虛擬裝置（virtio devices）的裝置介面標準。換句話說，它是驅動程式用來控制裝置的一種 API 標準。就像你使用 HTTP 來存取網頁伺服器一樣，你可以使用 Virtio 來存取 virtio 裝置。Virtio 廣泛應用於虛擬化環境中，例如 QEMU 和 Firecracker。

### Virtqueue

Virtio 裝置中有一種稱為 virtqueue 的結構，顧名思義，它是一個由驅動程式與裝置共享的佇列。簡單來說，一個 virtqueue 包含以下三個區域：

| 名稱            | 撰寫者 | 內容                                                                | 具體內容                                 |
| --------------- | ---------- | ---------------------------------------------------------------------- | ---------------------------------------------------- |
| Descriptor Area | 驅動程式     | 一個描述項（descriptor）表格：儲存請求的位址與大小            | 記憶體位址、長度、下一個描述項的索引 |
| Available Ring  | 驅動程式     | 通知裝置有哪些請求可以開始處理                                     | descriptor 鏈的起始索引（head index）           |
| Used Ring       | 裝置     | 裝置已經處理完成的請求                             | descriptor 鏈的起始索引（head index）            |

![virtqueue diagram](../images/virtio.svg)

每個請求（例如寫入磁碟）由多個描述項（descriptors）組成，這稱為描述項鏈（descriptor chain）。透過使用多個描述項，你可以指定分散的記憶體區塊（即 Scatter-Gather IO），或是設定不同的描述項屬性（例如是否允許裝置寫入）。

例如在寫入磁碟時，virtqueue 的使用流程如下：

1. 驅動程式在 Descriptor 區域中撰寫讀寫請求。
2. 驅動程式將該 descriptor chain 的起始索引加入 Available Ring。
3. 驅動程式通知裝置有新的請求。
4. 裝置從 Available Ring 中讀取請求並處理。
5. 裝置將已處理的 descriptor 索引寫入 Used Ring，並通知驅動程式完成。

詳細資訊可參考 [virtio 規格文件](https://docs.oasis-open.org/virtio/virtio/v1.1/csprd01/virtio-v1.1-csprd01.html)。在本章的實作中，我們會專注於一個名為 virtio-blk 的裝置。

## 啟用 virtio 裝置

在撰寫裝置驅動程式之前，讓我們先準備一個測試用的檔案。請建立一個名為 `lorem.txt` 的檔案，並填入像下面這樣的一些隨機文字內容：

```
$ echo "Lorem ipsum dolor sit amet, consectetur adipiscing elit. In ut magna consequat, cursus velit aliquam, scelerisque odio. Ut lorem eros, feugiat quis bibendum vitae, malesuada ac orci. Praesent eget quam non nunc fringilla cursus imperdiet non tellus. Aenean dictum lobortis turpis, non interdum leo rhoncus sed. Cras in tellus auctor, faucibus tortor ut, maximus metus. Praesent placerat ut magna non tristique. Pellentesque at nunc quis dui tempor vulputate. Vestibulum vitae massa orci. Mauris et tellus quis risus sagittis placerat. Integer lorem leo, feugiat sed molestie non, viverra a tellus." > lorem.txt
```

接著，將 virtio-blk 裝置掛載到 QEMU 上：

```bash [run.sh] {3-4}
$QEMU -machine virt -bios default -nographic -serial mon:stdio --no-reboot \
    -d unimp,guest_errors,int,cpu_reset -D qemu.log \
    -drive id=drive0,file=lorem.txt,format=raw,if=none \            # new
    -device virtio-blk-device,drive=drive0,bus=virtio-mmio-bus.0 \  # new
    -kernel kernel.elf
```

新加入的 QEMU 參數說明如下：

- `-drive id=drive0`：定義一個名為 `drive0` 的磁碟，並使用 `lorem.txt` 作為磁碟映像。磁碟格式為 `raw`，也就是直接將檔案內容視為磁碟資料。
- `-device virtio-blk-device`：加入一個 virtio-blk 裝置，並使用 `drive0` 作為磁碟來源。`bus=virtio-mmio-bus.0` 表示將該裝置掛載到 virtio 的記憶體對應匯流排（MMIO）。

## 定義 C 巨集與結構

首先，讓我們在 `kernel.h` 中加入一些與 virtio 相關的定義：

```c [kernel.h]
#define SECTOR_SIZE       512
#define VIRTQ_ENTRY_NUM   16
#define VIRTIO_DEVICE_BLK 2
#define VIRTIO_BLK_PADDR  0x10001000
#define VIRTIO_REG_MAGIC         0x00
#define VIRTIO_REG_VERSION       0x04
#define VIRTIO_REG_DEVICE_ID     0x08
#define VIRTIO_REG_QUEUE_SEL     0x30
#define VIRTIO_REG_QUEUE_NUM_MAX 0x34
#define VIRTIO_REG_QUEUE_NUM     0x38
#define VIRTIO_REG_QUEUE_ALIGN   0x3c
#define VIRTIO_REG_QUEUE_PFN     0x40
#define VIRTIO_REG_QUEUE_READY   0x44
#define VIRTIO_REG_QUEUE_NOTIFY  0x50
#define VIRTIO_REG_DEVICE_STATUS 0x70
#define VIRTIO_REG_DEVICE_CONFIG 0x100
#define VIRTIO_STATUS_ACK       1
#define VIRTIO_STATUS_DRIVER    2
#define VIRTIO_STATUS_DRIVER_OK 4
#define VIRTIO_STATUS_FEAT_OK   8
#define VIRTQ_DESC_F_NEXT          1
#define VIRTQ_DESC_F_WRITE         2
#define VIRTQ_AVAIL_F_NO_INTERRUPT 1
#define VIRTIO_BLK_T_IN  0
#define VIRTIO_BLK_T_OUT 1

// Virtqueue Descriptor area entry.
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
> `__attribute__((packed))` 是一種編譯器擴充語法，用來告訴編譯器「不要在結構成員之間加入*填充位元（padding）*」。否則，編譯器可能會為了對齊效能，在成員之間自動插入隱藏的填充位元，導致驅動程式與裝置看到的資料格式不一致，進而發生錯誤。

接下來，在 `kernel.c` 中加入存取 MMIO（記憶體對映 I/O）暫存器的輔助函式：

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
> 存取 MMIO（Memory-Mapped I/O）暫存器與存取一般記憶體不同。你應該使用 `volatile` 關鍵字，以防止編譯器將讀寫操作優化掉。在 MMIO 中，對記憶體的存取可能會觸發副作用（例如：向裝置發送指令）。

## 映射 MMIO 區域

首先，要將 `virtio-blk` 的 MMIO 區域映射到頁表中，以便核心可以存取這些 MMIO 暫存器。這個步驟非常簡單：

```c [kernel.c] {8}
struct process *create_process(const void *image, size_t image_size) {
    /* omitted */

    for (paddr_t paddr = (paddr_t) __kernel_base;
         paddr < (paddr_t) __free_ram_end; paddr += PAGE_SIZE)
        map_page(page_table, paddr, paddr, PAGE_R | PAGE_W | PAGE_X);

    map_page(page_table, VIRTIO_BLK_PADDR, VIRTIO_BLK_PADDR, PAGE_R | PAGE_W); // new
```

## Virtio 裝置初始化

初始化流程詳見 [virtio 規範](https://docs.oasis-open.org/virtio/virtio/v1.1/csprd01/virtio-v1.1-csprd01.html#x1-910003)：

> 3.1.1 驅動程式的要求：裝置初始化
> 驅動程式 必須 依照以下順序初始化裝置：
>
> 1. 重設（reset）裝置。
> 2. 設定 ACKNOWLEDGE 狀態位元：表示客體作業系統已經察覺到該裝置。
> 3. 設定 DRIVER 狀態位元：表示客體作業系統知道如何驅動這個裝置。
> 4. 讀取裝置的 feature 位元，然後將作業系統與驅動程式能支援的子集合寫入裝置。在此步驟中，驅動程式可以讀（但不得寫入）裝置特定的設定欄位，以確認是否能支援此裝置。
> 5. 設定 FEATURES_OK 狀態位元。該步驟之後，驅動程式不得再接受新的 feature 位元。
> 6. 再次讀取裝置狀態，確保 FEATURES_OK 仍然被設為 1。否則代表裝置不支援我們指定的 feature 子集合，裝置將無法使用。
> 7. 進行裝置特定的初始化，包括：發現（discover）該裝置的 virtqueue、進行必要的 bus 設定、讀取（必要時也寫入）裝置的 virtio 設定空間、建立 virtqueue。
> 8. 設定 DRIVER_OK 狀態位元。從這一刻起，裝置就算是「啟用」了。

你可能會被這些冗長的步驟搞得眼花撩亂，但別擔心，一個最簡單版本的實作其實非常簡單：

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

    // 1. Reset the device.
    virtio_reg_write32(VIRTIO_REG_DEVICE_STATUS, 0);
    // 2. Set the ACKNOWLEDGE status bit: the guest OS has noticed the device.
    virtio_reg_fetch_and_or32(VIRTIO_REG_DEVICE_STATUS, VIRTIO_STATUS_ACK);
    // 3. Set the DRIVER status bit.
    virtio_reg_fetch_and_or32(VIRTIO_REG_DEVICE_STATUS, VIRTIO_STATUS_DRIVER);
    // 5. Set the FEATURES_OK status bit.
    virtio_reg_fetch_and_or32(VIRTIO_REG_DEVICE_STATUS, VIRTIO_STATUS_FEAT_OK);
    // 7. Perform device-specific setup, including discovery of virtqueues for the device
    blk_request_vq = virtq_init(0);
    // 8. Set the DRIVER_OK status bit.
    virtio_reg_write32(VIRTIO_REG_DEVICE_STATUS, VIRTIO_STATUS_DRIVER_OK);

    // Get the disk capacity.
    blk_capacity = virtio_reg_read64(VIRTIO_REG_DEVICE_CONFIG + 0) * SECTOR_SIZE;
    printf("virtio-blk: capacity is %d bytes\n", blk_capacity);

    // Allocate a region to store requests to the device.
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

##  Virtqueue 初始化

Virtqueue 也需要初始化。我們來閱讀規範內容：

> 虛擬佇列的設定方式如下：
>
> 1. 選擇要使用的佇列，將其索引寫入 QueueSel（第一個佇列為索引 0）。
> 2. 確認該佇列尚未被使用：讀取 QueuePFN，期望回傳值為 0（0x0）。
> 3. 從 QueueNumMax 讀取最大佇列大小（元素數量）。如果回傳值為 0（0x0），表示該佇列無法使用。
> 4. 在一段連續的虛擬記憶體中分配並清除佇列用的頁面，並將 Used Ring 對齊到最佳邊界（通常為頁大小）。驅動程式應選擇小於或等於 QueueNumMax 的佇列大小。
> 5. 將選定的佇列大小寫入 QueueNum，以通知裝置。
> 6. 將 Used Ring 的對齊位元數（以位元組為單位）寫入 QueueAlign，通知裝置。
> 7. 將佇列第一頁的實體頁框編號（PFN）寫入 QueuePFN 暫存器。

以下是一個簡單的實作：

```c [kernel.c]
struct virtio_virtq *virtq_init(unsigned index) {
    // Allocate a region for the virtqueue.
    paddr_t virtq_paddr = alloc_pages(align_up(sizeof(struct virtio_virtq), PAGE_SIZE) / PAGE_SIZE);
    struct virtio_virtq *vq = (struct virtio_virtq *) virtq_paddr;
    vq->queue_index = index;
    vq->used_index = (volatile uint16_t *) &vq->used.index;
    // 1. Select the queue writing its index (first queue is 0) to QueueSel.
    virtio_reg_write32(VIRTIO_REG_QUEUE_SEL, index);
    // 5. Notify the device about the queue size by writing the size to QueueNum.
    virtio_reg_write32(VIRTIO_REG_QUEUE_NUM, VIRTQ_ENTRY_NUM);
    // 6. Notify the device about the used alignment by writing its value in bytes to QueueAlign.
    virtio_reg_write32(VIRTIO_REG_QUEUE_ALIGN, 0);
    // 7. Write the physical number of the first page of the queue to the QueuePFN register.
    virtio_reg_write32(VIRTIO_REG_QUEUE_PFN, virtq_paddr);
    return vq;
}
```

這個函式會為 virtqueue 分配一段記憶體區域，並將其實體位址告訴裝置。裝置將使用這段記憶體來讀寫請求資料。

> [!TIP]
>
> 驅動程式在初始化流程中所做的事，通常包括：檢查裝置能力與功能、分配作業系統資源（如記憶體區段）、以及設定參數。這過程是不是很像網路協定中的握手（handshake）機制呢？

## 傳送 I/O 請求

現在我們已經初始化好一個 virtio-blk 裝置了。接下來，我們要送出一筆 I/O 請求給磁碟。對磁碟的 I/O 請求，是透過「將處理請求加入 virtqueue」的方式來實作的，步驟如下：

```c [kernel.c]
// Notifies the device that there is a new request. `desc_index` is the index
// of the head descriptor of the new request.
void virtq_kick(struct virtio_virtq *vq, int desc_index) {
    vq->avail.ring[vq->avail.index % VIRTQ_ENTRY_NUM] = desc_index;
    vq->avail.index++;
    __sync_synchronize();
    virtio_reg_write32(VIRTIO_REG_QUEUE_NOTIFY, vq->queue_index);
    vq->last_used_index++;
}

// Returns whether there are requests being processed by the device.
bool virtq_is_busy(struct virtio_virtq *vq) {
    return vq->last_used_index != *vq->used_index;
}

// Reads/writes from/to virtio-blk device.
void read_write_disk(void *buf, unsigned sector, int is_write) {
    if (sector >= blk_capacity / SECTOR_SIZE) {
        printf("virtio: tried to read/write sector=%d, but capacity is %d\n",
              sector, blk_capacity / SECTOR_SIZE);
        return;
    }

    // Construct the request according to the virtio-blk specification.
    blk_req->sector = sector;
    blk_req->type = is_write ? VIRTIO_BLK_T_OUT : VIRTIO_BLK_T_IN;
    if (is_write)
        memcpy(blk_req->data, buf, SECTOR_SIZE);

    // Construct the virtqueue descriptors (using 3 descriptors).
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

    // Notify the device that there is a new request.
    virtq_kick(vq, 0);

    // Wait until the device finishes processing.
    while (virtq_is_busy(vq))
        ;

    // virtio-blk: If a non-zero value is returned, it's an error.
    if (blk_req->status != 0) {
        printf("virtio: warn: failed to read/write sector=%d status=%d\n",
               sector, blk_req->status);
        return;
    }

    // For read operations, copy the data into the buffer.
    if (!is_write)
        memcpy(buf, blk_req->data, SECTOR_SIZE);
}
```

傳送一筆請求的步驟如下：

1. 在 `blk_req` 中建立一筆請求。指定你要存取的磁區號（sector number）以及讀取或寫入的類型。
2. 建立一組描述元鏈（descriptor chain），指向 `blk_req` 中的每個區域（見後方描述）。
3. 將描述元鏈（descriptor chain）中第一個描述元的索引值加入 Available Ring 中。
4. 通知裝置：有一筆新的待處理請求。
5. 等待裝置處理完成（這個過程稱為 *busy-waiting* 或 *polling*）。
6. 檢查裝置的回應結果。

在這裡，我們建立了一組由三個描述元組成的描述元鏈。我們需要三個描述元，因為每個描述元具有不同的屬性（`flags`），如下所示：

```c
struct virtio_blk_req {
    // First descriptor: read-only from the device
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;

    // Second descriptor: writable by the device if it's a read operation (VIRTQ_DESC_F_WRITE)
    uint8_t data[512];

    // Third descriptor: writable by the device (VIRTQ_DESC_F_WRITE)
    uint8_t status;
} __attribute__((packed));
```

因為我們每次都會忙等（busy-wait）直到裝置處理完成，所以可以簡單地每次都使用環形緩衝區（ring）中的「前」三個描述元（descriptor）。然而，在實務中，若要同時處理多筆請求，就需要追蹤哪些描述元是「可用的」與「已使用的」。

## 實際試用看看

最後，我們來試試看磁碟 I/O。請將以下程式碼加入 `kernel.c`：

```c [kernel.c] {3-8}
    virtio_blk_init();

    char buf[SECTOR_SIZE];
    read_write_disk(buf, 0, false /* read from the disk */);
    printf("first sector: %s\n", buf);

    strcpy(buf, "hello from kernel!!!\n");
    read_write_disk(buf, 0, true /* write to the disk */);
```

由於我們指定 `lorem.txt` 作為（raw）磁碟映像檔，其內容應該會被原封不動地顯示出來：

```
$ ./run.sh

virtio-blk: capacity is 1024 bytes
first sector: Lorem ipsum dolor sit amet, consectetur adipiscing elit ...
```

接著，我們將第一個區段（sector）覆寫為字串 "hello from kernel!!!"。

```
$ head lorem.txt
hello from kernel!!!
amet, consectetur adipiscing elit ...
```

恭喜你！你已成功實作一個磁碟 I/O 驅動程式！

> [!TIP]
> 正如你可能已經注意到的，裝置驅動程式其實只是作業系統與硬體之間的「膠水」。驅動程式本身不直接控制硬體；它們是透過與硬體上運行的其他軟體（例如：韌體）來進行溝通的。真正負責「出力工作」的是裝置與它們內部的軟體，而不是作業系統的驅動程式。例如像移動磁碟的讀寫磁頭這類工作。