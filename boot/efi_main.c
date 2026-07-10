/*
 * boot/efi_main.c - UEFI Stub C Implementation
 *
 * Runs in UEFI boot services context.  Gathers memory map,
 * ACPI RSDP, and framebuffer info, then exits boot services
 * and hands off to the native kernel.
 *
 * Compiled WITHOUT -mgeneral-regs-only (UEFI may use SIMD).
 */

#include <shlte/types.h>
#include "efi.h"

/* Forward declarations */
extern void _start(void);
extern efi_boot_info_t g_boot_info;

/* ============================================================
 * GUID comparison
 * ============================================================ */

static int guid_equal(efi_guid_t *a, efi_guid_t *b)
{
    uint32_t *pa = (uint32_t *)a;
    uint32_t *pb = (uint32_t *)b;
    return (pa[0] == pb[0] && pa[1] == pb[1] &&
            pa[2] == pb[2] && pa[3] == pb[3]);
}

/* ============================================================
 * Simple printf for EFI (uses ConOut)
 * ============================================================ */
static void efi_puts(void *con_out, const char *s)
{
    /* EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL has OutputString at offset 8 */
    /* For simplicity, skip EFI console output in stub */
    (void)con_out; (void)s;
}

/* ============================================================
 * Memory helpers
 * ============================================================ */

static void efi_memcpy(void *d, const void *s, uint64_t n)
{
    uint8_t *dst = (uint8_t *)d;
    const uint8_t *src = (const uint8_t *)s;
    while (n--) *dst++ = *src++;
}

static void efi_memset(void *d, int c, uint64_t n)
{
    uint8_t *dst = (uint8_t *)d;
    while (n--) *dst++ = (uint8_t)c;
}

/* ============================================================
 * EFI Stub Main
 *
 * Called from assembly with:
 *   x0 = ImageHandle
 *   x1 = SystemTable
 * ============================================================ */

void efi_stub_main_c(efi_handle_t image, efi_system_table_t *st)
{
    efi_status_t status;
    efi_boot_services_t *bs = st->boot_services;
    efi_uintn map_size = 0;
    efi_uintn map_key = 0;
    efi_uintn desc_size = 0;
    uint32_t desc_version = 0;
    efi_memory_descriptor_t *mmap = NULL;
    acpi_rsdp_t *rsdp = NULL;

    /* ========================================================
     * Step 1: Get ACPI RSDP from configuration tables
     * ======================================================== */

    efi_configuration_table_t *ct =
        (efi_configuration_table_t *)st->configuration_table;
    efi_uintn num_entries = st->number_of_table_entries;

    efi_guid_t acpi_guid = EFI_ACPI_20_TABLE_GUID;
    efi_guid_t acpi_old_guid = EFI_ACPI_TABLE_GUID;

    for (efi_uintn i = 0; i < num_entries; i++) {
        if (guid_equal(&ct[i].vendor_guid, &acpi_guid) ||
            guid_equal(&ct[i].vendor_guid, &acpi_old_guid)) {
            rsdp = (acpi_rsdp_t *)ct[i].vendor_table;
            break;
        }
    }

    /* ========================================================
     * Step 2: Get GOP framebuffer
     * ======================================================== */

    efi_guid_t gop_guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
    efi_gop_protocol_t *gop = NULL;

    status = bs->locate_protocol(&gop_guid, NULL, (void **)&gop);
    if (status == EFI_SUCCESS && gop && gop->mode) {
        efi_gop_mode_t *mode = gop->mode;
        efi_gop_mode_info_t *info = mode->info;

        g_boot_info.fb_base   = mode->frame_buffer_base;
        g_boot_info.fb_width  = info->horizontal_resolution;
        g_boot_info.fb_height = info->vertical_resolution;
        g_boot_info.fb_stride = info->pixels_per_scan_line * 4;
        g_boot_info.fb_format = (info->pixel_format == PixelBlueGreenRedReserved8BitPerColor) ? 0 : 1;
    }

    /* ========================================================
     * Step 3: Get memory map
     * ======================================================== */

    /* First call: get required buffer size */
    status = bs->get_memory_map(&map_size, NULL, &map_key,
                                 &desc_size, &desc_version);
    if (status != EFI_BUFFER_TOO_SMALL) goto exit_boot;

    /* Allocate buffer for memory map */
    status = bs->allocate_pool(2 /* EfiLoaderData */,
                                map_size + 4096, (void **)&mmap);
    if (status != EFI_SUCCESS) goto exit_boot;

    status = bs->get_memory_map(&map_size, mmap, &map_key,
                                 &desc_size, &desc_version);
    if (status != EFI_SUCCESS) goto exit_boot;

    /* Save memory map info for kernel */
    g_boot_info.memory_map      = (uint64_t)mmap;
    g_boot_info.memory_map_size = map_size;
    g_boot_info.memory_map_desc_size = desc_size;

    /* ========================================================
     * Step 4: Exit boot services
     * ======================================================== */

exit_boot:
    status = bs->exit_boot_services(image, map_key);
    if (status != EFI_SUCCESS) {
        /* Try again with updated map key */
        map_size = 0;
        bs->get_memory_map(&map_size, mmap, &map_key,
                           &desc_size, &desc_version);
        bs->exit_boot_services(image, map_key);
    }

    /* ========================================================
     * Step 5: Save ACPI RSDP for kernel
     * ======================================================== */

    g_boot_info.acpi_rsdp = (uint64_t)rsdp;

    /* ========================================================
     * Step 6: Disable MMU (UEFI leaves it on with its own
     *         page tables; we set up our own)
     * ======================================================== */

    /* On ARM64, after ExitBootServices, the UEFI page tables may
     * still be active. We disable the MMU before entering the
     * kernel, which will re-enable it with our own tables. */
    __asm__ volatile(
        "mrs x0, sctlr_el1\n\t"
        "bic x0, x0, #1\n\t"         /* Clear M bit */
        "msr sctlr_el1, x0\n\t"
        "isb\n\t"
        "tlbi vmalle1\n\t"
        "dsb nsh\n\t"
        "isb\n\t"
        ::: "x0", "memory"
    );

    /* ========================================================
     * Step 7: Jump to real kernel
     * ======================================================== */

    /* The kernel _start expects:
     *   x0 = device tree pointer (we pass 0 if using ACPI)
     * We pass the ACPI RSDP in x0 (the kernel can detect it
     * by checking the "RSD PTR " signature). */
    uint64_t dtb_or_acpi = (uint64_t)rsdp;
    if (!dtb_or_acpi) dtb_or_acpi = 0;  /* No DTB, no ACPI */

    /* Call _start as a normal C function — it never returns */
    void (*kernel_entry)(uint64_t) = (void (*)(uint64_t))_start;
    kernel_entry(dtb_or_acpi);

    /* Unreachable */
    for (;;) { __asm__ volatile("wfi"); }
}
