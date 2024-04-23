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

#define MAX_SWAP_SLOTS SWAPBLOCKS/8

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
        for (int j = 0; j < NPROC; ++j)
        {
            swap_slots[i].swapmap[j] = 0;
            swap_slots[i].page_permmap[j] = 0;
        }
    }
    // cprintf("Swap slots initialized\n");
}

// New code
void page_swap_out(pte_t *victim_pte, struct proc *victim_proc)
{
    cprintf("pages_swap_out\n");
    if (victim_pte == (void *)-1)
    {
        panic("No victim page found");
    }
    struct swap_slot *swap_slot = swap_get_free_slot();
    if (swap_slot == (void *)-1)
    {
        panic("No free swap slot found");
    }
    uint pa = PTE_ADDR(*victim_pte);
    write_page_to_disk((char *)P2V(pa), swap_slot);
    cprintf("page written\n");
    for(int i=0;i<NPROC;++i){
        pte_t* pte = (pte_t *) mylist(pa, i);
        if(pte==0){
            continue;
        }
        (swap_slot->page_permmap)[i] = PTE_FLAGS(*pte);
        swap_slot->swapmap[i] = pte;
        *pte = ((swap_slot->swap_start) << PTXSHIFT) | PTE_FLAGS(*pte);
        *pte &= ~PTE_P| PTE_SWAP;
        update_ref_count(pa, -1, pte);
        cprintf("update %x %x\n", pa, *pte);
    }
    kfree((char*)P2V(pa));
    cprintf("page_swap_out exited\n");

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
static pte_t *
walkpgdir(pde_t *pgdir, const void *va, int alloc)
{
  pde_t *pde;
  pte_t *pgtab;

  pde = &pgdir[PDX(va)];
  if(*pde & PTE_P){
    pgtab = (pte_t*)P2V(PTE_ADDR(*pde));
  } else {
    if(!alloc || (pgtab = (pte_t*)kalloc()) == 0)
      return 0;

    memset(pgtab, 0, PGSIZE);
    *pde = V2P(pgtab) | PTE_P | PTE_W | PTE_U;
  }
  return &pgtab[PTX(va)];
}


void clean_all_slots(pte_t *pte)
{
    int slot_id = 0 ;
    while (slot_id < MAX_SWAP_SLOTS)
    {   
        int if_free = swap_slots[slot_id].is_free;
        if (if_free == 0)
        {
            for (int i = NPROC; i >=0; i--)
            {
                if (swap_slots[slot_id].swapmap[i] == pte)
                {
                    swap_slots[slot_id].swapmap[i] = 0;
                }
            }
            int count = 0;
            for (int i = 0; i < NPROC; i++)
            {
                if (swap_slots[slot_id].swapmap[i] == 0)
                {
                    count++;
                }
            }
            if (count == NPROC)
            {
                swap_slots[slot_id].is_free = 1;
                int proc = 0;
                while (proc < NPROC)
                {
                    swap_slots[slot_id].swapmap[proc] = 0;
                    swap_slots[slot_id].page_permmap[proc] = 0;
                    proc++;
                }
            }
        }
        slot_id++;
    }
}

void page_fault_handler(void)
{
    cprintf("Page fault handler\n");
    uint faulting_address = PGROUNDDOWN(rcr2());
    struct proc *curproc = myproc();
    pde_t *pgdir = curproc->pgdir;
    pte_t *pte = walkpgdir(pgdir, (void *)faulting_address, 0);

    int swap_slot = *pte >> PTXSHIFT; // Get the swap block number from the PTE
    char *mem = kalloc();
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
    int proc_id = 0;
    while(proc_id < NPROC)
    {
        if (swap_slots[swap_index].swapmap[proc_id] == 0)
        {
            proc_id++;
            continue;
        }
        pte_t *pte_2 = swap_slots[swap_index].swapmap[proc_id];
        uint perm = swap_slots[swap_index].page_permmap[proc_id];
        proc_id ++;
        *pte_2 = V2P(mem) | perm | PTE_P | PTE_W;
        *pte_2 &= ~PTE_SWAP;
        update_ref_count(V2P(mem), 1, pte_2);
    }
    swap_slots[swap_index].is_free = 1;
    int proc_j = NPROC - 1;
    while (proc_j>=0)
    {
        swap_slots[swap_index].swapmap[proc_j] = 0;
        swap_slots[swap_index].page_permmap[proc_j] = 0;
        proc_j--;
    }
    cprintf("Page fault handler exited\n");
    return;
}

void swap_in(pte_t *pte)
{
    // pte_t *pte = walkpgdir(pgdir, (void *)faulting_address, 0)
    // cprintf("swapping in va %x, pte %x, pid %x\n",faulting_address, *pte, curproc->pid);
    cprintf("Swapping in\n");

    int swap_slot = *pte >> PTXSHIFT; // Get the swap block number from the PTE
    char *mem = kalloc();
    // cprintf("Memory allocated, mem: %d\n", mem);
    if (mem == 0)
    {
        panic("Failed to allocate memory for swapped in page");
    }
    myproc()->rss += PGSIZE;
    int blockno = swap_slot;
    for (int i = 0; i < 8; i++)
    {
        struct buf *b = bread(ROOTDEV, blockno + i);
        memmove(mem + i * BSIZE, (void *)b->data, BSIZE);
        brelse(b);
    }
    int swap_index = (blockno - 2) / 8;
    int proc_id = 0;
    while(proc_id < NPROC)
    {
        if (swap_slots[swap_index].swapmap[proc_id] != 0)
        {
        pte_t *pte_2 = swap_slots[swap_index].swapmap[proc_id];
        uint perm = swap_slots[swap_index].page_permmap[proc_id];
        *pte_2 = V2P(mem);
        *pte_2 &= ~PTE_SWAP| perm| PTE_P | PTE_W;
        update_ref_count(V2P(mem), 1, pte_2);
        }
        else{
            proc_id++;
            continue;
        }
        proc_id++;
    }
    int proc_j = NPROC-1;
    swap_slots[swap_index].is_free = 1;
    while (proc_j >= 0)
    {
        swap_slots[swap_index].swapmap[proc_j] = 0;
        swap_slots[swap_index].page_permmap[proc_j] = 0;
        proc_j--;
    }
    cprintf("swap_in exited\n");
    return;
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

