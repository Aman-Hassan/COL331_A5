#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "traps.h"
#include "spinlock.h"
#include "fs.h"
#include "sleeplock.h"
#include "buf.h"

//? TLB Updates in swap out
//? Invalidate page in TLB in the Page fault handler function

#define MAX_SWAP_SLOTS NSWAPSLOTS

struct swap_slot swap_slots[MAX_SWAP_SLOTS];

// initialize swap slots during booting
void swapinit(int dev)
{
    for (int i = 0; i < MAX_SWAP_SLOTS; i++)
    {
        swap_slots[i].dev_id = dev;
        swap_slots[i].is_free = 1;
        swap_slots[i].page_perm = 0;
        swap_slots[i].proc_id = -1;
        swap_slots[i].swap_start = i * 8 + 2; // 2 is the starting block of swap slots
    }
    // cprintf("Swap slots initialized\n");
}

// New code
void page_swap_out(pte_t *victim_pte, struct proc *victim_proc)
{
    if (victim_pte == (void *)-1)
    {
        panic("No victim page found");
    }
    victim_proc->rss -= PGSIZE;
    struct swap_slot *swap_slot = swap_get_free_slot();
    if (swap_slot == (void *)-1)
    {
        panic("No free swap slot found");
    }
    swap_slot->page_perm = PTE_FLAGS(*victim_pte);
    uint pa = PTE_ADDR(*victim_pte);
    write_page_to_disk((char *)P2V(pa), swap_slot);
    kfree((char *)P2V(pa));
    *victim_pte = ((swap_slot->swap_start) << PTXSHIFT) | PTE_FLAGS(*victim_pte);
    *victim_pte &= ~PTE_P;
    *victim_pte |= PTE_SWAP;
    swap_slot->proc_id = victim_proc->pid;
}

void write_page_to_disk(char *page_start, struct swap_slot *swap_slot)
{
    // cprintf("Writing page to disk\n");
    int blockno = swap_slot->swap_start;
    // cprintf("blockno: %d\n", blockno);
    for (int i = 0; i < 8; i++)
    {
        struct buf *buffer = bread(ROOTDEV, blockno + i);
        memmove(buffer->data, page_start + i * BSIZE, BSIZE);
        bwrite(buffer);
        brelse(buffer);
    }
    // cprintf("Page written to disk\n");
}

struct swap_slot *swap_get_free_slot()
{
    // cprintf("Getting free slot\n");
    for (int i = 0; i < MAX_SWAP_SLOTS; i++)
    {
        if (swap_slots[i].is_free == 1)
        {
            swap_slots[i].is_free = 0; // mark it as occupied
            return &swap_slots[i];     // return the slot number
        }
    }
    cprintf("No free slot found\n");
    return (void *)-1;
}

void page_fault_handler(void)
{
    // cprintf("Page fault handler\n");
    uint faulting_address = rcr2();
    struct proc *curproc = myproc();
    pde_t *pgdir = curproc->pgdir;
    pte_t *pte = walkpgdir(pgdir, (void *)faulting_address, 0);
    int swap_slot = *pte >> PTXSHIFT; // Get the swap block number from the PTE
    char *mem = kalloc();
    // cprintf("Memory allocated, mem: %d\n", mem);
    if (mem == 0)
    {
        panic("Failed to allocate memory for swapped in page");
    }
    curproc->rss += PGSIZE;
    int blockno = swap_slot;
    for (int i = 0; i < 8; i++)
    {
        struct buf *b = bread(ROOTDEV, blockno + i);
        memmove(mem + i * BSIZE, (void *)b->data, BSIZE);
        brelse(b);
    }
    int swap_index = (blockno - 2) / 8;
    *pte = V2P(mem) | swap_slots[swap_index].page_perm;
    *pte |= PTE_P;
    swap_slots[swap_index].is_free = 1;
    swap_slots[swap_index].page_perm = 0;
    swap_slots[swap_index].proc_id = -1;
    *pte &= ~PTE_SWAP;
    // cprintf("Page fault handler exited\n");
    return;
}

// RSS Updates using  allocuvm
void update_rss(struct proc *p)
{
    // cprintf("Updating RSS\n");
    int oldsz = p->sz;
    int newsz = allocuvm(p->pgdir, oldsz, oldsz + 4096); // allocate one more page
    if (newsz == 0)
    {
        return;
    }
    p->sz = newsz;
}

// Process Terminate, free the slots and other updates
// Upon completion of the process, clean the unused swap slots.
void swap_free(struct proc *p)
{
    for (int i = 0; i < MAX_SWAP_SLOTS; i++)
    {
        if (swap_slots[i].proc_id == p->pid)
        {
            swap_slots[i].is_free = 1;
            swap_slots[i].dev_id = ROOTDEV;
            swap_slots[i].page_perm = 0;
            swap_slots[i].proc_id = -1;
        }
    }
}