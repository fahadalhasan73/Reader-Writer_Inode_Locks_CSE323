# Reader–Writer Inode Locks in xv6-riscv
## Demo 1 — Complete Implementation, Design Reasoning, Code Walkthrough, Testing and Evaluation

**Course:** CSE323 — Operating Systems  
**Project:** Reader–Writer Inode Locks  
**Checkpoint:** Demo 1  
**Repository:** [Reader-Writer_Inode_Locks_CSE323](https://github.com/fahadalhasan73/Reader-Writer_Inode_Locks_CSE323)  
**Implementation branch:** [`feature/rw-inode-lock`](https://github.com/fahadalhasan73/Reader-Writer_Inode_Locks_CSE323/tree/feature/rw-inode-lock)  
**Documentation basis:** Source code inspected on the feature branch, plus the compilation and xv6 execution logs from our development sessions.  
**Status:** Core implementation and custom demonstration tests complete; comprehensive regression, formal starvation verification, and final cleanup remain future work.

> **Reading guide.** This report documents the *actual Demo 1 checkpoint*, not a hypothetical redesign. File locations refer to paths relative to `xv6/`. Line numbers refer to the inspected branch snapshot and may move after edits. “Original” code examples describe the relevant pre-change behavior; where the exact pre-change Git revision was not independently retrieved, examples are marked *conceptual reconstruction* rather than represented as a verbatim diff. To obtain the authoritative exact before/after diff, use `git diff <baseline-commit> <demo1-commit> -- <path>`.

---

## Contents

1. Problem statement and goals
2. Essential xv6 filesystem and locking background
3. Original design and why it was insufficient
4. New architecture, invariants, and fairness policy
5. Full file-by-file change map
6. `kernel/rwlock.h`: every field and declaration
7. `kernel/rwlock.c`: line-by-line algorithm walkthrough
8. `kernel/file.h`: inode locks versus file-offset locks
9. `kernel/fs.c`: inode integration and block-mapping changes
10. `kernel/file.c`: read and write paths and offset protection
11. Headers, declarations, build system, and supporting changes
12. End-to-end execution traces and scheduling examples
13. Design decisions, rejected shortcuts, and debugging history
14. `user/rwtest.c`: complete explanatory walkthrough
15. Compilation, boot, execution, and observed results
16. Evaluation: supported claims versus remaining uncertainty
17. Known limitations, risks, and future validation
18. First-demo presentation plan and FAQ
19. Final implementation and evaluation summary
20. Source links and Git commands

---

## 1. Problem statement and goals

The assignment asks us to **replace xv6 inode sleeplocks with reader–writer locks**, permit multiple readers to share the lock, and select a policy that does not starve writers.

An inode represents a filesystem object and stores metadata such as its type, size, link count, and disk-block addresses. Multiple processes may access the same inode. Without coordination, simultaneous operations could observe partially updated metadata, allocate conflicting blocks, or corrupt filesystem state.

A **mutual-exclusion sleeplock** permits only one holder at a time, even when both operations merely read immutable data. A **reader–writer lock** distinguishes two classes of access:

| Access | Required property | Examples |
|---|---|---|
| Read | Several readers may hold the lock simultaneously | Reading inode data when metadata is already valid |
| Write | Exactly one writer; no concurrent readers | Updating size, allocating blocks, truncating or changing inode metadata |
| Writer waiting | Newly arriving readers should not indefinitely postpone the writer | A writer arriving during sustained reads |

### 1.1 Demo 1 goals

- Add a custom lock type and implementation.
- Replace the inode's sleeplock with the new lock.
- Preserve the existing `ilock()`/`iunlock()` interface for operations requiring exclusive access.
- Introduce a shared-lock interface for file reads.
- Avoid hidden filesystem modifications in the read path.
- Protect shared open-file offsets separately from inode metadata.
- Favor waiting writers and order writers with FIFO tickets.
- Compile, boot, and demonstrate reader, writer, and mixed workloads.

### 1.2 What this project is **not**

It does **not** replace every sleeplock in xv6. Buffer-cache sleeplocks and the new file-offset sleeplocks remain appropriate. It does not add a new syscall or change the user-visible `read()`/`write()` API. It also does not claim a measured throughput improvement yet.

---

## 2. Essential xv6 background

### 2.1 Inode versus file descriptor versus open-file object

A pathname identifies a file. xv6 resolves it to an **inode** (`struct inode`), whose fields include `dev`, `inum`, `ref`, `valid`, `type`, `size`, and `addrs[]`. A user-level file descriptor points to an **open-file object** (`struct file`), which holds the inode pointer `ip` and current offset `off`.

Two independently opened descriptors can refer to the **same inode** but different `struct file` objects and offsets. Conversely, descriptors shared by `fork()` or `dup()` can refer to the **same `struct file`**, including its shared offset. These are different synchronization problems.

### 2.2 Three locking layers

1. **`itable.lock`**, a spinlock, protects inode-table bookkeeping such as reference counts and table entries.
2. **`ip->lock`**, now our reader–writer lock, protects inode contents and metadata during filesystem operations.
3. **`f->offlock`**, a sleeplock added in this project, protects an open-file object's offset across a read or write operation.

The internal **`rwlock.lk` spinlock** is a fourth, very short-lived mechanism: it protects the *reader–writer lock's counters and flags*, not the file's data itself.

### 2.3 Why a spinlock is not held across disk I/O

Disk reads, writes, filesystem logging, and copying data may take time or block. Holding a spinlock throughout would waste CPU and interfere with sleeping. The new lock uses a spinlock only while inspecting or updating lock state, then releases it while the caller performs actual inode work.

### 2.4 Why sleeping and waking matter

A process that cannot acquire the lock must wait without busy-spinning. This xv6 fork exposes `sleep_prepare(chan)`, `sleep()`, and `wakeup(chan)` (see `kernel/defs.h` and `kernel/proc.c`). The lock uses the address of the lock object as its wait channel, so readers and writers of one inode do not confuse their wakeups with another inode.

---

## 3. Original design and its limitation

### 3.1 Original inode representation

Before the change, the inode held a sleeplock. The relevant old design was equivalent to:

```c
struct inode {
  // device, inode number, reference count, etc.
  struct sleeplock lock;
  // valid, type, size, addresses, etc.
};
```

In the original exclusive-lock pattern, `ilock(ip)` called `acquiresleep(&ip->lock)` and `iunlock(ip)` called `releasesleep(&ip->lock)`.

**Result:** reader A and reader B could not hold the inode lock together, even though neither intended to change inode contents.

### 3.2 Original file-read pattern

The essential old behavior was:

```c
ilock(f->ip);
r = readi(f->ip, 1, addr, f->off, n);
if (r > 0)
  f->off += r;
iunlock(f->ip);
```

This snippet is a **conceptual reconstruction of the pre-change pattern**, not a byte-for-byte historical diff. The inode's exclusive lock implicitly serialized the `f->off` update along with inode access.

### 3.3 The central problem with changing only the lock call

If we merely replace `ilock()` with `ilockread()` in `fileread()`, two processes sharing **one open-file object** can both observe the same `f->off` and update it incorrectly. The inode lock no longer excludes the second reader. Therefore, **a separate file-offset lock is necessary**.

### 3.4 A second hidden problem: reads could allocate blocks

The pre-change `readi()` used the general `bmap()` helper. `bmap()` can allocate missing direct or indirect blocks and update block-address metadata. Such mutation is unsafe while multiple readers hold a shared inode lock. The project introduces **`bmapread()`**, which only looks up existing blocks and never allocates them.

### 3.5 Starvation problem with unrestricted readers

If a reader may enter whenever no writer *currently holds* the lock, new readers could keep arriving while a writer waits. The active reader count might never reach zero. To address this, our reader admission condition checks both `writer` and `waiting_writers`.

---

## 4. New architecture and correctness model

### 4.1 Lock state

Each inode's `struct rwlock` tracks:

- `readers`: active reader holders.
- `writer`: whether a writer currently holds exclusive ownership.
- `writer_pid`: owning writer's process ID.
- `waiting_writers`: number of writers queued or awaiting admission.
- `next_ticket`: next ticket issued to a writer.
- `serving_ticket`: ticket currently eligible to proceed.
- `lk`: spinlock guarding all these shared fields.

### 4.2 Intended invariants

1. `readers >= 0`.
2. `writer` is either zero or one.
3. **If `writer == 1`, then `readers == 0`.**
4. If `readers > 0`, no writer is active.
5. A new reader does not enter while `waiting_writers > 0`.
6. A writer can enter only if its ticket equals `serving_ticket` and no holder remains.
7. The current writer alone may call `releasewrite()` successfully.

The internal spinlock makes admission decisions and counter changes atomic relative to other contenders.

### 4.3 Writer preference and FIFO are different guarantees

**Writer preference** blocks new readers when any writer is waiting. **FIFO tickets** order writers relative to each other. Both are implemented; neither should be confused with strict FIFO ordering of *all* readers and writers. Once a writer is waiting, incoming readers can themselves be delayed by a sustained sequence of writers. This is an intentional policy tradeoff.

### 4.4 Admission decision table

| Current state | New reader | New writer |
|---|---|---|
| No readers, no writer, no waiting writer | Enter | Enter if its ticket is current |
| Readers active, no waiting writer | Join readers | Wait for readers to drain |
| Readers active, writer waiting | Wait | Wait for readers and its turn |
| Writer active | Wait | Wait |
| No holder, several writers waiting | Wait | Oldest eligible ticket proceeds |

### 4.5 Illustrative timeline

```text
Time     Event                         readers  writer  waiting_writers
T0       Reader A enters                 1        0         0
T1       Reader B enters                 2        0         0
T2       Writer W takes ticket           2        0         1
T3       Reader C arrives, must wait     2        0         1
T4       Reader A exits                  1        0         1
T5       Reader B exits, wakes waiters   0        0         1
T6       Writer W enters                 0        1         0
T7       Writer W exits and wakes        0        0         0
T8       Reader C may enter              1        0         0
```

This is an **algorithmic illustration**, not a measured trace from the demo.

---

## 5. Complete change map

The Demo 1 staging output showed **9 staged files**, with **410 insertions and 22 deletions**. `kernel/rwlock.h` was already tracked before that particular commit, so it does not appear in that nine-file staging summary, but it is central to the full implementation.

| File | Important location | Change | Why |
|---|---|---|---|
| `kernel/rwlock.h` | Entire header, lines ~1–21 | Custom `struct rwlock`, six public function declarations, header guard | Define state and interface |
| `kernel/rwlock.c` | Entire file, lines ~1–124 | Lock initialization, reader/writer acquire/release, writer ownership check | Implement concurrency and fairness policy |
| `kernel/file.h` | `struct file`, ~2–12; `struct inode`, ~17–30 | Add `offlock`; replace inode sleeplock with `rwlock` | Separate offset protection from inode protection |
| `kernel/fs.c` | `iinit`, ~175; `ilock`, ~280; `ilockread`, ~305; `iunlock`, ~326; `iunlockread`, ~336; `iput`, ~362; `bmapread`, ~432; `readi`, ~546 | Integrate lock, handle metadata initialization, prevent allocation on reads | Safe shared inode reading |
| `kernel/file.c` | `fileinit`, ~20; `fileread`, ~107; `filewrite`, ~138 | Initialize offset locks; use shared inode read lock; guard file offsets | Preserve open-file offset correctness |
| `kernel/defs.h` | `fs.c` declarations, ~35–55 | Declare `ilockread` and `iunlockread` | Cross-file compilation |
| `kernel/spinlock.h` | Top/bottom | Header guard | Avoid duplicate type declarations |
| `kernel/sleeplock.h` | Top/bottom | Header guard | Avoid duplicate type declarations |
| `Makefile` | `OBJS`, ~3–31; `UPROGS`, ~124–145 | Link `rwlock.o`, include `_rwtest` in filesystem | Build and run new code |
| `user/rwtest.c` | Entire file, ~1–268 | Three demonstration workloads | Exercise basic behavior |

**Important scope note:** Existing `kernel/proc.c` sleep/wakeup primitives are *used* by the new code but were not among the nine files changed in the final staged Demo 1 set.

---

## 6. `kernel/rwlock.h` — structure explained field by field

**Location:** `kernel/rwlock.h`, lines ~1–21.

```c
#ifndef RWLOCK_H
#define RWLOCK_H
#include "spinlock.h"

struct rwlock {
  struct spinlock lk;
  int readers;
  int writer;
  int writer_pid;
  int waiting_writers;
  uint next_ticket;
  uint serving_ticket;
  char *name;
};

void initrwlock(struct rwlock *lock, char *name);
void acquireread(struct rwlock *lock);
void releaseread(struct rwlock *lock);
void acquirewrite(struct rwlock *lock);
void releasewrite(struct rwlock *lock);
int holdingwrite(struct rwlock *lock);
#endif
```

### 6.1 `#ifndef` / `#define` / `#endif`

The header guard ensures the type is defined only once per compilation unit. This matters because several filesystem headers include each other, and `rwlock.h` includes `spinlock.h`.

### 6.2 `struct spinlock lk`

This **does not** mean readers hold a spinlock for their whole file read. It protects the internal counters while a process decides whether it can enter, updates its state, or prepares to sleep. The spinlock is released before the actual file operation.

### 6.3 `int readers`

Counts the number of processes that have successfully acquired the shared lock and have not yet released it. A value of two or more demonstrates simultaneous **lock ownership**, although actual CPU execution can still be interleaved.

### 6.4 `int writer` and `int writer_pid`

`writer` marks exclusive ownership. `writer_pid` records who owns it. This enables `holdingwrite()` and helps detect invalid `releasewrite()` calls. Reader ownership is not individually tracked in this implementation.

### 6.5 `int waiting_writers`

The reader admission gate. As soon as a writer registers itself, newly arriving readers must wait. When the writer enters, the counter decreases; if another writer remains queued, readers continue to wait.

### 6.6 Ticket counters

`next_ticket++` allocates unique, increasing ticket values to arriving writers. `serving_ticket++` advances after each completed writer. For example, writers with tickets 8, 9, and 10 should enter in that order. This is a FIFO ordering **among writers**, not a priority score or CPU scheduling ticket.

### 6.7 `name`

A descriptive lock name stored for identification. The current implementation initializes the internal spinlock with the literal name `"rwlock"` and stores the caller-provided inode lock name separately.

### 6.8 Public API

- `initrwlock`: initialize lock state.
- `acquireread`: acquire shared access.
- `releaseread`: release shared access.
- `acquirewrite`: acquire exclusive access.
- `releasewrite`: release exclusive access and advance ticket order.
- `holdingwrite`: check current-process exclusive ownership.

---

## 7. `kernel/rwlock.c` — function-by-function, statement-by-statement

**Location:** `kernel/rwlock.c`, lines ~1–124. The actual file includes `types.h`, `riscv.h`, `rwlock.h`, `defs.h`, `param.h`, and `proc.h` to make types, locking/sleeping functions, and `myproc()` available.

### 7.1 `initrwlock` — lines ~8–20

```c
initlock(&lock->lk, "rwlock");
lock->readers = 0;
lock->writer = 0;
lock->writer_pid = 0;
lock->waiting_writers = 0;
lock->next_ticket = 0;
lock->serving_ticket = 0;
lock->name = name;
```

**Line-by-line:** initialize the short-duration internal spinlock; start with no active readers; no active writer; no owner PID; no waiting writers; ticket allocator at zero; current service ticket at zero; save a readable name. A newly initialized lock is unlocked and ready for its first reader or writer.

### 7.2 `acquireread` — lines ~22–43

```c
acquire(&lock->lk);
while (lock->writer || lock->waiting_writers > 0) {
  sleep_prepare(lock);
  release(&lock->lk);
  sleep();
  acquire(&lock->lk);
}
lock->readers++;
int active_readers = lock->readers;
release(&lock->lk);
if (active_readers > 1) {
  printk("[RWLOCK] Concurrent readers: %d\n", active_readers);
}
```

1. **Acquire `lk`:** protect the admission check against concurrent state changes.
2. **`while (...)`:** wait if a writer is holding the inode **or** a writer is waiting. Checking `waiting_writers` is the crucial writer-preference difference from a reader-biased lock.
3. **`sleep_prepare(lock)`:** register the current process to sleep on this lock's channel according to this fork's sleep API.
4. **`release(&lock->lk)`:** do not sleep while holding the internal spinlock; otherwise other processes could not release their locks and wake us.
5. **`sleep()`:** yield execution until awakened.
6. **`acquire(&lock->lk)`:** reenter the protected region after waking.
7. **Why `while`, not `if`?** A wakeup is not a reservation. By the time a process runs again, another writer might have entered or queued; the condition must be rechecked.
8. **`readers++`:** atomically record one additional active reader. Multiple readers can increment this counter, one at a time under `lk`, then all hold the *logical* reader lock concurrently.
9. **`active_readers`:** capture a diagnostic snapshot before releasing the spinlock.
10. **Release `lk`:** let other lock operations proceed while the caller reads data.
11. **Diagnostic `printk`:** if the snapshot exceeds one, print a concurrent-reader observation. This is **Demo 1 instrumentation** and can be removed later.

**Important nuance:** the diagnostic is printed after releasing `lk`, so it reports a previously observed counter value; it does not guarantee that the same number of readers still hold the lock when the message reaches the console.

### 7.3 `releaseread` — lines ~45–58

```c
acquire(&lock->lk);
if (lock->readers < 1)
  panic("releaseread");
lock->readers--;
if (lock->readers == 0)
  wakeup(lock);
release(&lock->lk);
```

1. Protect state under `lk`.
2. Detect underflow: a release without an active reader is invalid.
3. Decrement the number of active readers.
4. Wake contenders **only when the final reader exits**, because a writer cannot enter while any reader remains.
5. Release the internal spinlock.

This implementation checks that *some* reader exists but does not track which individual process owns each read lock. Misuse by one reader could therefore remain undetected unless it drives the count below zero.

### 7.4 `acquirewrite` — lines ~61–85

```c
acquire(&lock->lk);
uint my_ticket = lock->next_ticket++;
lock->waiting_writers++;
while (lock->writer ||
       lock->readers > 0 ||
       my_ticket != lock->serving_ticket) {
  sleep_prepare(lock);
  release(&lock->lk);
  sleep();
  acquire(&lock->lk);
}
if (lock->writer || lock->readers != 0)
  panic("rwlock: writer overlap");
lock->waiting_writers--;
lock->writer = 1;
lock->writer_pid = myproc()->pid;
release(&lock->lk);
```

1. **Acquire `lk`:** serialize arrival and ticket assignment.
2. **Allocate ticket:** `my_ticket` remembers this writer's position in the FIFO writer queue.
3. **Increment `waiting_writers`:** immediately prevent newly arriving readers from bypassing the queued writer.
4. **Check `lock->writer`:** never overlap another writer.
5. **Check `lock->readers > 0`:** never overlap readers.
6. **Check ticket:** even if the lock is empty, a later writer must not overtake an earlier ticket.
7. **Sleep protocol:** prepare, release internal spinlock, sleep, and reacquire. Repeat until **all three** conditions permit entry.
8. **Overlap assertion:** defensive check that a writer is not about to enter with another holder present. It is useful but **not an independent proof** of correctness, since it checks the same state variables used by admission logic.
9. **Decrement waiting count:** this writer is no longer waiting; it is becoming the holder.
10. **Set `writer = 1`:** reserve exclusive ownership before releasing the spinlock.
11. **Record PID:** enable owner checks during unlock.
12. **Release `lk`:** caller can perform filesystem writes without holding the internal spinlock.

**Why increment `waiting_writers` before sleeping?** Because the admission rule for new readers must become effective immediately after a writer arrives, not only after the writer is awakened.

### 7.5 `releasewrite` — lines ~88–108

```c
acquire(&lock->lk);
if (lock->writer == 0 ||
    lock->writer_pid != myproc()->pid)
  panic("releasewrite");
lock->writer = 0;
lock->writer_pid = 0;
lock->serving_ticket++;
wakeup(lock);
release(&lock->lk);
```

1. Lock the state.
2. Verify that an active writer exists **and** that the current process is its owner.
3. Clear exclusive ownership.
4. Clear the owner PID.
5. Advance `serving_ticket`, allowing the next writer ticket to become eligible.
6. Wake sleepers on the lock's channel. Readers and writers may wake; their own `while` loops decide who can proceed.
7. Release the state spinlock.

**Why advance the ticket on release rather than acquisition?** The currently served writer must finish before the next writer becomes eligible. Advancing too early would undermine exclusive ordering.

### 7.6 `holdingwrite` — lines ~110–124

```c
int r;
acquire(&lock->lk);
r = lock->writer &&
    (lock->writer_pid == myproc()->pid);
release(&lock->lk);
return r;
```

Read the writer state atomically, check whether the current process is the owner, release the state lock, and return the boolean result. `iunlock()` uses this to reject an attempt to unlock an inode without exclusive ownership.

### 7.7 Sleep/wakeup subtleties

`wakeup(lock)` uses the lock's address as the channel. Because waiters use `while`, waking more processes than can proceed is logically acceptable: unsuccessful contenders recheck and sleep again. However, correct *lost-wakeup prevention* depends on the exact implementation and locking contract of this fork's `sleep_prepare`, `sleep`, and `wakeup`. The Demo 1 tests did not establish a formal proof of that protocol; it should be reviewed under stress before final submission.

### 7.8 Ticket overflow and abnormal exits

Ticket counters are unsigned and will eventually wrap after a very large number of acquisitions; the current code has no explicit wraparound analysis. More immediately, there is no mechanism to skip a ticket if a process were to disappear while queued. Normal xv6 processes generally do not asynchronously abandon a kernel lock-acquisition call, but this assumption deserves documentation and review.

---

## 8. `kernel/file.h` — two different kinds of protection

**Locations:** `struct file` lines ~2–12 and `struct inode` lines ~17–30.

### 8.1 Original inode field versus new field

**Before:**

```c
struct sleeplock lock;
```

**After:**

```c
struct rwlock lock;
```

This is the structural replacement requested by the assignment. All code that operates on `ip->lock` must now use the custom reader–writer API, not `acquiresleep()`/`releasesleep()`.

### 8.2 Why `struct file` gained `offlock`

**New field:**

```c
uint off;
struct sleeplock offlock; // Protects the shared file offset
```

The inode lock protects *inode data and metadata*; the file-offset lock protects the cursor associated with one open-file object. These are not interchangeable.

**Example:** Process P opens file X, then forks. Parent and child inherit references to the same `struct file`. If both read concurrently, they must not both read using the same old offset and then both advance it as if alone. `offlock` serializes the `readi(..., f->off, ...)` plus `f->off += r` sequence.

**Another example:** Two processes independently open file X. They have distinct `struct file` objects and distinct `offlock`s, but their `ip` points to the same inode. Both may read that inode simultaneously under shared locks.

### 8.3 Why keep a sleeplock for `offlock`?

An offset is modified around operations that may sleep. A sleeplock is appropriate for holding across these operations. The assignment only requires changing **inode** locking; keeping a sleeplock for the *open-file offset* is consistent with the objective.

---

## 9. `kernel/fs.c` — every important integration change

### 9.1 `iinit()` — lines ~175–185

**Original behavior:** initialize each inode's sleeplock.

```c
// Original call (still visible as a comment in the snapshot):
// initsleeplock(&itable.inode[i].lock, "inode");
```

**New call:**

```c
initrwlock(&itable.inode[i].lock, "inode");
```

The loop visits all `NINODE` entries. Initialization must happen before any inode is used, so all lock counters and tickets begin in a known state.

### 9.2 `ilock(ip)` — lines ~278–304

**Original locking action:** `acquiresleep(&ip->lock)`.

**New locking action:**

```c
acquirewrite(&ip->lock);
```

**Why exclusive?** `ilock()` is a general-purpose API used by many paths that may modify metadata. It also performs lazy initialization when `ip->valid == 0`: it reads the on-disk inode into the in-memory fields, updates `type`, `major`, `minor`, `nlink`, `size`, `addrs`, and sets `valid = 1`. That is a state-changing operation and must not run under a shared read lock.

**Important distinction:** Even a caller that only reads metadata via `ilock()` still receives an exclusive lock. Demo 1 introduces shared locking specifically where it is known to be safe, rather than blindly changing all callers.

### 9.3 New `ilockread(ip)` — lines ~305–325

```c
if (ip == 0 || ip->ref < 1)
  panic("ilockread");
acquireread(&ip->lock);
if (ip->valid == 0) {
  releaseread(&ip->lock);
  ilock(ip);
  iunlock(ip);
  acquireread(&ip->lock);
}
```

**Line-by-line:**

1. Reject a null or unreferenced inode.
2. Acquire shared access.
3. Inspect `valid` while protected by the read lock.
4. If the metadata has not been loaded, release the read lock. **Never try to upgrade directly while still counted as a reader**: the writer would wait for all readers, including itself, causing deadlock.
5. Call `ilock()` to initialize the inode under exclusive ownership.
6. Call `iunlock()` to release exclusive ownership.
7. Reacquire shared access before returning to the caller.

**Why not just initialize under the reader lock?** Multiple readers might concurrently overwrite shared in-memory metadata or observe partially initialized fields. Exclusive initialization avoids this.

**Race consideration:** The inode can change between releasing the read lock and reacquiring it. This is not a lock upgrade that preserves a continuous snapshot; the caller must rely on the filesystem's reference and metadata-validity rules. A final audit should consider whether an additional validity recheck is necessary in unusual concurrent lifecycle scenarios.

### 9.4 `iunlock(ip)` — lines ~326–334

**Old pattern:** verify ownership via the sleeplock API and release the sleeplock.

**New pattern:**

```c
if (ip == 0 || ip->ref < 1 || !holdingwrite(&ip->lock))
  panic("iunlock");
releasewrite(&ip->lock);
```

The function remains the matching unlock for exclusive `ilock()`. It must not be used to release `ilockread()`; that has a separate counterpart.

### 9.5 New `iunlockread(ip)` — lines ~336–344

```c
if (ip == 0 || ip->ref < 1)
  panic("iunlockread");
releaseread(&ip->lock);
```

This is the symmetric release function for `ilockread()`. Pairing the correct acquire and release matters because read ownership and write ownership update different state.

### 9.6 `iput(ip)` — lines ~355–391

`iput()` drops an inode reference. When the last reference is to an unlinked inode, it may free the inode's data blocks and clear its validity. That is destructive metadata work.

**Original relevant operation:** acquire/release the inode sleeplock around final cleanup.

**New relevant operations:**

```c
acquirewrite(&ip->lock);
// ... itrunc(ip), ip->valid = 0 ...
releasewrite(&ip->lock);
```

**Why write mode?** Truncation changes disk block allocation and inode size, so no readers may overlap it.

**Important review point:** This fork's `iput()` acquires `itable.lock` and, in its last-reference path, then calls `acquirewrite()`. Lock ordering and sleeping while other locks are held are delicate kernel concerns. The change follows the existing cleanup structure, but this interaction merits careful regression/deadlock review before a final correctness claim.

### 9.7 Original `bmap()` — lines ~462–499

`bmap(ip, bn)` maps a file-relative block number to a disk block address. When the block is absent, it may call `balloc()` to allocate one and update direct/indirect address metadata; it may also call `log_write()` for metadata changes.

**Why that is unsafe for shared readers:** A function called during a read must not silently allocate blocks or mutate `ip->addrs[]`, especially while other readers share the inode lock.

### 9.8 New `bmapread()` — lines ~430–460

```c
static uint
bmapread(struct inode *ip, uint bn)
{
  uint addr;
  uint *a;
  struct buf *bp;

  if (bn < NDIRECT)
    return ip->addrs[bn];

  bn -= NDIRECT;
  if (bn < NINDIRECT) {
    if ((addr = ip->addrs[NDIRECT]) == 0)
      return 0;
    bp = bread(ip->dev, addr);
    a = (uint *)bp->data;
    addr = a[bn];
    brelse(bp);
    return addr;
  }
  panic("bmapread: out of range");
}
```

**Line-by-line:**

1. Declare variables for the disk block address, indirect block address array, and buffer.
2. If the logical block is in the direct-address range, return the existing address. **Do not allocate** if it is zero.
3. Otherwise subtract `NDIRECT` to convert to an index in the indirect range.
4. If within `NINDIRECT`, first check whether the indirect block itself exists. If not, return zero.
5. Read the existing indirect block with `bread()`.
6. Treat its bytes as an array of block addresses.
7. Look up the requested address.
8. Release the buffer with `brelse()`.
9. Return the address, which may be zero if the data block does not exist.
10. Panic for an out-of-range logical block number.

**Key property:** `bmapread()` never calls `balloc()`, never modifies the inode address array, and never logs a metadata write.

### 9.9 `readi()` — lines ~542–570

**Before (essential operation):**

```c
uint addr = bmap(ip, off / BSIZE);
```

**After:**

```c
uint addr = bmapread(ip, off / BSIZE);
if (addr == 0)
  break;
```

`readi()` checks offsets against `ip->size`, bounds the requested length, loops over the required disk blocks, reads each block with `bread()`, copies data to the destination using `either_copyout()`, releases the buffer, and returns the number of bytes read.

**Why this matters:** Shared readers should not allocate disk blocks. Returning or stopping at an absent block avoids writes in the read path. Note that this code **breaks** on a missing block rather than synthesizing zero bytes; filesystem invariants and any sparse-file expectations should be assessed separately in future tests.

### 9.10 `writei()` remains a writer-only operation

`writei()` uses `bmap()` because writes legitimately need to allocate blocks, update `ip->size`, and persist inode metadata via `iupdate()`. Its caller must hold an exclusive inode lock. This deliberate read/write separation is a core design choice.

### 9.11 Why not convert all directory and metadata operations to read locks?

Many filesystem routines reuse `ilock()` and may mutate directory entries or metadata, or call helpers that assume exclusive ownership. Converting every such call without auditing it could introduce corruption. Demo 1 leaves the general `ilock()` exclusive and uses `ilockread()` for the audited regular file-read path.

---

## 10. `kernel/file.c` — end-to-end read and write integration

### 10.1 `fileinit()` — lines ~20–30

```c
initlock(&ftable.lock, "ftable");
for (f = ftable.file; f < ftable.file + NFILE; f++) {
  initsleeplock(&f->offlock, "file_offset");
}
```

`ftable.lock` protects allocation and reference-count bookkeeping for open-file objects. The loop initializes the newly added sleeplock in **every** slot of the file table before any slot can be allocated. This prevents an uninitialized offset lock from being used.

### 10.2 `fileread()` — lines ~107–134

The relevant new inode-file branch is:

```c
acquiresleep(&f->offlock);
ilockread(f->ip);
if ((r = readi(f->ip, 1, addr, f->off, n)) > 0)
  f->off += r;
iunlockread(f->ip);
releasesleep(&f->offlock);
```

**Statement-by-statement:**

1. Lock the open-file offset so two users of the *same* `struct file` cannot race on `f->off`.
2. Acquire shared inode access; unrelated open-file objects pointing to the same inode can do this concurrently.
3. Read from the inode at the current file offset.
4. Advance the offset only by the positive number of bytes actually read.
5. Release the inode read lock.
6. Release the file-offset sleeplock.

**Why lock order matters:** The code acquires `offlock` before the inode lock and releases the inode lock before `offlock`. Consistent lock ordering reduces deadlock risks.

**What stays unchanged:** Pipe reads use `piperead()`, device reads use the device switch, and the function still checks `f->readable` and negative lengths. The new inode lock does not replace these separate mechanisms.

### 10.3 `filewrite()` — lines ~138–184

The relevant new structure is:

```c
acquiresleep(&f->offlock);
// split a large write into bounded log transactions
while (i < n) {
  // calculate chunk length n1
  begin_op();
  ilock(f->ip);
  if ((r = writei(f->ip, 1, addr + i, f->off, n1)) > 0)
    f->off += r;
  iunlock(f->ip);
  end_op();
  // handle partial/error and advance i
}
releasesleep(&f->offlock);
```

**Why use `ilock()` rather than `ilockread()`?** `writei()` may allocate blocks, update inode size, and write disk metadata. It must hold exclusive inode ownership.

**Why keep `begin_op()` / `end_op()`?** xv6 uses a filesystem log for crash recovery and bounded transactions. Reader–writer locking does not replace logging. Large writes are divided into chunks to respect transaction size limits.

**Why hold `offlock` across the whole chunk loop?** A single logical write may be split into multiple transactions. The offset must not be interleaved with another operation on the same open-file object between chunks.

**Nuance:** The inode writer lock is reacquired for each chunk, so a different open-file object may interleave work on the inode between chunks. This preserves the original transaction-chunk structure rather than claiming entire multi-chunk writes are atomic with respect to other writers.

### 10.4 Lock hierarchy summary

```text
User read(fd, ...)
    -> fileread(f)
       -> acquiresleep(f->offlock)       [shared open-file cursor]
       -> ilockread(f->ip)
          -> acquireread(ip->lock)       [shared inode ownership]
       -> readi(...)
          -> bmapread(...)              [no allocation]
       -> iunlockread(f->ip)
       -> releasesleep(f->offlock)

User write(fd, ...)
    -> filewrite(f)
       -> acquiresleep(f->offlock)       [shared open-file cursor]
       -> begin_op()                    [filesystem transaction]
       -> ilock(f->ip)
          -> acquirewrite(ip->lock)     [exclusive inode ownership]
       -> writei(...)
          -> bmap(...)                  [may allocate]
          -> iupdate(...)               [persist metadata]
       -> iunlock(f->ip)
       -> end_op()
       -> releasesleep(f->offlock)
```

---

## 11. Other files and build integration

### 11.1 `kernel/defs.h`

**Location:** near the `fs.c` declarations, lines ~35–55. Added declarations:

```c
void ilockread(struct inode*);
void iunlockread(struct inode*);
```

These tell other kernel source files the function signatures. Without them, calls from `file.c` may fail to compile under the project's warning-as-error settings.

### 11.2 `kernel/spinlock.h` and `kernel/sleeplock.h`

Both gained conventional include guards:

```c
#ifndef SPINLOCK_H
#define SPINLOCK_H
// existing struct definition
#endif
```

and similarly for `SLEEPLOCK_H`. This avoids duplicate `struct` definitions when multiple headers include the same type. **These changes are build hygiene**, not a change in the underlying spinlock or sleeplock algorithms.

### 11.3 `Makefile` — kernel object

**Location:** `OBJS` near line ~24.

```make
$K/rwlock.o \
```

This tells the kernel linker to include the compiled reader–writer lock implementation. Merely writing `rwlock.c` would not make the functions available at runtime unless its object file is linked.

### 11.4 `Makefile` — user test program

**Location:** `UPROGS` near line ~141.

```make
$U/_rwtest\
```

This causes the user test executable to be built and included in `fs.img`, allowing the xv6 shell to run `rwtest`.

### 11.5 Why compilation order matters

- Header declarations describe the API.
- `rwlock.c` implements the API.
- `Makefile` links `rwlock.o` into the kernel.
- `file.c` and `fs.c` call the API.
- `rwtest.c` is separately compiled into a user executable and placed in the filesystem image.

---

## 12. End-to-end execution examples

### 12.1 Two independent readers

```text
Reader A: open("data", O_RDONLY) -> file object FA -> inode I
Reader B: open("data", O_RDONLY) -> file object FB -> inode I

A locks FA.offlock (not shared with B)
A acquires read lock on I: readers=1
B locks FB.offlock
B acquires read lock on I: readers=2
A and B can both remain read-lock holders
A releases: readers=1
B releases: readers=0 -> wakeup(I.lock)
```

**Critical distinction:** `readers=2` means shared ownership is permitted; it does not mean both CPU cores executed the same instruction at the same instant.

### 12.2 Two readers sharing one open-file object

```text
Parent opens file -> file object F
Parent forks -> child inherits same F
Parent acquires F.offlock
Child waits for F.offlock
Parent reads at offset 0 and advances offset
Parent releases F.offlock
Child acquires F.offlock and sees updated offset
```

The inode reader lock alone would not protect this shared cursor. The new `offlock` solves the separate race.

### 12.3 A writer behind active readers

```text
R1 acquires shared inode lock
R2 acquires shared inode lock
W obtains ticket 0; waiting_writers=1; sleeps
R3 arrives; sees waiting_writers>0; sleeps
R1 releases
R2 releases; final reader wakes waiters
W sees no readers and serving_ticket=0; enters exclusively
W releases; serving_ticket becomes 1; wakes waiters
R3 may proceed if no writer remains queued
```

### 12.4 Two writers with tickets

```text
W1: ticket 0
W2: ticket 1
W2 cannot enter even if scheduled before W1
W1 enters, writes, releases, serving_ticket -> 1
W2 becomes eligible and enters
```

Ticket order is assigned when writers acquire the internal state spinlock, not necessarily by user-space `printf()` output order.

### 12.5 Lazy metadata initialization

```text
Reader calls ilockread(ip), valid=0
Reader releases shared lock
Reader calls ilock(ip) -> exclusive writer lock
ilock loads on-disk inode metadata and sets valid=1
Reader releases exclusive lock
Reader reacquires shared lock
Reader performs readi()
```

This avoids trying to modify metadata while holding a shared lock and avoids deadlocking by upgrading without first releasing the read lock.

---

## 13. Implementation decisions and development history

### 13.1 Why use writer preference?

The assignment explicitly asks for a policy that does not starve writers. A reader-biased design that only blocks readers while a writer *holds* the lock could repeatedly admit new readers and indefinitely postpone a writer. The `waiting_writers` check closes that admission loophole.

### 13.2 Why add tickets in addition to writer preference?

Writer preference alone does not prescribe which of several waiting writers gets the lock. Tickets provide an explicit ordering among writers. This is easy to explain and inspect: assign at arrival, wait for `serving_ticket`, increment when the current writer releases.

### 13.3 Why not make reads use `bmap()`?

Because `bmap()` may allocate missing blocks and mutate inode metadata. Shared readers must be read-only with respect to inode state. `bmapread()` separates lookup from allocation.

### 13.4 Why not reuse the inode lock for `f->off`?

Because that would reserialize otherwise independent readers. The offset belongs to a particular open-file object; the inode belongs to the filesystem object. Keeping separate locks preserves the intended concurrency.

### 13.5 Why retain existing exclusive `ilock()` callers?

Changing all metadata and directory operations to shared mode without an audit would be unsafe. Demo 1 changes the well-understood read path and leaves general inode operations exclusive.

### 13.6 Why use `printk()` in kernel code?

This xv6 fork provides kernel `printk()`; user programs use `printf()`. During development, a diagnostic message was added to `acquireread()` when more than one reader held the lock. That message is diagnostic evidence, not part of the locking algorithm.

### 13.7 Temporary delay used during testing

A temporary busy loop was inserted after a successful read-lock acquisition to enlarge the overlap window. During that experiment, the kernel printed:

```text
[RWLOCK] Concurrent readers: 2
```

That observation demonstrates that at least two readers simultaneously held the *same lock*. The artificial delay was subsequently **removed** before the Demo 1 checkpoint. The diagnostic counter/print remained in the inspected checkpoint.

### 13.8 Writer-overlap assertion

A `panic("rwlock: writer overlap")` check was added immediately before setting writer ownership. This is a defensive invariant check. It did not trigger during the demonstrated runs. Because it checks the lock's own counters, it should not be described as independent proof that no overlap can ever occur.

### 13.9 Evolution of `rwtest.c`

The test was built incrementally: first three readers, then three writers, then a separate starvation-test function, a shared file, five reader children, repeated reads, one writer child, and finally parent-side `wait()`/status checks. The final version also writes initial file contents and reopens the file in each read iteration so each read begins at offset zero.

### 13.10 Checkpoint management

The Demo 1 work was staged as nine files and committed on `feature/rw-inode-lock` with the message:

```text
Demo 1: Implement reader-writer inode locks and basic tests
```

The user reported pushing the commit. The precise resulting commit hash was not supplied in the session, so this report does **not** invent one.

---

## 14. `user/rwtest.c` — complete explanatory walkthrough

**Location:** `user/rwtest.c`, approximately 268 lines in the inspected Demo 1 source. This is a **user-space test program**, not a kernel lock implementation. It exercises `open`, `read`, `write`, `fork`, `wait`, `close`, `unlink`, and `exit` to trigger the kernel code.

### 14.1 Headers — lines ~1–4

```c
#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user/user.h"
```

- `types.h`: xv6 integer types.
- `stat.h`: filesystem metadata declarations (included although the current test does not directly call `stat`).
- `fcntl.h`: file-opening flags such as `O_CREATE`, `O_RDWR`, `O_RDONLY`, and `O_WRONLY`.
- `user.h`: user-space system-call wrappers and `printf()`.

### 14.2 Test constants — lines ~4–6

```c
#define NUM_READERS 3
#define NUM_WRITERS 3
#define STARVATION_READERS 5
```

These make the workload sizes explicit. The first test forks three readers; the second forks three writers; the third forks five reader processes and one writer.

### 14.3 `test_writers()` — lines ~8–67

**Purpose:** exercise exclusive-write paths with several processes referring to the same inode.

1. Print the test heading and number of writers.
2. `open("rwtestwriters", O_CREATE | O_RDWR)` creates or opens the shared target file.
3. Check `fd < 0`; abort if creation failed.
4. Close the setup descriptor; each child will open its own descriptor.
5. Loop `NUM_WRITERS` times, calling `fork()`.
6. Check `pid < 0` for fork failure.
7. In each child (`pid == 0`), print its PID.
8. Open `rwtestwriters` with `O_WRONLY`; fail if the open fails.
9. Define `char message[] = "Writer process data\n";`.
10. Call `write(writer_fd, message, sizeof(message) - 1)`. Subtracting one excludes the terminating NUL byte.
11. Compare the returned byte count with the requested count. A short or failed write is treated as an error.
12. Print a completion message, close the descriptor, and `exit(0)`.
13. In the parent, call `wait(0)` three times to collect child exits.
14. Print `Writer processes completed!`.

**What it checks:** all three write calls can complete without a visible panic or write error. **What it does not independently check:** whether writes were serialized internally, whether data was preserved in a particular order, or whether the three child exit statuses were zero (this test uses `wait(0)` without checking statuses). All writers open independently at offset zero and write the same string, so final file content cannot reveal the write ordering.

### 14.4 `test_writer_starvation()` — lines ~69–193

**Purpose:** exercise a writer while repeated reader operations are taking place.

#### A. Create the test file — lines ~73–92

```c
int fd = open("rwstarve", O_CREATE | O_RDWR);
char initial_data[] = "Reader-writer contention test\n";
write(fd, initial_data, sizeof(initial_data) - 1);
close(fd);
```

The actual code checks both the `open` result and the exact byte count returned by `write()`. Creating the file first ensures all children refer to the same pathname/inode. Initial content ensures that reads have bytes available rather than immediately hitting EOF.

**Subtlety:** `O_CREATE` here does not imply truncation of an existing file. Repeated runs may reuse an existing `rwtestwriters` file; `rwstarve` is normally removed at the end of a successful starvation test.

#### B. Spawn five readers — lines ~94–131

```c
for (int i = 0; i < STARVATION_READERS; i++) {
  int pid = fork();
  // check pid < 0
  if (pid == 0) {
    // child repeatedly opens, reads, closes
  }
}
```

Every child has its own process and repeatedly accesses the same inode. The parent continues the loop and eventually starts the writer.

#### C. Reader loop — lines ~106–129

```c
char buffer[32];
for (int j = 0; j < 1000; j++) {
  int reader_fd = open("rwstarve", O_RDONLY);
  // check open
  int n = read(reader_fd, buffer, sizeof(buffer));
  // check n < 0
  close(reader_fd);
}
```

**Why 1,000 iterations?** To create many opportunities for reader/writer overlap. Five readers × 1,000 iterations = **5,000 attempted read operations** if all children complete normally.

**Why reopen on every iteration?** A descriptor's file offset advances after a successful read. If the child kept the same descriptor, subsequent reads would usually return EOF. Reopening resets the offset to zero so the child reads existing data again.

**Why check only `n < 0`?** A negative return means an error. The current test does not assert a particular positive byte count or compare the returned bytes with expected contents; it is a concurrency/progress test, not a data-integrity test.

**Additional observation:** Reopening on every iteration also exercises pathname lookup and file-table operations. It introduces overhead and other lock interactions beyond the inode read lock itself.

#### D. Spawn one writer — lines ~132–163

After the five reader forks, the parent forks one writer. The child:

1. Prints its PID and start message.
2. Opens the same `rwstarve` file with `O_WRONLY`.
3. Prepares `"Writer acquired access\n"`.
4. Calls `write()` and checks that the exact number of bytes was written.
5. Prints `Starvation writer <pid> completed`.
6. Closes the file and exits successfully.

**Why this matters:** The writer must use the same inode as the reader workload, so its exclusive inode lock contends with their shared inode locks *if the operations overlap in time*.

**Scheduling caveat:** Forking readers before the writer does **not guarantee** that the writer actually waits behind active readers. xv6 scheduling may allow the writer to run during a gap, or the readers may be executing other parts of `open()`/`close()` rather than holding the inode read lock.

#### E. Collect all six children — lines ~165–184

```c
int writer_completed = 0;
int failed_children = 0;
for (int i = 0; i < STARVATION_READERS + 1; i++) {
  int status = -1;
  int finished_pid = wait(&status);
  if (finished_pid < 0) {
    // fail
  }
  if (status != 0)
    failed_children++;
  if (finished_pid == writer_pid && status == 0)
    writer_completed = 1;
}
```

- `STARVATION_READERS + 1` equals six children.
- `wait(&status)` returns the PID of whichever child finishes next and writes its exit status to `status`.
- `finished_pid < 0` means the expected wait failed.
- `status != 0` counts a failed child.
- Comparing the finished PID with `writer_pid` proves that **the writer child**, not merely an arbitrary reader, exited successfully.

**Why not call `wait()` five times?** The writer would remain unreaped or might be reaped in place of a reader, leaving the parent unable to account for all six children.

#### F. Report and clean up — lines ~185–193

If the writer was collected successfully and no child reported an error, print:

```text
Starvation test: all processes completed successfully!
```

Otherwise print the failure count. Then `unlink("rwstarve")` removes the temporary file name. The current test does not check `unlink()`'s return value and does not return a failing process status to `main()` on every reported failure; this can be improved later.

### 14.5 `main()` — lines ~195–268

1. Declare file descriptor, child PID, and 64-byte read buffer.
2. Print the overall test heading.
3. Open/create `rwtestdata` with read/write access.
4. Write exactly 27 bytes of `"Concurrent reader test data"`; check the returned count.
5. Close the setup descriptor.
6. Print `Starting 3 reader processes...`.
7. Fork three child readers.
8. Each child opens `rwtestdata` in `O_RDONLY` mode.
9. Each child calls `read(reader_fd, buffer, sizeof(buffer) - 1)`; reserving one byte allows a string terminator.
10. Check negative read results.
11. Set `buffer[n] = 0` to terminate the C string safely.
12. Print its PID and the data it read.
13. Close its descriptor and exit.
14. Parent waits three times with `wait(0)`.
15. Parent unlinks `rwtestdata`.
16. Print reader-test completion.
17. Call `test_writers()`.
18. Call `test_writer_starvation()`.
19. Exit with status zero.

**Important:** `main()` does not inspect the exit statuses of the initial three readers. Its final `exit(0)` is unconditional if execution reaches that point. The displayed success messages are useful, but a more rigorous automated test should propagate all failures.

### 14.6 Test-to-kernel call map

| User-space action | Kernel path of interest | Lock behavior |
|---|---|---|
| `open(..., O_RDONLY)` | pathname lookup and inode metadata operations | Some paths still use exclusive `ilock()` |
| `read(fd, ...)` | `fileread` → `ilockread` → `acquireread` → `readi` → `bmapread` | Shared inode read lock |
| `write(fd, ...)` | `filewrite` → `ilock` → `acquirewrite` → `writei` → `bmap` | Exclusive inode writer lock |
| `close(fd)` | `fileclose` → inode reference management | May invoke inode cleanup logic |
| `fork()` | Duplicate process; inherited file references where applicable | Creates concurrency |
| `wait(&status)` | Wait for child exit | Confirms child completion, not lock ordering |
| `unlink(...)` | Filesystem namespace and inode lifecycle operations | General exclusive metadata locking |

---

## 15. Build, boot, and test evidence

### 15.1 Environment and project directory

The development commands were run from:

```text
~/CSE323/Reader-Writer_Inode_Locks_CSE323/xv6
```

The observed compiler was `riscv64-linux-gnu-gcc`, linker `riscv64-linux-gnu-ld`, and runtime QEMU for RISC-V. The Makefile defaults to three CPUs (`CPUS := 3`), and the boot log showed additional harts starting.

### 15.2 Kernel build — observed successful

```bash
make kernel/kernel
```

The log showed compilation of `kernel/rwlock.o`, linking of `kernel/kernel`, and generation of `kernel/kernel.asm` and `kernel/kernel.sym`. The linker emitted:

```text
warning: kernel/kernel has a LOAD segment with RWX permissions
```

This was a **nonfatal linker warning**, not a compilation failure. The command returned to the shell prompt.

### 15.3 User test build — observed successful

```bash
make user/_rwtest
```

The log showed `user/rwtest.c` compiled into `user/rwtest.o`, linked into `user/_rwtest`, and assembly/symbol listings generated. No compiler errors were shown.

### 15.4 Filesystem image — observed successful

```bash
make fs.img
```

The `mkfs/mkfs` output included `user/_rwtest` among the filesystem programs and printed filesystem allocation statistics. This confirms the test executable was packaged into the image.

### 15.5 Boot — observed successful

```bash
make qemu
```

The captured boot log included:

```text
xv6 kernel is booting
hart 1 starting
hart 2 starting
init: starting sh
$
```

The `$` prompt demonstrates that the xv6 shell started successfully.

### 15.6 Final Demo 1 `rwtest` run — observed output

The user ran:

```text
$ rwtest
=== Reader-Writer Inode Lock Test ===
Starting 3 reader processes...
Reader 4: Concurrent reader test data
Reader 5: Concurrent reader test data
Reader 6: Concurrent reader test data
Concurrent reader test completed!

=== Exclusive Writer Lock Test ===
Starting 3 writer processes...
Writer 7 started
Writer 8 started
Writer 9 started
Writer 7: write completed
Writer 9: write completed
Writer 8: write completed
Writer processes completed!

=== Writer Starvation Prevention Test ===
Starvation reader 10 started
Starvation reader 11 started
Starvation reader 12 started
Starvation reader 13 started
Starvation reader 14 started
Starvation writer 15 started
Starvation writer 15 completed
Starvation reader 10 completed
Starvation reader 12 completed
Starvation reader 11 completed
Starvation reader 13 completed
Starvation reader 14 completed
Starvation test: all processes completed successfully!
```

**Transcription note:** The original console capture contained heavily interleaved characters in several `printf()` messages because multiple processes printed concurrently. The text above is a **readability-normalized summary** of the visible messages and completion order, not an exact byte-for-byte console transcript. The observed completion order was: writer 15, then readers 10, 12, 11, 13, 14.

### 15.7 How to reproduce

```bash
# In the xv6 directory
make kernel/kernel
make user/_rwtest
make fs.img
make qemu
```

Inside xv6:

```text
$ rwtest
```

To leave QEMU, press **Ctrl+A**, release, then **X**.

### 15.8 Historical concurrent-reader diagnostic

During the earlier instrumented run with an intentionally extended read-lock hold time, the kernel printed:

```text
[RWLOCK] Concurrent readers: 2
```

This is stronger direct evidence of shared read-lock ownership than merely seeing three reader processes finish. The artificial delay was later removed; the exact diagnostic may not appear on every fast run because scheduling and overlap are nondeterministic.

---

## 16. Testing evaluation — what the evidence establishes

| Claim | Evidence | Assessment |
|---|---|---|
| Kernel builds | `make kernel/kernel` completed | **Verified for the tested build** |
| User test builds | `make user/_rwtest` completed | **Verified** |
| Test is included in filesystem | `make fs.img` included `_rwtest` | **Verified** |
| xv6 boots | Boot log reached shell prompt | **Verified** |
| Three readers retrieve expected data | All three printed the expected string | **Verified in the observed run** |
| More than one reader can hold same lock | Earlier `Concurrent readers: 2` diagnostic | **Observed with temporary timing instrumentation** |
| Three writers can finish | All three printed write completion | **Verified basic functionality**, not a full exclusivity proof |
| One writer completes during mixed workload | Writer 15 completed before five readers finished | **Verified progress in this finite test** |
| No visible kernel panic during test | Shell output completed normally | **Verified for the captured run** |
| FIFO ordering of multiple waiting writers | Ticket algorithm present | **Implemented; no direct ordering trace captured** |
| No writer starvation under unbounded readers | Writer-preference algorithm and finite workload | **Not formally or exhaustively verified** |
| No filesystem regressions | No `usertests` or `stressfs` log supplied | **Not yet established** |
| Measurable performance improvement | No timings or throughput comparison supplied | **Not yet established** |

### 16.1 Why writer completion before readers is encouraging

The writer finished while reader processes still had work remaining. This shows the writer did not have to wait for all reader processes to terminate before making progress. It is consistent with a writer-preference implementation.

### 16.2 Why it is not proof of starvation freedom

The reader processes each perform only 1,000 iterations. Their workload is finite. Furthermore, a reader process may be alive without currently holding an inode read lock. The test does not record the moment the writer becomes queued, the reader count at that moment, or the moment the writer acquires the inode lock. Therefore, it does not establish that the writer was actually forced to wait, or that an indefinitely arriving stream of readers could never starve it.

### 16.3 Why the exclusive-writer test is limited

All three writers write the same string from independent file offsets initially at zero. They can all succeed even if the test never measures the exact ordering of lock acquisition. The internal state assertions are useful checks, but a stronger exclusivity test would record entry/exit events under contention and assert that no read and write critical sections overlap.

### 16.4 Why concurrent `printf()` output looks corrupted

Different user processes print to the same console. Their characters may interleave, especially when processes are scheduled on multiple CPUs. Interleaved text alone does not imply filesystem corruption or a locking bug. Test conclusions should rely on process completion, checked return values, and instrumented lock state rather than neatness of console output.

### 16.5 What a stronger starvation experiment would measure

A future test should ensure a writer actually queues behind one or more active readers, then keep introducing readers **after** that writer queues. It should log or count: writer ticket, queued time, reader count on arrival, writer acquisition time, and whether any later reader entered before the waiting writer. It should have a bounded watchdog or timeout so a deadlock does not hang the demonstration indefinitely.

---

## 17. Known limitations, code-review risks, and next-stage plan

### 17.1 Testing gaps

- Full xv6 `usertests` has not been shown passing after the changes.
- `stressfs` has not been shown passing after the changes.
- No repeated randomized stress run or deterministic contention barrier was captured.
- No writer-ticket FIFO trace was captured.
- No throughput/latency comparison against original inode sleeplocks was measured.

### 17.2 Source-level issues worth reviewing

1. **Sleeping and waking:** verify the custom `sleep_prepare()` protocol against `proc.c` for lost wakeups and lock-order safety.
2. **`iput()` lock ordering:** inspect the path holding `itable.lock` while acquiring a potentially sleeping inode writer lock. A lock-order inversion or sleeping-with-spinlock issue would be serious.
3. **`ilockread()` release/reacquire:** assess metadata validity and inode lifetime across the temporary drop of shared ownership.
4. **`fileclose()` copies `struct file`:** adding `struct sleeplock offlock` enlarges the struct, so copying a file object also copies lock fields. The copy is not used as a live offset lock in the observed code, but the implications should be audited.
5. **Read-lock ownership checks:** `releaseread()` checks the global count but not individual ownership.
6. **`bmapread()` missing blocks:** returning early on missing blocks may need explicit sparse-file or corruption semantics.
7. **Reader fairness:** continuous writers can postpone readers under strict writer preference.
8. **Ticket wraparound:** no explicit long-run rollover handling or proof.
9. **Test status propagation:** initial reader/writer tests do not inspect child statuses; the mixed test prints a failure but does not always cause `main()` to exit nonzero.
10. **Diagnostic overhead:** the `Concurrent readers` kernel print remains in Demo 1 and may perturb timing.
11. **Leftover comments:** commented-out old sleeplock calls remain in some kernel code and can be cleaned up after review.

These are **review items**, not assertions that a specific failure was observed. The recorded Demo 1 run completed successfully.

### 17.3 Recommended next-stage order

1. Save Demo 1 as a stable Git checkpoint (reported complete).
2. Review lock-order and sleep/wakeup correctness before more aggressive stress tests.
3. Add instrumentation for actual writer queueing and acquisition.
4. Run `usertests` and `stressfs`, capture their output, and fix any regressions.
5. Repeat mixed contention with controlled overlap and explicit failure/timeout handling.
6. Remove temporary diagnostic prints and obsolete comments.
7. Measure read throughput or wait time against a baseline build, if performance evaluation is required.
8. Document the final tested commit hash and update the report with the new evidence.

---

## 18. First-demo explanation and likely questions

### 18.1 Suggested 5–8 minute walkthrough

1. **Problem (45 sec):** Original inode sleeplock serializes all readers. Need shared readers and safe writers.
2. **Data structure (60 sec):** Show `rwlock.h`: `readers`, `writer`, `waiting_writers`, ticket counters, internal spinlock.
3. **Algorithm (90 sec):** Explain `acquireread` gate and `acquirewrite` three-condition wait; demonstrate writer preference and ticket FIFO.
4. **Integration (90 sec):** Show `file.h`, `fs.c`'s `ilockread`, `file.c`'s `fileread`, and `bmapread`.
5. **Correctness detail (45 sec):** Explain why `offlock` is needed even though readers share inode ownership.
6. **Live demo (60 sec):** Run `rwtest`; point to reader output, three writer completions, and writer completion during reader activity.
7. **Limitations (30 sec):** Explain that stronger starvation proof and regression testing are planned.

### 18.2 Frequently asked questions

**Q: Why not just use a spinlock?**  
A: Filesystem operations can sleep or take a long time. The spinlock protects only short lock-state updates; the logical read/write lock can remain held while I/O proceeds.

**Q: Why is `waiting_writers > 0` important?**  
A: It blocks new readers once a writer has queued, allowing existing readers to drain so the writer can eventually enter.

**Q: Why both `waiting_writers` and tickets?**  
A: The first implements reader admission preference; the second orders waiting writers relative to each other.

**Q: Can two writers ever hold the inode lock together?**  
A: The intended invariant forbids it: admission checks `writer == 0`, `readers == 0`, and the current ticket under the state spinlock. We have basic test evidence, but final rigorous verification is pending.

**Q: Why do you need `offlock`?**  
A: Two descriptors may share the same open-file object and file offset. The inode read lock deliberately allows multiple readers, so it cannot also serialize shared offset updates.

**Q: Why create `bmapread()`?**  
A: The old block-mapping helper can allocate blocks. Reads under shared inode ownership must not modify inode block-address metadata.

**Q: What if the inode has not yet been loaded from disk?**  
A: `ilockread()` temporarily drops shared ownership, calls exclusive `ilock()` to initialize metadata, and reacquires the shared lock.

**Q: Is starvation prevention proven?**  
A: Writer preference is implemented, and a finite mixed test completed successfully. A controlled test with sustained arrivals and direct queueing evidence is planned for the next stage.

**Q: Why is the console text sometimes jumbled?**  
A: Several processes print concurrently. Interleaved console output is expected and is not by itself evidence of an inode locking failure.

**Q: Did you remove all sleeplocks?**  
A: No. We replaced the inode sleeplock, but retained sleeplocks where exclusive sleeping synchronization is still needed, including the new per-open-file offset lock.

---

## 19. Final implementation and evaluation

### 19.1 Final Demo 1 architecture

The implementation replaces the inode's single-owner sleeplock with a custom reader–writer lock that permits multiple shared holders, excludes writers from readers and other writers, gives waiting writers admission priority over new readers, and orders writers with FIFO tickets. A short-lived spinlock protects lock state, while waiting processes sleep on the lock's channel. `ilock()` remains the exclusive inode interface; `ilockread()` and `iunlockread()` provide the shared interface for file reads. `bmapread()` prevents unintended block allocation in shared-read code. A separate `offlock` protects offsets shared through an open-file object.

### 19.2 Final observed test results

- **Build:** kernel and `rwtest` compiled successfully.
- **Packaging:** `rwtest` was included in the filesystem image.
- **Boot:** xv6 reached its shell prompt.
- **Reader functional test:** three readers printed the expected data.
- **Shared-lock diagnostic:** a previous instrumented run observed two active readers.
- **Writer functional test:** three writer processes completed.
- **Mixed progress test:** five readers performed repeated read workloads and the writer completed before the reader processes finished; all six children reported successful completion.
- **Stability in captured run:** no visible kernel panic or deadlock.

### 19.3 Overall assessment

**Demo 1 is ready for a first demonstration.** The core code is integrated, builds, boots, and passes the recorded custom functional workloads. The design directly addresses the assignment's two requirements: shared reader access and a policy intended to prevent writer starvation.

**It is not yet a final, exhaustively validated kernel change.** In particular, the finite mixed workload does not prove unbounded starvation freedom; full filesystem regression tests, careful sleep/wakeup and lock-order review, and stronger controlled contention testing remain outstanding. The correct academic claim is therefore: **“Implemented and demonstrated writer-preference reader–writer inode locking, with further stress and correctness validation planned.”**

### 19.4 Final file-level summary

```text
xv6/
├── Makefile                 # Link rwlock.o; package _rwtest
├── kernel/
│   ├── rwlock.h             # RW lock structure and API
│   ├── rwlock.c             # Shared/exclusive lock algorithm
│   ├── file.h               # inode.rwlock + file.offlock
│   ├── fs.c                 # ilockread, iunlockread, bmapread, integration
│   ├── file.c               # Offset locking and shared read path
│   ├── defs.h               # New function declarations
│   ├── spinlock.h           # Header guard
│   └── sleeplock.h          # Header guard
└── user/
    └── rwtest.c            # Reader, writer, mixed contention tests
```

---

## 20. Source reference and reproducibility appendix

### 20.1 Inspected source files

The following links point to the implementation branch that was inspected while preparing this report:

- [`kernel/rwlock.h`](https://github.com/fahadalhasan73/Reader-Writer_Inode_Locks_CSE323/blob/feature/rw-inode-lock/xv6/kernel/rwlock.h)
- [`kernel/rwlock.c`](https://github.com/fahadalhasan73/Reader-Writer_Inode_Locks_CSE323/blob/feature/rw-inode-lock/xv6/kernel/rwlock.c)
- [`kernel/file.h`](https://github.com/fahadalhasan73/Reader-Writer_Inode_Locks_CSE323/blob/feature/rw-inode-lock/xv6/kernel/file.h)
- [`kernel/file.c`](https://github.com/fahadalhasan73/Reader-Writer_Inode_Locks_CSE323/blob/feature/rw-inode-lock/xv6/kernel/file.c)
- [`kernel/fs.c`](https://github.com/fahadalhasan73/Reader-Writer_Inode_Locks_CSE323/blob/feature/rw-inode-lock/xv6/kernel/fs.c)
- [`kernel/defs.h`](https://github.com/fahadalhasan73/Reader-Writer_Inode_Locks_CSE323/blob/feature/rw-inode-lock/xv6/kernel/defs.h)
- [`kernel/spinlock.h`](https://github.com/fahadalhasan73/Reader-Writer_Inode_Locks_CSE323/blob/feature/rw-inode-lock/xv6/kernel/spinlock.h)
- [`kernel/sleeplock.h`](https://github.com/fahadalhasan73/Reader-Writer_Inode_Locks_CSE323/blob/feature/rw-inode-lock/xv6/kernel/sleeplock.h)
- [`user/rwtest.c`](https://github.com/fahadalhasan73/Reader-Writer_Inode_Locks_CSE323/blob/feature/rw-inode-lock/xv6/user/rwtest.c)
- [`Makefile`](https://github.com/fahadalhasan73/Reader-Writer_Inode_Locks_CSE323/blob/feature/rw-inode-lock/xv6/Makefile)
- [`kernel/proc.c`](https://github.com/fahadalhasan73/Reader-Writer_Inode_Locks_CSE323/blob/feature/rw-inode-lock/xv6/kernel/proc.c) (supporting sleep/wakeup context; not a final-stage modified file)

### 20.2 Commands for groupmates

From the repository root:

```bash
cd xv6
make kernel/kernel
make user/_rwtest
make fs.img
make qemu
```

Inside xv6:

```text
$ rwtest
```

For Git history and exact change verification:

```bash
git status
git log --oneline --decorate -10
git show --stat HEAD
git show --format=fuller --no-patch HEAD
# Substitute the actual baseline and Demo 1 commit hashes:
git diff <baseline-commit> <demo1-commit> -- xv6/kernel/fs.c
```

**Path reminder:** The repository appears to contain `xv6/` as a subdirectory. When running `git diff` from inside `xv6`, the path arguments above may be interpreted relative to the repository root; use `git rev-parse --show-toplevel` to confirm the root.

### 20.3 How to extend this report after Demo 1

Add a new dated section for each subsequent checkpoint rather than overwriting the historical Demo 1 results. Include: exact commit hash, changed files, test commands, complete raw logs, pass/fail outcomes, any observed bugs, and how they were resolved. This preserves the difference between what was demonstrated at Demo 1 and what was verified later.

---

**End of Demo 1 explanatory report.**
