// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"
//#include "param.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem[NCPU];

char *kmem_lock_names[] = {
  "kmem_lock_0",
  "kmem_lock_1",
  "kmem_lock_2",
  "kmem_lock_3",
  "kmem_lock_4",
  "kmem_lock_5",
  "kmem_lock_6",
  "kmem_lock_7",
};

void
kinit()
{
  for(int i = 0; i < NCPU; i++){
    initlock(&kmem[i].lock, kmem_lock_names[i]);
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

// Free the page of physical memory pointed at by v,
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

  int cpu = cpuid();

  acquire(&kmem[cpu].lock);
  r->next = kmem[cpu].freelist;
  kmem[cpu].freelist = r;
  release(&kmem[cpu].lock);

  pop_off();
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void*
kalloc(void){
  struct run* r;

  push_off();             //cpuid()返回CPU的编号，只有当中断关闭时调用cpuid()并使用CPU编号才是安全的

  int cpu = cpuid();

  acquire(&kmem[cpu].lock);
  //如果本cpu上的物理页没了，需要进行偷页，如果偷的话，偷64页
  if(!kmem[cpu].freelist){
    int steal_page = 64;
    for(int i = 0; i < NCPU; i++){
      if(i == cpu) continue;
      acquire(&kmem[i].lock);                         //将被偷的cpu上的锁进行锁定
      struct run* rr = kmem[i].freelist;
      while(rr && steal_page){
        kmem[i].freelist = rr->next;
        rr->next = kmem[cpu].freelist;
        kmem[cpu].freelist = rr;
        rr = kmem[i].freelist;
        steal_page--;
      }
      release(&kmem[i].lock);
      if(steal_page == 0) break;
    }
  }

  r = kmem[cpu].freelist; 
  if(r){
    kmem[cpu].freelist = r->next;
  }
  release(&kmem[cpu].lock);

  pop_off();

  if(r){
    memset((char*)r, 5, PGSIZE);         //fill with junk
  }
  return (void*)r;
}
