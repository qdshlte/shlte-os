/*
 * loader.c — EL0 binary loader
 *
 * Loads a raw binary from the embedded rootfs (linked into kernel).
 * The binary is copied to a fixed location in writable memory.
 */

#include <shlte/types.h>
#include <shlte/loader.h>
#include <shlte/string.h>

extern char _binary_build_init_bin_start[];
extern char _binary_build_init_bin_end[];

int load_raw_binary(const char *path, uint64_t *entry_out, void **stack_out)
{
    (void)path;
    if (!entry_out || !stack_out)
        return -1;

    /* Load init from embedded rootfs — copy to fixed writable address */
    size_t sz = (size_t)(_binary_build_init_bin_end - _binary_build_init_bin_start);
    if (sz == 0 || sz > LOADER_MAX_BINARY)
        return -1;

    /* Use a writable address in the kernel heap region */
    uint64_t load_addr = 0x40200000UL;
    uint64_t stack_addr = load_addr + 0x10000UL;

    memcpy((void *)load_addr, _binary_build_init_bin_start, sz);

    *entry_out = load_addr;
    *stack_out = (void *)stack_addr;
    return 0;
}
