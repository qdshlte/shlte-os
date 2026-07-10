/*
 * boot/efi.h - UEFI Types and Protocol Definitions
 *
 * Minimal definitions needed for an ARM64 EFI stub.
 * The kernel runs as a UEFI application, collects system info,
 * exits boot services, and transitions to the native kernel entry.
 */

#ifndef BOOT_EFI_H
#define BOOT_EFI_H

#include <shlte/types.h>

/* ============================================================
 * UEFI base types
 * ============================================================ */

#define EFIAPI  __attribute__((ms_abi))

typedef uint8_t     efi_boolean;
typedef int64_t     efi_intn;
typedef uint64_t    efi_uintn;
typedef uint16_t    efi_char16_t;
typedef void       *efi_handle_t;
typedef void       *efi_event_t;
typedef uint64_t    efi_physical_addr_t;
typedef uint64_t    efi_virtual_addr_t;
typedef uint64_t    efi_status_t;

#define EFI_SUCCESS              0
#define EFI_LOAD_ERROR           1
#define EFI_INVALID_PARAMETER    2
#define EFI_UNSUPPORTED          3
#define EFI_BAD_BUFFER_SIZE      4
#define EFI_BUFFER_TOO_SMALL     5
#define EFI_NOT_READY            6
#define EFI_DEVICE_ERROR         7
#define EFI_WRITE_PROTECTED      8
#define EFI_OUT_OF_RESOURCES     9
#define EFI_NOT_FOUND            14

/* ============================================================
 * Memory
 * ============================================================ */

#define EFI_RESERVED_MEMORY_TYPE      0
#define EFI_LOADER_CODE               1
#define EFI_LOADER_DATA               2
#define EFI_BOOT_SERVICES_CODE        3
#define EFI_BOOT_SERVICES_DATA        4
#define EFI_RUNTIME_SERVICES_CODE     5
#define EFI_RUNTIME_SERVICES_DATA     6
#define EFI_CONVENTIONAL_MEMORY       7
#define EFI_UNUSABLE_MEMORY           8
#define EFI_ACPI_RECLAIM_MEMORY       9
#define EFI_ACPI_MEMORY_NVS           10
#define EFI_MEMORY_MAPPED_IO          11
#define EFI_MEMORY_MAPPED_IO_PORT_SPACE 12
#define EFI_PAL_CODE                  13
#define EFI_PERSISTENT_MEMORY         14
#define EFI_MAX_MEMORY_TYPE           15

typedef struct {
    uint32_t type;
    uint32_t pad;
    efi_physical_addr_t physical_start;
    efi_virtual_addr_t  virtual_start;
    uint64_t number_of_pages;
    uint64_t attribute;
} efi_memory_descriptor_t;

/* ============================================================
 * Time
 * ============================================================ */

typedef struct {
    uint16_t year;
    uint8_t  month;
    uint8_t  day;
    uint8_t  hour;
    uint8_t  minute;
    uint8_t  second;
    uint8_t  pad1;
    uint32_t nanosecond;
    int16_t  timezone;
    uint8_t  daylight;
    uint8_t  pad2;
} efi_time_t;

/* ============================================================
 * Table Header
 * ============================================================ */

typedef struct {
    uint64_t signature;
    uint32_t revision;
    uint32_t header_size;
    uint32_t crc32;
    uint32_t reserved;
} efi_table_header_t;

/* ============================================================
 * GUID
 * ============================================================ */

typedef struct {
    uint32_t data1;
    uint16_t data2;
    uint16_t data3;
    uint8_t  data4[8];
} efi_guid_t;

#define EFI_GUID(a,b,c,d0,d1,d2,d3,d4,d5,d6,d7) \
    { (a), (b), (c), { (d0),(d1),(d2),(d3),(d4),(d5),(d6),(d7) } }

/* ============================================================
 * Boot Services function typedefs
 * ============================================================ */

typedef efi_status_t (EFIAPI *efi_allocate_pages_t)(
    uint32_t type, uint32_t memory_type, efi_uintn pages,
    efi_physical_addr_t *memory);
typedef efi_status_t (EFIAPI *efi_free_pages_t)(
    efi_physical_addr_t memory, efi_uintn pages);
