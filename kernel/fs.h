// On-disk file system format.
// Both the kernel and user programs use this header file.


#define ROOTINO  1   // root i-number
#define BSIZE 1024  // block size

// Disk layout:
// [ boot block | super block | log | inode blocks |
//                                          free bit map | data blocks]
//
// mkfs computes the super block and builds an initial file system. The
// super block describes the disk layout:
struct superblock {
  uint magic;        // Must be FSMAGIC            #define FSMAGIC 0x10203040  
  uint size;         // Size of file system image (blocks)
  uint nblocks;      // Number of data blocks                数据块的个数
  uint ninodes;      // Number of inodes.                    inode块的个数
  uint nlog;         // Number of log blocks                 日志块的个数
  uint logstart;     // Block number of first log block
  uint inodestart;   // Block number of first inode block
  uint bmapstart;    // Block number of first free map block
};

#define FSMAGIC 0x10203040

#define NDIRECT 11
#define NINDIRECT (BSIZE / sizeof(uint))
#define NNINDIRECT (BSIZE * BSIZE / (sizeof(uint) * sizeof(uint)))
#define MAXFILE (NDIRECT + NINDIRECT + NNINDIRECT)

// On-disk inode structure        //磁盘上的节点信息
struct dinode {                                                   // 64 字节
  short type;           // File type   //区分文件、目录和特殊文件（设备）。0表示磁盘inode是空闲的
  short major;          // Major device number (T_DEVICE only)
  short minor;          // Minor device number (T_DEVICE only)
  short nlink;          // Number of links to inode in file system //代表在磁盘中，有多少个目录去引用了这个inode, 为0时，内核会从磁盘上销毁这个inode
  uint size;            // Size of file (bytes)     记录文件中内容的字节数
  uint addrs[NDIRECT+1+1];   // Data block addresses  记录保存文件内容的磁盘块的块号
};

// Inodes per block.
#define IPB           (BSIZE / sizeof(struct dinode))        // IPB = 16, 意味着每个block大小为64字节

// Block containing inode i
#define IBLOCK(i, sb)     ((i) / IPB + sb.inodestart)

// Bitmap bits per block                                每个块能有多少 bit
#define BPB           (BSIZE*8)

// Block of free map containing bit for block b          块 b 在哪个位图块上
#define BBLOCK(b, sb) ((b)/BPB + sb.bmapstart)

// Directory is a file containing a sequence of dirent structures.
#define DIRSIZ 14

struct dirent {
  ushort inum;          // 2字节 目录中文件或者子目录的inode编号
  char name[DIRSIZ];    // 包含了文件或者子目录名
};

