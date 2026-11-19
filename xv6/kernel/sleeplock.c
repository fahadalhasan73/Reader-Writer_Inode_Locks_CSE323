// Sleeping locks

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "sleeplock.h"

void
initsleeplock(struct sleeplock *lk, char *name)
{
  initlock(&lk->lk, "sleep lock");
  lk->name = name;
  lk->locked = 0;
  lk->pid = 0;
}

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

int
holdingsleep(struct sleeplock *lk)
{
  int r;

  acquire(&lk->lk);
  r = lk->locked && (lk->pid == myproc()->pid);
  release(&lk->lk);
  return r;
}
