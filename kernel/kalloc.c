// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

#define STEALCNT 1024 // CPU的freelist资源不足时从其他CPU获取的资源数量

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem[NCPU]; // 每个CPU占有一个lock和一个freelist

void
kinit()
{
  char lockname[20];
  for(int i = 0; i < NCPU; i++){
    snprintf(lockname, 20, "kmem-%d", i);
    // initlock(&kmem.lock, "kmem");
    initlock(&kmem[i].lock, lockname);
  }
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;
  push_off();
  int curcpu = cpuid();
  pop_off();

  acquire(&kmem[curcpu].lock);
  r->next = kmem[curcpu].freelist;
  kmem[curcpu].freelist = r;
  release(&kmem[curcpu].lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  push_off();
  int curcpu = cpuid();
  pop_off();

  acquire(&kmem[curcpu].lock);
  r = kmem[curcpu].freelist;
  
  if(r)
    kmem[curcpu].freelist = r->next;
  else{
    // 从其它cpu中获取可用空间
    for(int i = 0; i < NCPU; i++){
      if(i == curcpu)
        continue;
      acquire(&kmem[i].lock);
      if(kmem[i].freelist){
        r = kmem[i].freelist;
        kmem[i].freelist = r->next;

        struct run* newhead = kmem[i].freelist;
        for(int j = 0; j < STEALCNT && kmem[i].freelist; j++){
          kmem[i].freelist = kmem[i].freelist->next;
        }
        if(kmem[i].freelist){
          struct run* nexthead = kmem[i].freelist->next;
          kmem[i].freelist->next = 0;
          kmem[i].freelist = nexthead;
        }
        release(&kmem[i].lock);
        kmem[curcpu].freelist = newhead;
        break;
        
        
        // release(&kmem[i].lock);
        // break;
      }
      else{
        release(&kmem[i].lock);
      }
    }
  }
  release(&kmem[curcpu].lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
