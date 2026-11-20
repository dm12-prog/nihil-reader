// SPDX-License-Identifier: GPL-2.0
/*
 * nihil_chainload: "use the running Kobo Linux as a bootloader".
 *
 * Kobo's kernel has no kexec (CONFIG_KEXEC is not set in
 * arch/arm/configs/sun8iw15p1smp_kobo_defconfig), but it does allow kernel
 * modules. This module:
 *   1. reads our bare-metal image from a file into RAM,
 *   2. stops the other 3 CPU cores,
 *   3. calls the kernel's own soft_restart(), the same routine kexec uses:
 *      it turns off the MMU and caches and jumps to our image.
 *
 * Nothing is written to the Kobo's storage. Holding the power button
 * (or our image's reboot) brings back the normal Kobo software.
 *
 * Usage on the Kobo:  insmod nihil_chainload.ko image=/mnt/onboard/.adds/nihil/sage.bin
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/gfp.h>
#include <linux/slab.h>
#include <linux/kallsyms.h>
#include <linux/irqflags.h>
#include <linux/delay.h>
#include <asm/io.h>

static char *image = "/mnt/onboard/.adds/nihil/sage.bin";
module_param(image, charp, 0444);
MODULE_PARM_DESC(image, "path of the bare-metal image to start");

static int dryrun;
module_param(dryrun, int, 0444);
MODULE_PARM_DESC(dryrun, "1 = do every step except the final jump, then fail to load");

static int readlog;
module_param(readlog, int, 0444);
MODULE_PARM_DESC(readlog, "1 = print Nihil's black box (RAM at 0x5FA00000) to the kernel log, then fail to load");

static int regdump;
module_param(regdump, int, 0444);
MODULE_PARM_DESC(regdump, "1 = watch the screen hardware registers for ~4 s from inside Linux (read-only), print them, then fail to load");

#define BB_PHYS   0x5FA00000UL
#define BB_MAGIC  0x4E49484CU
#define BB_EVENTS 32
#define BB_MAX    300

static int nihil_readlog(void)
{
	u32 __iomem *bb = ioremap(BB_PHYS, 16384);
	u32 i, n;

	if (!bb) {
		pr_err("nihil-bb: ioremap failed\n");
		return -ENOMEM;
	}
	pr_info("nihil-bb: magic=%08x version=%u cntfrq=%u events=%u end=%08x button=%08x\n",
		readl(bb + 0), readl(bb + 1), readl(bb + 2), readl(bb + 3), readl(bb + 4), readl(bb + 5));
	if (readl(bb + 0) != BB_MAGIC) {
		pr_info("nihil-bb: no valid log (RAM was cleared, or Nihil never got that far)\n");
		pr_info("nihil-bb: first words %08x %08x %08x %08x\n",
			readl(bb + 0), readl(bb + 1), readl(bb + 2), readl(bb + 3));
	}
	for (i = 8; i < 24; i += 8)
		pr_info("nihil-bb: %s L.cfg0=%08x L.cfg1=%08x L.data=%08x L.pull=%08x H.cfg0=%08x H.cfg1=%08x H.data=%08x H.pull=%08x\n",
			i == 8 ? "before" : "after ",
			readl(bb + i), readl(bb + i + 1), readl(bb + i + 2), readl(bb + i + 3),
			readl(bb + i + 4), readl(bb + i + 5), readl(bb + i + 6), readl(bb + i + 7));
	n = readl(bb + 3);
	if (readl(bb + 1) == 3) {
		/* v3: (address, value) pairs from word 40; word 6 = last address tried, word 7 = clocked blocks */
		pr_info("nihil-bb: v3 pairs=%u last_addr=%08x clocked=%08x (1=eink 2=tcon 4=iommu 8=disptop 16=de)\n",
			n, readl(bb + 6), readl(bb + 7));
		if (n > 1000)
			n = 1000;
		for (i = 0; i + 3 < n; i += 4)
			pr_info("nihil-bb: r %08x=%08x %08x=%08x %08x=%08x %08x=%08x\n",
				readl(bb + 40 + i * 2), readl(bb + 41 + i * 2), readl(bb + 42 + i * 2), readl(bb + 43 + i * 2),
				readl(bb + 44 + i * 2), readl(bb + 45 + i * 2), readl(bb + 46 + i * 2), readl(bb + 47 + i * 2));
		for (; i < n; i++)
			pr_info("nihil-bb: r %08x=%08x\n", readl(bb + 40 + i * 2), readl(bb + 41 + i * 2));
	} else if (readl(bb + 1) >= 2) {
		/* v2: words 24-33 = start data of ports A..I and L; events of 11 words from 40 */
		pr_info("nihil-bb: start A=%08x B=%08x C=%08x D=%08x E=%08x F=%08x G=%08x H=%08x I=%08x L=%08x\n",
			readl(bb + 24), readl(bb + 25), readl(bb + 26), readl(bb + 27), readl(bb + 28),
			readl(bb + 29), readl(bb + 30), readl(bb + 31), readl(bb + 32), readl(bb + 33));
		if (n > 200)
			n = 200;
		for (i = 0; i < n; i++) {
			u32 __iomem *e = bb + 40 + i * 11;
			pr_info("nihil-bb: ev %3u t=%6u A=%08x B=%08x C=%08x D=%08x E=%08x F=%08x G=%08x H=%08x I=%08x L=%08x\n",
				i, readl(e), readl(e + 1), readl(e + 2), readl(e + 3), readl(e + 4), readl(e + 5),
				readl(e + 6), readl(e + 7), readl(e + 8), readl(e + 9), readl(e + 10));
		}
	} else {
		if (n > BB_MAX)
			n = BB_MAX;
		for (i = 0; i < n; i++)
			pr_info("nihil-bb: ev %3u t=%6u ms L=%08x H=%08x\n", i,
				readl(bb + BB_EVENTS + i * 3), readl(bb + BB_EVENTS + i * 3 + 1),
				readl(bb + BB_EVENTS + i * 3 + 2));
	}
	writel(0, bb);              /* mark as read */
	iounmap(bb);
	return -EAGAIN;
}

