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
	if (readl(bb + 1) >= 2) {
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
