/*
 * linux/arch/arm/mach-nuc970/time.c
 *
 *
 * Copyright (c) 2014 Nuvoton technology corporation
 * All rights reserved.
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/leds.h>
#include <linux/clocksource.h>
#include <linux/clockchips.h>


#include <linux/module.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/string.h>
#include <linux/spinlock.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/clkdev.h>
#include <linux/sched_clock.h>

#include <asm/mach-types.h>
#include <asm/mach/irq.h>
#include <asm/mach/time.h>

#include <mach/mfp.h>
#include <mach/map.h>
#include <mach/regs-timer.h>
#include <mach/hardware.h>
#include <mach/regs-clock.h>
#include <mach/regs-aic.h>

#define RESETINT	0x1f
#define PERIOD		(0x01 << 27)
#define ONESHOT		(0x00 << 27)
#define COUNTEN		(0x01 << 30)
#define INTEN		(0x01 << 29)

#define TICKS_PER_SEC	100
#define PRESCALE	0x63 /* Divider = prescale + 1 */

#define	TDR_SHIFT	24
#define	TDR_MASK	((1 << TDR_SHIFT) - 1)

static unsigned int timer0_load;

/*static void nuc970_clockevent_setmode(enum clock_event_state state,
		struct clock_event_device *clk)
{
	unsigned int val;

	val = __raw_readl(REG_TMR_TCSR0);
	val &= ~(0x03 << 27);

	switch (state) {
	case CLOCK_EVT_STATE_PERIODIC:
		__raw_writel(timer0_load, REG_TMR_TICR0);
		val |= (PERIOD | COUNTEN | INTEN | PRESCALE);
		break;

	case CLOCK_EVT_STATE_ONESHOT:
		val |= (ONESHOT | COUNTEN | INTEN | PRESCALE);
		break;

	case CLOCK_EVT_STATE_DETACHED:
	case CLOCK_EVT_STATE_SHUTDOWN:
	case CLOCK_EVT_STATE_ONESHOT_STOPPED:
		break;
	}

	__raw_writel(val, REG_TMR_TCSR0);
}*/

static inline void timer_shutdown(struct clock_event_device *evt)
{
	/* disable timer */
	__raw_writel(0x00, REG_TMR_TCSR0);
}

static int nuc970_shutdown(struct clock_event_device *evt)
{
	timer_shutdown(evt);

	return 0;
}

int nuc970_set_periodic(struct clock_event_device *clk)
{
	unsigned int val;

    val = __raw_readl(REG_TMR_TCSR0) & ~(0x03 << 27);
	__raw_writel(timer0_load, REG_TMR_TICR0);	
    val |= (PERIOD | COUNTEN | INTEN | PRESCALE);
	__raw_writel(val, REG_TMR_TCSR0);
	return 0;
}

int nuc970_set_oneshot(struct clock_event_device *clk)
{
	unsigned int val;

	val = __raw_readl(REG_TMR_TCSR0) & ~(0x03 << 27);
	val |= (ONESHOT | COUNTEN | INTEN | PRESCALE);
	__raw_writel(val, REG_TMR_TCSR0);
	return 0;
}

static int nuc970_clockevent_setnextevent(unsigned long evt,
		struct clock_event_device *clk)
{
	unsigned int tcsr, tdelta;

    tcsr = __raw_readl(REG_TMR_TCSR0);
    tdelta = __raw_readl(REG_TMR_TICR0) - __raw_readl(REG_TMR_TDR0);

	__raw_writel(evt, REG_TMR_TICR0);
    if(!(tcsr & COUNTEN) && ((tdelta > 2) || (tdelta == 0)))
        __raw_writel(__raw_readl(REG_TMR_TCSR0) | COUNTEN, REG_TMR_TCSR0);

	return 0;
}
#ifdef CONFIG_PM
static int tmr0_msk;
static void nuc970_clockevent_suspend(struct clock_event_device *clk)
{
	unsigned long flags;

	local_irq_save(flags);
	if(__raw_readl(REG_AIC_IMR) & (1 << 16)) {
		tmr0_msk = 1;
		__raw_writel(0x10000, REG_AIC_MDCR);  //timer0
	} else
		tmr0_msk = 0;

	local_irq_restore(flags);

	printk("clk event suspend\n");
}

static void nuc970_clockevent_resume(struct clock_event_device *clk)
{
	unsigned long flags;

	local_irq_save(flags);
	if(tmr0_msk == 1)
		__raw_writel(0x10000, REG_AIC_MECR);  //timer0
	local_irq_restore(flags);

	printk("clk event resume\n");
}
#endif
static struct clock_event_device nuc970_clockevent_device = {
	.name		= "nuc970-timer0",
	.shift		= 32,
	.features	= CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT,
	.set_state_shutdown = nuc970_shutdown,
    .set_state_oneshot	= nuc970_set_oneshot,
    .set_state_periodic = nuc970_set_periodic,
	.set_next_event	= nuc970_clockevent_setnextevent,
#ifdef CONFIG_PM
	.suspend	= nuc970_clockevent_suspend,
	.resume		= nuc970_clockevent_resume,
#endif
	.rating		= 300,
};

