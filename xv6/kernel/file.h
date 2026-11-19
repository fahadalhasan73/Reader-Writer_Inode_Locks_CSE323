#include "rwlock.h"
#include "sleeplock.h"
struct file {
  enum { FD_NONE, FD_PIPE, FD_INODE, FD_DEVICE } type;
  int ref; // reference count
  char readable;
  char writable;
  struct pipe *pipe; // FD_PIPE
  struct inode *ip;  // FD_INODE and FD_DEVICE
  uint off;          // FD_INODE
  struct sleeplock offlock;  // Protects the shared file offset
  short major;       // FD_DEVICE
};

#define major(dev)  ((dev) >> 16 & 0xFFFF)
#define minor(dev)  ((dev) & 0xFFFF)
#define mkdev(m, n) ((uint)((m) << 16 | (n)))

// in-memory copy of an inode or in memory representation of an xv6 inode.
struct inode {
  uint dev;              // Device number. Identifies the device containing the inode
  uint inum;             // Identifies the Inode number
  int ref;               // Reference count. 	Number of in-memory references to the inode
  //struct sleeplock lock; // protects everything below here. Protects inode contents during operations
  struct rwlock lock; // Protects inode contents using reader-writer locking
  int valid;             // inode has been read from disk? Indicates whether inode metadata has been loaded from disk

  short type; // copy of disk inode. Identifies the file type
  short major; // Device identifiers for device files
  short minor; // Device identifiers for device files
  short nlink; // Number of directory links to the inode
  uint size; // File size in bytes
  uint addrs[NDIRECT + 1]; // Addresses of the file's data blocks
};

// map major device number to device functions.
struct devsw {
  int (*read)(int, uint64, int);
  int (*write)(int, uint64, int);
};

extern struct devsw devsw[];

#define CONSOLE 1
