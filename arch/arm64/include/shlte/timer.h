#ifndef SHLTE_TIMER_H
#define SHLTE_TIMER_H

#include <shlte/types.h>

/* Global tick counter */
extern volatile uint64_t jiffies;

/* Initialize the ARM Generic Timer as a periodic interrupt source */
void timer_init(void);

/* Register the timer interrupt handler with the GIC */
void timer_register_irq(void);

/* Get current system ticks */
uint64_t get_jiffies(void);

/* Timer interrupt handler */
void timer_handler(int irq_num);

#endif
