// SPDX-License-Identifier: GPL-2.0
/*
 * need this to force device to enable usb gadget
 */
#include <linux/init.h>
#include <linux/io.h>
#include <linux/bits.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/workqueue.h>
#include <linux/moduleparam.h>

static bool force_vbus;
module_param(force_vbus, bool, 0444);	/* bison_usb_sw.force_vbus=1 */

#define GPIO_BASE	0x10005000
#define GPIO_SIZE	0x1000
#define MODE(p)		(0x300 + 0x10 * ((p) / 8))
#define DIR(p)		(0x000 + 0x10 * ((p) / 32))
#define DOUT(p)		(0x100 + 0x10 * ((p) / 32))
#define SET		4
#define CLR		8
#define U2COM		0x300
#define FORCE_VBUSVALID	BIT(13)
#define FORCE_SESSEND	BIT(12)
#define FORCE_AVALID	BIT(10)
#define RG_VBUSVALID	BIT(5)
#define RG_SESSEND	BIT(4)
#define RG_AVALID	BIT(2)
#define BC11_SW_EN	BIT(23)	/* USBPHYACR6 (0x18) */


static int sel = 1;	/* pin 177 */
static int c1  = 1;	/* pin 18  */
static int c2  = 0;	/* pin 17  */
module_param(sel, int, 0444);
module_param(c1, int, 0444);
module_param(c2, int, 0444);

static void gpio_cfg(void __iomem *g, unsigned int p, int v)
{
	u32 sh = (p % 8) * 4;

	writel(readl(g + MODE(p)) & ~(0xfu << sh), g + MODE(p)); /* GPIO */
	if (v == 2) {
		writel(BIT(p % 32), g + DIR(p) + CLR);		/* input */
		return;
	}
	writel(BIT(p % 32), g + DIR(p) + SET);			/* output */
	writel(BIT(p % 32), g + DOUT(p) + (v ? SET : CLR));
}

static int __init bison_usb_sw_init(void)
{
	void __iomem *g = ioremap(GPIO_BASE, GPIO_SIZE);

	if (!g)
		return -ENOMEM;

	gpio_cfg(g, 177, sel);
	gpio_cfg(g, 18, c1);
	gpio_cfg(g, 17, c2);

	pr_info("bison-usb-sw: sel=%d c1=%d c2=%d | dir5=%08x dout5=%08x dir0=%08x dout0=%08x\n",
		sel, c1, c2, readl(g + DIR(177)), readl(g + DOUT(177)),
		readl(g + DIR(18)), readl(g + DOUT(18)));
	iounmap(g);
	return 0;
}
early_initcall(bison_usb_sw_init);

static void bison_dump_fn(struct work_struct *w);
static DECLARE_DELAYED_WORK(bison_dump_work, bison_dump_fn);

static void bison_dump_fn(struct work_struct *w)
{
	static int n;
	void __iomem *p = ioremap(0x11f40000, 0x1000);
	void __iomem *i = ioremap(0x11203e00, 0x100);
	u32 v;

	if (!p || !i)
		goto out;

	if (force_vbus && n == 0) {
		v = readl(p + U2COM + 0x6c);
		v |= FORCE_VBUSVALID | FORCE_AVALID | FORCE_SESSEND |
		     RG_VBUSVALID | RG_AVALID;
		v &= ~RG_SESSEND;
		writel(v, p + U2COM + 0x6c);
		v = readl(p + U2COM + 0x18);
		writel(v & ~BC11_SW_EN, p + U2COM + 0x18);
		pr_info("bison-usb-dump: applied vbus force\n");
	}

	pr_info("bison-usb-dump[%d]: U2COM ACR0=%08x ACR1=%08x ACR5=%08x ACR6=%08x ACR4=%08x DTM0=%08x DTM1=%08x\n",
		n, readl(p + U2COM + 0x00), readl(p + U2COM + 0x04),
		readl(p + U2COM + 0x14), readl(p + U2COM + 0x18),
		readl(p + U2COM + 0x20), readl(p + U2COM + 0x68),
		readl(p + U2COM + 0x6c));
	pr_info("bison-usb-dump[%d]: IPPC CTRL0=%08x PDN(h/d)=%08x/%08x STS1=%08x STS2=%08x U3_0P=%08x U2_0P=%08x\n",
		n, readl(i + 0x00), readl(i + 0x04), readl(i + 0x08),
		readl(i + 0x10), readl(i + 0x14), readl(i + 0x30), readl(i + 0x50));
out:
	if (p) iounmap(p);
	if (i) iounmap(i);
	if (++n < 8)
		schedule_delayed_work(&bison_dump_work, 5 * HZ);
}

static int __init bison_usb_dump(void)
{
	schedule_delayed_work(&bison_dump_work, 10 * HZ);
	return 0;
}
late_initcall_sync(bison_usb_dump);
