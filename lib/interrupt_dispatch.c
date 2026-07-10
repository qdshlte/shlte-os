/*
 * interrupt_dispatch.c - IRQ dispatch table and handler registration
 */

#include <shlte/types.h>
#include <shlte/interrupt.h>

/* IRQ dispatch table */
irq_handler_t irq_dispatch_table[MAX_IRQ_HANDLERS] = {0};

/**
 * irq_register - Register an interrupt handler
 */
int irq_register(int irq_num, irq_handler_t handler)
{
    if (irq_num < 0 || irq_num >= MAX_IRQ_HANDLERS)
        return -1;
    irq_dispatch_table[irq_num] = handler;
    return 0;
}

/**
 * irq_unregister - Unregister an interrupt handler
 */
int irq_unregister(int irq_num)
{
    if (irq_num < 0 || irq_num >= MAX_IRQ_HANDLERS)
        return -1;
    irq_dispatch_table[irq_num] = NULL;
    return 0;
}

/**
 * irq_enable - Enable a specific IRQ
 */
void irq_enable(int irq_num)
{
    /* TODO: Write to GIC ISENABLER register */
    (void)irq_num;
}

/**
 * irq_disable - Disable a specific IRQ
 */
void irq_disable(int irq_num)
{
    /* TODO: Write to GIC ICENABLER register */
    (void)irq_num;
}