/*
 * regdump: read-only look at the screen hardware while Linux is still driving it.
 * Samples every 5 ms for ~4 s; prints the clock registers whenever they change,
 * and a full dump of the EINK engine / timing controller the first few times
 * they are seen switched on (a screen refresh).
 */
#define CCU_PHYS   0x03001000UL
#define EINK_PHYS  0x01400000UL
#define TCON_PHYS  0x06511000UL
#define IOMMU_PHYS 0x030F0000UL
#define PIO_PHYS   0x0300B000UL

static void nihil_dump(const char *tag, void __iomem *base, u32 from, u32 to)
{
	u32 o;

	for (o = from; o <= to; o += 16)
		pr_info("nihil-rd: %s +%03x: %08x %08x %08x %08x\n", tag, o,
			readl(base + o), readl(base + o + 4), readl(base + o + 8), readl(base + o + 12));
}

static int nihil_regdump(void)
{
	static const u16 w[] = { 0x060, 0x040, 0x600, 0x60C, 0x610, 0x61C, 0x640, 0x7BC, 0x804, 0xB5C, 0xB60, 0xB7C };
	void __iomem *ccu = ioremap(CCU_PHYS, 0x1000);
	void __iomem *ee = ioremap(EINK_PHYS, 0x1000);
	void __iomem *tcon = ioremap(TCON_PHYS, 0x1000);
	void __iomem *mmu = ioremap(IOMMU_PHYS, 0x1000);
	void __iomem *pio = ioremap(PIO_PHYS, 0x400);
	u32 last[ARRAY_SIZE(w)], now, lastctl = 0xffffffff, lastirq = 0xffffffff, lastpe = 0, lastph = 0;
	int i, t, ee_dumps = 0, tcon_dumps = 0, changes = 0;

	if (!ccu || !ee || !tcon || !mmu || !pio) {
		pr_err("nihil-rd: ioremap failed\n");
		return -ENOMEM;
	}
	for (i = 0; i < ARRAY_SIZE(w); i++) {
		last[i] = readl(ccu + w[i]);
		pr_info("nihil-rd: t=0 ccu+%03x=%08x\n", w[i], last[i]);
	}
	if ((readl(ccu + 0x7BC) & 0x10001) == 0x10001)
		nihil_dump("iommu", mmu, 0x00, 0x9C);
	for (i = 0; i < 9; i++)
		pr_info("nihil-rd: port %c cfg %08x %08x %08x %08x data %08x pull %08x %08x\n", 'A' + i,
			readl(pio + i * 0x24), readl(pio + i * 0x24 + 4), readl(pio + i * 0x24 + 8),
			readl(pio + i * 0x24 + 12), readl(pio + i * 0x24 + 16),
			readl(pio + i * 0x24 + 0x1C), readl(pio + i * 0x24 + 0x20));

	for (t = 0; t < 800; t++) {
		for (i = 0; i < ARRAY_SIZE(w); i++) {
			now = readl(ccu + w[i]);
			if (now != last[i] && changes < 60) {
				pr_info("nihil-rd: t=%d ccu+%03x %08x -> %08x\n", t * 5, w[i], last[i], now);
				last[i] = now;
				changes++;
			}
		}
		now = readl(pio + 4 * 0x24 + 16);        /* port E data: screen power chip pins */
		if (now != lastpe && changes < 60) {
			pr_info("nihil-rd: t=%d portE data %08x -> %08x\n", t * 5, lastpe, now);
			lastpe = now; changes++;
		}
		now = readl(pio + 7 * 0x24 + 16);        /* port H data */
		if (now != lastph && changes < 60) {
			pr_info("nihil-rd: t=%d portH data %08x -> %08x\n", t * 5, lastph, now);
			lastph = now; changes++;
		}
		if ((readl(ccu + 0x61C) & 0x10001) == 0x10001) {
			u32 ctl = readl(ee + 0x000), irq = readl(ee + 0x004);

			if (ee_dumps < 3 && (ctl != lastctl || ee_dumps == 0)) {
				pr_info("nihil-rd: t=%d EINK engine on, dump %d\n", t * 5, ee_dumps);
				nihil_dump("ee", ee, 0x000, 0x05C);
				nihil_dump("ee", ee, 0x150, 0x15C);
				nihil_dump("ee", ee, 0x400, 0x41C);
				nihil_dump("ee", ee, 0x440, 0x44C);
				ee_dumps++;
			} else if ((ctl != lastctl || irq != lastirq) && changes < 60) {
				pr_info("nihil-rd: t=%d ee ctl=%08x irq=%08x\n", t * 5, ctl, irq);
				changes++;
			}
			lastctl = ctl;
			lastirq = irq;
		}
		if ((readl(ccu + 0xB7C) & 0x10001) == 0x10001 && tcon_dumps < 2) {
			pr_info("nihil-rd: t=%d timing controller on, dump %d\n", t * 5, tcon_dumps);
			nihil_dump("tcon", tcon, 0x000, 0x0FC);
			tcon_dumps++;
		}
		usleep_range(4500, 5500);
	}
	pr_info("nihil-rd: done, ee_dumps=%d tcon_dumps=%d changes=%d\n", ee_dumps, tcon_dumps, changes);
	iounmap(ccu); iounmap(ee); iounmap(tcon); iounmap(mmu); iounmap(pio);
	return -EAGAIN;
}

