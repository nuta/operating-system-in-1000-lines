#include "kernel.h"
#include "common.h"
extern char __kernel_base[];
extern char __stack_top[];
extern char __bss[], __bss_end[];
extern char __free_ram[], __free_ram_end[];
extern char _binary_shell_bin_start[], _binary_shell_bin_size[];
struct process procs[PROCS_MAX];
struct process *current_proc;
struct process *idle_proc;
paddr_t alloc_pages(uint32_t n) {
    static paddr_t next_paddr = (paddr_t) __free_ram;
    paddr_t paddr = next_paddr;
    next_paddr += n * PAGE_SIZE;
    if (next_paddr > (paddr_t) __free_ram_end)
        PANIC("out of memory");
    memset((void *) paddr, 0, n * PAGE_SIZE);
    return paddr;
}
void map_page(uint32_t *table1, uint32_t vaddr, paddr_t paddr, uint32_t flags) {
    if (!is_aligned(vaddr, PAGE_SIZE))
        PANIC("unaligned vaddr %x", vaddr);
    if (!is_aligned(paddr, PAGE_SIZE))
        PANIC("unaligned paddr %x", paddr);
    uint32_t vpn1 = (vaddr >> 22) & 0x3ff;
    if ((table1[vpn1] & PAGE_V) == 0) {
        uint32_t pt_paddr = alloc_pages(1);
        table1[vpn1] = ((pt_paddr / PAGE_SIZE) << 10) | PAGE_V;
    }
    uint32_t vpn0 = (vaddr >> 12) & 0x3ff;
    uint32_t *table0 = (uint32_t *) ((table1[vpn1] >> 10) * PAGE_SIZE);
    table0[vpn0] = ((paddr / PAGE_SIZE) << 10) | flags | PAGE_V;
}
struct sbiret sbi_call(long arg0, long arg1, long arg2, long arg3, long arg4,
                       long arg5, long fid, long eid) {
    register long a0 __asm__("a0") = arg0;
    register long a1 __asm__("a1") = arg1;
    register long a2 __asm__("a2") = arg2;
    register long a3 __asm__("a3") = arg3;
    register long a4 __asm__("a4") = arg4;
    register long a5 __asm__("a5") = arg5;
    register long a6 __asm__("a6") = fid;
    register long a7 __asm__("a7") = eid;
    __asm__ __volatile__("ecall"
                         : "=r"(a0), "=r"(a1)
                         : "r"(a0), "r"(a1), "r"(a2), "r"(a3), "r"(a4), "r"(a5),
                           "r"(a6), "r"(a7)
                         : "memory");
    return (struct sbiret){.error = a0, .value = a1};
}
struct virtio_virtq *blk_request_vq;
struct virtio_blk_req *blk_req;
paddr_t blk_req_paddr;
uint64_t blk_capacity;
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
bool virtq_is_busy(struct virtio_virtq *vq) {
    return vq->last_used_index != *vq->used_index;
}
void virtq_kick(struct virtio_virtq *vq, int desc_index) {
    vq->avail.ring[vq->avail.index % VIRTQ_ENTRY_NUM] = desc_index;
    vq->avail.index++;
    __sync_synchronize();
    virtio_reg_write32(VIRTIO_REG_QUEUE_NOTIFY, vq->queue_index);
    vq->last_used_index++;
}
struct virtio_virtq *virtq_init(unsigned index) {
    paddr_t virtq_paddr = alloc_pages(align_up(sizeof(struct virtio_virtq), PAGE_SIZE) / PAGE_SIZE);
    struct virtio_virtq *vq = (struct virtio_virtq *) virtq_paddr;
    vq->queue_index = index;
    vq->used_index = (volatile uint16_t *) &vq->used.index;
    virtio_reg_write32(VIRTIO_REG_QUEUE_SEL, index);
    virtio_reg_write32(VIRTIO_REG_QUEUE_NUM, VIRTQ_ENTRY_NUM);
    virtio_reg_write32(VIRTIO_REG_QUEUE_PFN, virtq_paddr / PAGE_SIZE);
    return vq;
}
void virtio_blk_init(void) {
    if (virtio_reg_read32(VIRTIO_REG_MAGIC) != 0x74726976)
        PANIC("virtio: invalid magic value");
    if (virtio_reg_read32(VIRTIO_REG_VERSION) != 1)
        PANIC("virtio: invalid version");
    if (virtio_reg_read32(VIRTIO_REG_DEVICE_ID) != VIRTIO_DEVICE_BLK)
        PANIC("virtio: invalid device id");
    virtio_reg_write32(VIRTIO_REG_DEVICE_STATUS, 0);
    virtio_reg_fetch_and_or32(VIRTIO_REG_DEVICE_STATUS, VIRTIO_STATUS_ACK);
    virtio_reg_fetch_and_or32(VIRTIO_REG_DEVICE_STATUS, VIRTIO_STATUS_DRIVER);
    virtio_reg_write32(VIRTIO_REG_PAGE_SIZE, PAGE_SIZE);
    blk_request_vq = virtq_init(0);
    virtio_reg_write32(VIRTIO_REG_DEVICE_STATUS, VIRTIO_STATUS_DRIVER_OK);
    blk_capacity = virtio_reg_read64(VIRTIO_REG_DEVICE_CONFIG + 0) * SECTOR_SIZE;
    printf("virtio-blk: capacity is %d bytes\n", (int)blk_capacity);
    blk_req_paddr = alloc_pages(align_up(sizeof(*blk_req), PAGE_SIZE) / PAGE_SIZE);
    blk_req = (struct virtio_blk_req *) blk_req_paddr;
}
void read_write_disk(void *buf, unsigned sector, int is_write) {
    if (sector >= blk_capacity / SECTOR_SIZE) {
        printf("virtio: tried to read/write sector=%d, but capacity is %d\n",
              sector, blk_capacity / SECTOR_SIZE);
        return;
    }
    blk_req->sector = sector;
    blk_req->type = is_write ? VIRTIO_BLK_T_OUT : VIRTIO_BLK_T_IN;
    if (is_write)
        memcpy(blk_req->data, buf, SECTOR_SIZE);
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
    virtq_kick(vq, 0);
    while (virtq_is_busy(vq))
        ;
    if (blk_req->status != 0) {
        printf("virtio: warn: failed to read/write sector=%d status=%d\n",
               sector, blk_req->status);
        return;
    }
    if (!is_write)
        memcpy(buf, blk_req->data, SECTOR_SIZE);
}
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
void fs_flush(void) {
    memset(disk, 0, sizeof(disk));
    unsigned off = 0;
    for (int file_i = 0; file_i < FILES_MAX; file_i++) {
        struct file *file = &files[file_i];
        if (!file->in_use || file_i == 0) continue;
        struct tar_header *header = (struct tar_header *) &disk[off];
        memset(header, 0, sizeof(*header));
        if (file->parent == -1 || file->parent == 0) strcpy(header->name, file->name);
        else { struct file *parent = &files[file->parent]; char path[128]; strcpy(path, parent->name); int len = strlen(path); path[len++] = '/'; strcpy(path + len, file->name); strcpy(header->name, path); }
        strcpy(header->mode, file->is_dir ? "0000755" : "0000644");
        strcpy(header->magic, "ustar");
        strcpy(header->version, "00");
        header->type = file->is_dir ? '5' : '0';
        int filesz = file->size;
        for (int i = sizeof(header->size); i > 0; i--) { header->size[i - 1] = (filesz % 8) + '0'; filesz /= 8; }
        int checksum = ' ' * sizeof(header->checksum);
        for (unsigned i = 0; i < sizeof(struct tar_header); i++) checksum += (unsigned char) disk[off + i];
        for (int i = 5; i >= 0; i--) { header->checksum[i] = (checksum % 8) + '0'; checksum /= 8; }
        if (!file->is_dir) memcpy(header->data, file->data, file->size);
        off += align_up(sizeof(struct tar_header) + file->size, SECTOR_SIZE);
    }
    for (unsigned sector = 0; sector < sizeof(disk) / SECTOR_SIZE; sector++) read_write_disk(&disk[sector * SECTOR_SIZE], sector, true);
    printf("wrote %d bytes to disk\n", sizeof(disk));
}
void fs_init(void) {
    memset(files, 0, sizeof(files));
    files[0].in_use = true;
    files[0].is_dir = true;
    files[0].name[0] = '/';
    files[0].name[1] = '\0';
    files[0].parent = -1;
    for (unsigned sector = 0; sector < sizeof(disk) / SECTOR_SIZE; sector++) read_write_disk(&disk[sector * SECTOR_SIZE], sector, false);
    unsigned off = 0;
    int file_idx = 1;
    for (int i = 0; i < FILES_MAX - 1; i++) {
        struct tar_header *header = (struct tar_header *) &disk[off];
        if (header->name[0] == '\0') break;
        if (strcmp(header->magic, "ustar") != 0) { off += SECTOR_SIZE; continue; }
        int filesz = oct2int(header->size, sizeof(header->size));
        if (file_idx >= FILES_MAX) break;
        struct file *file = &files[file_idx++];
        file->in_use = true;
        file->is_dir = (header->type == '5');
        file->size = filesz;
        char *last_slash = strrchr(header->name, '/');
        if (last_slash) {
            strcpy(file->name, last_slash + 1);
            char parent_path[128];
            int parent_len = last_slash - header->name;
            memcpy(parent_path, header->name, parent_len);
            parent_path[parent_len] = '\0';
            struct file *parent = &files[0];
            for (int j = 1; j < file_idx; j++) { if (files[j].is_dir && !strcmp(files[j].name, parent_path)) { parent = &files[j]; break; } }
            file->parent = parent - files;
            parent->children[parent->child_count++] = file - files;
        } else {
            strcpy(file->name, header->name);
            file->parent = 0;
            files[0].children[files[0].child_count++] = file - files;
        }
        if (!file->is_dir && filesz > 0) memcpy(file->data, header->data, filesz);
        printf("%s: %s, size=%d\n", file->is_dir ? "dir" : "file", file->name, file->size);
        off += align_up(sizeof(struct tar_header) + filesz, SECTOR_SIZE);
    }
}
struct file *fs_lookup_path(const char *path) {
    if (path[0] != '/') return NULL;
    struct file *current = &files[0];
    if (!strcmp(path, "/")) return current;
    char path_copy[128];
    strcpy(path_copy, path + 1);
    char *token = path_copy;
    for (;;) {
        char *next = token;
        while (*next && *next != '/') next++;
        bool is_last = (*next == '\0');
        if (*next == '/') *next++ = '\0';
        if (current->is_dir) {
            bool found = false;
            for (int i = 0; i < current->child_count; i++) {
                struct file *child = &files[current->children[i]];
                if (!strcmp(child->name, token)) {
                    if (is_last) return child;
                    current = child;
                    found = true;
                    break;
                }
            }
            if (!found) return NULL;
            token = next;
            if (is_last) break;
        } else return NULL;
    }
    return current;
}
struct file *fs_lookup(const char *filename) { return fs_lookup_path(filename); }
int fs_mkdir(const char *path) {
    if (path[0] != '/') return -1;
    if (fs_lookup_path(path) != NULL) return -1;
    struct file *parent = NULL;
    char *last_slash = NULL;
    for (char *p = (char*)path; *p; p++) if (*p == '/') last_slash = p;
    if (last_slash && last_slash[1] != '\0') {
        char parent_path[128];
        int len = last_slash - path;
        if (len == 0) strcpy(parent_path, "/");
        else { memcpy(parent_path, path, len); parent_path[len] = '\0'; }
        parent = fs_lookup_path(parent_path);
        if (!parent || !parent->is_dir) return -1;
    } else return -1;
    char *dirname = last_slash ? last_slash + 1 : (char*)path + 1;
    for (int i = 0; i < FILES_MAX; i++) {
        if (!files[i].in_use) {
            files[i].in_use = true;
            files[i].is_dir = true;
            files[i].size = 0;
            strcpy(files[i].name, dirname);
            files[i].parent = parent - files;
            if (parent) parent->children[parent->child_count++] = i;
            return 0;
        }
    }
    return -1;
}
void putchar(char ch) {
    sbi_call(ch, 0, 0, 0, 0, 0, 0, 1 /* Console Putchar */);
}
long getchar(void) {
    struct sbiret ret = sbi_call(0, 0, 0, 0, 0, 0, 0, 2);
    return ret.error;
}
__attribute__((naked))
__attribute__((aligned(4)))
void kernel_entry(void) {
    __asm__ __volatile__(
        "csrrw sp, sscratch, sp\n"
        "addi sp, sp, -4 * 31\n"
        "sw ra,  4 * 0(sp)\n"
        "sw gp,  4 * 1(sp)\n"
        "sw tp,  4 * 2(sp)\n"
        "sw t0,  4 * 3(sp)\n"
        "sw t1,  4 * 4(sp)\n"
        "sw t2,  4 * 5(sp)\n"
        "sw t3,  4 * 6(sp)\n"
        "sw t4,  4 * 7(sp)\n"
        "sw t5,  4 * 8(sp)\n"
        "sw t6,  4 * 9(sp)\n"
        "sw a0,  4 * 10(sp)\n"
        "sw a1,  4 * 11(sp)\n"
        "sw a2,  4 * 12(sp)\n"
        "sw a3,  4 * 13(sp)\n"
        "sw a4,  4 * 14(sp)\n"
        "sw a5,  4 * 15(sp)\n"
        "sw a6,  4 * 16(sp)\n"
        "sw a7,  4 * 17(sp)\n"
        "sw s0,  4 * 18(sp)\n"
        "sw s1,  4 * 19(sp)\n"
        "sw s2,  4 * 20(sp)\n"
        "sw s3,  4 * 21(sp)\n"
        "sw s4,  4 * 22(sp)\n"
        "sw s5,  4 * 23(sp)\n"
        "sw s6,  4 * 24(sp)\n"
        "sw s7,  4 * 25(sp)\n"
        "sw s8,  4 * 26(sp)\n"
        "sw s9,  4 * 27(sp)\n"
        "sw s10, 4 * 28(sp)\n"
        "sw s11, 4 * 29(sp)\n"
        "csrr a0, sscratch\n"
        "sw a0,  4 * 30(sp)\n"
        "addi a0, sp, 4 * 31\n"
        "csrw sscratch, a0\n"
        "mv a0, sp\n"
        "call handle_trap\n"
        "lw ra,  4 * 0(sp)\n"
        "lw gp,  4 * 1(sp)\n"
        "lw tp,  4 * 2(sp)\n"
        "lw t0,  4 * 3(sp)\n"
        "lw t1,  4 * 4(sp)\n"
        "lw t2,  4 * 5(sp)\n"
        "lw t3,  4 * 6(sp)\n"
        "lw t4,  4 * 7(sp)\n"
        "lw t5,  4 * 8(sp)\n"
        "lw t6,  4 * 9(sp)\n"
        "lw a0,  4 * 10(sp)\n"
        "lw a1,  4 * 11(sp)\n"
        "lw a2,  4 * 12(sp)\n"
        "lw a3,  4 * 13(sp)\n"
        "lw a4,  4 * 14(sp)\n"
        "lw a5,  4 * 15(sp)\n"
        "lw a6,  4 * 16(sp)\n"
        "lw a7,  4 * 17(sp)\n"
        "lw s0,  4 * 18(sp)\n"
        "lw s1,  4 * 19(sp)\n"
        "lw s2,  4 * 20(sp)\n"
        "lw s3,  4 * 21(sp)\n"
        "lw s4,  4 * 22(sp)\n"
        "lw s5,  4 * 23(sp)\n"
        "lw s6,  4 * 24(sp)\n"
        "lw s7,  4 * 25(sp)\n"
        "lw s8,  4 * 26(sp)\n"
        "lw s9,  4 * 27(sp)\n"
        "lw s10, 4 * 28(sp)\n"
        "lw s11, 4 * 29(sp)\n"
        "lw sp,  4 * 30(sp)\n"
        "sret\n"
    );
}
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
__attribute__((naked)) void switch_context(uint32_t *prev_sp,
                                           uint32_t *next_sp) {
    __asm__ __volatile__(
        "addi sp, sp, -13 * 4\n"
        "sw ra,  0  * 4(sp)\n"
        "sw s0,  1  * 4(sp)\n"
        "sw s1,  2  * 4(sp)\n"
        "sw s2,  3  * 4(sp)\n"
        "sw s3,  4  * 4(sp)\n"
        "sw s4,  5  * 4(sp)\n"
        "sw s5,  6  * 4(sp)\n"
        "sw s6,  7  * 4(sp)\n"
        "sw s7,  8  * 4(sp)\n"
        "sw s8,  9  * 4(sp)\n"
        "sw s9,  10 * 4(sp)\n"
        "sw s10, 11 * 4(sp)\n"
        "sw s11, 12 * 4(sp)\n"
        "sw sp, (a0)\n"
        "lw sp, (a1)\n"
        "lw ra,  0  * 4(sp)\n"
        "lw s0,  1  * 4(sp)\n"
        "lw s1,  2  * 4(sp)\n"
        "lw s2,  3  * 4(sp)\n"
        "lw s3,  4  * 4(sp)\n"
        "lw s4,  5  * 4(sp)\n"
        "lw s5,  6  * 4(sp)\n"
        "lw s6,  7  * 4(sp)\n"
        "lw s7,  8  * 4(sp)\n"
        "lw s8,  9  * 4(sp)\n"
        "lw s9,  10 * 4(sp)\n"
        "lw s10, 11 * 4(sp)\n"
        "lw s11, 12 * 4(sp)\n"
        "addi sp, sp, 13 * 4\n"
        "ret\n"
    );
}
struct process *create_process(const void *image, size_t image_size) {
    struct process *proc = NULL;
    int i;
    for (i = 0; i < PROCS_MAX; i++) {
        if (procs[i].state == PROC_UNUSED) {
            proc = &procs[i];
            break;
        }
    }
    if (!proc)
        PANIC("no free process slots");
    uint32_t *sp = (uint32_t *) &proc->stack[sizeof(proc->stack)];
    *--sp = 0;                      // s11
    *--sp = 0;                      // s10
    *--sp = 0;                      // s9
    *--sp = 0;                      // s8
    *--sp = 0;                      // s7
    *--sp = 0;                      // s6
    *--sp = 0;                      // s5
    *--sp = 0;                      // s4
    *--sp = 0;                      // s3
    *--sp = 0;                      // s2
    *--sp = 0;                      // s1
    *--sp = 0;                      // s0
    *--sp = (uint32_t) user_entry;  // ra
    uint32_t *page_table = (uint32_t *) alloc_pages(1);
    for (paddr_t paddr = (paddr_t) __kernel_base;
         paddr < (paddr_t) __free_ram_end; paddr += PAGE_SIZE)
        map_page(page_table, paddr, paddr, PAGE_R | PAGE_W | PAGE_X);
    map_page(page_table, VIRTIO_BLK_PADDR, VIRTIO_BLK_PADDR, PAGE_R | PAGE_W);
    for (uint32_t off = 0; off < image_size; off += PAGE_SIZE) {
        paddr_t page = alloc_pages(1);
        size_t remaining = image_size - off;
        size_t copy_size = PAGE_SIZE <= remaining ? PAGE_SIZE : remaining;
        memcpy((void *) page, image + off, copy_size);
        map_page(page_table, USER_BASE + off, page,
                 PAGE_U | PAGE_R | PAGE_W | PAGE_X);
    }
    proc->pid = i + 1;
    proc->state = PROC_RUNNABLE;
    proc->sp = (uint32_t) sp;
    proc->page_table = page_table;
    return proc;
}
void yield(void) {
    struct process *next = idle_proc;
    for (int i = 0; i < PROCS_MAX; i++) {
        struct process *proc = &procs[(current_proc->pid + i) % PROCS_MAX];
        if (proc->state == PROC_RUNNABLE && proc->pid > 0) {
            next = proc;
            break;
        }
    }
    if (next == current_proc)
        return;
    struct process *prev = current_proc;
    current_proc = next;
    __asm__ __volatile__(
        "sfence.vma\n"
        "csrw satp, %[satp]\n"
        "sfence.vma\n"
        "csrw sscratch, %[sscratch]\n"
        :
        : [satp] "r" (SATP_SV32 | ((uint32_t) next->page_table / PAGE_SIZE)),
          [sscratch] "r" ((uint32_t) &next->stack[sizeof(next->stack)])
    );
    switch_context(&prev->sp, &next->sp);
}
void handle_syscall(struct trap_frame *f) {
    switch (f->a3) {
        case SYS_PUTCHAR:
            putchar(f->a0);
            break;
        case SYS_GETCHAR:
            while (1) {
                long ch = getchar();
                if (ch >= 0) {
                    f->a0 = ch;
                    break;
                }
                yield();
            }
            break;
        case SYS_EXIT:
            printf("process %d exited, shutting down...\n", current_proc->pid);
            current_proc->state = PROC_EXITED;
            sbi_call(0, 0, 0, 0, 0, 0, 0, 0x08);
            for (;;) {}
        case SYS_READFILE:
        case SYS_WRITEFILE: {
            const char *filename = (const char *) f->a0;
            char *buf = (char *) f->a1;
            int len = f->a2;
            struct file *file = fs_lookup(filename);
            if (!file) {
                char parent_path[128];
                const char *last_slash = strrchr(filename, '/');
                if (!last_slash) { f->a0 = -1; break; }
                int parent_len = last_slash - filename;
                if (parent_len == 0) strcpy(parent_path, "/");
                else { memcpy(parent_path, filename, parent_len); parent_path[parent_len] = '\0'; }
                struct file *parent = fs_lookup_path(parent_path);
                if (!parent || !parent->is_dir) { f->a0 = -1; break; }
                if (fs_lookup_path(filename) != NULL) { f->a0 = -1; break; }
                for (int i = 0; i < FILES_MAX; i++) {
                    if (!files[i].in_use) {
                        file = &files[i];
                        file->in_use = true;
                        file->is_dir = false;
                        file->size = 0;
                        strcpy(file->name, last_slash + 1);
                        file->parent = parent - files;
                        parent->children[parent->child_count++] = i;
                        break;
                    }
                }
                if (!file) { f->a0 = -1; break; }
            }
            if (len > (int) sizeof(file->data) && file->size > 0) len = file->size;
            if (f->a3 == SYS_WRITEFILE) {
                if (len > 0) memcpy(file->data, buf, len);
                file->size = len;
                fs_flush();
            } else memcpy(buf, file->data, len);
            f->a0 = len;
            break;
        }
        case SYS_SHUTDOWN: { printf("shutting down...\n"); sbi_call(0, 0, 0, 0, 0, 0, 0, 0x08); printf("shutdown failed (not supported)\n"); for (;;) {} }
        case SYS_REBOOT: { printf("rebooting...\n"); sbi_call(0, 0, 0, 0, 0, 0, 0, 0x08); printf("reboot failed (not supported)\n"); for (;;) {} }
        case SYS_MKDIR: { const char *path = (const char *) f->a0; int ret = fs_mkdir(path); if (ret == 0) fs_flush(); f->a0 = ret; break; }
        case SYS_LISTDIR: {
            const char *path = (const char *) f->a0;
            char *buf = (char *) f->a1;
            int len = f->a2;
            struct file *dir = fs_lookup_path(path);
            if (!dir || !dir->is_dir) { f->a0 = -1; break; }
            int offset = 0;
            for (int i = 0; i < dir->child_count && offset < len - 1; i++) {
                struct file *child = &files[dir->children[i]];
                int namelen = strlen(child->name);
                if (offset + namelen + 2 < len) {
                    strcpy(buf + offset, child->name);
                    offset += namelen;
                    buf[offset++] = child->is_dir ? '/' : ' ';
                    buf[offset] = '\0';
                }
            }
            f->a0 = offset;
            break;
        }
        case SYS_REMOVE: {
            const char *path = (const char *) f->a0;
            struct file *file = fs_lookup_path(path);
            if (!file || file == &files[0] || (file->child_count > 0 && !f->a1)) { f->a0 = -1; break; }
            if (file->child_count > 0) { for (int i = file->child_count - 1; i >= 0; i--) { files[file->children[i]].in_use = false; files[file->children[i]].parent = -1; } file->child_count = 0; }
            if (file->parent != -1) {
                struct file *parent = &files[file->parent];
                for (int i = 0; i < parent->child_count; i++) {
                    if (parent->children[i] == file - files) { for (int j = i; j < parent->child_count - 1; j++) parent->children[j] = parent->children[j + 1]; parent->child_count--; break; }
                }
            }
            file->in_use = false;
            fs_flush();
            f->a0 = 0;
            break;
        }
        default:
            PANIC("unexpected syscall a3=%x\n", f->a3);
    }
}
void handle_trap(struct trap_frame *f) {
    uint32_t scause = READ_CSR(scause);
    uint32_t stval = READ_CSR(stval);
    uint32_t user_pc = READ_CSR(sepc);
    if (scause == SCAUSE_ECALL) {
        handle_syscall(f);
        user_pc += 4;
    } else {
        PANIC("unexpected trap scause=%x, stval=%x, sepc=%x\n", scause, stval, user_pc);
    }
    WRITE_CSR(sepc, user_pc);
}
void kernel_main(void) {
    memset(__bss, 0, (size_t) __bss_end - (size_t) __bss);
    printf("\n\n");
    WRITE_CSR(stvec, (uint32_t) kernel_entry);
    virtio_blk_init();
    fs_init();
    idle_proc = create_process(NULL, 0);
    idle_proc->pid = 0; // idle
    current_proc = idle_proc;
    create_process(_binary_shell_bin_start, (size_t) _binary_shell_bin_size);
    yield();
    printf("system halted\n");
    for (;;) {}
}
__attribute__((section(".text.boot")))
__attribute__((naked))
void boot(void) {
    __asm__ __volatile__(
        "mv sp, %[stack_top]\n"
        "j kernel_main\n"
        :
        : [stack_top] "r" (__stack_top)
    );
}
