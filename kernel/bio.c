// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.

#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

#define NBUCKETS (13)

typedef int bitmap_32;

int
bitmap_32_lookup(bitmap_32* bitmap, int bit) {
  return (*bitmap) & (1 << bit);
}

void
bitmap_32_set(bitmap_32* bitmap, int bit) {
  *bitmap |= (1 << bit);
}

void
bitmap_32_reset(bitmap_32* bitmap, int bit) {
  *bitmap &= ~(1 << bit);
}

struct {
  struct buf buf[NBUF];
  struct spinlock buf_lock;

  bitmap_32 buckets[NBUCKETS];
  struct spinlock bucket_lock[NBUCKETS];
} bcache;

static int
bucket_num(int num) {
  return (num % NBUCKETS);
}

void
binit(void) {
  // init lockes
  initlock(&bcache.buf_lock, "bcache_lock");
  for (int i = 0; i < NBUCKETS; ++i) {
    initlock(&bcache.bucket_lock[i], "bcache_lock_");
  }

  // init buckets
  for (int i = 0; i < NBUCKETS; ++i) {
    bcache.buckets[i] = 0;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno) {
  struct buf* b;

  int bucket = bucket_num(blockno);

  acquire(&bcache.bucket_lock[bucket]);

  // Is the block already cached?
  for (int i = 0; i < NBUF; ++i) {
    if (bitmap_32_lookup(&bcache.buckets[bucket], i)) {
      b = &bcache.buf[i];
      if (b->dev == dev && b->blockno == blockno) {
        b->refcnt++;
        release(&bcache.bucket_lock[bucket]);
        acquiresleep(&b->lock);
        return b;
      }
    }
  }

  // Not cached.
  acquire(&bcache.buf_lock);
  for (int i = 0; i < NBUF; ++i) {
    b = &bcache.buf[i];
    if (b->refcnt == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      bitmap_32_set(&bcache.buckets[bucket], i);
      release(&bcache.buf_lock);
      release(&bcache.bucket_lock[bucket]);
      acquiresleep(&b->lock);
      return b;
    }
  }
  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno) {
  struct buf* b;

  b = bget(dev, blockno);
  if (!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf* b) {
  if (!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
void
brelse(struct buf* b) {
  if (!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  int bucket = bucket_num(b->blockno);
  acquire(&bcache.bucket_lock[bucket]);
  b->refcnt--;
  if (b->refcnt == 0) {
    int i = (b - bcache.buf);
    bitmap_32_reset(&bcache.buckets[bucket], i);
  }

  release(&bcache.bucket_lock[bucket]);
}

void
bpin(struct buf* b) {
  int bucket = bucket_num(b->blockno);
  acquire(&bcache.bucket_lock[bucket]);
  b->refcnt++;
  release(&bcache.bucket_lock[bucket]);
}

void
bunpin(struct buf* b) {
  int bucket = bucket_num(b->blockno);
  acquire(&bcache.bucket_lock[bucket]);
  b->refcnt--;
  release(&bcache.bucket_lock[bucket]);
}
