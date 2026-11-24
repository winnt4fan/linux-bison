// SPDX-License-Identifier: GPL-2.0
/* messes around with pwrap WACS2 */
#include <linux/init.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/bits.h>
#include <linux/printk.h>

#define PWRAP_PHYS	0x1000d000
#define WACS2_CMD	0xC20
#define WACS2_RDATA	0xC24
#define WACS2_VLDCLR	0xC28

#define FSM(x)		(((x) >> 16) & 0x7)
#define FSM_IDLE	0
#define FSM_VLDCLR	6

#define VUSB_CON0_0	0x1b30
#define VUSB_CON1	0x1b40

static int wait_fsm(void __iomem *b, u32 want)
{
	int i;

	for (i = 0; i < 100000; i++) {
		if (FSM(readl(b + WACS2_RDATA)) == want)
			return 0;
		udelay(1);
	}
	return -ETIMEDOUT;
}

static int pmic_read(void __iomem *b, u32 adr, u32 *val)
{
	if (wait_fsm(b, FSM_IDLE))
		return -ETIMEDOUT;
	writel((adr >> 1) << 16, b + WACS2_CMD);
	if (wait_fsm(b, FSM_VLDCLR))
		return -ETIMEDOUT;
	*val = readl(b + WACS2_RDATA) & 0xffff;
	writel(1, b + WACS2_VLDCLR);
	return 0;
}

static int pmic_write(void __iomem *b, u32 adr, u32 val)
{
	if (wait_fsm(b, FSM_IDLE))
		return -ETIMEDOUT;
	writel(BIT(31) | ((adr >> 1) << 16) | (val & 0xffff), b + WACS2_CMD);
	return wait_fsm(b, FSM_IDLE);
}

static int __init bison_vusb_init(void)
{
	void __iomem *b = ioremap(PWRAP_PHYS, 0x1000);
	u32 con0, con1;
	int ret;

	if (!b)
		return -ENOMEM;

	ret = pmic_read(b, VUSB_CON0_0, &con0);
	ret |= pmic_read(b, VUSB_CON1, &con1);
	pr_info("bison-vusb: before ret=%d CON0_0=%04x CON1=%04x (DA_EN=%u)\n",
		ret, con0, con1, !!(con1 & BIT(15)));
	if (ret)
		goto out;

	pmic_write(b, VUSB_CON0_0, con0 | BIT(0));
	usleep_range(500, 1000);
	ret = pmic_read(b, VUSB_CON0_0, &con0);
	ret |= pmic_read(b, VUSB_CON1, &con1);
	pr_info("bison-vusb: after  ret=%d CON0_0=%04x CON1=%04x (DA_EN=%u)\n",
		ret, con0, con1, !!(con1 & BIT(15)));
out:
	iounmap(b);
	return 0;
}
late_initcall_sync(bison_vusb_init);