typedef efi_status_t (EFIAPI *efi_get_memory_map_t)(
    efi_uintn *memory_map_size, efi_memory_descriptor_t *memory_map,
    efi_uintn *map_key, efi_uintn *descriptor_size,
    uint32_t *descriptor_version);
typedef efi_status_t (EFIAPI *efi_allocate_pool_t)(
    uint32_t pool_type, efi_uintn size, void **buffer);
typedef efi_status_t (EFIAPI *efi_free_pool_t)(void *buffer);
typedef efi_status_t (EFIAPI *efi_exit_boot_services_t)(
    efi_handle_t image_handle, efi_uintn map_key);
typedef efi_status_t (EFIAPI *efi_handle_protocol_t)(
    efi_handle_t handle, efi_guid_t *protocol, void **interface);
typedef efi_status_t (EFIAPI *efi_locate_protocol_t)(
    efi_guid_t *protocol, void *registration, void **interface);
typedef efi_status_t (EFIAPI *efi_open_protocol_t)(
    efi_handle_t handle, efi_guid_t *protocol, void **interface,
    efi_handle_t agent_handle, efi_handle_t controller_handle,
    uint32_t attributes);

typedef struct {
    efi_table_header_t hdr;
    void *raise_tpl;
    void *restore_tpl;
    efi_allocate_pages_t   allocate_pages;
    efi_free_pages_t       free_pages;
    efi_get_memory_map_t   get_memory_map;
    efi_allocate_pool_t    allocate_pool;
    efi_free_pool_t        free_pool;
    void *create_event;
    void *set_timer;
    void *wait_for_event;
    void *signal_event;
    void *close_event;
    void *check_event;
    void *install_protocol_interface;
    void *reinstall_protocol_interface;
    void *uninstall_protocol_interface;
    efi_handle_protocol_t  handle_protocol;
    void *reserved;
    void *register_protocol_notify;
    efi_locate_protocol_t  locate_protocol;          /* offset 120 */
    void *locate_device_path;
    void *install_configuration_table;
    void *load_image;
    void *start_image;
    void *exit;
    void *unload_image;
    efi_exit_boot_services_t exit_boot_services;     /* offset 192 */
    void *get_watchdog_timer;
    void *set_watchdog_timer;
    void *stall;
    void *copy_mem;
    void *set_mem;
    void *create_event_ex;
} efi_boot_services_t;

/* ============================================================
 * Runtime Services
 * ============================================================ */

typedef efi_status_t (EFIAPI *efi_get_variable_t)(
    efi_char16_t *variable_name, efi_guid_t *vendor_guid,
    uint32_t *attributes, efi_uintn *data_size, void *data);
typedef efi_status_t (EFIAPI *efi_get_next_variable_name_t)(
    efi_uintn *variable_name_size, efi_char16_t *variable_name,
    efi_guid_t *vendor_guid);
typedef efi_status_t (EFIAPI *efi_set_variable_t)(
    efi_char16_t *variable_name, efi_guid_t *vendor_guid,
    uint32_t attributes, efi_uintn data_size, void *data);

typedef struct {
    efi_table_header_t hdr;
    efi_get_variable_t           get_variable;
    efi_get_next_variable_name_t get_next_variable_name;
    efi_set_variable_t           set_variable;
} efi_runtime_services_t;

/* Well-known GUIDs */
#define EFI_ACPI_TABLE_GUID \
    EFI_GUID(0x8868e871, 0xe4f1, 0x11d3, 0xbc, 0x22, 0x00, 0x80, 0xc7, 0x3c, 0x88, 0x81)
#define EFI_ACPI_20_TABLE_GUID \
    EFI_GUID(0x8868e871, 0xe4f1, 0x11d3, 0xbc, 0x22, 0x00, 0x80, 0xc7, 0x3c, 0x88, 0x82)
#define EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID \
    EFI_GUID(0x9042a9de, 0x23dc, 0x4a38, 0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a)
#define EFI_LOADED_IMAGE_PROTOCOL_GUID \
    EFI_GUID(0x5b1b31a1, 0x9562, 0x11d2, 0x8e, 0x3f, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b)

/* ============================================================
 * System Table
 * ============================================================ */

