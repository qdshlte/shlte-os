/*
 * interrupt.c - ARM64 Interrupt Controller (GICv2/v3) Initialization
 */

#include <shlte/types.h>
#include <shlte/printk.h>
#include <shlte/interrupt.h>

/* GICv2 registers (virtual mapped) */
#define GICD_BASE   0x08000000UL   /* Distributor (virt machine) */
#define GICC_BASE   0x08010000UL   /* CPU interface (virt machine) */

#define GICD_CTLR       0x0000
#define GICD_TYPER      0x0004
#define GICD_ISENABLER  0x0100
#define GICD_ISPENDER   0x0200
#define GICD_IPRIORITYR 0x0400

#define GICC_CTLR       0x0000
#define GICC_PMR        0x0004
#define GICC_BPR        0x0008
#define GICC_IAR        0x000C
#define GICC_EOIAR      0x0010

#define GIC_MAX_INTERRUPTS 1020

/* Volatile pointer for memory-mapped I/O */
typedef volatile uint32_t vuint32_t;

static vuint32_t *gicd = (vuint32_t *)GICD_BASE;
static vuint32_t *gicc = (vuint32_t *)GICC_BASE;

/**
 * gic_read - Read GIC register
 */
static inline uint32_t gic_read(vuint32_t *reg)
{
    return *reg;
}

/**
 * gic_write - Write GIC register
 */
static inline void gic_write(vuint32_t *reg, uint32_t val)
{
    *reg = val;
}

/**
 * handle_irq - Handle a hardware IRQ
 */
void handle_irq(void)
{
    uint32_t irq_num;

    /* Read IAR to get the highest priority pending IRQ */
    irq_num = gic_read(gicc + (GICC_IAR / 4));

    if (irq_num != 1020 && irq_num != 1021) {
        /* Acknowledge the interrupt */
        gic_write(gicc + (GICC_EOIAR / 4), irq_num);

        /* Dispatch to interrupt handler */
        if (irq_num < GIC_MAX_INTERRUPTS) {
            irq_handler_t handler = irq_dispatch_table[irq_num];
            if (handler != NULL) {
                handler(irq_num);
            }
        }
    }
}

/**
 * interrupt_init - Initialize the GIC interrupt controller
 */
void interrupt_init(void)
{
    uint32_t typer;
    int num_irqs;

    /* Read GIC distributor type */
    typer = gic_read(gicd + (GICD_TYPER / 4));
    num_irqs = ((typer & 0x1F) + 1) * 32;

    printk("[GIC] Type=%u, IRQs=%d\n", typer & 0x1F, num_irqs);

    /* Set all interrupt priorities to default (128) */
    for (int i = 0; i < 32; i++) {
        gic_write(gicd + (GICD_IPRIORITYR + i * 16) / 4, 0x80808080);
    }

    /* Enable all interrupts */
    for (int i = 0; i < 4; i++) {
        gic_write(gicd + (GICD_ISENABLER + i * 16) / 4, 0xFFFFFFFF);
    }

    /* Set interrupt priority mask (allow all priorities) */
    gic_write(gicc + (GICC_PMR / 4), 0xFF);

    /* Enable GIC CPU interface */
    gic_write(gicc + (GICC_CTLR / 4), 0x01);

    printk("[GIC] Interrupt controller initialized.\n");
}
