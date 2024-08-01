// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void
freerange(void* pa_start, void* pa_end);

extern char end[];  // first address after kernel.
                    // defined by kernel.ld.

struct run {
  struct run* next;
};

struct {
  struct spinlock lock;
  struct run* freelist;
} kmem;

static int page_ref_count[(PHYSTOP - KERNBASE) / PGSIZE];

static void
init_page_ref_count();

void
kinit() {
  initlock(&kmem.lock, "kmem");
  init_page_ref_count();
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void* pa_start, void* pa_end) {
  char* p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for (; p + PGSIZE <= (char*)pa_end; p += PGSIZE) kfree(p);
}

static int
pa2idx(uint64 pa) {
  uint page_index = (pa - KERNBASE) / PGSIZE;
  if (page_index > (sizeof(page_ref_count) / sizeof(int))) {
    panic("unexpected pa2idx use");
  }
  return page_index;
}

static void
init_page_ref_count() {
  for (unsigned long i = 0; i < (sizeof(page_ref_count) / sizeof(int)); ++i) {
    page_ref_count[i] = 1;
  }
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

  int page_index = pa2idx((uint64)pa);
  acquire(&kmem.lock);
  int ref_count = (--(page_ref_count[page_index]));
  if (ref_count == 0) {
    // Fill with junk to catch dangling refs.
    memset(pa, 1, PGSIZE);

    r = (struct run*)pa;

    r->next = kmem.freelist;
    kmem.freelist = r;
  } else if (ref_count < 0) {
    printf("Error: ref_count = %d, pa = %p, page_index = %d\n", ref_count, pa, page_index);
    panic("incorrect use of kfree.\n");
  }
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void*
kalloc(void) {
  struct run* r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if (r) {
    kmem.freelist = r->next;
    int page_index = pa2idx((uint64)r);
    page_ref_count[page_index] = 1;
  }
  release(&kmem.lock);

  if (r) {
    memset((char*)r, 5, PGSIZE);  // fill with junk
  }
  return (void*)r;
}

void
incrpagerefcount(uint64 pa) {
  int pageindex = pa2idx(pa);
  page_ref_count[pageindex] += 1;
}

void
decrpagerefcount(uint64 pa) {
  int pageindex = pa2idx(pa);
  page_ref_count[pageindex] -= 1;
  if (page_ref_count[pageindex] < 0) {
    panic("incorrect use of decrpagerefcount\n");
  }
}

int
getpagerefcount(uint64 pa) {
  return page_ref_count[pa2idx(pa)];
}