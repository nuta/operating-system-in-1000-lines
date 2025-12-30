---
title: ディスク読み書き
---

# ディスク読み書き

## Virtio入門

Virtioは、仮想マシンとホストOS間でデータをやり取りするための仕組みです。各virtioデバイスは1つ以上のvirtqueueを持ちます。virtqueueは次の3つのメモリ領域から構成されます。

> [!NOTE]
>
> [最新のVirtio仕様書](https://docs.oasis-open.org/virtio/virtio/v1.3/csd01/virtio-v1.3-csd01.html) ではLegacyとModernの2つのインターフェースが定義されています。この実装では**Legacyインターフェース**を使用します。Modernとの違いは少なく、若干シンプルなためです。
>
> 詳細は[Legacy仕様書 (PDF)](https://ozlabs.org/~rusty/virtio-spec/virtio-0.9.5.pdf)を参照するか、[最新のHTML仕様書](https://docs.oasis-open.org/virtio/virtio/v1.3/csd01/virtio-v1.3-csd01.html)で*Legacy Interface:*から始まるセクションを検索してください。

| 名前                   | 誰が書き込むか | 内容                               | 各エントリの内容                                         |
| ---------------------- | -------------- | ---------------------------------- | -------------------------------------------------------- |
| ディスクリプタテーブル | ドライバ          | ディスクリプタ表 | メモリアドレス、長さ、続きのディスクリプタのインデックス |
| availableリング        | ドライバ       | デバイスへの処理要求               | ディスクリプタチェーンの先頭インデックス                 |
| usedリング             | デバイス       | デバイスによって処理済みの処理要求 | ディスクリプタチェーンの先頭インデックス                 |

![virtqueue diagram](../images/virtio.svg)

各処理要求 (例: ディスクへの書き込み) は複数のディスクリプタから構成され、ディスクリプタチェーンと呼びます。複数のディスクリプタに分けることで、飛び飛びのメモリデータを指定したり (いわゆる Scatter-Gather IO)、異なるディスクリプタ属性 (デバイスから書き込み可能か) を持たせたりできます。

詳細は [virtioの仕様書](https://docs.oasis-open.org/virtio/virtio/v1.1/csprd01/virtio-v1.1-csprd01.html) を参照してください。今回実装するのは、virtio-blkというデバイスです。

## virtioデバイスの有効化

virtioデバイスドライバを書く前に、適当なテストファイルを作成しておきます。`lorem.txt`というファイルを作成し、その中に適当な文章を書き込んでおきます。

```
$ echo "Lorem ipsum dolor sit amet, consectetur adipiscing elit. In ut magna consequat, cursus velit aliquam, scelerisque odio. Ut lorem eros, feugiat quis bibendum vitae, malesuada ac orci. Praesent eget quam non nunc fringilla cursus imperdiet non tellus. Aenean dictum lobortis turpis, non interdum leo rhoncus sed. Cras in tellus auctor, faucibus tortor ut, maximus metus. Praesent placerat ut magna non tristique. Pellentesque at nunc quis dui tempor vulputate. Vestibulum vitae massa orci. Mauris et tellus quis risus sagittis placerat. Integer lorem leo, feugiat sed molestie non, viverra a tellus." > lorem.txt
```

また、QEMUにvirtio-blkデバイスを追加するために、QEMUのオプションを変更します。

```bash [run.sh] {3-4}
$QEMU -machine virt -bios default -nographic -serial mon:stdio --no-reboot \
    -d unimp,guest_errors,int,cpu_reset -D qemu.log \
    -drive id=drive0,file=lorem.txt,format=raw,if=none \
    -device virtio-blk-device,drive=drive0,bus=virtio-mmio-bus.0 \
    -kernel kernel.elf
```

新たに追加したオプションは次の通りです:

- `-drive id=drive0`: ディスク`drive0`を定義。`lorem.txt`をディスクイメージとしてQEMUに渡す。ディスクイメージの形式は`raw` (ファイルの内容をそのままディスクデータとして扱う)。
- `-device virtio-blk-device`: virtio-blkデバイスを追加する。ディスク`drive0`に接続する。`bus=virtio-mmio-bus.0`を指定することで、MMIO (Memory Mapped I/O) 領域にデバイスをマップする。

## 雑多な定義

まずは雑多な定義を`kernel.h`に追加します。

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

struct virtq_desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} __attribute__((packed));

struct virtq_avail {
    uint16_t flags;
    uint16_t index;
    uint16_t ring[VIRTQ_ENTRY_NUM];
} __attribute__((packed));

struct virtq_used_elem {
    uint32_t id;
    uint32_t len;
} __attribute__((packed));

struct virtq_used {
    uint16_t flags;
    uint16_t index;
    struct virtq_used_elem ring[VIRTQ_ENTRY_NUM];
} __attribute__((packed));

struct virtio_virtq {
    struct virtq_desc descs[VIRTQ_ENTRY_NUM];
    struct virtq_avail avail;
    struct virtq_used used __attribute__((aligned(PAGE_SIZE)));
    int queue_index;
    volatile uint16_t *used_index;
    uint16_t last_used_index;
} __attribute__((packed));

struct virtio_blk_req {
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;
    uint8_t data[512];
    uint8_t status;
} __attribute__((packed));
```

続いてvirtioデバイスのMMIO上のレジスタを操作するための便利な関数を `kernel.c` に追加します。

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

## デバイスドライバの初期化

まずは、各プロセスのページテーブルに `virtio-blk` のMMIO領域をマップします。一行付け足すだけです。

```c [kernel.c] {8}
struct process *create_process(const void *image, size_t image_size) {
    /* 省略 */

    for (paddr_t paddr = (paddr_t) __kernel_base;
         paddr < (paddr_t) __free_ram_end; paddr += PAGE_SIZE)
        map_page(page_table, paddr, paddr, PAGE_R | PAGE_W | PAGE_X);

    map_page(page_table, VIRTIO_BLK_PADDR, VIRTIO_BLK_PADDR, PAGE_R | PAGE_W); // new
```

## Virtioデバイスの初期化

初期化手順は仕様書に以下のように記載されています:

> 1. Reset the device. This is not required on initial start up.
> 2. The ACKNOWLEDGE status bit is set: we have noticed the device.
> 3. The DRIVER status bit is set: we know how to drive the device.
> 4. Device-specific setup, including reading the Device Feature Bits, discovery of virtqueues for the device, optional MSI-X setup, and reading and possibly writing the virtio configuration space.
> 5. The subset of Device Feature Bits understood by the driver is written to the device.
> 6. The DRIVER_OK status bit is set.
>
> [Virtio 0.9.5 Specification (PDF)](https://ozlabs.org/~rusty/virtio-spec/virtio-0.9.5.pdf)

長いステップに圧倒されるかもしれませんが、心配無用です。シンプルな実装は非常に簡単です:

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

    // 1. デバイスをリセット
    virtio_reg_write32(VIRTIO_REG_DEVICE_STATUS, 0);
    // 2. ACKNOWLEDGEステータスビットを設定: デバイスを認識した
    virtio_reg_fetch_and_or32(VIRTIO_REG_DEVICE_STATUS, VIRTIO_STATUS_ACK);
    // 3. DRIVERステータスビットを設定: デバイスの使い方を知っている
    virtio_reg_fetch_and_or32(VIRTIO_REG_DEVICE_STATUS, VIRTIO_STATUS_DRIVER);
    // ページサイズを設定: 4KBページを使用。PFN (ページフレーム番号) の計算に使われる
    virtio_reg_write32(VIRTIO_REG_PAGE_SIZE, PAGE_SIZE);
    // ディスク読み書き用のキューを初期化
    blk_request_vq = virtq_init(0);
    // 6. DRIVER_OKステータスビットを設定: デバイスが使用可能になった
    virtio_reg_write32(VIRTIO_REG_DEVICE_STATUS, VIRTIO_STATUS_DRIVER_OK);

    // ディスクの容量を取得
    blk_capacity = virtio_reg_read64(VIRTIO_REG_DEVICE_CONFIG + 0) * SECTOR_SIZE;
    printf("virtio-blk: capacity is %d bytes\n", (int)blk_capacity);

    // デバイスへの処理要求を格納する領域を確保
    blk_req_paddr = alloc_pages(align_up(sizeof(*blk_req), PAGE_SIZE) / PAGE_SIZE);
    blk_req = (struct virtio_blk_req *) blk_req_paddr;
}
```

```c [kernel.c] {5}
void kernel_main(void) {
    memset(__bss, 0, (size_t) __bss_end - (size_t) __bss);
    WRITE_CSR(stvec, (uint32_t) kernel_entry);

    virtio_blk_init();
```

これはデバイスドライバの典型的な初期化パターンです。デバイスをリセットし、パラメータを設定し、デバイスを有効化します。OS側はデバイス内部で何が起こっているかを気にする必要はありません。上記のようなメモリ読み書き操作を行うだけです。

## Virtqueueの初期化

virtqueueも以下のように初期化する必要があります:

> 1. Write the virtqueue index (first queue is 0) to the Queue Select field.
> 2. Read the virtqueue size from the Queue Size field, which is always a power of 2. This controls how big the virtqueue is (see below). If this field is 0, the virtqueue does not exist.
> 3. Allocate and zero virtqueue in contiguous physical memory, on a 4096 byte alignment. Write the physical address, divided by 4096 to the Queue Address field.
>
> [Virtio 0.9.5 Specification (PDF)](https://ozlabs.org/~rusty/virtio-spec/virtio-0.9.5.pdf)

```c [kernel.c]
struct virtio_virtq *virtq_init(unsigned index) {
    paddr_t virtq_paddr = alloc_pages(align_up(sizeof(struct virtio_virtq), PAGE_SIZE) / PAGE_SIZE);
    struct virtio_virtq *vq = (struct virtio_virtq *) virtq_paddr;
    vq->queue_index = index;
    vq->used_index = (volatile uint16_t *) &vq->used.index;
    // キューを選択: virtqueueのインデックスを書き込む (最初のキューは0)
    virtio_reg_write32(VIRTIO_REG_QUEUE_SEL, index);
    // キューサイズを指定: 使用するディスクリプタの数を書き込む
    virtio_reg_write32(VIRTIO_REG_QUEUE_NUM, VIRTQ_ENTRY_NUM);
    // キューのページフレーム番号 (物理アドレスではない!) を書き込む
    virtio_reg_write32(VIRTIO_REG_QUEUE_PFN, virtq_paddr / PAGE_SIZE);
    return vq;
}
```

ここで指定しているアドレスは、割り当てた `struct virtio_virtq` の領域のページフレーム番号 (物理アドレスではなく!) です。この中にディスクリプタテーブル、availableリング、usedリングが格納されます。

## IOリクエストの送信

初期化ができたので、ディスクへのIOリクエストを送信してみましょう。ディスクへのIOリクエストは、以下のように「virtqueueへの処理要求の追加」で行います。

```c [kernel.c]
// デバイスに新しいリクエストがあることを通知する。desc_indexは、新しいリクエストの
// 先頭ディスクリプタのインデックス。
void virtq_kick(struct virtio_virtq *vq, int desc_index) {
    vq->avail.ring[vq->avail.index % VIRTQ_ENTRY_NUM] = desc_index;
    vq->avail.index++;
    __sync_synchronize();
    virtio_reg_write32(VIRTIO_REG_QUEUE_NOTIFY, vq->queue_index);
    vq->last_used_index++;
}

// デバイスが処理中のリクエストがあるかどうかを返す。
bool virtq_is_busy(struct virtio_virtq *vq) {
    return vq->last_used_index != *vq->used_index;
}

// virtio-blkデバイスの読み書き。
void read_write_disk(void *buf, unsigned sector, int is_write) {
    if (sector >= blk_capacity / SECTOR_SIZE) {
        printf("virtio: tried to read/write sector=%d, but capacity is %d\n",
              sector, blk_capacity / SECTOR_SIZE);
        return;
    }

    // virtio-blkの仕様に従って、リクエストを構築する
    blk_req->sector = sector;
    blk_req->type = is_write ? VIRTIO_BLK_T_OUT : VIRTIO_BLK_T_IN;
    if (is_write)
        memcpy(blk_req->data, buf, SECTOR_SIZE);

    // virtqueueのディスクリプタを構築する (3つのディスクリプタを使う)
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

    // デバイスに新しいリクエストがあることを通知する
    virtq_kick(vq, 0);

    // デバイス側の処理が終わるまで待つ
    while (virtq_is_busy(vq))
        ;

    // virtio-blk: 0でない値が返ってきたらエラー
    if (blk_req->status != 0) {
        printf("virtio: warn: failed to read/write sector=%d status=%d\n",
               sector, blk_req->status);
        return;
    }

    // 読み込み処理の場合は、バッファにデータをコピーする
    if (!is_write)
        memcpy(buf, blk_req->data, SECTOR_SIZE);
}
```

大まかには、以下のような流れでリクエストを送信しています。

1. `blk_req` にリクエストを構築する。アクセスしたいセクタ番号と、読み書きの種類を指定します。
2. `blk_req` の各領域を指すディスクリプタチェーンを構築する。
3. ディスクリプタチェーンの先頭ディスクリプタのインデックスを `avail` リングに追加する。
4. デバイスに「新しい処理すべき処理要求がある」ことを通知する。
5. デバイスが処理を終えるまで待つ。
6. デバイスからの応答を確認する。

ここでは、3のディスクリプタから構成されるディスクリプタチェーンを構築しています。3つに分けているのは、次のように各ディスクリプタが異なる属性 (`flags`) を持つためです。

```c [kernel.h]
struct virtio_blk_req {
    // 1つ目のディスクリプタ: デバイスからは読み込み専用
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;

    // 2つ目のディスクリプタ: 読み込み処理の場合は、デバイスから書き込み可 (VIRTQ_DESC_F_WRITE)
    uint8_t data[512];

    // 3つ目のディスクリプタ: デバイスから書き込み可 (VIRTQ_DESC_F_WRITE)
    uint8_t status;
} __attribute__((packed));
```

今回は処理が終わるまでビジーウェイトしているため、毎回同じディスクリプタを使っています (0から2番目)。

## ディスクの読み込みテスト

最後に、ディスクの読み書きができることを確認しましょう。

```c [kernel.c] {3-8}
    virtio_blk_init();

    char buf[SECTOR_SIZE];
    read_write_disk(buf, 0, false);
    printf("first sector: %s\n", buf);

    strcpy(buf, "hello from kernel!!!\n");
    read_write_disk(buf, 0, true);
```

ディスクイメージとして `lorem.txt` を指定しているので、ファイルの中身がそのまま表示されるはずです。

```
$ ./run.sh

virtio-blk: capacity is 1024 bytes
first sector: Lorem ipsum dolor sit amet, consectetur adipiscing elit ...
```

また、先頭セクタに書き込んだ内容が `lorem.txt` の冒頭に反映されていれば完璧です。

```
$ head lorem.txt
hello from kernel!!!
amet, consectetur adipiscing elit ...
```