static int __init nihil_init(void)
{
	void (*soft_restart_fn)(unsigned long);
	int (*freeze_cpus_fn)(int primary);
	struct file *f;
	loff_t size, pos = 0;
	void *buf;
	unsigned int order;
	ssize_t got;

	if (readlog)
		return nihil_readlog();
	if (regdump)
		return nihil_regdump();

	soft_restart_fn = (void *)kallsyms_lookup_name("soft_restart");
	freeze_cpus_fn = (void *)kallsyms_lookup_name("freeze_secondary_cpus");
	if (!soft_restart_fn || !freeze_cpus_fn) {
		pr_err("nihil: kernel helpers not found, nothing done\n");
		return -ENOENT;
	}

	f = filp_open(image, O_RDONLY, 0);
	if (IS_ERR(f)) {
		pr_err("nihil: cannot open %s\n", image);
		return PTR_ERR(f);
	}
	size = i_size_read(file_inode(f));
	if (size <= 0 || size > (64 << 20)) {
		filp_close(f, NULL);
		pr_err("nihil: bad image size %lld\n", size);
		return -EINVAL;
	}

	order = get_order(size);
	buf = (void *)__get_free_pages(GFP_KERNEL, order); /* physically contiguous */
	if (!buf) {
		filp_close(f, NULL);
		return -ENOMEM;
	}
	got = kernel_read(f, pos, buf, size);
	filp_close(f, NULL);
	if (got != size) {
		free_pages((unsigned long)buf, order);
		pr_err("nihil: short read\n");
		return -EIO;
	}

	pr_info("nihil: starting %s (%lld bytes) at phys 0x%lx. Bye, Linux.\n",
		image, size, (unsigned long)virt_to_phys(buf));

	pr_info("nihil: soft_restart=%p freeze_secondary_cpus=%p first word=%08x magic=%08x\n",
		soft_restart_fn, freeze_cpus_fn, ((u32 *)buf)[0], ((u32 *)buf)[9]);
	if (dryrun) {
		free_pages((unsigned long)buf, order);
		pr_info("nihil: dry run OK, not jumping\n");
		return -EAGAIN;
	}

	freeze_cpus_fn(0);               /* only CPU 0 keeps running */
	local_irq_disable();
	local_fiq_disable();
	soft_restart_fn(virt_to_phys(buf));  /* never returns */
	return 0;
}

module_init(nihil_init);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Start a bare-metal image from running Linux, without writing storage");
