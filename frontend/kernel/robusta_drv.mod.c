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
	{ 0x84b303c1, __VMLINUX_SYMBOL_STR(module_layout) },
	{ 0xdc473860, __VMLINUX_SYMBOL_STR(kmalloc_caches) },
	{ 0xd2b09ce5, __VMLINUX_SYMBOL_STR(__kmalloc) },
	{ 0xf9a482f9, __VMLINUX_SYMBOL_STR(msleep) },
	{ 0x8043c323, __VMLINUX_SYMBOL_STR(alloc_disk) },
	{ 0x59714d02, __VMLINUX_SYMBOL_STR(blk_cleanup_queue) },
	{ 0xf7e0ff7, __VMLINUX_SYMBOL_STR(blk_queue_io_opt) },
	{ 0xdedb6611, __VMLINUX_SYMBOL_STR(try_wait_for_completion) },
	{ 0xce31b8a1, __VMLINUX_SYMBOL_STR(pv_lock_ops) },
	{ 0xe5981209, __VMLINUX_SYMBOL_STR(param_ops_int) },
	{ 0x754d539c, __VMLINUX_SYMBOL_STR(strlen) },
	{ 0xb51154f4, __VMLINUX_SYMBOL_STR(send_sig) },
	{ 0x8526c35a, __VMLINUX_SYMBOL_STR(remove_wait_queue) },
	{ 0x6a5d670, __VMLINUX_SYMBOL_STR(blk_queue_io_min) },
	{ 0x1549d08c, __VMLINUX_SYMBOL_STR(filp_close) },
	{ 0x6fa6cc87, __VMLINUX_SYMBOL_STR(vfs_fsync) },
	{ 0xd5f6d247, __VMLINUX_SYMBOL_STR(bdbm_dm_init) },
	{ 0x999e8297, __VMLINUX_SYMBOL_STR(vfree) },
	{ 0x97651e6c, __VMLINUX_SYMBOL_STR(vmemmap_base) },
	{ 0x91715312, __VMLINUX_SYMBOL_STR(sprintf) },
	{ 0x8b2fb712, __VMLINUX_SYMBOL_STR(kthread_create_on_node) },
	{ 0xecf1a8c4, __VMLINUX_SYMBOL_STR(bdbm_dm_exit) },
	{ 0x9e88526, __VMLINUX_SYMBOL_STR(__init_waitqueue_head) },
	{ 0x4f8b5ddb, __VMLINUX_SYMBOL_STR(_copy_to_user) },
	{ 0xffd5a395, __VMLINUX_SYMBOL_STR(default_wake_function) },
	{ 0xe9ec5411, __VMLINUX_SYMBOL_STR(vfs_read) },
	{ 0xfb578fc5, __VMLINUX_SYMBOL_STR(memset) },
	{ 0x823c348d, __VMLINUX_SYMBOL_STR(blk_alloc_queue) },
	{ 0x1916e38c, __VMLINUX_SYMBOL_STR(_raw_spin_unlock_irqrestore) },
	{ 0x887745c8, __VMLINUX_SYMBOL_STR(current_task) },
	{ 0x156a8a59, __VMLINUX_SYMBOL_STR(down_trylock) },
	{ 0x27e1a049, __VMLINUX_SYMBOL_STR(printk) },
	{ 0x449ad0a7, __VMLINUX_SYMBOL_STR(memcmp) },
	{ 0xa2c60124, __VMLINUX_SYMBOL_STR(del_gendisk) },
	{ 0x6dc6dd56, __VMLINUX_SYMBOL_STR(down) },
	{ 0x71a50dbc, __VMLINUX_SYMBOL_STR(register_blkdev) },
	{ 0x41c81e72, __VMLINUX_SYMBOL_STR(bio_endio) },
	{ 0x61651be, __VMLINUX_SYMBOL_STR(strcat) },
	{ 0xb5a459dc, __VMLINUX_SYMBOL_STR(unregister_blkdev) },
	{ 0x7cd8d75e, __VMLINUX_SYMBOL_STR(page_offset_base) },
	{ 0x40a9b349, __VMLINUX_SYMBOL_STR(vzalloc) },
	{ 0x90ada41e, __VMLINUX_SYMBOL_STR(blk_queue_make_request) },
	{ 0xdb7305a1, __VMLINUX_SYMBOL_STR(__stack_chk_fail) },
	{ 0x1000e51, __VMLINUX_SYMBOL_STR(schedule) },
	{ 0xa202a8e5, __VMLINUX_SYMBOL_STR(kmalloc_order_trace) },
	{ 0xcaa6eb72, __VMLINUX_SYMBOL_STR(put_disk) },
	{ 0x4ce51a8d, __VMLINUX_SYMBOL_STR(wake_up_process) },
	{ 0xbdfb6dbb, __VMLINUX_SYMBOL_STR(__fentry__) },
	{ 0x4ca81f8d, __VMLINUX_SYMBOL_STR(kmem_cache_alloc_trace) },
	{ 0xe259ae9e, __VMLINUX_SYMBOL_STR(_raw_spin_lock) },
	{ 0x680ec266, __VMLINUX_SYMBOL_STR(_raw_spin_lock_irqsave) },
	{ 0xa6bbd805, __VMLINUX_SYMBOL_STR(__wake_up) },
	{ 0x4f68e5c9, __VMLINUX_SYMBOL_STR(do_gettimeofday) },
	{ 0x1e047854, __VMLINUX_SYMBOL_STR(warn_slowpath_fmt) },
	{ 0xc9fef317, __VMLINUX_SYMBOL_STR(add_wait_queue) },
	{ 0x9b26fae8, __VMLINUX_SYMBOL_STR(bdbm_dm_get_inf) },
	{ 0x37a0cba, __VMLINUX_SYMBOL_STR(kfree) },
	{ 0x69acdf38, __VMLINUX_SYMBOL_STR(memcpy) },
	{ 0x6df1aaf1, __VMLINUX_SYMBOL_STR(kernel_sigaction) },
	{ 0x78e739aa, __VMLINUX_SYMBOL_STR(up) },
	{ 0xb2d5a552, __VMLINUX_SYMBOL_STR(complete) },
	{ 0xf0e1ebd, __VMLINUX_SYMBOL_STR(device_add_disk) },
	{ 0x25857558, __VMLINUX_SYMBOL_STR(blk_queue_logical_block_size) },
	{ 0x760a0f4f, __VMLINUX_SYMBOL_STR(yield) },
	{ 0xe7a09921, __VMLINUX_SYMBOL_STR(vfs_write) },
	{ 0xb7bd8bf0, __VMLINUX_SYMBOL_STR(filp_open) },
};

static const char __module_depends[]
__used
__attribute__((section(".modinfo"))) =
"depends=risa_dev_ramdrive";


MODULE_INFO(srcversion, "515841DAECAD60901DF7563");
