## Understanding xv6 inode locking [Fahad Al Hasan] ##
Before implementing the reader-writer lock, we need to understand how the original inode lock works. Let's start with one file.  
`file.h`  
xv6 → kernel → file.h  
Find the following structure:  
```powershell
struct inode {
  uint dev;
  uint inum;
  int ref;
  struct sleeplock lock;
  int valid;

  short type;
  short major;
  short minor;
  short nlink;
  uint size;
  uint addrs[NDIRECT+1];
};
```
This is the in-memory representation of an xv6 inode.  

The most important line for our project is:
```powershell
struct sleeplock lock;
```
Currently, xv6 uses an exclusive sleeping lock to protect inode operations.  
Your project's goal is to investigate how this locking mechanism can be modified to support concurrent readers while preventing writer starvation.  
We will need to distinguish operations that genuinely read inode data from operations that modify it.
