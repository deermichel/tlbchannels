#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/proc_fs.h>

// meta
MODULE_AUTHOR("Micha Hanselmann, Marc Rittinghaus");
MODULE_DESCRIPTION("Page table mapper helper for tlbchannels");
MODULE_LICENSE("GPL");

// proc file for user-space communication
static struct proc_dir_entry *proc_file;

// pointer to page table for access bit monitoring
static pmd_t *pmd_base = NULL;

// retrieve page table
static pmd_t* walk_page_table(unsigned long vaddr) {
    pgd_t* pgd; p4d_t* p4d; pud_t* pud; pmd_t* pmd;

    pgd = pgd_offset(current->mm, vaddr);
    if (pgd_none(*pgd) || pgd_bad(*pgd)) {
        printk(KERN_DEBUG "[ptmapper] invalid pgd (vaddr: 0x%lx)\n", vaddr);
        return NULL;
    }
    p4d = p4d_offset(pgd, vaddr);
    if (p4d_none(*p4d) || p4d_bad(*p4d)) {
        printk(KERN_DEBUG "[ptmapper] invalid p4d (vaddr: 0x%lx)\n", vaddr);
        return NULL;
    }
    pud = pud_offset(p4d, vaddr);
    if (pud_none(*pud) || pud_bad(*pud)) {
        printk(KERN_DEBUG "[ptmapper] invalid pud (vaddr: 0x%lx)\n", vaddr);
        return NULL;
    }
    pmd = pmd_offset(pud, vaddr);
    if (pmd_none(*pmd) || pmd_bad(*pmd)) {
        printk(KERN_DEBUG "[ptmapper] invalid pmd (vaddr: 0x%lx)\n", vaddr);
        return NULL;
    }

    return pmd;
}

// page fault handler
static unsigned int proc_vm_fault(struct vm_fault *vmf) {
    struct page *page;

    if (pmd_none(*pmd_base)) {
        return -1;
    } else {
        page = pmd_page(*pmd_base);

        // hacky! PTs are marked as such using the mapcount field and thus fail to unmap, due to broken mapcount.
        if (atomic_read(&page->_mapcount) < 0) {
            atomic_set(&page->_mapcount, 0);
        }
    }
    get_page(page);
    vmf->page = page;

    return 0;
}

// proc file write handler
static ssize_t proc_write(struct file *file, const char __user *ubuf, size_t count, loff_t *ppos) {
    uint64_t vaddr;

    // validate data
    if (count != sizeof(uint64_t)) {
        printk(KERN_DEBUG "[ptmapper] invalid write (count: %ld, pos: %lld)\n", count, *ppos);
        return -1;
    }

    // copy data from user-space
    if (copy_from_user(&vaddr, ubuf, sizeof(uint64_t))) return -1;

    // get and store page table pointer
    pmd_base = walk_page_table((unsigned long)vaddr);
    if (!pmd_base) return -1;

    return count;
}

// page mapping config
static struct vm_operations_struct vm_ops = {
    .fault = proc_vm_fault
};

// proc mmap handler
static int proc_mmap(struct file *filp, struct vm_area_struct *vma) {
    vma->vm_ops = &vm_ops;
    vma->vm_flags |= VM_DONTEXPAND | VM_DONTDUMP;
    vma->vm_private_data = filp->private_data;

    return 0;
}

// proc file config
static struct file_operations proc_ops = {
    .owner = THIS_MODULE,
    .mmap = proc_mmap,
    .write = proc_write,
};

// init module
static int __init ptmapper_init(void) {
    proc_file = proc_create("ptmapper", 0666, NULL, &proc_ops);
    printk(KERN_DEBUG "[ptmapper] module loaded\n");
    return 0;
}

// unload module
static void __exit ptmapper_exit(void) {
    proc_remove(proc_file);
    printk(KERN_DEBUG "[ptmapper] module unloaded\n");
}

module_init(ptmapper_init);
module_exit(ptmapper_exit);