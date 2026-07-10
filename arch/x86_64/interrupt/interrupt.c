/*
 * interrupt.c - x86_64 Interrupt Controller (8259A PIC)
 */

#include <shlte/types.h>
#include <shlte/printk.h>
#include <shlte/interrupt.h>
#include <shlte/io.h>

/* 8259A PIC I/O ports */
#define PIC1_CMD     0x20
#define PIC1_DATA    0x21
#define PIC2_CMD     0xA0
#define PIC2_DATA    0xA1

#define PIC_EOI      0x20

/* ICW1 flags */
#define ICW1_ICW4    0x01
#define ICW1_INIT    0x10

/* ICW4 flags */
#define ICW4_8086    0x01

/**
 * pic_send_eoi - Send End-Of-Interrupt to the PIC
 */
__attribute__((unused))
static void pic_send_eoi(uint8_t irq)
{
    (void)irq;
    /* Send non-specific EOI to both PICs */
    outb(PIC2_CMD, PIC_EOI);
    outb(PIC1_CMD, PIC_EOI);
}

/**
 * pic_remap - Remap the PIC interrupt vectors
 */
static void pic_remap(void)
{
    uint8_t mask1 = inb(PIC1_DATA);
    uint8_t mask2 = inb(PIC2_DATA);

    outb(PIC1_CMD, ICW1_INIT | ICW1_ICW4);
    outb(PIC2_CMD, ICW1_INIT | ICW1_ICW4);

    outb(PIC1_DATA, 0x20);
    outb(PIC2_DATA, 0x28);

    outb(PIC1_DATA, 0x04);
    outb(PIC2_DATA, 0x02);

    outb(PIC1_DATA, ICW4_8086);
    outb(PIC2_DATA, ICW4_8086);

    outb(PIC1_DATA, mask1);
    outb(PIC2_DATA, mask2);
}

static void pic_mask_all(void)
{
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);
}

static void pic_unmask(uint8_t irq)
{
    uint16_t port;
    uint8_t value;

    if (irq < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq -= 8;
    }
    value = inb(port) & ~(1 << irq);
    outb(port, value);
}

/**
 * handle_irq - Handle an interrupt request
 */
void handle_irq(uint64_t vector)
{
    (void)vector;

    printk("[IRQ] x86_64 IRQ received (vector 0x%lx)\n", (unsigned long)vector);
}

/**
 * interrupt_init - Initialize the interrupt controller
 */
void interrupt_init(void)
{
    printk("[PIC] Initializing 8259A PIC...\n");

    pic_remap();
    pic_mask_all();
    pic_unmask(1);

    printk("[PIC] Initialized. IRQ vectors: 0x20-0x2F\n");
}
