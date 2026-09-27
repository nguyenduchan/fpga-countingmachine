// Two contiguous frame buffers from the low-DDR CMA pool.
// The FPGA HP port can reach this window. Huge pages on this board land in high DDR.

#include <linux/cma.h>
#include <linux/kprobes.h>
#include <linux/miscdevice.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/uaccess.h>

#define FPGA_FRAME_IOCTL_PHYS 1

typedef unsigned long (*kallsyms_lookup_name_t)(const char *name);
typedef struct page *(*cma_alloc_t)(struct cma *, unsigned long, unsigned int, bool);
typedef bool (*cma_release_t)(struct cma *, const struct page *, unsigned long);

static struct cma *cma_area;
static cma_alloc_t cma_alloc_fn;
static cma_release_t cma_release_fn;
static struct page *pages;
static u64 phys;
static unsigned long frame_pages;

static unsigned long resolve_symbol(const char *name)
{
	struct kprobe probe = {.symbol_name = "kallsyms_lookup_name"};
	kallsyms_lookup_name_t lookup;
	unsigned long address;

	if (register_kprobe(&probe) < 0)
		return 0;
	lookup = (kallsyms_lookup_name_t)probe.addr;
	unregister_kprobe(&probe);
	address = lookup(name);
	return address;
}

static int fpga_frame_mmap(struct file *file, struct vm_area_struct *vma)
{
	unsigned long size = vma->vm_end - vma->vm_start;

	if (pages == NULL || size > frame_pages * PAGE_SIZE)
		return -EINVAL;
	return remap_pfn_range(vma, vma->vm_start, page_to_pfn(pages), size, vma->vm_page_prot);
}

static long fpga_frame_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	u64 value = phys;

	if (cmd != FPGA_FRAME_IOCTL_PHYS)
		return -ENOTTY;
	if (copy_to_user((void __user *)arg, &value, sizeof(value)))
		return -EFAULT;
	return 0;
}

static const struct file_operations fpga_frame_fops = {
	.owner = THIS_MODULE,
	.mmap = fpga_frame_mmap,
	.unlocked_ioctl = fpga_frame_ioctl,
};

static struct miscdevice fpga_frame_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "fpga_frame",
	.fops = &fpga_frame_fops,
};

static int __init fpga_frame_init(void)
{
	int ret;

	frame_pages = (4UL * 1024UL * 1024UL) / PAGE_SIZE;
	cma_alloc_fn = (cma_alloc_t)resolve_symbol("cma_alloc");
	cma_release_fn = (cma_release_t)resolve_symbol("cma_release");
	cma_area = *(struct cma **)resolve_symbol("dma_contiguous_default_area");
	if (cma_alloc_fn == NULL || cma_release_fn == NULL || cma_area == NULL)
		return -ENOENT;
	pages = cma_alloc_fn(cma_area, frame_pages, 9, false);
	if (pages == NULL)
		return -ENOMEM;
	phys = page_to_phys(pages);
	pr_info("fpga_frame phys=0x%llx bytes=%lu\n", phys, frame_pages * PAGE_SIZE);
	ret = misc_register(&fpga_frame_misc);
	if (ret != 0)
		cma_release_fn(cma_area, pages, frame_pages);
	return ret;
}

static void __exit fpga_frame_exit(void)
{
	misc_deregister(&fpga_frame_misc);
	if (pages != NULL)
		cma_release_fn(cma_area, pages, frame_pages);
}

module_init(fpga_frame_init);
module_exit(fpga_frame_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Low-DDR frame buffers for the tile brightness FPGA");
