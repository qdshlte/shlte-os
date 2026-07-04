/*
 * interrupt_dispatch.c - IRQ dispatch table and handler registration
 */

#include <shlte/types.h>
#include <shlte/interrupt.h>
#include <shlte/printk.h>

/* IRQ dispatch table */
irq_handler_t irq_dispatch_table[MAX_IRQ_HANDLERS] = {0};

/**
 * irq_register - Register an interrupt handler
 */
int irq_register(int irq_num, irq_handler_t handler)
{
    if (irq_num < 0 || irq_num >= MAX_IRQ_HANDLERS) {
        printk("[IRQ] Invalid IRQ number: %d\n", irq_num);
        return -1;
    }

    irq_dispatch_table[irq_num] = handler;
    printk("[IRQ] Registered handler for IRQ %d\n", irq_num);
    return 0;
}

/**
 * irq_unregister - Unregister an interrupt handler
 */
int irq_unregister(int irq_num)
{
    if (irq_num < 0 || irq_num >= MAX_IRQ_HANDLERS) {
        return -1;
    }

    irq_dispatch_table[irq_num] = NULL;
    printk("[IRQ] Unregistered handler for IRQ %d\n", irq_num);
    return 0;
}

/**
 * irq_enable - Enable a specific IRQ
 */
void irq_enable(int irq_num)
{
    /* TODO: Write to GIC ISENABLER register */
    printk("[IRQ] Enabled IRQ %d\n", irq_num);
}

/**
 * irq_disable - Disable a specific IRQ
 */
void irq_disable(int irq_num)
{
    /* TODO: Write to GIC ICENABLER register */
    printk("[IRQ] Disabled IRQ %d\n", irq_num);
}
