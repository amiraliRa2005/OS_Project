#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

extern char end[]; // first address after kernel.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

struct {
  struct spinlock lock;
  int count[PHYSTOP / PGSIZE];
} ref;

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE) {
    ref.count[(uint64)p / PGSIZE] = 1;
    kfree(p);
  }
}

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  initlock(&ref.lock, "ref");
  acquire(&ref.lock);
  for(int i = 0; i < PHYSTOP/PGSIZE; i++)
    ref.count[i] = 0;
  release(&ref.lock);
  
  freerange(end, (void*)PHYSTOP);
}

void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  acquire(&ref.lock);
  if(ref.count[(uint64)pa / PGSIZE] < 1)
    panic("kfree: ref count");
  
  ref.count[(uint64)pa / PGSIZE] -= 1;
  int tmp_c = ref.count[(uint64)pa / PGSIZE];
  release(&ref.lock);

  if(tmp_c > 0)
    return;

  memset(pa, 1, PGSIZE);
  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

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
    memset((char*)r, 5, PGSIZE);
    acquire(&ref.lock);
    ref.count[(uint64)r / PGSIZE] = 1;
    release(&ref.lock);
  }
  return (void*)r;
}

void
incref(uint64 pa) {
  if(pa >= PHYSTOP) panic("incref");
  acquire(&ref.lock);
  ref.count[pa / PGSIZE]++;
  release(&ref.lock);
}

int get_ref(uint64 pa) {
  if(pa >= PHYSTOP) panic("get_ref");
  int c;
  acquire(&ref.lock);
  c = ref.count[pa / PGSIZE];
  release(&ref.lock);
  return c;
}