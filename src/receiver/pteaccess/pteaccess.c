#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/slab.h>

// meta
MODULE_AUTHOR("Micha Hanselmann");
MODULE_DESCRIPTION("PTE access helper for tlbchannels");
MODULE_LICENSE("GPL");

// proc file for user-space communication
static struct proc_dir_entry *proc_file;

// accessed bit monitoring
static size_t ptes_size = -1;
static pte_t **ptes = NULL;
static char *bits = NULL;

// retrieve pte from page table
static pte_t* walk_page_table(unsigned long vaddr) {
    pgd_t *pgd; p4d_t *p4d; pud_t *pud; pmd_t *pmd; pte_t *pte;

    pgd = pgd_offset(current->mm, vaddr);
    if (pgd_none(*pgd) || pgd_bad(*pgd)) {
        printk(KERN_DEBUG "[pteaccess] invalid pgd (vaddr: 0x%lx)\n", vaddr);
        return NULL;
    }
    p4d = p4d_offset(pgd, vaddr);
    if (p4d_none(*p4d) || p4d_bad(*p4d)) {
        printk(KERN_DEBUG "[pteaccess] invalid p4d (vaddr: 0x%lx)\n", vaddr);
        return NULL;
    }
    pud = pud_offset(p4d, vaddr);
    if (pud_none(*pud) || pud_bad(*pud)) {
        printk(KERN_DEBUG "[pteaccess] invalid pud (vaddr: 0x%lx)\n", vaddr);
        return NULL;
    }
    pmd = pmd_offset(pud, vaddr);
    if (pmd_none(*pmd) || pmd_bad(*pmd)) {
        printk(KERN_DEBUG "[pteaccess] invalid pmd (vaddr: 0x%lx)\n", vaddr);
        return NULL;
    }
    pte = pte_offset_map(pmd, vaddr);
    if (pte_none(*pte)) {
        printk(KERN_DEBUG "[pteaccess] invalid pte (vaddr: 0x%lx)\n", vaddr);
        return NULL;
    }

    return pte;
}

// proc file read handler
static ssize_t proc_read(struct file *file, char __user *ubuf, size_t count, loff_t *ppos) {
    pte_t pte;
    int i;

    // iterate over ptes
    for (i = 0; i < ptes_size; i++) {
        pte = *ptes[i];
        if (i % 8 == 0) bits[i / 8] = 0;

        // read and reset accessed bit (1 byte contains 8 accessed bits)
        if (pte_present(pte)) {
            bits[i / 8] |= ((pte_young(pte) ? 1 : 0) << (i % 8));
            set_pte(ptes[i], pte_mkold(pte));
        } else {
            printk(KERN_DEBUG "[pteaccess] error: pte not present\n");
            return -1;
        }
    }

    // copy data to user-space
    if (copy_to_user(ubuf, bits, ptes_size / 8)) return -1;

    return count;
}

// proc file write handler
static ssize_t proc_write(struct file *file, const char __user *ubuf, size_t count, loff_t *ppos) {
    uint64_t vaddr;

    // validate data
    if (count != sizeof(uint64_t)) {
        printk(KERN_DEBUG "[pteaccess] invalid write (count: %ld, pos: %lld)\n", count, *ppos);
        return -1;
    }

    // copy data from user-space
    if (copy_from_user(&vaddr, ubuf, sizeof(uint64_t))) return -1;

    // config vaddr
    if (vaddr == 0) {
        const uint64_t num_entries = *ppos;
        if (num_entries % 8 != 0) {
            printk(KERN_DEBUG "[pteaccess] invalid config: entries not a multiple of 8");
            return -1;
        }
        printk(KERN_DEBUG "[pteaccess] config: %lld entries\n", num_entries);
        ptes = (pte_t**)krealloc(ptes, num_entries * sizeof(pte_t*), GFP_KERNEL);
        bits = (char*)krealloc(bits, (num_entries / 8) * sizeof(char), GFP_KERNEL);
        if (!ptes || !bits) {
            printk(KERN_DEBUG "[pteaccess] error allocating memory");
            return -1;
        } else {
            ptes_size = num_entries;
        }
    }

    // valid vaddr
    else {
        // get and store pte
        ptes[*ppos] = walk_page_table((unsigned long)vaddr);
        // printk(KERN_DEBUG "[pteaccess] write vaddr: %#lx (pos: %lld, pte: %#lx)\n", (unsigned long)vaddr, *ppos, (unsigned long)ptes[*ppos] & 0xffffffffffff);
        if (!ptes[*ppos]) return -1;
    }

    return count;
}

// proc file config
static struct file_operations proc_ops = {
    .owner = THIS_MODULE,
    .read = proc_read,
    .write = proc_write,
};

// init module
static int __init pteaccess_init(void) {
    proc_file = proc_create("pteaccess", 0666, NULL, &proc_ops);
    printk(KERN_DEBUG "[pteaccess] module loaded\n");
    return 0;
}

// unload module
static void __exit pteaccess_exit(void) {
    proc_remove(proc_file);
    kfree(ptes);
    kfree(bits);
    printk(KERN_DEBUG "[pteaccess] module unloaded\n");
}

module_init(pteaccess_init);
module_exit(pteaccess_exit);