---
name: kernel-warning-cleanup
description: Systematic cleanup of all compiler/linker warnings in a freestanding OS kernel build
source: auto-skill
extracted_at: '2026-07-10T13:30:02.687Z'
---

# Kernel Compiler Warning Cleanup (Freestanding C + Assembly)

## When to apply
When building a freestanding kernel with `-Wall -Wextra` and there are warnings to fix, or when a kernel build should be warning-free.

## Approach

### 1. Redefined macros
**Symptoms:** `warning: "FOO" redefined`, `note: this is the location of the previous definition`

**Cause:** Same macro defined in both a header and a `.c` file that includes it.

**Fix:** Delete the local definition in the `.c` file. Keep only the header definition. If the values differ, verify which is correct and update the header — the header is the authority.

**Common culprits:** `PAGE_SIZE`, UART register offsets, IRQ numbers.

### 2. Int-to-pointer casts without explicit cast
**Symptoms:** `assignment makes pointer from integer without a cast [-Wint-conversion]`

**Fix:** Add explicit cast: `ptr = (target_type *)int_value;`

**Why:** Freestanding kernels manipulate pointers as integers (physical addresses, linked list pointers stored in uint64_t fields). The compiler needs an explicit cast for these ownership transfers.

### 3. Unused parameters
**Symptoms:** `unused parameter 'x' [-Wunused-parameter]`

**Fix:** Add `(void)param;` as the first line of the function body. This is the C-standard, compiler-recognized suppression.

**When NOT to fix by removing the parameter:**
- The function matches an API signature (POSIX stubs, syscall handlers)
- External/assembly callers expect that ABI
- The parameter is reserved for future use (documented as such)

### 4. Unused static functions (dead code)
**Symptoms:** `'foo' defined but not used [-Wunused-function]`

**Two options:**
- **Remove it** — if truly dead and the git history preserves it
- **`__attribute__((unused))`** — if it's scaffolding that will be used soon (e.g., an EOI function in an interrupt controller that isn't wired up yet)

### 5. Char subscript warning
**Symptoms:** `array subscript has type 'char' [-Wchar-subscripts]`

**Fix:** Cast to `unsigned char`: `array[(unsigned char)index]`

**Why:** Plain `char` can be signed, and negative array indices are undefined behavior. Hex digit arrays indexed by a nibble value should always use unsigned.

### 6. Linker RWX segment (LOAD segment with RWX permissions)
**Symptoms:** `warning: <binary> has a LOAD segment with RWX permissions`

**Cause:** All sections (.text, .rodata, .data, .bss) are in a single program header, making everything writable + executable.

**Fix:** Add `PHDRS` to the linker script with separate segments:
```
PHDRS
{
    text   PT_LOAD FLAGS(5);   /* R-X */
    data   PT_LOAD FLAGS(6);   /* RW- */
}
SECTIONS
{
    .text : { *(.text*) } :text
    .rodata : { *(.rodata*) } :text
    . = ALIGN(4K);
    .data : { *(.data*) } :data
    .bss : { ... } :data
}
```
**Why:** A 4K-aligned boundary between code and data allows the MMU to enforce W^X at page granularity. Even without paging enabled, removing the RWX segment eliminates the linker warning and is correct.

### 7. Executable stack warning (x86_64)
**Symptoms:** `missing .note.GNU-stack section implies executable stack`

**Fix:** Add `-z noexecstack` to `LDFLAGS` in the Makefile. This tells the linker to emit a `.note.GNU-stack` section with the NX bit set.

### 8. PL011 UART register correctness
**Issue:** Wrong PL011 register offsets in a header (e.g., UART_IMSC at 0x3C instead of 0x038, UART_RIS at 0x24 instead of 0x03C).

**How to detect:** Compare the header definitions against the PL011 specification:
```
UART_DR  = 0x000   IMSC = 0x038   MIS  = 0x040
UART_RSR = 0x004   RIS  = 0x03C   ICR  = 0x044
UART_FR  = 0x018
UART_CR  = 0x030
```
**Fix:** Correct the header. Remove duplicate definitions from any `.c` file that redefines them (they'll inherit from the header). Note: the source file may have had *different* (also wrong) values than the header — reference the spec, not either file.

### Verification
```bash
make clean && make kernel 2>&1 | grep -c warning    # must be 0
make clean && make kernel 2>&1 | grep -c error      # must be 0
```
