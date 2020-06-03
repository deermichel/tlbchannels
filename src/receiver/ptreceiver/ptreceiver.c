#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/slab.h>
#include "../../packet.h"

// meta
MODULE_AUTHOR("Micha Hanselmann");
MODULE_DESCRIPTION("Kernel module receiver for tlbchannels");
MODULE_LICENSE("GPL");

// proc file for user-space communication
static struct proc_dir_entry *proc_file;

// accessed bit monitoring
static size_t ptes_size = -1;
static pte_t **ptes = NULL;
static uint8_t **vaddrs = NULL;

// receiver buffer
#define BUFFER_SIZE 20
static uint8_t *buffer = NULL;

// retrieve pte from page table
static pte_t* walk_page_table(unsigned long vaddr) {
    pgd_t *pgd; p4d_t *p4d; pud_t *pud; pmd_t *pmd; pte_t *pte;

    pgd = pgd_offset(current->mm, vaddr);
    if (pgd_none(*pgd) || pgd_bad(*pgd)) {
        printk(KERN_DEBUG "[ptreceiver] invalid pgd (vaddr: 0x%lx)\n", vaddr);
        return NULL;
    }
    p4d = p4d_offset(pgd, vaddr);
    if (p4d_none(*p4d) || p4d_bad(*p4d)) {
        printk(KERN_DEBUG "[ptreceiver] invalid p4d (vaddr: 0x%lx)\n", vaddr);
        return NULL;
    }
    pud = pud_offset(p4d, vaddr);
    if (pud_none(*pud) || pud_bad(*pud)) {
        printk(KERN_DEBUG "[ptreceiver] invalid pud (vaddr: 0x%lx)\n", vaddr);
        return NULL;
    }
    pmd = pmd_offset(pud, vaddr);
    if (pmd_none(*pmd) || pmd_bad(*pmd)) {
        printk(KERN_DEBUG "[ptreceiver] invalid pmd (vaddr: 0x%lx)\n", vaddr);
        return NULL;
    }
    pte = pte_offset_map(pmd, vaddr);
    if (pte_none(*pte)) {
        printk(KERN_DEBUG "[ptreceiver] invalid pte (vaddr: 0x%lx)\n", vaddr);
        return NULL;
    }

    return pte;
}

// proc file read handler
static ssize_t proc_read(struct file *file, char __user *ubuf, size_t count, loff_t *ppos) {
    pte_t pte;
    int i;
    int _temp;
    uint32_t buffer_index = 0;
    uint8_t seq = 0;
    static uint8_t stop_count = 0;
    static uint8_t last_seq = 0xFF;
    packet_t packet;

    // receiver loop
    while (buffer_index < BUFFER_SIZE) {
        // prepare packet
        uint8_t *raw = &buffer[buffer_index * PACKET_SIZE];
        memset(raw, 0x00, PACKET_SIZE);

        // touch pages to create tlb entries
        for (i = 0; i < ptes_size; i++) {
            get_user(_temp, vaddrs[i]);
        }

        // iterate over ptes
        for (i = 0; i < ptes_size; i++) {
            pte = *ptes[i];

            // read and reset accessed bit
            if (pte_present(pte)) {
                raw[i / 8] |= ((pte_young(pte) ? 1 : 0) << (i % 8));
                set_pte(ptes[i], pte_mkold(pte));
            } else {
                printk(KERN_DEBUG "[ptreceiver] error: pte not present\n");
                return -1;
            }
        }
        memcpy(packet.raw, raw, PACKET_SIZE);

        // data stop
        if (is_data_stop(&packet) && stop_count++ == 100) {
            stop_count = 0;
            last_seq = 0xFF;
            break;
        };

        // seq
        seq = packet.header[0];
        if (seq == 0x00 || seq == 0xFF || seq == last_seq) continue; // same or invalid seq
        last_seq = seq;

        buffer_index++;
    }

    return buffer_index;
}

// proc file write handler
static ssize_t proc_write(struct file *file, const char __user *ubuf, size_t count, loff_t *ppos) {
    uint64_t vaddr;

    // validate data
    if (count != sizeof(uint64_t)) {
        printk(KERN_DEBUG "[ptreceiver] invalid write (count: %ld, pos: %lld)\n", count, *ppos);
        return -1;
    }

    // copy data from user-space
    if (copy_from_user(&vaddr, ubuf, sizeof(uint64_t))) return -1;

    // config vaddr
    if (vaddr == 0) {
        const uint64_t num_entries = *ppos;
        if (num_entries % 8 != 0) {
            printk(KERN_DEBUG "[ptreceiver] invalid config: entries not a multiple of 8");
            return -1;
        }
        printk(KERN_DEBUG "[ptreceiver] config: %lld entries\n", num_entries);
        ptes = (pte_t**)krealloc(ptes, num_entries * sizeof(pte_t*), GFP_KERNEL);
        vaddrs = (uint8_t**)krealloc(vaddrs, num_entries * sizeof(uint8_t*), GFP_KERNEL);
        if (!ptes || !vaddrs) {
            printk(KERN_DEBUG "[ptreceiver] error allocating memory");
            return -1;
        } else {
            ptes_size = num_entries;
        }
    }

    // valid vaddr
    else {
        // get and store pte
        vaddrs[*ppos] = (uint8_t*)vaddr;
        ptes[*ppos] = walk_page_table((unsigned long)vaddr);
        // printk(KERN_DEBUG "[ptreceiver] write vaddr: %#lx (pos: %lld, pte: %#lx)\n", (unsigned long)vaddr, *ppos, (unsigned long)ptes[*ppos] & 0xffffffffffff);
        if (!ptes[*ppos]) return -1;
    }

    return count;
}

// page fault handler
static unsigned int proc_vm_fault(struct vm_fault *vmf) {
    vmf->page = vmalloc_to_page(buffer);
    get_page(vmf->page);

    return 0;
}

// mmap mapping config
static struct vm_operations_struct vm_ops = {
    .fault = proc_vm_fault
};

// proc file mmap handler
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
    .read = proc_read,
    .write = proc_write,
};

// init module
static int __init ptreceiver_init(void) {
    buffer = vmalloc_user(PACKET_SIZE * BUFFER_SIZE);
    if (!buffer) printk(KERN_DEBUG "[ptreceiver] error allocating buffer\n");
    proc_file = proc_create("ptreceiver", 0666, NULL, &proc_ops);
    printk(KERN_DEBUG "[ptreceiver] module loaded\n");
    return 0;
}

// unload module
static void __exit ptreceiver_exit(void) {
    proc_remove(proc_file);
    kfree(ptes);
    kfree(vaddrs);
    vfree(buffer);
    printk(KERN_DEBUG "[ptreceiver] module unloaded\n");
}

module_init(ptreceiver_init);
module_exit(ptreceiver_exit); 