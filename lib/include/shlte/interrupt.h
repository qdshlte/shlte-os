/*
 * shlte/interrupt.h - Interrupt subsystem declarations
 */

#ifndef SHLTE_INTERRUPT_H
#define SHLTE_INTERRUPT_H

#include <shlte/types.h>

/* Maximum number of IRQ handlers */
#define MAX_IRQ_HANDLERS 256

/* Interrupt handler registration */
typedef void (*irq_handler_t)(int irq_num);

int irq_register(int irq_num, irq_handler_t handler);
int irq_unregister(int irq_num);
void irq_enable(int irq_num);
void irq_disable(int irq_num);

/* Global interrupt control */
void interrupts_enable(void);
void interrupts_disable(void);

/* External declarations */
void handle_irq(uint64_t vector);
void interrupt_init(void);

/* IRQ dispatch table */
extern irq_handler_t irq_dispatch_table[MAX_IRQ_HANDLERS];

#endif /* SHLTE_INTERRUPT_H */