typedef struct {
    efi_table_header_t   hdr;
    efi_char16_t        *firmware_vendor;
    uint32_t             firmware_revision;
    efi_handle_t         console_in_handle;
    void                *con_in;
    efi_handle_t         console_out_handle;
    void                *con_out;
    efi_handle_t         standard_error_handle;
    void                *std_err;
    efi_runtime_services_t *runtime_services;
    efi_boot_services_t    *boot_services;
    efi_uintn             number_of_table_entries;
    void                 *configuration_table;
} efi_system_table_t;

/* ============================================================
 * Graphics Output Protocol (GOP)
 * ============================================================ */

typedef struct {
    uint32_t red_mask;
    uint32_t green_mask;
    uint32_t blue_mask;
    uint32_t reserved_mask;
} efi_pixel_bitmask_t;

typedef enum {
    PixelRedGreenBlueReserved8BitPerColor,
    PixelBlueGreenRedReserved8BitPerColor,
    PixelBitMask,
    PixelBltOnly,
    PixelFormatMax
} efi_graphics_pixel_format_t;

typedef struct {
    uint32_t version;
    uint32_t horizontal_resolution;
    uint32_t vertical_resolution;
    efi_graphics_pixel_format_t pixel_format;
    efi_pixel_bitmask_t pixel_information;
    uint32_t pixels_per_scan_line;
} efi_gop_mode_info_t;

typedef struct {
    uint32_t max_mode;
    uint32_t mode;
    efi_gop_mode_info_t *info;
    efi_uintn size_of_info;
    efi_physical_addr_t frame_buffer_base;
    efi_uintn frame_buffer_size;
} efi_gop_mode_t;

typedef struct efi_gop_protocol {
    void *query_mode;
    void *set_mode;
    void *blt;
    efi_gop_mode_t *mode;
} efi_gop_protocol_t;

/* ============================================================
 * Configuration Table
 * ============================================================ */

typedef struct {
    efi_guid_t vendor_guid;
    void      *vendor_table;
} efi_configuration_table_t;

/* ============================================================
 * ACPI RSDP
 * ============================================================ */

typedef struct __attribute__((packed)) {
    char     signature[8];        /* "RSD PTR " */
    uint8_t  checksum;
    char     oem_id[6];
    uint8_t  revision;
    uint32_t rsdt_address;        /* 32-bit RSDT */
    /* ACPI 2.0+ fields */
    uint32_t length;
    uint64_t xsdt_address;        /* 64-bit XSDT */
    uint8_t  extended_checksum;
    uint8_t  reserved[3];
} acpi_rsdp_t;

/* ============================================================
 * ACPI SDT Header
 * ============================================================ */

typedef struct __attribute__((packed)) {
    char     signature[4];
    uint32_t length;
    uint8_t  revision;
    uint8_t  checksum;
    char     oem_id[6];
    char     oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} acpi_sdt_header_t;

/* ACPI table signatures */
#define ACPI_MCFG_SIG  0x4746434D  /* "MCFG" */
#define ACPI_MADT_SIG  0x5444414D  /* "MADT" */
#define ACPI_DSDT_SIG  0x54445344  /* "DSDT" */
#define ACPI_FADT_SIG  0x54434146  /* "FACP" */
#define ACPI_RSDT_SIG  0x54445352  /* "RSDT" */
#define ACPI_XSDT_SIG  0x54445358  /* "XSDT" */

/* MCFG entry (PCIe ECAM) */
typedef struct __attribute__((packed)) {
    uint64_t base_address;
    uint16_t pci_segment_group;
    uint8_t  start_bus;
    uint8_t  end_bus;
    uint32_t reserved;
} acpi_mcfg_entry_t;

/* ============================================================
 * Boot info passed to kernel
 * ============================================================ */

typedef struct {
    uint64_t memory_map;
    uint64_t memory_map_size;
    uint64_t memory_map_desc_size;
    uint64_t acpi_rsdp;
    uint64_t fb_base;
    uint32_t fb_width;
    uint32_t fb_height;
    uint32_t fb_stride;
    uint32_t fb_format;    /* 0=BGRX, 1=RGBX */
} efi_boot_info_t;

#endif /* BOOT_EFI_H */
