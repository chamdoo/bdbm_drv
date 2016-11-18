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
	{ 0xc4581639, __VMLINUX_SYMBOL_STR(module_layout) },
	{ 0x122e6e41, __VMLINUX_SYMBOL_STR(cpu_tss) },
	{ 0x419165be, __VMLINUX_SYMBOL_STR(kmalloc_caches) },
	{ 0xd2b09ce5, __VMLINUX_SYMBOL_STR(__kmalloc) },
	{ 0xf9a482f9, __VMLINUX_SYMBOL_STR(msleep) },
	{ 0x415c4366, __VMLINUX_SYMBOL_STR(alloc_disk) },
	{ 0xa61fee29, __VMLINUX_SYMBOL_STR(blk_cleanup_queue) },
	{ 0x3240ae60, __VMLINUX_SYMBOL_STR(blk_queue_io_opt) },
	{ 0xdedb6611, __VMLINUX_SYMBOL_STR(try_wait_for_completion) },
	{ 0x6bf1c17f, __VMLINUX_SYMBOL_STR(pv_lock_ops) },
	{ 0x99c55e20, __VMLINUX_SYMBOL_STR(param_ops_int) },
	{ 0x754d539c, __VMLINUX_SYMBOL_STR(strlen) },
	{ 0x1e3ec562, __VMLINUX_SYMBOL_STR(send_sig) },
	{ 0x8526c35a, __VMLINUX_SYMBOL_STR(remove_wait_queue) },
	{ 0xa20330ef, __VMLINUX_SYMBOL_STR(blk_queue_io_min) },
	{ 0x16708856, __VMLINUX_SYMBOL_STR(filp_close) },
	{ 0x5d51a436, __VMLINUX_SYMBOL_STR(vfs_fsync) },
	{ 0x60bdde33, __VMLINUX_SYMBOL_STR(bdbm_dm_init) },
	{ 0x999e8297, __VMLINUX_SYMBOL_STR(vfree) },
	{ 0x91715312, __VMLINUX_SYMBOL_STR(sprintf) },
	{ 0x27f18779, __VMLINUX_SYMBOL_STR(kthread_create_on_node) },
	{ 0x34015c38, __VMLINUX_SYMBOL_STR(bdbm_dm_exit) },
	{ 0x9e88526, __VMLINUX_SYMBOL_STR(__init_waitqueue_head) },
	{ 0x4f8b5ddb, __VMLINUX_SYMBOL_STR(_copy_to_user) },
	{ 0xffd5a395, __VMLINUX_SYMBOL_STR(default_wake_function) },
	{ 0xbe44f0ac, __VMLINUX_SYMBOL_STR(vfs_read) },
	{ 0xfb578fc5, __VMLINUX_SYMBOL_STR(memset) },
	{ 0xce183919, __VMLINUX_SYMBOL_STR(blk_alloc_queue) },
	{ 0x1916e38c, __VMLINUX_SYMBOL_STR(_raw_spin_unlock_irqrestore) },
	{ 0x57abc279, __VMLINUX_SYMBOL_STR(current_task) },
	{ 0x156a8a59, __VMLINUX_SYMBOL_STR(down_trylock) },
	{ 0x27e1a049, __VMLINUX_SYMBOL_STR(printk) },
	{ 0x449ad0a7, __VMLINUX_SYMBOL_STR(memcmp) },
	{ 0xb0ca423b, __VMLINUX_SYMBOL_STR(del_gendisk) },
	{ 0x6dc6dd56, __VMLINUX_SYMBOL_STR(down) },
	{ 0x71a50dbc, __VMLINUX_SYMBOL_STR(register_blkdev) },
	{ 0xc4ec820, __VMLINUX_SYMBOL_STR(bio_endio) },
	{ 0x61651be, __VMLINUX_SYMBOL_STR(strcat) },
	{ 0xb5a459dc, __VMLINUX_SYMBOL_STR(unregister_blkdev) },
	{ 0x40a9b349, __VMLINUX_SYMBOL_STR(vzalloc) },
	{ 0xb9d6e1cb, __VMLINUX_SYMBOL_STR(blk_queue_make_request) },
	{ 0xdb7305a1, __VMLINUX_SYMBOL_STR(__stack_chk_fail) },
	{ 0x1000e51, __VMLINUX_SYMBOL_STR(schedule) },
	{ 0xa202a8e5, __VMLINUX_SYMBOL_STR(kmalloc_order_trace) },
	{ 0xbe9e515c, __VMLINUX_SYMBOL_STR(put_disk) },
	{ 0xfcdb41e6, __VMLINUX_SYMBOL_STR(wake_up_process) },
	{ 0xbdfb6dbb, __VMLINUX_SYMBOL_STR(__fentry__) },
	{ 0x99f6ace4, __VMLINUX_SYMBOL_STR(kmem_cache_alloc_trace) },
	{ 0xe259ae9e, __VMLINUX_SYMBOL_STR(_raw_spin_lock) },
	{ 0x680ec266, __VMLINUX_SYMBOL_STR(_raw_spin_lock_irqsave) },
	{ 0xa6bbd805, __VMLINUX_SYMBOL_STR(__wake_up) },
	{ 0x4f68e5c9, __VMLINUX_SYMBOL_STR(do_gettimeofday) },
	{ 0x1e047854, __VMLINUX_SYMBOL_STR(warn_slowpath_fmt) },
	{ 0xc9fef317, __VMLINUX_SYMBOL_STR(add_wait_queue) },
	{ 0xf7d15482, __VMLINUX_SYMBOL_STR(bdbm_dm_get_inf) },
	{ 0x37a0cba, __VMLINUX_SYMBOL_STR(kfree) },
	{ 0x69acdf38, __VMLINUX_SYMBOL_STR(memcpy) },
	{ 0x6df1aaf1, __VMLINUX_SYMBOL_STR(kernel_sigaction) },
	{ 0x5a722cff, __VMLINUX_SYMBOL_STR(add_disk) },
	{ 0x78e739aa, __VMLINUX_SYMBOL_STR(up) },
	{ 0xb2d5a552, __VMLINUX_SYMBOL_STR(complete) },
	{ 0xa8d86963, __VMLINUX_SYMBOL_STR(blk_queue_logical_block_size) },
	{ 0x760a0f4f, __VMLINUX_SYMBOL_STR(yield) },
	{ 0xb42385a, __VMLINUX_SYMBOL_STR(vfs_write) },
	{ 0xaa5a41d3, __VMLINUX_SYMBOL_STR(filp_open) },
};

static const char __module_depends[]
__used
__attribute__((section(".modinfo"))) =
"depends=risa_dev_ramdrive_timing";


MODULE_INFO(srcversion, "C199FDF55A00E214C374088");
