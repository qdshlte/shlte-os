/*
 * shlte/loader.h — EL0 binary loader declarations
 */

#ifndef SHLTE_LOADER_H
#define SHLTE_LOADER_H

#include <shlte/types.h>

/* Maximum size for a single loaded binary */
#define LOADER_MAX_BINARY   (4 * 1024 * 1024)   /* 4 MB */

/* Default user stack size */
#define USER_STACK_SIZE     (16 * 4096)          /* 64 KB */

/**
 * load_raw_binary - Load a raw binary from spfs into allocated memory
 * @path: Full VFS path, e.g. "/mnt/init"
 * @entry_out: Output — entry point address
 * @stack_out: Output — allocated user stack base
 *
 * Returns 0 on success, -1 on failure.
 * On success, caller should call enter_el0(entry, stack_top) to jump to EL0.
 */
int load_raw_binary(const char *path, uint64_t *entry_out, void **stack_out);

#endif /* SHLTE_LOADER_H */
