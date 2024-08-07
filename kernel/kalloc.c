// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "proc.h"
#include "defs.h"

struct run {
  struct run* next;
};

void
freerange(void* pa_start, void* pa_end);

void
kfree_local(struct run* r, int hartid);

void*
kalloc_local(int hartid);

extern char end[];  // first address after kernel.
                    // defined by kernel.ld.

struct {
  struct spinlock global_lock;
  struct spinlock lock[NPROC];
  struct run* freelist[NPROC];
} kmem;

void
kinit() {
  initlock(&kmem.global_lock, "kmem");
  for (int i = 0; i < NPROC; ++i) {
    initlock(&kmem.lock[i], "kmem");
  }

  void* pa_start = end;
  void* pa_end = (void*)PHYSTOP;
  char* p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for (int i = 0; p + PGSIZE <= (char*)pa_end; p += PGSIZE, i = (i + 1) % NPROC) {
    struct run* r = (struct run*)p;
    kfree_local(r, i);
  }
}

void
freerange(void* pa_start, void* pa_end) {
  char* p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for (; p + PGSIZE <= (char*)pa_end; p += PGSIZE) kfree(p);
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void* pa) {
  struct run* r;

  if (((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;
  push_off();
  int hartid = cpuid();
  kfree_local(r, hartid);
  pop_off();
}

// Append the node to the end of the local free list.
void
kfree_local(struct run* r, int hartid) {
  acquire(&kmem.lock[hartid]);
  r->next = kmem.freelist[hartid];
  kmem.freelist[hartid] = r;
  release(&kmem.lock[hartid]);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void*
kalloc(void) {
  struct run* r;

  push_off();
  int hartid = cpuid();
  r = kalloc_local(hartid);
  pop_off();

  if (r)
    memset((char*)r, 5, PGSIZE);  // fill with junk
  return (void*)r;
}

void*
kalloc_local(int hartid) {
  struct run* r;

  acquire(&kmem.lock[hartid]);
  r = kmem.freelist[hartid];
  if (r) {
    kmem.freelist[hartid] = r->next;
  } else {
    acquire(&kmem.global_lock);
    for (int i = 0; i < NPROC; ++i) {
      if (i == hartid) {
        continue;
      }

      acquire(&kmem.lock[i]);
      struct run* stolen = kmem.freelist[i];
      if (stolen) {
        kmem.freelist[i] = stolen->next;
        r = stolen;
      }
      release(&kmem.lock[i]);

      if (r) {
        break;
      }
    }
    release(&kmem.global_lock);
  }
  release(&kmem.lock[hartid]);

  return r;
}