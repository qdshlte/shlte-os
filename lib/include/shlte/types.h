/*
 * shlte/types.h - Basic type definitions for Shlte OS
 */

#ifndef SHLTE_TYPES_H
#define SHLTE_TYPES_H

/* Standard integer types */
typedef signed char         int8_t;
typedef short               int16_t;
typedef int                 int32_t;
typedef long long           int64_t;

typedef unsigned char       uint8_t;
typedef unsigned short      uint16_t;
typedef unsigned int        uint32_t;
typedef unsigned long long  uint64_t;

/* Pointer-sized types */
typedef unsigned long       uintptr_t;
typedef long                intptr_t;

/* Common typedefs */
typedef _Bool             bool;
typedef uint8_t           u8;
typedef uint16_t          u16;
typedef uint32_t          u32;
typedef uint64_t          u64;
typedef int8_t            i8;
typedef int16_t           i16;
typedef int32_t           i32;
typedef int64_t           i64;

/* Special types */
typedef void              (*irq_handler_t)(int irq_num);
typedef void              (*syscall_handler_t)(void);

#define true  1
#define false 0
#define NULL  ((void *)0)

/* Standard library types (freestanding) */
typedef unsigned long size_t;
typedef long ssize_t;
typedef long off_t;
typedef __builtin_va_list va_list;
#define VA_START(ap, param) __builtin_va_start(ap, param)
#define VA_ARG(ap, type) __builtin_va_arg(ap, type)
#define VA_END(ap) __builtin_va_end(ap)

/* Limits */
#define INT_MAX    2147483647
#define INT_MIN    (-INT_MAX - 1)
#define UINT_MAX   4294967295U
#define LONG_MAX   9223372036854775807L
#define SIZE_MAX   18446744073709551615UL

#endif /* SHLTE_TYPES_H */
