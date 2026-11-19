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

## Understanding understanding how xv6 acquires and releases a sleeping lock [Fahad Al Hasan] ##
Inside `xv6/kernel/sleeplock.c` there are functions including  

`initsleeplock()`  
`acquiresleep()`  
`releasesleep()`  
`holdingsleep()`  


**1. `acquiresleep()` — Acquiring the lock**  
```powershell
void
acquiresleep(struct sleeplock *lk)
{
  acquire(&lk->lk); // Acquire the internal spinlock
  //acquire(&lk->lk) protects the sleeping lock's internal state from concurrent modifications.
  
  while (lk->locked) { 
    /*Check whether the sleeping lock is occupied. 
    while (lk->locked) checks whether another process already holds it.
    If lk->locked is nonzero, another process holds the sleeping lock. The current process must wait.
    The while loop ensures that after waking up, the process checks the condition again.
    */

    
    sleep_prepare(lk);
    /*
    It prepares the current process to sleep on the channel identified by lk. The purpose is to coordinate sleeping with subsequent wakeups so that a process does not miss a notification that the lock has become available. The exact internal behavior depends on the implementation of sleep_prepare() in xv6 version.
    */

    release(&lk->lk);
    /*
    The process releases the internal spinlock before actually sleeping. Why?
    Because another process needs to acquire this spinlock to release the sleeping lock and wake waiting processes.

    If the waiting process held the internal spinlock while sleeping, it could prevent the current lock holder from making progress.
    */


    sleep();
    /*
    This suspends the current process until it becomes runnable again.

    Unlike the conventional xv6 interface, This version calls sleep() without passing the channel and spinlock directly.

    The preparation was performed separately by sleep_prepare(lk).
    */



    acquire(&lk->lk);
    /*
    After waking, the process reacquires the internal spinlock before checking lk->locked again.

    This ensures that the next condition check is synchronized with other processes.
    */
  }
  lk->locked = 1; //records that the current process now holds the lock.
  lk->pid = myproc()->pid;
  release(&lk->lk); //allows other processes to inspect the sleeping lock's state, while the sleeping lock itself remains held.
}
```
The order matters here.  
1. Prepare to sleep: Register or prepare the waiting state while the internal spinlock is still held.
2. Release the spinlock: Allow other processes to change the lock state and perform wakeups.
3. Sleep: Suspend execution until the scheduler makes the process runnable again.
4. Reacquire the spinlock: Safely inspect the lock state again after waking.

Critical Section:  
`while (lk->locked)` This means the sleeping lock is exclusively held by one process at a time. Our future reader-writer implementation will require different conditions for readers and writers, as well as a policy that prevents writer starvation.  

**2. releasesleep() — Releasing the lock**  
```powershell
void
releasesleep(struct sleeplock *lk) //Releasing the lock
{
  acquire(&lk->lk);
  lk->locked = 0; //makes the sleeping lock available
  lk->pid = 0;
  wakeup(lk); //wakes processes sleeping on that lock's channel.
  /*
  However, waking up does not guarantee that a particular waiting process acquires the lock next. Each awakened process must check the condition again.
  */
  release(&lk->lk);
}
```
Visualizing the existing locking mechanism  
<img width="638" height="571" alt="image" src="https://github.com/user-attachments/assets/ed357f95-ccb6-42b7-9bf9-41d3b3621ef4" />
The current sleeping lock has only two logical states:  
- `locked = 0` — available
- `locked = 1` — occupied  
It cannot distinguish between a reader holding the lock and a writer holding the lock. So our reader-writer lock will eventually need a track additional information such as:

| Variable | Purpose |
|----------|---------|
| `active_readers` | Number of readers currently holding the lock |
| `active_writer` | Whether a writer currently holds the lock |
| `waiting_writers` | Number of writers waiting for exclusive access |  

For example, a writer-preference policy could prevent new readers from acquiring the lock once a writer starts waiting. That can prevent writer starvation, although we must also consider fairness to readers.

## Understanding sleep_prepare() and sleep() functions [Fahad Al Hasan] ##
These two functions work together to make a process wait safely. They are particularly important for Reader-Writer Inode Locks modification, because we'll eventually need to make readers and writers wait without missing wakeup notifications.

Inside `xv6/kernal/proc.c`
```poweshell
void
sleep_prepare(void *chan)
{
  struct proc *p = myproc();

  acquire(&p->lock);

  if (chan == 0)
    panic("sleep_prepare: zero chan");

  p->chan = chan;

  release(&p->lock);
}
```
**Understanding Step by step**  
`void sleep_prepare(void *chan)`  
`voide *chan` means the parameter is a generic pointer.  
A `void *` can hold an address associated with different data types.  
In the acquiresleep() function  
`sleep_prepare(lk)` the pointer `lk` is used as **sleep channel.**  
A sleep channel identifies what a process is waiting for. It is not necessarily a memory location whose contents the process needs to read.  

