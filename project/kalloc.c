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

//rmap data structure as defined in the project
// stores ref count of each page and a 2D map for page to process
// TODO: Could possibly change the page_proc_map to something more space efficient
// TODO: Possibly something like a bitmap for each page to store which process is mapped to it (seems like a good idea)
// TODO: Or maybe a linked list of processes mapped to it (but that would be slow and hard to maintain)
struct rmap{
  int ref_count_map[PHYSTOP >> PTXSHIFT]; //store the reference count of each page
  pte_t* page_proc_map[PHYSTOP >> PTXSHIFT][NPROC]; //Stores whether each page is mapped to a process or not [a 2D matrix where each row corresponds to a page and each column corresponds to a process]
};

struct {
  struct spinlock lock;
  int use_lock;
  uint num_free_pages;  //store number of free pages
  struct run *freelist;
  struct rmap rmap;
} kmem;

// Small function to obtain the pointer to reference count of a page given virtual address v [we'll return pointer so that we can increment/decrement the reference count easily]
//! This should not be used as get_ref_count (since we are not applying any lock here) -> this will be done in the caller function
//! Use this function wisely -> only when the caller function acquires lock before calling this function
int* get_ref_count_without_locks(char* v)
{
  return &kmem.rmap.ref_count_map[V2P(v) >> PTXSHIFT];
}

// Small function to obtain the pointer to page_proc_map of a page given virtual address v and process id pid [we'll return pointer so that we can easily check if the page is mapped to a process or not]
//! This should not be used as get_page_proc (since we are not applying any lock here) -> this will be done in the caller function
//! Use this function wisely -> only when the caller function acquires lock before calling this function
pte_t* get_page_proc_without_locks(char* v, int pid)
{
  return kmem.rmap.page_proc_map[V2P(v) >> PTXSHIFT][pid];
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
    kfree(p);
    kmem.rmap.ref_count_map[V2P(p) >> PTXSHIFT] = 0; //initialize the reference count of each page to 0
    
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

  // // Fill with junk to catch dangling refs.
  // memset(v, 1, PGSIZE);


  if(kmem.use_lock)
    acquire(&kmem.lock);

  // Now depending on the value of ref_count, we can decide whether to free the page or not (if ref_count > 0, then don't free the page, just decrement)
  int* ref_count = get_ref_count_without_locks(v); // Notice how we are not applying any locks here since lock has been applied before calling this function

  if(*ref_count > 0) //if ref_count > 0, then don't free the page, just decrement
  {
    *ref_count -= 1;
  }
  if (*ref_count == 0){ //if ref_count == 0, then free the page and add it to the free list
    *ref_count = 0; //just to be sure
    memset(v, 1, PGSIZE); // Fill with junk to catch dangling refs.
    //incrememnt the number of free pages and add the page to the free list
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
  struct run *r;

  if(kmem.use_lock)
    acquire(&kmem.lock);
  r = kmem.freelist;
  
  if(r)
  {
    kmem.freelist = r->next;
    kmem.num_free_pages-=1;

    // Set the reference count of the page to 1 since page has just been allocated
    int* ref_count = get_ref_count_without_locks((char*)r); // Notice how we are not applying any locks here since lock has been applied before calling this function
    *ref_count = 1;
  }
    
  if(kmem.use_lock)
    release(&kmem.lock);
  return (char*)r;
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
void update_ref_count(uint pa, int increment) // increment = 1 if we want to increment the ref_count, increment = -1 if we want to decrement the ref_count
{
  // sanity check for bounds of pa
  if (pa >= PHYSTOP || pa < (uint) V2P(end))
    panic("update_ref_count: pa out of bounds");
  
  acquire(&kmem.lock);
  char* v = P2V(pa);
  int* ref_count = get_ref_count_without_locks(v); // Notice how we are not applying any locks here since lock has been applied before calling this function
  if (increment == 1 || increment == -1)
    *ref_count += increment;
  else
    panic("update_ref_count: increment should be either 1 or -1");
  release(&kmem.lock);
}

// function to obtain the reference count of a page [This is diff from get_ref_count_without_locks since we are applying locks here and this function is called by pagefault handler]
uint get_ref_count(uint pa){
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

// function to update page_proc_map of a page

