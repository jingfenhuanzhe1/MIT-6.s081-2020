struct file {
  enum { FD_NONE, FD_PIPE, FD_INODE, FD_DEVICE } type;
  int ref; // reference count
  char readable;
  char writable;
  struct pipe *pipe; // FD_PIPE
  struct inode *ip;  // FD_INODE and FD_DEVICE
  uint off;          // FD_INODE
  short major;       // FD_DEVICE
};

#define major(dev)  ((dev) >> 16 & 0xFFFF)
#define minor(dev)  ((dev) & 0xFFFF)
#define	mkdev(m,n)  ((uint)((m)<<16| (n)))

// in-memory copy of an inode        只有C指针引用某个inode时，内核才会在内存中存储该inode   
struct inode {                  // 大小为 64 字节
  uint dev;           // Device number
  uint inum;          // Inode number
  int ref;            // Reference count 统计引用内存中inode的C指针的数量，引用计数降至0，内核将该inode从内存写回磁盘
  struct sleeplock lock; // protects everything below here
  int valid;          // inode has been read from disk?

  short type;         // copy of disk inode
  short major;
  short minor;
  short nlink;           //统计引用文件的目录项的数量,
  uint size;              //文件中内容的字节数
  uint addrs[NDIRECT+1 + 1];
};

// map major device number to device functions.
struct devsw {
  int (*read)(int, uint64, int);
  int (*write)(int, uint64, int);
};

extern struct devsw devsw[];

#define CONSOLE 1
