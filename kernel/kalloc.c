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

//用于访问物理页引用计数数组
#define PA2PGREF_ID(p) (((p) - KERNBASE) / PGSIZE)
#define PGREF_MAX_ENTRIES PA2PGREF_ID(PHYSTOP)

struct spinlock pgreflock;           // 用于 pageref 数组的锁，防止竞态条件引起内存泄漏
int pageref[PGREF_MAX_ENTRIES];      // 从 KERNBASE 开始到 PHYSTOP 之间的每个物理页的引用计数

//通过物理地址获取引用计数
#define PA2PGREF(p) pageref[PA2PGREF_ID((uint64)(p))]

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  initlock(&pgreflock, "pgref");
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
kfree(void *pa)        //释放物理页的一个引用，引用计数减 1；如果计数变为 0，则释放回收物理页
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  acquire(&pgreflock);
  if(--PA2PGREF(pa) <= 0){         // 当页面的引用计数小于等于 0 的时候，释放页面
    // Fill with junk to catch dangling refs.
    memset(pa, 1, PGSIZE);

    r = (struct run*)pa;

    acquire(&kmem.lock);
    r->next = kmem.freelist;
    kmem.freelist = r;
    release(&kmem.lock);
  }
  release(&pgreflock);
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
    // 新分配的物理页的引用计数为 1 ，这里无需加锁
    PA2PGREF(r) = 1;
  }
    
  return (void*)r;
}

//创建物理页的一个新引用，引用计数加 1
void krefpage(void* pa){
  acquire(&pgreflock);
  PA2PGREF(pa)++;
  release(&pgreflock);
}

//将物理页的一个引用实复制到一个新物理页上（引用计数为 1），返回得到的副本页；
//并将本物理页的引用计数减 1
void* kcopy_n_deref(void* pa){
  acquire(&pgreflock);
  //当引用计数已经小于等于1时，不创建和复制到新的物理页，而是直接返回该页本身
  if(PA2PGREF(pa) <= 1){
    release(&pgreflock);
    return pa;
  }

  uint64 mem = (uint64)kalloc();
  if(mem == 0){
    release(&pgreflock);
    return 0;
  }

  memmove((void*)mem, (void*)pa, PGSIZE);
  PA2PGREF(pa)--;         //旧业引用减1
  release(&pgreflock);
  return (void*)mem;
}
