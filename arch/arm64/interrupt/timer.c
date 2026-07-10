/*
 * timer.c - ARM Generic Timer driver for periodic interrupt
 *
 * Uses CNTP (Physical Count-timer) as a 100 Hz periodic interrupt
 * source for preemptive scheduling.
 */

#include <shlte/types.h>
#include <shlte/interrupt.h>
#include <shlte/printk.h>
#include <shlte/timer.h>

/* Generic Timer frequency (typically 62.5 MHz on QEMU virt) */
static uint64_t timer_freq;

/* Timer ticks per second — 100 Hz */
#define TIMER_HZ 100

/* IRQ number for the physical timer (PPI 29 = INTID 30 in GIC) */
#define TIMER_IRQ 30

/* CNTP_CTL register bits */
#define CNTP_CTL_ENABLE    (1UL << 0)
#define CNTP_CTL_IMASK     (1UL << 1)
#define CNTP_CTL_ISTATUS   (1UL << 2)

/* Global tick count */
volatile uint64_t jiffies = 0;

/* Forward declarations */
void timer_tick(void);

/**
 * timer_handler - Called from interrupt dispatch when TIMER_IRQ fires
 */
void timer_handler(int irq_num)
{
    (void)irq_num;
    jiffies++;

    /* Call process tick for preemptive scheduling */
    timer_tick();

    /* Reset the timer compare value for the next 100 Hz tick */
    __asm__ volatile("msr CNTP_TVAL_EL0, %0" : : "r"(timer_freq / TIMER_HZ));
}

/**
 * timer_init - Initialize the ARM Generic Timer
 *
 * Reads the counter frequency, sets the first compare value,
 * and enables the timer.
 */
void timer_init(void)
{
    /* Read the counter frequency */
    __asm__ volatile("mrs %0, CNTFRQ_EL0" : "=r"(timer_freq));
    if (timer_freq == 0)
        timer_freq = 62500000;  /* Default QEMU virt frequency */

    /* Set the compare value for 100 Hz */
    __asm__ volatile("msr CNTP_TVAL_EL0, %0" : : "r"(timer_freq / TIMER_HZ));

    /* Enable the timer, leave IMASK clear so interrupts are enabled */
    __asm__ volatile("msr CNTP_CTL_EL0, %0" : : "r"(CNTP_CTL_ENABLE));
}

/**
 * timer_register_irq - Register the timer handler with the GIC
 */
void timer_register_irq(void)
{
    irq_register(TIMER_IRQ, timer_handler);
    irq_enable(TIMER_IRQ);
}

/**
 * get_jiffies - Return the global tick count
 */
uint64_t get_jiffies(void)
{
    return jiffies;
}
