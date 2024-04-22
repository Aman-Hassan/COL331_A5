// Physical memory allocator, intended to allocate
// memory for user processes, kernel stacks, page table pages,
// and pipe buffers. Allocates 4096-byte pages.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "spinlock.h"

void freerange(void *vstart, void *vend);
extern char end[]; // first address after kernel loaded from ELF file
                   // defined by the kernel linker script in kernel.ld

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  int use_lock;
  uint num_free_pages;  //store number of free pages
  struct run *freelist;
  int rmap[PHYSTOP >> PTXSHIFT]; //store the reference count of each page
  pte_t* rmap_2D[PHYSTOP >> PTXSHIFT][NPROC]; //store the reference count of each page for each process
} kmem;

// Small function to obtain the pointer to reference count of a page given virtual address v [we'll return pointer so that we can increment/decrement the reference count easily]
//! This should not be used as get_ref_count (since we are not applying any lock here) -> this will be done in the caller function
//! Use this function wisely -> only when the caller function acquires lock before calling this function
int* get_ref_count_without_locks(char* v)
{
  return &kmem.rmap[V2P(v) >> PTXSHIFT];
}


// Initialization happens in two phases.
// 1. main() calls kinit1() while still using entrypgdir to place just
// the pages mapped by entrypgdir on free list.
// 2. main() calls kinit2() with the rest of the physical pages
// after installing a full page table that maps them on all cores.
void
kinit1(void *vstart, void *vend)
{
  initlock(&kmem.lock, "kmem");
  kmem.use_lock = 0;
  kmem.num_free_pages = 0;
  freerange(vstart, vend);
}

void
kinit2(void *vstart, void *vend)
{
  freerange(vstart, vend);
  kmem.use_lock = 1;
}

void
freerange(void *vstart, void *vend)
{
  char *p;
  p = (char*)PGROUNDUP((uint)vstart);
  for(; p + PGSIZE <= (char*)vend; p += PGSIZE)
  {
    // kmem.num_free_pages+=1;
    kmem.rmap[V2P(p) >> PTXSHIFT] = 0; //initialize the reference count of each page to 0
    for(int p_id=0;p_id<NPROC;++p_id){
      kmem.rmap_2D[V2P(p) >> PTXSHIFT][p_id] = 0;
    }  
      kfree(p);
  }
    
}
//PAGEBREAK: 21
// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(char *v)
{
  struct run *r;

  if((uint)v % PGSIZE || v < end || V2P(v) >= PHYSTOP)
    panic("kfree");

  if(kmem.use_lock)
    acquire(&kmem.lock);

  // Now depending on the value of ref_count, we can decide whether to free the page or not (if ref_count > 0, then don't free the page, just decrement)
  int* ref_count = get_ref_count_without_locks(v); // Notice how we are not applying any locks here since lock has been applied before calling this function
  if (*ref_count == 0){ //if ref_count == 0, then free the page and add it to the free list //just to be sure
    memset(v, 1, PGSIZE); // Fill with junk to catch dangling refs.
    r = (struct run*)v;
    r->next = kmem.freelist;
    kmem.num_free_pages+=1;
    kmem.freelist = r;
  }

  if(kmem.use_lock)
    release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
char*
kalloc(void)
{
  // cprintf("kalloc: num_free_pages = %d\n", kmem.num_free_pages);
  struct run *r;

  if(kmem.use_lock)
    acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
  {
    kmem.freelist = r->next;
    kmem.num_free_pages-=1;
  }
  if(kmem.use_lock)
    release(&kmem.lock);
  if (r)
    return (char*)r;
  struct proc* victim = find_victim_proc();
  if(victim == 0)
    cprintf("No victim proc found\n");
  pte_t* pte = find_victim_page(victim);
  page_swap_out(pte,victim);
  return kalloc();
}

uint 
num_of_FreePages(void)
{
  acquire(&kmem.lock);

  uint num_free_pages = kmem.num_free_pages;
  
  release(&kmem.lock);
  
  return num_free_pages;
}

// The following functions will be called in vm.c's pagefault handler to increment/decrement the reference count of the page
// So these functions will first acquire locks and then operate (since we are not going to be calling lock in the pagefault handler)

// function to update ref_count of a page
void update_ref_count(uint pa, int increment, pte_t * pt_entry) // increment = 1 if we want to increment the ref_count, increment = -1 if we want to decrement the ref_count
{
  // sanity check for bounds of pa
  if (pa >= PHYSTOP || pa < (uint) V2P(end))
    panic("update_ref_count: pa out of bounds");
  
  acquire(&kmem.lock);
  char* v = P2V(pa);
  int* ref_count = get_ref_count_without_locks(v); // Notice how we are not applying any locks here since lock has been applied before calling this function
  if (increment == 1){
    kmem.rmap_2D[pa >> PTXSHIFT][kmem.rmap[pa >> PTXSHIFT]] = pt_entry;
    *ref_count += increment;
  }
  else if (increment == -1){
    int index = NPROC;
    for(int j=0;j<NPROC;++j){
    if(kmem.rmap_2D[pa >> PTXSHIFT][j] == pt_entry){
      index = j;
      kmem.rmap_2D[pa >> PTXSHIFT][j] = 0;
      break;
    }
  }
  for(int j= index+1 ; j< NPROC; ++j){
    kmem.rmap_2D[pa >> PTXSHIFT][j-1] = kmem.rmap_2D[pa >> PTXSHIFT][j];
    kmem.rmap_2D[pa >> PTXSHIFT][j] = 0;
  }

    *ref_count += increment;
  }
  else
    panic("update_ref_count: increment should be either 1 or -1");
  release(&kmem.lock);
}

pte_t * mylist(uint phy_addr, int column){
  if(phy_addr >= PHYSTOP || phy_addr < (uint)V2P(end)){
    panic("false");
  }
  pte_t* answer_pte = kmem.rmap_2D[phy_addr >> PTXSHIFT][column];
  return answer_pte;
}

// function to obtain the reference count of a page [This is diff from get_ref_count_without_locks since we are applying locks here and this function is called by pagefault handler]
uint get_count_ref(uint pa){
  // sanity check for bounds of pa
  if (pa >= PHYSTOP || pa < (uint) V2P(end))
    panic("get_ref_count: pa out of bounds");
  
  acquire(&kmem.lock);
  char* v = P2V(pa);
  int* ref_count = get_ref_count_without_locks(v); // Notice how we are not applying any locks here since lock has been applied before calling this function
  // cprintf("Reference count of page at address %x is %d\n", v, *ref_count);
  release(&kmem.lock);
  return (uint) *ref_count;
}
