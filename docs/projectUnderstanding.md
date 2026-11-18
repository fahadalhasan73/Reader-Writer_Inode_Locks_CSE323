**Project - AI-driven operating systems experiment**  
Where we will develop locking policies, measures their performance, improves them, and explains the results.

## Tentative Project Roadmap ##
**Phase 1 — Environment setup**  
Install WSL2, Ubuntu, QEMU, RISC-V tools, Git, and xv6. Verify that xv6 boots successfully.  
Start here  
**Phase 2 — Understand xv6**  
Learn about inodes, file-system operations, sleeplocks, spinlocks, and synchronization.  
**Phase 3 — Build the baseline**  
Examine the original inode lock, establish a working baseline, and create performance measurements.  
**Phase 4 — Implement reader-writer locking**  
Introduce shared read access and exclusive write access while preserving file-system correctness.  
**Phase 5 — Develop starvation-free policies**  
Experiment with writer-preference, fair queuing, and potentially adaptive locking policies.  
**Phase 6 — Benchmark and optimize**  
Compare reader throughput, writer waiting time, fairness, and correctness under different workloads.  
**Phase 7 — Final evaluation and documentation**  
Test unseen workloads, perform ablation experiments, maintain AI interaction logs, and prepare the final report.  

## Some prerequisite Concepts ##
**What is an inode?**  
An inode is a data structure that stores information about a file, including its size, type, and where its data blocks are located.  
In xv6, multiple processes may try to access the same inode concurrently.  

**What happens with the original locking mechanism?**  
The original xv6 inode lock is an exclusive sleeplock. Only one process can hold that inode lock at a time.  
For example, suppose three processes want to read the same file.
<img width="738" height="324" alt="image" src="https://github.com/user-attachments/assets/833247f7-1692-4d73-b0c9-0398c6eb748f" />  
<img width="740" height="387" alt="image" src="https://github.com/user-attachments/assets/40ef2c8b-ba16-44eb-97c4-bd215788f534" />  

However, the challenge is not just enabling concurrent readers.  
Suppose readers keep arriving while a writer is waiting. If the policy continuously allows new readers to enter, the writer might wait indefinitely.  
This is called **writer starvation**.  

Our project must solve both problems:  
- Allow safe concurrent reading.  
- Ensure waiting writers eventually acquire exclusive access.

Our goal: 
A good starting policy is writer preference: once a writer is waiting, prevent new readers from entering until the writer gets an opportunity to run.  
We will eventually test whether that policy provides the best balance between performance and fairness.

## Understanding xv6 inode locking [Fahad Al Hasan] ##
Before implementing the reader-writer lock, we need to understand how the original inode lock works. Let's start with one file.  
`file.h`  
xv6 → kernel → file.h  
Find the following structure:  
```powershell
// in-memory copy of an inode or in memory representation of an xv6 inode.
struct inode {
  uint dev;              // Device number. Identifies the device containing the inode
  uint inum;             // Identifies the Inode number
  int ref;               // Reference count. 	Number of in-memory references to the inode
  struct sleeplock lock; // protects everything below here. Protects inode contents during operations
  int valid;             // inode has been read from disk? Indicates whether inode metadata has been loaded from disk

  short type; // copy of disk inode. Identifies the file type
  short major; // Device identifiers for device files
  short minor; // Device identifiers for device files
  short nlink; // Number of directory links to the inode
  uint size; // File size in bytes
  uint addrs[NDIRECT + 1]; // Addresses of the file's data blocks
};
```
This is the in-memory representation of an xv6 inode.  

The most important line for our project is:
```powershell
struct sleeplock lock;
```
Currently, xv6 uses an exclusive sleeping lock to protect inode operations.  
Our project's goal is to investigate how this locking mechanism can be modified to support concurrent readers while preventing writer starvation.  
We will need to distinguish operations that genuinely read inode data from operations that modify it.  

**Important Fields for our project**

1. `ref` — Reference count
```powershell
int ref;
```
This tracks how many references the kernel holds to this in-memory inode.  
For example, two processes may hold references to the same inode.  
Important: `ref` is not the number of readers currently holding the inode lock.  
Our future reader-writer lock will need its own mechanism to track active readers.  

2. `lock` — The inode lock
```powershell
struct sleeplock lock;
```
This is the most important field for our project. Currently, xv6 uses a sleeping lock that allows only one holder at a time.
<img width="748" height="329" alt="image" src="https://github.com/user-attachments/assets/7834d9ae-7fc1-4274-8dd6-67341d834142" />
Our project aims to allow multiple safe readers to hold a shared lock while ensuring writers eventually get exclusive access.

3. `valid` — Metadata initialization
```powershell
int valid;
```
This field indicates whether the inode's metadata has been loaded into its in-memory structure.

When an inode is accessed for the first time, xv6 may need to read its metadata from disk.

That initialization modifies the in-memory inode, which is why not every operation that appears to be a read is automatically safe under a shared lock.

This will matter when we design the new locking mechanism.  

## Understanding sleeping lock implementation [Fahad Al Hasan] ##
`xv6/kernel/sleeplock.h` Inside this file 
```powershell
// Long-term locks for processes
struct sleeplock {
  uint locked;        // Is the lock held? Indicates whether the sleeping lock is held.
  struct spinlock lk; // spinlock protecting this sleep lock. A spinlock used internally to protect the sleeping lock's state.

  // For debugging:
  char *name; // Name of lock.
  int pid;    // Process holding lock
};
```
- `locked` — Indicates whether the sleeping lock is held.
- `lk` — A spinlock used internally to protect the sleeping lock's state.
This structure does not maintain a reader count or a waiting-writer count.  
That is one reason the existing sleeping lock does not directly provide the reader-writer behavior that our project requires.


