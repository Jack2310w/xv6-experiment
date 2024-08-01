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

/*
struct {
  struct spinlock lock;
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  struct buf head;
} bcache;

void
binit(void)
{
  struct buf *b;

  initlock(&bcache.lock, "bcache");

  // Create linked list of buffers
  bcache.head.prev = &bcache.head;
  bcache.head.next = &bcache.head;
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    initsleeplock(&b->lock, "buffer");
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

  acquire(&bcache.lock);

  // Is the block already cached?
  for(b = bcache.head.next; b != &bcache.head; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      printf("%d\n", b->refcnt);
      b->refcnt++;
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  for(b = bcache.head.prev; b != &bcache.head; b = b->prev){
    if(b->refcnt == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
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

  acquire(&bcache.lock);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }
  
  release(&bcache.lock);
}

void
bpin(struct buf *b) {
  acquire(&bcache.lock);
  b->refcnt++;
  release(&bcache.lock);
}

void
bunpin(struct buf *b) {
  acquire(&bcache.lock);
  b->refcnt--;
  release(&bcache.lock);
}

*/


#define BUFHASHNUM  97
// #define EMPTYBUCKET -1

struct {
  struct spinlock lock;
  uint bitmap; // 记录bucket中存放的buf
}bbuckets[BUFHASHNUM];

// struct {
  // struct spinlock lock;
  // int head;
// }emptybucket;

struct spinlock findlock;
struct spinlock buflock[NBUF];

struct buf bufs[NBUF];

struct spinlock glock;

void
binit(void)
{
  char lockname[20];
  for(int i = 0; i < BUFHASHNUM; i++){
    snprintf(lockname, 20, "bcache-hash%d", i);
    initlock(&bbuckets[i].lock, lockname);
  }
  for(int i = 0; i < NBUF; i++){
    bufs[i].curbucket = i; // 初始化buf在前NBUF个bucket中
    initsleeplock(&bufs[i].lock, "buffer");
    initlock(&buflock[i], "bcache-buffer");
  }

  initlock(&findlock, "bcache-find");
  initlock(&glock, "bcache-test");
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  // acquire(&glock);
  int targetbucket = blockno % BUFHASHNUM;
  // Is the block already cached?
  acquire(&bbuckets[targetbucket].lock);
  
  for(int i = 0; i < NBUF; i++){
    if(bufs[i].curbucket == targetbucket &&
    bufs[i].dev == dev && bufs[i].blockno == blockno){
      // 找到了cached buf
      bufs[i].refcnt++;
      release(&bbuckets[targetbucket].lock);
      // release(&glock);
      acquiresleep(&bufs[i].lock);
      return &bufs[i];
    }
  }
  
  // Not cached. 找到一个refcnt为0的buf
  acquire(&findlock);
  for(int i = 0; i < NBUF; i++){
    if(bufs[i].refcnt == 0){ // 找到了一个可以回收的buf
      // 检查目标buf是否已经在对应bucket中，如果不在则需要申请原来bucket的锁
      
      int oldbucket = bufs[i].curbucket;
      if(oldbucket != targetbucket){
        acquire(&bbuckets[oldbucket].lock);
        if(bufs[i].refcnt > 0){ // 判断等于0的时候没有获取lock，此处确认仍然为0
          release(&bbuckets[oldbucket].lock);
          continue;
        }
        bufs[i].curbucket = targetbucket;
        release(&bbuckets[oldbucket].lock);
      }
  
      // 初始化buf并返回
      bufs[i].valid = 0;
      bufs[i].refcnt = 1;
      bufs[i].dev = dev;
      bufs[i].blockno = blockno;
      
      release(&findlock);
      release(&bbuckets[targetbucket].lock);
      // release(&glock);
      acquiresleep(&bufs[i].lock);
      return &bufs[i];
    }
  }
  release(&findlock);
  release(&bbuckets[targetbucket].lock);
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
  // acquire(&glock);
  
  int targetbucket = b->curbucket;
  if(targetbucket < 0 || targetbucket >= BUFHASHNUM){
    panic("brelse");
  }
  acquire(&bbuckets[targetbucket].lock);
  b->refcnt--;
  release(&bbuckets[targetbucket].lock);
  // release(&glock);
}

void
bpin(struct buf *b) {
  // acquire(&glock);
  int targetbucket = b->curbucket;
  if(targetbucket < 0 || targetbucket >= BUFHASHNUM){
    panic("bpin");
  }
  acquire(&bbuckets[targetbucket].lock);
  b->refcnt++;
  release(&bbuckets[targetbucket].lock);
  // release(&glock);
}

void
bunpin(struct buf *b) {
  // acquire(&glock);
  int targetbucket = b->curbucket;
  if(targetbucket < 0 || targetbucket >= BUFHASHNUM){
    panic("bunpin");
  }
  acquire(&bbuckets[targetbucket].lock);
  b->refcnt--;
  release(&bbuckets[targetbucket].lock);
  // release(&glock);
}
