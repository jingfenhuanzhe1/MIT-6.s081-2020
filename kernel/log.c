#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"

// Simple logging that allows concurrent FS system calls.
//
// A log transaction contains the updates of multiple FS system
// calls. The logging system only commits when there are
// no FS system calls active. Thus there is never
// any reasoning required about whether a commit might
// write an uncommitted system call's updates to disk.
//
// A system call should call begin_op()/end_op() to mark
// its start and end. Usually begin_op() just increments
// the count of in-progress FS system calls and returns.
// But if it thinks the log is close to running out, it
// sleeps until the last outstanding end_op() commits.
//
// The log is a physical re-do log containing disk blocks.
// The on-disk log format:
//   header block, containing block #s for block A, B, C, ...
//   block A
//   block B
//   block C
//   ...
// Log appends are synchronous.

// Contents of the header block, used for both the on-disk header block
// and to keep track in memory of logged block# before commit.
struct logheader {
  int n;               //日志块的计数
  int block[LOGSIZE];  //扇区号数组         LOGSIZE = 30
};

struct log {          //只存在内存， 记录当前的日志信息，因为属于公共资源，所以有一把锁要避免竞争
  struct spinlock lock;
  int start;                //日志区第一块块号
  int size;                 //日志区大小
  int outstanding; // how many FS sys calls are executing.   有多少系统调用正在执行
  int committing;  // in commit(), please wait.        //正在提交
  int dev;                                             //设备
  struct logheader lh;                                 //日志头
};
struct log log;

static void recover_from_log(void);
static void commit();

void
initlog(int dev, struct superblock *sb)
{
  if (sizeof(struct logheader) >= BSIZE)
    panic("initlog: too big logheader");

  initlock(&log.lock, "log");
  log.start = sb->logstart;    //第一个日志块块号
  log.size = sb->nlog;         //日志块块数
  log.dev = dev;               //日志所在设备
  recover_from_log();          //从日志中恢复
}

// Copy committed blocks from log to their home location
static void
install_trans(int recovering)
{
  int tail;

  for (tail = 0; tail < log.lh.n; tail++) {
    struct buf *lbuf = bread(log.dev, log.start+tail+1); // read log block   读取日志块
    struct buf *dbuf = bread(log.dev, log.lh.block[tail]); // read dst   读取日志块中数据本身应该在的磁盘块
    memmove(dbuf->data, lbuf->data, BSIZE);  // copy block to dst       将数据复制到目的地
    bwrite(dbuf);  // write dst to disk                         //同步缓存到磁盘
    if(recovering == 0)
      bunpin(dbuf);
    brelse(lbuf);
    brelse(dbuf);
  }
}

// Read the log header from disk into the in-memory log header
static void
read_head(void)            //从磁盘将日志头读出并拷贝到内存中
{
  struct buf *buf = bread(log.dev, log.start);
  struct logheader *lh = (struct logheader *) (buf->data);
  int i;
  log.lh.n = lh->n;
  for (i = 0; i < log.lh.n; i++) {
    log.lh.block[i] = lh->block[i];
  }
  brelse(buf);
}

// Write in-memory log header to disk.
// This is the true point at which the
// current transaction commits.
static void
write_head(void)        //从内存向磁盘写入日志头
{
  struct buf *buf = bread(log.dev, log.start);
  struct logheader *hb = (struct logheader *) (buf->data);
  int i;
  hb->n = log.lh.n;
  for (i = 0; i < log.lh.n; i++) {
    hb->block[i] = log.lh.block[i];
  }
  bwrite(buf);    //以此函数为分界线，在这之前，数据还没有落盘，不可以恢复数据。在这函数之后数据已经落盘，可以恢复出数据。（commit point)
  brelse(buf);
}

static void
recover_from_log(void)
{
  read_head();              //读取日志头
  install_trans(1); // if committed, copy from log to disk
  log.lh.n = 0;
  write_head(); // clear the log
}

// called at the start of each FS system call.
void
begin_op(void)
{
  acquire(&log.lock);
  while(1){
    if(log.committing){                  //如果日志正在提交， 休眠
      sleep(&log, &log.lock);
    } else if(log.lh.n + (log.outstanding+1)*MAXOPBLOCKS > LOGSIZE){
      // this op might exhaust log space; wait for commit. 如果此次文件系统调用涉及的块数超过日志块数上限，休眠
      sleep(&log, &log.lock);
    } else {
      log.outstanding += 1;           //文件系统调用加1
      release(&log.lock);
      break;
    }
  }
}

// called at the end of each FS system call.
// commits if this was the last outstanding operation.
void
end_op(void)
{
  int do_commit = 0;

  acquire(&log.lock);
  log.outstanding -= 1;
  if(log.committing)
    panic("log.committing");
  if(log.outstanding == 0){        //如果正在执行的文件系统调用为0，则可以提交了
    do_commit = 1;
    log.committing = 1;
  } else {
    // begin_op() may be waiting for log space,       唤醒因日志空间不够而休眠的进程
    // and decrementing log.outstanding has decreased
    // the amount of reserved space.
    wakeup(&log);
  }
  release(&log.lock);

  if(do_commit){
    // call commit w/o holding locks, since not allowed
    // to sleep with locks.
    commit();
    acquire(&log.lock);
    log.committing = 0;             //提交完知后设为没有处于提交状态
    wakeup(&log);                   //日志空间已重置，唤醒因正在提交和空间不够而休眠的进程
    release(&log.lock);
  }
}

// Copy modified blocks from cache to log.
static void
write_log(void)                 // 将内存中的缓存块写到日志区
{ 
  int tail;

  for (tail = 0; tail < log.lh.n; tail++) {            
    struct buf *to = bread(log.dev, log.start+tail+1); // log block
    struct buf *from = bread(log.dev, log.lh.block[tail]); // cache block
    memmove(to->data, from->data, BSIZE);
    bwrite(to);  // write the log
    brelse(from);
    brelse(to);
  }
}

static void
commit()
{
  if (log.lh.n > 0) {
    write_log();     // Write modified blocks from cache to log   将内存中的缓存块写到日志区
    write_head();    // Write header to disk -- the real commit  *** 将日志头写到日志区第一块 完成这一步表示已提交，未完成表示没有提交
    install_trans(0); // Now install writes to home locations
    log.lh.n = 0;
    write_head();    // Erase the transaction from the log
  }
}

// Caller has modified b->data and is done with the buffer.
// Record the block number and pin in the cache by increasing refcnt.
// commit()/write_log() will do the disk write.
//
// log_write() replaces bwrite(); a typical use is:
//   bp = bread(...)
//   modify bp->data[]
//   log_write(bp)
//   brelse(bp)
void
log_write(struct buf *b)
{
  int i;

  if (log.lh.n >= LOGSIZE || log.lh.n >= log.size - 1)
    panic("too big a transaction");
  if (log.outstanding < 1)
    panic("log_write outside of trans");

  acquire(&log.lock);
  for (i = 0; i < log.lh.n; i++) {
    if (log.lh.block[i] == b->blockno)   // log absorbtion
      break;
  }
  log.lh.block[i] = b->blockno;
  if (i == log.lh.n) {  // Add new block to log?
    bpin(b);
    log.lh.n++;
  }
  release(&log.lock);
}

