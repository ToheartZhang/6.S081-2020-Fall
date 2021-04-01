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

#define NBUCKET 13

struct {
  struct spinlock locks[NBUCKET];
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  struct buf head[NBUCKET];
  struct spinlock steal_lock;
} bcache;

char buf[NBUCKET][20];

uint
ihash(uint blockno) {
  return blockno % NBUCKET;
}

void
binit(void)
{
  struct buf *b;
  for (int i = 0; i < NBUCKET; i++) {
    snprintf(buf[i], 20, "bcache.bucket%d", i);
    initlock(&bcache.locks[i], (char*)buf[i]);
  }
  initlock(&bcache.steal_lock, "bcache");

  for (int i = 0; i < NBUCKET; i++) {
    struct buf *head = &bcache.head[i];
    // Create linked list of buffers
    head->prev = head;
    head->next = head;
  }

  int i;
  for(b = bcache.buf, i = 0; b < bcache.buf+NBUF; b++, i = ihash(i + 1)){
    b->next = bcache.head[i].next;
    b->prev = &bcache.head[i];
    bcache.head[i].next->prev = b;
    bcache.head[i].next = b;
    initsleeplock(&b->lock, "buffer");
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

  uint id = ihash(blockno);

  acquire(&bcache.locks[id]);

  // Is the block already cached?
  for(b = bcache.head[id].next; b != &bcache.head[id]; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.locks[id]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  for(b = bcache.head[id].prev; b != &bcache.head[id]; b = b->prev){
    if(b->refcnt == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      release(&bcache.locks[id]);
      acquiresleep(&b->lock);
      return b;
    }
  }
  release(&bcache.locks[id]);
  acquire(&bcache.steal_lock);
  acquire(&bcache.locks[id]);

  for (b = bcache.head[id].next; b != &bcache.head[id]; b = b->next) {
    if(b->dev == dev && b->blockno == blockno) {
      b->refcnt++;
      release(&bcache.locks[id]);
      release(&bcache.steal_lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  for (b = bcache.head[id].prev; b != &bcache.head[id]; b = b->prev) {
    if (b->refcnt == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      release(&bcache.locks[id]);
      release(&bcache.steal_lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  for (int i = ihash(id + 1); i != id; i = ihash(i + 1)) {
    acquire(&bcache.locks[i]);
    for (b = bcache.head[i].prev; b != &bcache.head[i]; b = b->prev) {
      if (b->refcnt == 0) {
        b->dev = dev;
        b->blockno = blockno;
        b->valid = 0;
        b->refcnt = 1;
        // delete from other
        b->prev->next = b->next;
        b->next->prev = b->prev;
        release(&bcache.locks[i]);
        // insert here
        b->next = bcache.head[id].next;
        b->prev = &bcache.head[id];
        b->next->prev = b;
        b->prev->next = b;
        release(&bcache.locks[id]);
        release(&bcache.steal_lock);
        acquiresleep(&b->lock);
        return b;
      }
    }
    release(&bcache.locks[i]);
  }
  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  uint id = ihash(b->blockno);
  acquire(&bcache.locks[id]);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache.head[id].next;
    b->prev = &bcache.head[id];
    bcache.head[id].next->prev = b;
    bcache.head[id].next = b;
  }
  
  release(&bcache.locks[id]);
}

void
bpin(struct buf *b) {
  uint id = ihash(b->blockno);
  acquire(&bcache.locks[id]);
  b->refcnt++;
  release(&bcache.locks[id]);
}

void
bunpin(struct buf *b) {
  uint id = ihash(b->blockno);
  acquire(&bcache.locks[id]);
  b->refcnt--;
  release(&bcache.locks[id]);
}


