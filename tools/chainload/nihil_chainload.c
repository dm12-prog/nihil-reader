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

static int __init nihil_init(void)
{
	void (*soft_restart_fn)(unsigned long);
	int (*freeze_cpus_fn)(int primary);
	struct file *f;
	loff_t size, pos = 0;
	void *buf;
	unsigned int order;
	ssize_t got;

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
