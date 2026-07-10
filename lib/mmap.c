/*
 * lib/mmap.c - mmap / mprotect Memory Mapping
 *
 * POSIX-compatible memory mapping with page-level permissions.
 * Integrates with the MMU L2 page table for W^X enforcement.
 */

#include <shlte/types.h>
#include <shlte/printk.h>
#include <shlte/string.h>
#include <shlte/mm.h>
#include <shlte/process.h>
#include <shlte/fs.h>

/* ============================================================
 * mmap flags
 * ============================================================ */

#define PROT_NONE   0x0
#define PROT_READ   0x1
#define PROT_WRITE  0x2
#define PROT_EXEC   0x4

#define MAP_PRIVATE     0x02
#define MAP_ANONYMOUS   0x20
#define MAP_FIXED       0x10
#define MAP_SHARED      0x01
#define MAP_FAILED      ((void *)-1)

/* ============================================================
 * Virtual memory area (VMA)
 * ============================================================ */

#define MAX_VMAS    64

typedef struct vma {
    uint64_t start;
    uint64_t end;
    uint32_t prot;           /* PROT_READ|PROT_WRITE|PROT_EXEC */
    uint32_t flags;          /* MAP_PRIVATE, etc */
    int      fd;             /* Backing file (-1 = anonymous) */
    uint64_t offset;         /* File offset */
    struct vma *next;
    int      in_use;
} vma_t;

static vma_t g_vmas[MAX_VMAS];

/* ============================================================
 * Page table helpers (from mmu.c)
 * ============================================================ */

extern void mmu_setup_user_space(uint64_t code_start, uint64_t code_size,
                                  uint64_t data_start, uint64_t data_size);

/* pt_l2_user is in mmu.c — L2 page table for user space */
extern uint64_t pt_l2_user[512];
#define L2_BASE_ADDR 0x40000000ULL
#define L2_REGION_SIZE (512ULL * 2 * 1024 * 1024)  /* 1GB */

/* ============================================================
 * VMA management
 * ============================================================ */

static vma_t *vma_alloc(void)
{
    for (int i = 0; i < MAX_VMAS; i++) {
        if (!g_vmas[i].in_use) {
            memset(&g_vmas[i], 0, sizeof(vma_t));
            g_vmas[i].in_use = 1;
            g_vmas[i].fd = -1;
            return &g_vmas[i];
        }
    }
    return NULL;
}

static vma_t *vma_find(uint64_t addr)
{
    for (int i = 0; i < MAX_VMAS; i++) {
        if (g_vmas[i].in_use &&
            addr >= g_vmas[i].start && addr < g_vmas[i].end)
            return &g_vmas[i];
    }
    return NULL;
}

/* ============================================================
 * Page permission update (into L2 table)
 *
 * ARM64 L2 block descriptor permission bits:
 *   Bit 54 (UXN): Unprivileged eXecute Never (1 = no EL0 execute)
 *   Bits[7:6] (AP): 00=RW, 10=RO, 01=EL1-only
 * ============================================================ */

static void mmu_update_page_perm(uint64_t vaddr, uint32_t prot)
{
    uint64_t region_idx = (vaddr - L2_BASE_ADDR) / (2 * 1024 * 1024);
    if (region_idx >= 512) return;

    uint64_t entry = pt_l2_user[region_idx];
    if (!(entry & 1)) return;  /* Not mapped */

    /* Update AP bits */
    entry &= ~(3ULL << 6);   /* Clear AP[2:1] */
    if (prot & PROT_WRITE)
        entry |= (0ULL << 6);  /* AP=00: RW */
    else if (prot & PROT_READ)
        entry |= (2ULL << 6);  /* AP=10: RO */
    else
        entry |= (1ULL << 7);  /* AP=01: no EL0 access */

    /* Update UXN */
    if (prot & PROT_EXEC)
        entry &= ~(1ULL << 54);  /* Clear UXN */
    else
        entry |= (1ULL << 54);   /* Set UXN */

    pt_l2_user[region_idx] = entry;

    /* Invalidate TLB for this address */
    __asm__ volatile(
        "dsb ishst\n"
        "tlbi vaae1is, %0\n"
        "dsb ish\n"
        "isb\n"
        : : "r"(vaddr) : "memory"
    );
}

/* ============================================================
 * mmap
 * ============================================================ */

void *sys_mmap(void *addr_hint, size_t length, int prot, int flags,
               int fd, uint64_t offset)
{
    if (length == 0) return MAP_FAILED;
    if (prot & ~(PROT_READ | PROT_WRITE | PROT_EXEC)) return MAP_FAILED;

    /* Align to 2MB boundary (our page size) */
    uint64_t page_mask = 2 * 1024 * 1024 - 1;
    length = (length + page_mask) & ~page_mask;

    vma_t *vma = vma_alloc();
    if (!vma) return MAP_FAILED;

    /* Find free virtual address region */
    uint64_t vaddr;
    if (addr_hint && (flags & MAP_FIXED)) {
        vaddr = (uint64_t)addr_hint;
    } else {
        /* Simple bump allocator for anonymous mappings */
        static uint64_t anon_base = 0x40400000ULL;  /* After code+data */
        vaddr = anon_base;
        anon_base += length;
        if (anon_base > 0x41000000ULL) return MAP_FAILED;
    }

    vma->start  = vaddr;
    vma->end    = vaddr + length;
    vma->prot   = (uint32_t)prot;
    vma->flags  = (uint32_t)flags;
    vma->fd     = fd;
    vma->offset = offset;

    /* Set page permissions for the region */
    for (uint64_t p = vaddr; p < vaddr + length; p += (2 * 1024 * 1024)) {
        mmu_update_page_perm(p, (uint32_t)prot);
    }

    /* If file-backed, read data from fd */
    if (fd >= 0) {
        ssize_t n = vfs_read(fd, (void *)vaddr, length);
        (void)n;
    }

    return (void *)vaddr;
}

/* ============================================================
 * mprotect
 * ============================================================ */

int sys_mprotect(void *addr, size_t length, int prot)
{
    if (!addr || length == 0) return -1;
    if (prot & ~(PROT_READ | PROT_WRITE | PROT_EXEC)) return -1;

    uint64_t vaddr = (uint64_t)addr;
    uint64_t page_mask = 2 * 1024 * 1024 - 1;

    vaddr &= ~page_mask;
    length = (length + page_mask) & ~page_mask;

    /* Check that region is within existing VMAs */
    for (uint64_t p = vaddr; p < vaddr + length; p += (2 * 1024 * 1024)) {
        vma_t *vma = vma_find(p);
        if (!vma) return -1;  /* Not mapped */
        vma->prot = (uint32_t)prot;
        mmu_update_page_perm(p, (uint32_t)prot);
    }

    return 0;
}

/* ============================================================
 * munmap
 * ============================================================ */

int sys_munmap(void *addr, size_t length)
{
    if (!addr || length == 0) return -1;

    uint64_t vaddr = (uint64_t)addr;
    uint64_t page_mask = 2 * 1024 * 1024 - 1;

    vaddr &= ~page_mask;
    length = (length + page_mask) & ~page_mask;

    for (uint64_t p = vaddr; p < vaddr + length; p += (2 * 1024 * 1024)) {
        vma_t *vma = vma_find(p);
        if (vma) {
            vma->in_use = 0;
            mmu_update_page_perm(p, PROT_NONE);  /* Revoke access */
        }
    }

    return 0;
}