`struct proc *p = myproc()` — Identify the current process   

 Here:
 - `struct proc` is the structure representing a process.
 - `*p` declares a pointer to that structure.
 - `myproc()` returns the current process.
For example, if Process B is executing `sleep_prepare()`, then `p` points to Process B's process structure.

`acquire(&p->lock);` — Acquire the process lock  
This acquires the spinlock protecting the process's state.  
This is not the same spinlock as `lk->lk` in `acquiresleep()`.  
| Lock | Protects |
|----------|---------|
| `lk->lk` |The sleeping lock's internal state |
| `p->lock` | The process's state, including its sleep channel |  

```powershell
if (chan == 0)  
  panic("sleep_prepare: zero chan");
```
Check for an invalid channel  
The expression `chan == 0` checks whether the channel is a null pointer.  
If it is null, `panic()` reports a fatal kernel error.  

`p->chan = chan;` — Record the sleep channel.  
It records the channel that the current process intends to wait on.  
For example: 
```powershell
Process B:
    p->chan = address of inode sleeping lock
```
This function does not set `p->state = SLEEPING`.  
It only prepares the process for sleeping by recording its channel.  

`release(&p->lock);` — Release the process lock  
This releases the spinlock protecting the process structure.  
At this point, `sleep_prepare()` is finished, and the process can continue executing.  

**Understanding `sleep()`**  
```powershell
void
sleep(void)
{
  struct proc *p = myproc();

  acquire(&p->lock);

  if (p->chan != 0) {
    p->state = SLEEPING;
    sched();
  }

  release(&p->lock);
}
```

`void sleep(void)` — Function declaration  
- The first `void` means the function returns no value.
- The second `void`, inside parentheses, means the function accepts no arguments.

`struct proc *p = myproc();` — Get the current process  
Again, this obtains the process structure of the currently running process.  

`acquire(&p->lock);` — Acquire the process lock  
The kernel acquires the process lock before checking or changing the process's sleep state.  
This coordinates with other kernel operations, including wakeups.  

`if (p->chan != 0) {` — Check whether a sleep channel is registered  
The operator `!=` means not equal to.  
This checks whether the process still has a non-null sleep channel.  
Why is this important?  
Because a wakeup might have happened after `sleep_prepare()` but before `sleep()`.  
If the wakeup clears `p->chan`, then this condition becomes false and the process does not go to sleep.  
That is an important part of the intended lost-wakeup prevention mechanism.  

`p->state = SLEEPING;` — Mark the process as sleeping  
This changes the process's state to `SLEEPING`.  
In xv6, process states include:  
```powershell
UNUSED
USED
SLEEPING
RUNNABLE
RUNNING
ZOMBIE
```
A sleeping process is not eligible for normal CPU scheduling until it is awakened.

`sched();` — Release the process lock  
When the process resumes, or when the `if` condition was false, this statement releases its process lock.  
Then `sleep()` returns to its caller.  

**How do these functions work together?**  
Consider the following code from your `acquiresleep()`:
```powershell
while (lk->locked) {
  sleep_prepare(lk);
  release(&lk->lk);
  sleep();
  acquire(&lk->lk);
}
```
Suppose Process A already holds the sleeping lock, and Process B wants to acquire it.  

**Process B's waiting sequence**  
1. **Finds the lock occupied**
   > `lk->locked == 1`

2. **Registers the waiting channel**
   > `sleep_prepare(lk)` sets `p->chan = lk`

3. **Releases the internal spinlock**
   > `release(&lk->lk)` allows Process A to release the sleeping lock

4. **Attempts to sleep**
   > `sleep()` checks whether the registered channel is still active

5. **Eventually resumes**
   > After being awakened and scheduled, Process B reacquires `lk->lk`

6. **Checks the condition again**
   > If the sleeping lock is still occupied, Process B repeats the loop

**Important Part: Preventing a lost wakeup**  
Considering the scenario, Process B executes
```powershell
sleep_prepare(lk);
release(&lk->lk);
```
But before B executes `sleep()`, Process A releases the sleeping lock and calls `wakeup(lk)`.  
What happens?  
In a correctly coordinated implementation, `wakeup(lk)` recognizes B's registered channel and clears it.  
Then, when B calls:
```powershell
if (p->chan != 0)
```
the condition is false.  
Therefore, B skips sleeping and returns to acquiresleep(), where it rechecks the lock.  
This prevents B from sleeping indefinitely after the lock has already become available.  

There is one important qualification: the exact guarantee depends on how our version implements `wakeup()`. These two functions show the preparation and sleeping sides, but not the complete wakeup mechanism.  

So far summery  

| Function | Responsibility |
|----------|----------------|
| `sleep_prepare(chan)` | Registers the process's intended sleep channel |
| `sleep()` | Puts the process to sleep if its channel is still registered |
| `sched()` | Switches execution away from the current process |
| `wakeup(chan)` | Notifies waiting processes; exact behavior depends on its implementation |
| `acquiresleep()` | Coordinates these operations to acquire an exclusive sleeping lock |













