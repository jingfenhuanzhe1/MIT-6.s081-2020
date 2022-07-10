struct buf {
  int valid;   // has data been read from disk?         缓冲区是否包含块的副本
  int disk;    // does disk "own" buf?               缓冲区的内容是否已交给磁盘，这可能会更改缓冲区
  uint dev;
  uint blockno;
  struct sleeplock lock;
  uint refcnt;
  struct buf *prev; // LRU cache list
  struct buf *next;
  uchar data[BSIZE];
};

