// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

static int refcnt[(uint64)PHYSTOP / 4096];

inline uint64 getrefpos(void* pa)
{
  return ((uint64)pa / 4096);
}

void
kinit()
{
  initlock(&kmem.lock, "kmem");
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

  // 检查页面的ref数量，如果refcnt不为0则不释放
  if(--refcnt[getrefpos(pa)] > 0){
    return;
  }

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r){
    memset((char*)r, 5, PGSIZE); // fill with junk
    refcnt[getrefpos(r)] = 1; // 初始化refcnt为1
  }
  
  return (void*)r;
}

// kref函数：将页面的被引用次数加1
void kref(void* pa)
{
  refcnt[getrefpos(pa)]++;
}

// kgetref函数：获取页面的被引用次数
int kgetref(void* pa)
{
  return refcnt[getrefpos(pa)];
}

int cow_handler(pagetable_t pagetable, pte_t* pte, uint64 va)
{
  uint64 pa = PTE2PA(*pte);
  // 进行重新分配页面处理
  char* mem;
  // 分配新页面
  if((mem = kalloc()) == 0){
    printf("kalloc fail\n");
    return -1;
  }
  uint flags = PTE_FLAGS(*pte);
  flags = flags & ~PTE_C;
  flags = flags | PTE_W;
  memmove(mem, (char*)pa, PGSIZE);
  if(kgetref((void*)pa) == 2){
    *pte = *pte | PTE_W;
    *pte = *pte & ~PTE_C;
  }
  uvmunmap(pagetable, va, 1, 1);  // 完成所有操作后才能unmap旧页面
  if(mappages(pagetable, va, PGSIZE, (uint64)mem, flags) != 0){
    printf("mappage fail\n");
    kfree((void*)mem);
    return -1;
  }
  return 0;
}
