#include <linux/module.h>
#include <linux/vermagic.h>
#include <linux/compiler.h>

MODULE_INFO(vermagic, VERMAGIC_STRING);

__visible struct module __this_module
__attribute__((section(".gnu.linkonce.this_module"))) = {
	.name = KBUILD_MODNAME,
	.init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
	.exit = cleanup_module,
#endif
	.arch = MODULE_ARCH_INIT,
};

static const struct modversion_info ____versions[]
__used
__attribute__((section("__versions"))) = {
	{ 0xa085e153, __VMLINUX_SYMBOL_STR(module_layout) },
	{ 0x44959639, __VMLINUX_SYMBOL_STR(kmalloc_caches) },
	{ 0xe3c1c03c, __VMLINUX_SYMBOL_STR(usb_gstrings_attach) },
	{ 0xcd5191e2, __VMLINUX_SYMBOL_STR(usb_free_all_descriptors) },
	{ 0xdcd8c48e, __VMLINUX_SYMBOL_STR(usb_ep_disable) },
	{ 0xeb99179c, __VMLINUX_SYMBOL_STR(usb_ep_enable) },
	{ 0x2e5810c6, __VMLINUX_SYMBOL_STR(__aeabi_unwind_cpp_pr1) },
	{ 0xe877dc91, __VMLINUX_SYMBOL_STR(usb_ep_queue) },
	{ 0xe2fae716, __VMLINUX_SYMBOL_STR(kmemdup) },
	{ 0xb1ad28e0, __VMLINUX_SYMBOL_STR(__gnu_mcount_nc) },
	{ 0x28cc25db, __VMLINUX_SYMBOL_STR(arm_copy_from_user) },
	{ 0x3c550eb9, __VMLINUX_SYMBOL_STR(mutex_unlock) },
	{ 0xcbee6439, __VMLINUX_SYMBOL_STR(ida_simple_get) },
	{ 0x91715312, __VMLINUX_SYMBOL_STR(sprintf) },
	{ 0xbbde57fe, __VMLINUX_SYMBOL_STR(usb_function_unregister) },
	{ 0xf4fa543b, __VMLINUX_SYMBOL_STR(arm_copy_to_user) },
	{ 0x275ef902, __VMLINUX_SYMBOL_STR(__init_waitqueue_head) },
	{ 0xfa2a45e, __VMLINUX_SYMBOL_STR(__memzero) },
	{ 0xb91458e2, __VMLINUX_SYMBOL_STR(usb_put_function_instance) },
	{ 0x5a5a94a6, __VMLINUX_SYMBOL_STR(kstrtou8) },
	{ 0x8a7c0a1f, __VMLINUX_SYMBOL_STR(dev_err) },
	{ 0x51d559d1, __VMLINUX_SYMBOL_STR(_raw_spin_unlock_irqrestore) },
	{ 0xd4e69f65, __VMLINUX_SYMBOL_STR(__mutex_init) },
	{ 0x3e8812c2, __VMLINUX_SYMBOL_STR(usb_ep_autoconfig) },
	{ 0x29e1b020, __VMLINUX_SYMBOL_STR(ida_simple_remove) },
	{ 0x16305289, __VMLINUX_SYMBOL_STR(warn_slowpath_null) },
	{ 0xe5943ca8, __VMLINUX_SYMBOL_STR(mutex_lock) },
	{ 0x2c22684e, __VMLINUX_SYMBOL_STR(config_group_init_type_name) },
	{ 0x622598b1, __VMLINUX_SYMBOL_STR(init_wait_entry) },
	{ 0x30b5d0f8, __VMLINUX_SYMBOL_STR(usb_function_register) },
	{ 0xdb7305a1, __VMLINUX_SYMBOL_STR(__stack_chk_fail) },
	{ 0x1000e51, __VMLINUX_SYMBOL_STR(schedule) },
	{ 0x18799b93, __VMLINUX_SYMBOL_STR(config_ep_by_speed) },
	{ 0xae1286e8, __VMLINUX_SYMBOL_STR(usb_ep_free_request) },
	{ 0xb3d86502, __VMLINUX_SYMBOL_STR(kmem_cache_alloc_trace) },
	{ 0x598542b2, __VMLINUX_SYMBOL_STR(_raw_spin_lock_irqsave) },
	{ 0xd85cd67e, __VMLINUX_SYMBOL_STR(__wake_up) },
	{ 0x344b7739, __VMLINUX_SYMBOL_STR(prepare_to_wait_event) },
	{ 0x37a0cba, __VMLINUX_SYMBOL_STR(kfree) },
	{ 0x9d669763, __VMLINUX_SYMBOL_STR(memcpy) },
	{ 0x753dc0e, __VMLINUX_SYMBOL_STR(usb_assign_descriptors) },
	{ 0x1cfb04fa, __VMLINUX_SYMBOL_STR(finish_wait) },
	{ 0x41501d48, __VMLINUX_SYMBOL_STR(usb_interface_id) },
	{ 0x8f678b07, __VMLINUX_SYMBOL_STR(__stack_chk_guard) },
	{ 0x88db9f48, __VMLINUX_SYMBOL_STR(__check_object_size) },
};

static const char __module_depends[]
__used
__attribute__((section(".modinfo"))) =
"depends=";


MODULE_INFO(srcversion, "3599C5ABC1E322259BCEB0B");