/*IRQ handler for the timer*/

static irqreturn_t nuc970_timer0_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *evt = &nuc970_clockevent_device;

	__raw_writel(0x01, REG_TMR_TISR); /* clear TIF0 */
	evt->event_handler(evt);

	return IRQ_HANDLED;
}

static struct irqaction nuc970_timer0_irq = {
	.name		= "nuc970-timer0",
	.flags		= IRQF_TIMER | IRQF_IRQPOLL,
	.handler	= nuc970_timer0_interrupt,
};

static void __init nuc970_clockevents_init(void)
{
	unsigned int rate;
	struct clk *clk = clk_get(NULL, "timer0");

	BUG_ON(IS_ERR(clk));

	clk_prepare(clk);
	clk_enable(clk);

	__raw_writel(0x00, REG_TMR_TCSR0);

	rate = clk_get_rate(clk) / (PRESCALE + 1);

	timer0_load = (rate / TICKS_PER_SEC) - 1;

	__raw_writel(RESETINT, REG_TMR_TISR);
	setup_irq(IRQ_TMR0, &nuc970_timer0_irq);

	nuc970_clockevent_device.mult = div_sc(rate, NSEC_PER_SEC,
					nuc970_clockevent_device.shift);
	nuc970_clockevent_device.max_delta_ns = clockevent_delta2ns(0xffffffff,
					&nuc970_clockevent_device);
	nuc970_clockevent_device.min_delta_ns = clockevent_delta2ns(0xf,
					&nuc970_clockevent_device);
	nuc970_clockevent_device.cpumask = cpumask_of(0);

	clockevents_register_device(&nuc970_clockevent_device);
}

static cycle_t nuc970_get_cycles(struct clocksource *cs)
{
	return (__raw_readl(REG_TMR_TDR1)) & TDR_MASK;
}
#ifdef CONFIG_PM
static int tmr1_msk;
static void nuc970_clocksource_suspend(struct clocksource *cs)
{
	unsigned long flags;

	local_irq_save(flags);
	if(__raw_readl(REG_AIC_IMR) & (1 << 17)) {
		tmr1_msk = 1;
		__raw_writel(0x20000, REG_AIC_MDCR);  //timer1
	} else
		tmr1_msk = 0;

	local_irq_restore(flags);

	printk("clk source suspend\n");
}

static void nuc970_clocksource_resume(struct clocksource *cs)
{
	unsigned long flags;


	local_irq_save(flags);
	if(tmr1_msk == 1)
		__raw_writel(0x20000, REG_AIC_MECR);  //timer1
	local_irq_restore(flags);

	printk("clk source resume\n");
}
#endif
static struct clocksource clocksource_nuc970 = {
	.name	= "nuc970-timer1",
	.rating	= 200,
	.read	= nuc970_get_cycles,
	.mask	= CLOCKSOURCE_MASK(TDR_SHIFT),
	.shift	= 10,
	.flags	= CLOCK_SOURCE_IS_CONTINUOUS,
#ifdef CONFIG_PM
	.suspend	= nuc970_clocksource_suspend,
	.resume		= nuc970_clocksource_resume,
#endif
};

static void __init nuc970_clocksource_init(void)
{
	unsigned int val;
	unsigned int rate = 0;
	struct clk *clk = clk_get(NULL, "timer1");

	BUG_ON(IS_ERR(clk));

	clk_prepare(clk);
	clk_enable(clk);

	__raw_writel(0x00, REG_TMR_TCSR1);


	rate = clk_get_rate(clk) / (PRESCALE + 1);

	__raw_writel(0xffffffff, REG_TMR_TICR1);

	val = __raw_readl(REG_TMR_TCSR1);
	val |= (COUNTEN | PERIOD | PRESCALE);
	__raw_writel(val, REG_TMR_TCSR1);

	clocksource_nuc970.mult =
		clocksource_khz2mult((rate / 1000), clocksource_nuc970.shift);
	clocksource_register_khz(&clocksource_nuc970, rate / 1000);
}

void __init nuc970_setup_default_serial_console(void)
{
	struct clk *clk = clk_get(NULL, "uart0");

	BUG_ON(IS_ERR(clk));

	clk_prepare(clk);
	clk_enable(clk);

	/* GPE0, GPE1 */
	nuc970_mfp_set_port_e(0, 0x9);
	nuc970_mfp_set_port_e(1, 0x9);
}

extern int nuc970_init_clocks(void);
void __init nuc970_timer_init(void)
{
	nuc970_init_clocks();
	nuc970_clocksource_init();
	nuc970_clockevents_init();
	nuc970_setup_default_serial_console();
}
